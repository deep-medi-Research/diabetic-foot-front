#include "hwlab/platform/perfusion_analysis.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

using hwlab::platform::AnalyzerLine;
using hwlab::platform::AnalyzerLineKind;
using hwlab::platform::ParseAnalyzerLine;
using hwlab::platform::PerfusionRequest;

// 요청 줄은 기존 앱과 같은 키를 고정된 순서로 쓰고, 역슬래시와 따옴표만 이스케이프하며 한글은 그대로 둡니다.
TEST(PerfusionAnalysis, BuildsRequestLineWithEscapes) {
  PerfusionRequest request;
  request.recording_dir = "C:\\촬영\\홍길동 \"A\"";
  request.output_dir = "C:\\촬영\\perfusion";
  request.name = "20261019_홍길동";
  EXPECT_EQ(hwlab::platform::BuildPerfusionRequestLine(request),
            "{\"rec_dir\":\"C:\\\\촬영\\\\홍길동 \\\"A\\\"\",\"output\":\"C:\\\\촬영\\\\perfusion\","
            "\"name\":\"20261019_홍길동\",\"cuff_side\":\"none\",\"template\":\"v106w\","
            "\"classify\":true,\"pad_classifier\":false,\"gif\":true,\"defer_figures\":true}");

  request.classify = false;
  request.pad_classifier = true;
  request.gif = false;
  request.name = "줄\n바꿈";
  const std::string line = hwlab::platform::BuildPerfusionRequestLine(request).value();
  EXPECT_NE(line.find("\"name\":\"줄\\n바꿈\""), std::string::npos);
  EXPECT_NE(line.find("\"classify\":false,\"pad_classifier\":true,\"gif\":false,\"defer_figures\":true}"), std::string::npos);
  EXPECT_EQ(line.find('\n'), std::string::npos);
}

// 영상 경로가 있으면 rec_dir 뒤에 left_video, right_video 키를 넣고, 한쪽만 있으면 그 키만 넣습니다.
TEST(PerfusionAnalysis, BuildsRequestLineWithVideoPaths) {
  PerfusionRequest request;
  request.recording_dir = "C:\\촬영\\101500";
  request.left_video = "C:\\촬영\\101500\\101500_left.mkv";
  request.right_video = "C:\\촬영\\101500\\101500_right.mkv";
  request.output_dir = "C:\\촬영\\101500\\perfusion";
  request.name = "n";
  EXPECT_EQ(hwlab::platform::BuildPerfusionRequestLine(request),
            "{\"rec_dir\":\"C:\\\\촬영\\\\101500\","
            "\"left_video\":\"C:\\\\촬영\\\\101500\\\\101500_left.mkv\","
            "\"right_video\":\"C:\\\\촬영\\\\101500\\\\101500_right.mkv\","
            "\"output\":\"C:\\\\촬영\\\\101500\\\\perfusion\",\"name\":\"n\",\"cuff_side\":\"none\","
            "\"template\":\"v106w\",\"classify\":true,\"pad_classifier\":false,\"gif\":true,\"defer_figures\":true}");

  // 장 수는 영상 경로 뒤에 정수로 넣습니다.
  request.left_frames = 900;
  request.right_frames = 899;
  const std::string with_frames = hwlab::platform::BuildPerfusionRequestLine(request).value();
  EXPECT_NE(with_frames.find("101500_right.mkv\",\"left_frames\":900,\"right_frames\":899,\"output\":"),
            std::string::npos);
}

