// FFV1 MKV의 압축된 장을 그대로 옮겨 담고 장 시각과 장당 길이(fps)만 새로 적습니다.
//
//   hwlab_mkv_rewrite <입력.mkv> <출력.mkv> [fps]
//
// 2026-10-20 이전 녹화는 MKV에 fps가 없어, 읽는 쪽(OpenCV, 이 앱의 영상 읽기)이 장이 큰 파일에서 29.97로 짐작합니다.
// 그러면 장 수를 899로 세고 장 번호도 500장 근처부터 어긋납니다. 이 도구는 디코딩하지 않으므로 화소가 바뀌지 않고,
// 다 쓴 뒤 두 파일의 장을 순서대로 다시 읽어 장 수와 압축 바이트가 같은지 확인합니다.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

struct Input {
  AVFormatContext* format = nullptr;
  int stream = -1;
  ~Input() { avformat_close_input(&format); }
  bool Open(const std::string& path) {
    if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(format, nullptr) < 0) return false;
    stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    return stream >= 0;
  }
  // 다음 영상 패킷입니다. 끝이면 false입니다.
  bool Next(AVPacket* packet) {
    while (av_read_frame(format, packet) >= 0) {
      if (packet->stream_index == stream) return true;
      av_packet_unref(packet);
    }
    return false;
  }
};

int Run(const std::vector<std::string>& args) {
  if (args.size() < 3) {
    std::fprintf(stderr, "사용법: hwlab_mkv_rewrite <입력.mkv> <출력.mkv> [fps]\n");
    return 2;
  }
  const std::string& in_path = args[1];
  const std::string& out_path = args[2];
  const int fps = args.size() > 3 ? std::atoi(args[3].c_str()) : 30;
  if (fps <= 0) return 2;

  Input in;
  if (!in.Open(in_path)) {
    std::fprintf(stderr, "입력을 열지 못했습니다: %s\n", in_path.c_str());
    return 1;
  }
  AVFormatContext* out = nullptr;
  if (avformat_alloc_output_context2(&out, nullptr, "matroska", out_path.c_str()) < 0 || out == nullptr) return 1;
  AVStream* stream = avformat_new_stream(out, nullptr);
  if (stream == nullptr || avcodec_parameters_copy(stream->codecpar, in.format->streams[in.stream]->codecpar) < 0) return 1;
  stream->codecpar->codec_tag = 0;
  stream->time_base = {1, fps};
  // 녹화 앱(ffv1_file.cc)과 같이 장당 길이를 적습니다.
  stream->avg_frame_rate = {fps, 1};
  stream->r_frame_rate = {fps, 1};
  if (avio_open(&out->pb, out_path.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, nullptr) < 0) {
    std::fprintf(stderr, "출력을 만들지 못했습니다: %s\n", out_path.c_str());
    return 1;
  }
  AVPacket* packet = av_packet_alloc();
  int64_t count = 0;
  while (in.Next(packet)) {
    // 장 번호를 그대로 시각으로 씁니다. 녹화 앱도 장 시각을 고정 fps로 매기고 실제 촬영 시각은 사이드카에 둡니다.
    packet->pts = count;
    packet->dts = count;
    packet->duration = 1;
    packet->pos = -1;
    packet->stream_index = stream->index;
    av_packet_rescale_ts(packet, AVRational{1, fps}, stream->time_base);
    if (av_interleaved_write_frame(out, packet) < 0) {
      std::fprintf(stderr, "%lld번째 장을 쓰지 못했습니다\n", static_cast<long long>(count));
      return 1;
    }
    ++count;
  }
  av_write_trailer(out);
  avio_closep(&out->pb);
  avformat_free_context(out);

  // 다시 읽어 fps, 장 수, 장마다 압축 바이트를 견줍니다.
  Input a;
  Input b;
  if (!a.Open(in_path) || !b.Open(out_path)) return 1;
  const AVRational rate = b.format->streams[b.stream]->avg_frame_rate;
  AVPacket* pa = av_packet_alloc();
  AVPacket* pb = av_packet_alloc();
  int64_t checked = 0;
  for (;;) {
    const bool ha = a.Next(pa);
    const bool hb = b.Next(pb);
    if (ha != hb) {
      std::fprintf(stderr, "다시 읽은 장 수가 다릅니다(%lld장째)\n", static_cast<long long>(checked));
      return 1;
    }
    if (!ha) break;
    const bool same = pa->size == pb->size && std::equal(pa->data, pa->data + pa->size, pb->data);
    av_packet_unref(pa);
    av_packet_unref(pb);
    if (!same) {
      std::fprintf(stderr, "%lld번째 장의 압축 바이트가 다릅니다\n", static_cast<long long>(checked));
      return 1;
    }
    ++checked;
  }
  av_packet_free(&pa);
  av_packet_free(&pb);
  av_packet_free(&packet);
  if (rate.num != fps || rate.den != 1) {
    std::fprintf(stderr, "출력 파일의 fps가 %d/%d입니다\n", rate.num, rate.den);
    return 1;
  }
  std::printf("다시 씀: %lld장, fps %d/1, 장마다 압축 바이트 일치\n", static_cast<long long>(checked), fps);
  return 0;
}

}  // namespace

#ifdef _WIN32
// Windows의 char 인자는 시스템 코드 페이지라 한글 경로가 깨지므로 넓은 문자 인자를 UTF-8로 바꿉니다.
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, text.data(), n, nullptr, nullptr);
    args.push_back(std::move(text));
  }
  SetConsoleOutputCP(CP_UTF8);
  return Run(args);
}
#else
int main(int argc, char** argv) { return Run(std::vector<std::string>(argv, argv + argc)); }
#endif
