// FFV1(MKV) 영상에서 번호로 프레임을 읽습니다. libavcodec과 libavformat 타입은 이 파일 밖으로 나가지 않습니다.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <cmath>

#include "hwlab/platform/video_reader.h"

namespace hwlab::platform {

namespace {

class Ffv1Reader final : public VideoReader {
 public:
  ~Ffv1Reader() override { Release(); }

  bool Open(const std::string& path, std::string* error) override {
    Release();
    const auto fail = [&](const std::string& why) {
      if (error != nullptr) *error = why + ": " + path;
      Release();
      return false;
    };
    if (avformat_open_input(&format_, path.c_str(), nullptr, nullptr) < 0) return fail("영상 파일을 열지 못함");
    if (avformat_find_stream_info(format_, nullptr) < 0) return fail("영상 정보를 읽지 못함");
    stream_index_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index_ < 0) return fail("영상 스트림이 없음");
    const AVStream* stream = format_->streams[stream_index_];
    if (stream->codecpar->format != AV_PIX_FMT_YUV422P) return fail("화소 형식이 4:2:2 8비트가 아님");
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) return fail("디코더가 없음");
    decoder_ = avcodec_alloc_context3(codec);
    if (decoder_ == nullptr || avcodec_parameters_to_context(decoder_, stream->codecpar) < 0) return fail("디코더 준비 실패");
    // 프레임 단위 스레드는 출력이 몇 장 늦게 나오므로, 한 장씩 바로 받도록 슬라이스 스레드만 씁니다.
    decoder_->thread_count = 0;
    decoder_->thread_type = FF_THREAD_SLICE;
    if (avcodec_open2(decoder_, codec, nullptr) < 0) return fail("디코더를 열지 못함");
    rate_ = av_guess_frame_rate(format_, format_->streams[stream_index_], nullptr);
    if (rate_.num <= 0 || rate_.den <= 0) return fail("fps를 알 수 없음");
    time_base_ = stream->time_base;
    start_ = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    width_ = stream->codecpar->width;
    height_ = stream->codecpar->height;
    if (format_->duration > 0) {
      const double seconds = static_cast<double>(format_->duration) / AV_TIME_BASE;
      frames_ = std::llround(seconds * av_q2d(rate_));
    }
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    next_index_ = 0;
    return true;
  }

  int32_t Width() const override { return width_; }
  int32_t Height() const override { return height_; }
  int64_t FrameCount() const override { return frames_; }

  bool ReadFrame(int64_t index, std::vector<uint8_t>* uyvy) override {
    if (format_ == nullptr || index < 0 || index >= frames_) return false;
    if (index != next_index_ && !Seek(index)) return false;
    bool rewound = false;
    for (;;) {
      if (av_read_frame(format_, packet_) < 0) break;
      if (packet_->stream_index != stream_index_) {
        av_packet_unref(packet_);
        continue;
      }
      const int64_t got = IndexOf(packet_->pts);
      if (got < index) {
        av_packet_unref(packet_);
        continue;
      }
      if (got > index) {
        av_packet_unref(packet_);
        // 색인(Cues)이 없는 파일에서 탐색이 목표보다 뒤에 서면 처음부터 다시 읽습니다.
        if (rewound || !Seek(0)) break;
        rewound = true;
        continue;
      }
      const bool ok = Decode(uyvy);
      av_packet_unref(packet_);
      next_index_ = ok ? index + 1 : -1;
      return ok;
    }
    next_index_ = -1;
    return false;
  }

 private:
  int64_t IndexOf(int64_t pts) const {
    if (pts == AV_NOPTS_VALUE) return -1;
    return av_rescale_q_rnd(pts - start_, time_base_, av_inv_q(rate_),
                            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
  }

  bool Seek(int64_t index) {
    const int64_t ts = start_ + av_rescale_q(index, av_inv_q(rate_), time_base_);
    if (av_seek_frame(format_, stream_index_, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(decoder_);
    next_index_ = -1;
    return true;
  }

  bool Decode(std::vector<uint8_t>* uyvy) {
    if (avcodec_send_packet(decoder_, packet_) < 0) return false;
    if (avcodec_receive_frame(decoder_, frame_) < 0) return false;
    const bool ok = frame_->format == AV_PIX_FMT_YUV422P && frame_->width == width_ && frame_->height == height_;
    if (ok) {
      uyvy->resize(static_cast<size_t>(width_) * height_ * 2);
      uint8_t* out = uyvy->data();
      for (int32_t r = 0; r < height_; ++r) {
        const uint8_t* y = frame_->data[0] + static_cast<ptrdiff_t>(r) * frame_->linesize[0];
        const uint8_t* u = frame_->data[1] + static_cast<ptrdiff_t>(r) * frame_->linesize[1];
        const uint8_t* v = frame_->data[2] + static_cast<ptrdiff_t>(r) * frame_->linesize[2];
        for (int32_t c = 0; c < width_ / 2; ++c) {
          *out++ = u[c];
          *out++ = y[2 * c];
          *out++ = v[c];
          *out++ = y[2 * c + 1];
        }
      }
    }
    av_frame_unref(frame_);
    return ok;
  }

  void Release() {
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    avcodec_free_context(&decoder_);
    if (format_ != nullptr) avformat_close_input(&format_);
    width_ = height_ = 0;
    frames_ = 0;
    next_index_ = 0;
  }

  AVFormatContext* format_ = nullptr;
  AVCodecContext* decoder_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  int stream_index_ = -1;
  AVRational rate_{0, 1};
  AVRational time_base_{0, 1};
  int64_t start_ = 0;
  int32_t width_ = 0;
  int32_t height_ = 0;
  int64_t frames_ = 0;
  int64_t next_index_ = 0;  // 이어서 읽으면 나올 번호. 모르면 -1
};

}  // namespace

std::unique_ptr<VideoReader> MakeFfv1Reader() { return std::make_unique<Ffv1Reader>(); }

}  // namespace hwlab::platform