// 영상 경로를 한쪽만 채우거나 장 수가 음수인 요청은 만들지 않고 이유를 돌려줍니다.
TEST(PerfusionAnalysis, RejectsOneSidedVideoPathOrNegativeFrames) {
  PerfusionRequest request;
  request.recording_dir = "C:\\촬영";
  request.right_video = "C:\\촬영\\right.mkv";
  std::string error;
  EXPECT_EQ(hwlab::platform::BuildPerfusionRequestLine(request, &error), std::nullopt);
  EXPECT_FALSE(error.empty());

  request.right_video.clear();
  request.left_video = "C:\\촬영\\left.mkv";
  EXPECT_EQ(hwlab::platform::BuildPerfusionRequestLine(request), std::nullopt);

  request.right_video = "C:\\촬영\\right.mkv";
  request.left_frames = -1;
  error.clear();
  EXPECT_EQ(hwlab::platform::BuildPerfusionRequestLine(request, &error), std::nullopt);
  EXPECT_FALSE(error.empty());

  // 경로 없이 장 수만 주면 관류 분석 프로그램이 촬영 폴더의 기본 이름 영상과 비교하므로 받아들입니다.
  PerfusionRequest frames_only;
  frames_only.left_frames = 900;
  frames_only.right_frames = 900;
  const std::string line = hwlab::platform::BuildPerfusionRequestLine(frames_only).value();
  EXPECT_EQ(line.find("_video"), std::string::npos);
  EXPECT_NE(line.find("\"left_frames\":900,\"right_frames\":900"), std::string::npos);
}

// 미리 올리기 요청은 관류 분석 프로그램이 preload 키로 알아보는 한 줄입니다.
TEST(PerfusionAnalysis, BuildsPreloadLine) {
  EXPECT_EQ(hwlab::platform::BuildPreloadLine(), "{\"preload\":true}");
}

// 미리 분할 요청은 좌우 경로를 이스케이프해 담습니다.
TEST(PerfusionAnalysis, BuildsPresegmentLine) {
  EXPECT_EQ(hwlab::platform::BuildPresegmentLine("C:\\촬영\\a_left.mkv", "b\"r.mkv"),
            "{\"presegment\":true,\"left_video\":\"C:\\\\촬영\\\\a_left.mkv\",\"right_video\":\"b\\\"r.mkv\"}");
}

// 미리 분할 결과 줄은 JSON을 문구로 둡니다.
TEST(PerfusionAnalysis, ParsesPresegmentedLine) {
  const AnalyzerLine line = ParseAnalyzerLine("[dfr] presegmented {\"ok\": false, \"error\": \"x\"}");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kPresegmented);
  EXPECT_EQ(line.message, "{\"ok\": false, \"error\": \"x\"}");
}

// 미룬 그림을 다 그렸다는 줄은 JSON을 문구로 둡니다.
TEST(PerfusionAnalysis, ParsesFiguresLine) {
  const AnalyzerLine line = ParseAnalyzerLine("[dfr] figures {\"ok\": true, \"seconds\": 2.1}");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kFigures);
  EXPECT_EQ(line.message, "{\"ok\": true, \"seconds\": 2.1}");
}

// 진행 단계 줄에서 번호와 문구를 읽고 끝의 경과 시간을 제거합니다.
TEST(PerfusionAnalysis, ParsesStageAndStripsElapsed) {
  const AnalyzerLine line = ParseAnalyzerLine("[3/5] Computing perfusion maps... [12 s]");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kStage);
  EXPECT_EQ(line.step, 3);
  EXPECT_EQ(line.total, 5);
  EXPECT_EQ(line.message, "Computing perfusion maps...");

  const AnalyzerLine zero = ParseAnalyzerLine("[0/5] Loading");
  EXPECT_EQ(zero.kind, AnalyzerLineKind::kStage);
  EXPECT_EQ(zero.step, 0);
  EXPECT_EQ(zero.message, "Loading");
}

// 들여 쓴 `[k/n]`은 하위 단계입니다. 끝의 `(0 s)`는 떼고, 문구 안의 괄호는 둡니다.
TEST(PerfusionAnalysis, ParsesIndentedSubStage) {
  const AnalyzerLine sub = ParseAnalyzerLine("  [3/7] thermal foot masks: RGB mask -> thermal registration  (0 s)");
  EXPECT_EQ(sub.kind, AnalyzerLineKind::kSubStage);
  EXPECT_EQ(sub.step, 3);
  EXPECT_EQ(sub.total, 7);
  EXPECT_EQ(sub.message, "thermal foot masks: RGB mask -> thermal registration");

  const AnalyzerLine kept = ParseAnalyzerLine("  [1/7] thermal: reading (started in the background)  (0 s)");
  EXPECT_EQ(kept.kind, AnalyzerLineKind::kSubStage);
  EXPECT_EQ(kept.message, "thermal: reading (started in the background)");

  const AnalyzerLine tab = ParseAnalyzerLine("\t[7/7] inference (12 s)");
  EXPECT_EQ(tab.kind, AnalyzerLineKind::kSubStage);
  EXPECT_EQ(tab.step, 7);
  EXPECT_EQ(tab.message, "inference");
  // 들여 쓴 줄의 깊이 표시(`[2/7] ... (8 of 12 members)`)처럼 숫자 밖의 글자가 있으면 떼지 않습니다.
  EXPECT_EQ(ParseAnalyzerLine("  [2/7] model (8 of 12 members)").message, "model (8 of 12 members)");
}

