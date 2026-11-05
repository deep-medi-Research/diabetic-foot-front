// FFV1 무손실 압축을 MKV에 담습니다. libavcodec과 libavformat 타입은 이 파일 밖으로 나가지 않습니다.
// 인코딩 설정은 2026-10-16 실험 노트북 실측과 같습니다: level 3, 슬라이스 16, 슬라이스 CRC, 슬라이스 스레드.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

#include "hwlab/pixel/convert.h"
#include "hwlab/platform/video_writer.h"

namespace hwlab::platform {

namespace {

// 실험(디스크 멈춤): 압축한 패킷을 이만큼 메모리에 모아 두고 파일 쓰기는 전용 스레드가 합니다.
// 실험 노트북 SSD가 쓰기를 10~27초 멈춰도(2026-11-02 실측) 압축과 수신이 멈추지 않게 합니다. HWLAB_WRITE_QUEUE_MB로 파일당 한도를 바꿉니다.
size_t MaxQueueBytes() {
  static const size_t bytes = [] {
    const char* v = std::getenv("HWLAB_WRITE_QUEUE_MB");
    const long long mb = v != nullptr ? std::atoll(v) : 0;
    return static_cast<size_t>(mb > 0 ? mb : 768) << 20;
  }();
  return bytes;
}

class Ffv1File final : public VideoWriter {
 public:
  ~Ffv1File() override {
    if (format_ != nullptr) Close();
  }

  bool Open(const std::string& path, const record::VideoFormat& format, int64_t /*max_frames*/) override {
    format_info_ = format;
    if (format.format != PixelFormat::kUyvy || format.width % 2 != 0 || format.frame_rate <= 0) return false;
    if (avformat_alloc_output_context2(&format_, nullptr, "matroska", path.c_str()) < 0) return false;
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (codec == nullptr) return Fail();
    encoder_ = avcodec_alloc_context3(codec);
    encoder_->width = format.width;
    encoder_->height = format.height;
    encoder_->pix_fmt = AV_PIX_FMT_YUV422P;
    encoder_->time_base = {1, format.frame_rate};
    encoder_->framerate = {format.frame_rate, 1};
    encoder_->gop_size = 1;
    encoder_->level = 3;
    encoder_->thread_count = 0;  // CPU 수에 맞춤
    encoder_->thread_type = FF_THREAD_SLICE;
    if (format_->oformat->flags & AVFMT_GLOBALHEADER) encoder_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "slices", "16", 0);
    av_dict_set(&options, "slicecrc", "1", 0);
    const int opened = avcodec_open2(encoder_, codec, &options);
    av_dict_free(&options);
    if (opened < 0) return Fail();

    stream_ = avformat_new_stream(format_, nullptr);
    if (stream_ == nullptr || avcodec_parameters_from_context(stream_->codecpar, encoder_) < 0) return Fail();
    stream_->time_base = encoder_->time_base;
    // MKV에 장당 길이(DefaultDuration)를 적습니다. 없으면 읽는 쪽(OpenCV)이 앞 몇 장의 시각으로 fps를 짐작하는데,
    // 장이 크면 짐작에 쓰는 장 수가 줄어 29.97로 읽고 장 수를 899로 셉니다(2026-10-20 실기, 440MB 이상 파일 2건).
    stream_->avg_frame_rate = encoder_->framerate;
    stream_->r_frame_rate = encoder_->framerate;
    if (avio_open(&format_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) return Fail();
    if (avformat_write_header(format_, nullptr) < 0) return Fail();

    frame_ = av_frame_alloc();
    frame_->format = AV_PIX_FMT_YUV422P;
    frame_->width = format.width;
    frame_->height = format.height;
    if (av_frame_get_buffer(frame_, 0) < 0) return Fail();
    packet_ = av_packet_alloc();
    frames_ = 0;
    path_ = path;
    closing_ = false;
    mux_failed_ = false;
    close_failed_ = false;
    queued_bytes_ = 0;
    max_queued_bytes_ = 0;
    producer_wait_ms_ = 0;
    mux_ = std::thread([this] { MuxLoop(); });
    return true;
  }

  bool WriteFrame(const uint8_t* uyvy) override {
    if (encoder_ == nullptr || av_frame_make_writable(frame_) < 0) return false;
    FrameView view;
    view.format = PixelFormat::kUyvy;
    view.width = format_info_.width;
    view.height = format_info_.height;
    view.plane[0] = uyvy;
    view.row_stride[0] = format_info_.width * 2;
    if (!pixel::UyvyToPlanar422(view, frame_->data[0], frame_->linesize[0], frame_->data[1], frame_->linesize[1],
                                frame_->data[2], frame_->linesize[2])) {
      return false;
    }
    // 실제 촬영 시각은 사이드카에 있고, 파일 안 시각은 고정 fps로 매깁니다.
    frame_->pts = frames_;
    if (avcodec_send_frame(encoder_, frame_) < 0 || !Drain()) return false;
    ++frames_;
    return true;
  }