// 번호가 아닌 대괄호, 전체보다 큰 번호, 전체가 0인 줄은 진행 단계가 아닙니다.
TEST(PerfusionAnalysis, RejectsNonStageBrackets) {
  EXPECT_EQ(ParseAnalyzerLine("  [a/7] thermal").kind, AnalyzerLineKind::kOther);
  EXPECT_EQ(ParseAnalyzerLine("[WARN] something").kind, AnalyzerLineKind::kOther);
  EXPECT_EQ(ParseAnalyzerLine("[a/5] x").kind, AnalyzerLineKind::kOther);
  EXPECT_EQ(ParseAnalyzerLine("[6/5] x").kind, AnalyzerLineKind::kOther);
  EXPECT_EQ(ParseAnalyzerLine("[1/0] x").kind, AnalyzerLineKind::kOther);
  const AnalyzerLine other = ParseAnalyzerLine("  Window 3/11: t=5.5s");
  EXPECT_EQ(other.kind, AnalyzerLineKind::kOther);
  EXPECT_EQ(other.message, "  Window 3/11: t=5.5s");
}

// 파이썬 json.dumps가 찍는 완료 줄(쉼표와 콜론 뒤 공백)을 읽습니다.
TEST(PerfusionAnalysis, ParsesDoneSuccess) {
  const AnalyzerLine line = ParseAnalyzerLine("[dfr] done {\"ok\": true, \"error\": \"\"}");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kDone);
  EXPECT_TRUE(line.ok);
  EXPECT_EQ(line.error, "");
}

// 실패한 완료 줄의 오류 문장에서 줄바꿈, 역슬래시, 따옴표, 유니코드 이스케이프를 풉니다.
TEST(PerfusionAnalysis, ParsesDoneFailureWithEscapes) {
  const AnalyzerLine line = ParseAnalyzerLine(
      "[dfr] done {\"ok\": false, \"error\": \"Traceback\\n  File \\\"C:\\\\\\ud55c\\uae00.py\\\"\\n\\ud83d\\ude00\"}");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kDone);
  EXPECT_FALSE(line.ok);
  EXPECT_EQ(line.error, "Traceback\n  File \"C:\\한글.py\"\n\xF0\x9F\x98\x80");
}

// 완료 줄의 JSON이 깨졌으면 실패로 보고 원문을 이유에 남깁니다.
TEST(PerfusionAnalysis, MalformedDoneIsFailure) {
  const AnalyzerLine line = ParseAnalyzerLine("[dfr] done {\"ok\": tru");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kDone);
  EXPECT_FALSE(line.ok);
  EXPECT_NE(line.error.find("{\"ok\": tru"), std::string::npos);

  const AnalyzerLine missing = ParseAnalyzerLine("[dfr] done {}");
  EXPECT_FALSE(missing.ok);
  EXPECT_FALSE(missing.error.empty());
}

// 관류 분석 프로그램 표식 줄(ready, preloaded, preload-failed, idle-exit)을 구분하고 모르는 표식은 기록용으로 둡니다.
TEST(PerfusionAnalysis, ParsesWorkerMarks) {
  const AnalyzerLine plain = ParseAnalyzerLine("[dfr] ready");
  EXPECT_EQ(plain.kind, AnalyzerLineKind::kReady);
  EXPECT_EQ(plain.worker_version, "");
  EXPECT_EQ(ParseAnalyzerLine("[dfr] preloaded").kind, AnalyzerLineKind::kPreloaded);
  const AnalyzerLine failed = ParseAnalyzerLine("[dfr] preload-failed 모델 파일이 없습니다");
  EXPECT_EQ(failed.kind, AnalyzerLineKind::kPreloadFailed);
  EXPECT_EQ(failed.message, "모델 파일이 없습니다");
  EXPECT_EQ(ParseAnalyzerLine("[dfr] idle-exit").kind, AnalyzerLineKind::kIdleExit);
  EXPECT_EQ(ParseAnalyzerLine("[dfr] stamp-failed x").kind, AnalyzerLineKind::kOther);
}

// ready 뒤의 버전 JSON을 읽고, JSON이 깨졌거나 키가 없어도 준비 완료로 봅니다.
TEST(PerfusionAnalysis, ParsesReadyWithVersion) {
  const AnalyzerLine line =
      ParseAnalyzerLine("[dfr] ready {\"worker_version\": \"2026.10.19\", \"upstream_commit\": \"5e5178d\"}");
  EXPECT_EQ(line.kind, AnalyzerLineKind::kReady);
  EXPECT_EQ(line.worker_version, "2026.10.19");
  EXPECT_EQ(line.upstream_commit, "5e5178d");

  const AnalyzerLine broken = ParseAnalyzerLine("[dfr] ready {\"worker_version\": ");
  EXPECT_EQ(broken.kind, AnalyzerLineKind::kReady);
  EXPECT_EQ(broken.worker_version, "");
  EXPECT_EQ(ParseAnalyzerLine("[dfr] ready {\"other\": 1}").kind, AnalyzerLineKind::kReady);
  EXPECT_EQ(ParseAnalyzerLine("[dfr] readyx").kind, AnalyzerLineKind::kOther);
}

// 요약 JSON(json.dump indent=1 꼴, NaN과 배열 포함)의 dfr 항목에서 버전 값만 읽고, 없으면 빈 값입니다.
TEST(PerfusionAnalysis, ReadsWorkerStampFromSummary) {
  const std::string summary =
      "{\n \"n_frames\": 900,\n \"fps\": 30.0,\n \"pi\": NaN,\n \"windows\": [1.5, -Infinity, {\"upstream_commit\": \"x\"}],\n"
      " \"classification\": {\"decision\": \"normal\", \"upstream_commit\": \"wrong\"},\n"
      " \"dfr\": {\n  \"upstream_commit\": \"5e5178d3836c\",\n  \"detector_edge\": 400,\n"
      "  \"models\": null,\n  \"worker_version\": \"2026.10.19\"\n }\n}\n";
  const hwlab::platform::WorkerStamp stamp = hwlab::platform::ReadWorkerStamp(summary);
  EXPECT_EQ(stamp.upstream_commit, "5e5178d3836c");
  EXPECT_EQ(stamp.worker_version, "2026.10.19");

  const hwlab::platform::WorkerStamp old = hwlab::platform::ReadWorkerStamp("{\"dfr\": {\"upstream_commit\": \"abc\"}}");
  EXPECT_EQ(old.upstream_commit, "abc");
  EXPECT_EQ(old.worker_version, "");
  EXPECT_EQ(hwlab::platform::ReadWorkerStamp("{\"pi\": 1}").upstream_commit, "");
  EXPECT_EQ(hwlab::platform::ReadWorkerStamp("{\"dfr\": {\"upstream_commit\": ").upstream_commit, "");
}

// 요약 JSON 경로는 결과 폴더와 이름으로 정해지고, 판별 결과 폴더는 perfusion 아래 판 이름입니다.
TEST(PerfusionAnalysis, SummaryPath) {
  EXPECT_EQ(hwlab::platform::PerfusionSummaryPath("C:/촬영/perfusion", "이름"), "C:/촬영/perfusion/이름_summary.json");
  EXPECT_EQ(hwlab::platform::PerfusionSummaryPath("out/", "n"), "out/n_summary.json");
  EXPECT_EQ(hwlab::platform::PerfusionOutputDir("C:/촬영/101500", ""), "C:/촬영/101500/perfusion");
  EXPECT_EQ(hwlab::platform::PerfusionOutputDir("C:/촬영/101500/", "v7"), "C:/촬영/101500/perfusion/v7");
}

}  // namespace