  bool Close() override {
    if (format_ == nullptr) return false;
    bool ok = true;
    const auto close_start = std::chrono::steady_clock::now();
    if (encoder_ != nullptr && avcodec_is_open(encoder_)) {
      ok = avcodec_send_frame(encoder_, nullptr) >= 0 && Drain();
    }
    StopMux();
    if (encoder_ != nullptr && avcodec_is_open(encoder_)) {
      ok = !mux_failed_ && av_write_trailer(format_) >= 0 && ok;
    }
    const auto close_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - close_start).count();
    if (std::FILE* f = std::fopen((path_ + ".queue.txt").c_str(), "w")) {
      std::fprintf(f, "frames=%lld limit_mb=%zu max_queued_mb=%.1f producer_wait_ms=%lld close_wait_ms=%lld mux_failed=%d\n",
                   static_cast<long long>(frames_), MaxQueueBytes() >> 20, static_cast<double>(max_queued_bytes_) / (1 << 20),
                   static_cast<long long>(producer_wait_ms_), static_cast<long long>(close_ms), mux_failed_ ? 1 : 0);
      std::fclose(f);
    }
    Release();
    return ok && !close_failed_;
  }

  int64_t FramesWritten() const override { return frames_; }

 private:
  bool Drain() {
    for (;;) {
      const int got = avcodec_receive_packet(encoder_, packet_);
      if (got == AVERROR(EAGAIN) || got == AVERROR_EOF) return true;
      if (got < 0) return false;
      // 패킷 길이가 0이면 MKV의 전체 길이에 마지막 장의 길이가 빠져, 길이와 fps로 장 수를 구하는 OpenCV가 한 장 적게 셉니다.
      if (packet_->duration <= 0) packet_->duration = 1;
      av_packet_rescale_ts(packet_, encoder_->time_base, stream_->time_base);
      packet_->stream_index = stream_->index;
      AVPacket* queued = av_packet_alloc();
      if (queued == nullptr) return false;
      av_packet_move_ref(queued, packet_);
      if (!Enqueue(queued)) return false;
    }
  }

  // 대기열이 한도에 닿으면 자리가 날 때까지 기다립니다. 그동안은 수신 쪽 원본 대기열이 저장 누락을 셉니다.
  bool Enqueue(AVPacket* p) {
    const size_t bytes = static_cast<size_t>(std::max(p->size, 0));
    std::unique_lock<std::mutex> lock(queue_mutex_);
    const size_t limit = MaxQueueBytes();
    if (queued_bytes_ + bytes > limit && !mux_failed_) {
      const auto wait_start = std::chrono::steady_clock::now();
      space_cv_.wait(lock, [&] { return queued_bytes_ + bytes <= limit || mux_failed_; });
      producer_wait_ms_ += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - wait_start).count();
    }
    if (mux_failed_) {
      av_packet_free(&p);
      return false;
    }
    queue_.push_back(p);
    queued_bytes_ += bytes;
    max_queued_bytes_ = std::max(max_queued_bytes_, queued_bytes_);
    data_cv_.notify_one();
    return true;
  }

  void MuxLoop() {
    for (;;) {
      AVPacket* p = nullptr;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        data_cv_.wait(lock, [&] { return !queue_.empty() || closing_; });
        if (queue_.empty()) return;
        p = queue_.front();
        queue_.pop_front();
        queued_bytes_ -= static_cast<size_t>(std::max(p->size, 0));
      }
      space_cv_.notify_one();
      if (!mux_failed_ && av_interleaved_write_frame(format_, p) < 0) {
        mux_failed_ = true;
        space_cv_.notify_all();
      }
      av_packet_free(&p);
    }
  }

  void StopMux() {
    if (!mux_.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      closing_ = true;
    }
    data_cv_.notify_all();
    mux_.join();
  }

  bool Fail() {
    Release();
    return false;
  }

  void Release() {
    StopMux();
    for (AVPacket* p : queue_) av_packet_free(&p);
    queue_.clear();
    queued_bytes_ = 0;
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&encoder_);
    if (format_ != nullptr) {
      // 닫을 때 남은 버퍼를 쓰다 실패하면(디스크 가득 참) 파일이 잘린 것이므로 Close가 실패로 알립니다.
      if (format_->pb != nullptr && avio_closep(&format_->pb) < 0) close_failed_ = true;
      avformat_free_context(format_);
      format_ = nullptr;
    }
    stream_ = nullptr;
  }

  record::VideoFormat format_info_;
  AVFormatContext* format_ = nullptr;
  AVCodecContext* encoder_ = nullptr;
  AVStream* stream_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  int64_t frames_ = 0;
  std::string path_;
  std::thread mux_;
  std::mutex queue_mutex_;
  std::condition_variable data_cv_;
  std::condition_variable space_cv_;
  std::deque<AVPacket*> queue_;
  size_t queued_bytes_ = 0;
  size_t max_queued_bytes_ = 0;
  int64_t producer_wait_ms_ = 0;
  bool closing_ = false;
  std::atomic<bool> mux_failed_{false};
  bool close_failed_ = false;
};

}  // namespace

std::unique_ptr<VideoWriter> MakeFfv1Writer() { return std::make_unique<Ffv1File>(); }

}  // namespace hwlab::platform
