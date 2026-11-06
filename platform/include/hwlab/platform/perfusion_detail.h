#ifndef HWLAB_PLATFORM_PERFUSION_DETAIL_H_
#define HWLAB_PLATFORM_PERFUSION_DETAIL_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hwlab/platform/npy_archive.h"
#include "hwlab/platform/perfusion_summary.h"

namespace hwlab::platform {

/**
 * 관류 분석 프로그램이 쓴 `<name>_perfusion.npz`에서 결과 창이 그래프로 그리는 값입니다.
 *
 * 맥파 템플릿은 영상 장마다 값이 하나이고, 나머지 열은 시간 창마다 값이 하나입니다. 좌우는 피험자 기준입니다.
 * 관류지수(PI) 계열 값은 퍼센트입니다.
 *
 * @note 배열 이름과 모양은 관류 분석 프로그램의 np.savez 호출이 정합니다.
 */
struct PerfusionDetail {
  /** `time_s`: 시간 창 가운데 시각(초, 영상 첫 장 기준). 길이가 시간 창 수입니다. */
  std::vector<double> window_centers_s;
  /** `template_left`: 왼발 맥파 템플릿. 영상 장마다 값 하나입니다. */
  std::vector<double> template_left;
  /** `template_right`: 오른발 맥파 템플릿. 길이는 template_left와 같습니다. */
  std::vector<double> template_right;
  /** `fps`: 관류 분석 프로그램이 쓴 영상 fps. 없으면 kNoNumber입니다. 템플릿의 가로축(초)은 장 번호 / fps입니다. */
  double fps = kNoNumber;
  /** `template_corr`: 시간 창별 좌우 템플릿 상관계수. 없으면 비었습니다. */
  std::vector<double> template_corr;
  /** `n_light_ref`: 시간 창별 조명 기준 영역 수. 0이면 그 창은 POS 맥파로 PI를 구했습니다. 없으면 비었습니다. */
  std::vector<int32_t> light_ref_count;
  /**
   * 왼발의 시간 창별 지표. 키는 지표 이름이고 배열 이름 `<키>_left`에서 왔습니다.
   *
   * @note 관류 분석 프로그램이 쓰는 키: `pi`, `pi_toes`, `pi_fore`, `pi_mid`, `pi_heel`, `pi_green_legacy`, `pi_pos_lockin`,
   *       `pi_pos_std`, `pi_ref_std`. 관류 분석 프로그램 판에 따라 키가 더 있거나 빠질 수 있습니다. 값을 못 구한 창은 NaN입니다.
   */
  std::map<std::string, std::vector<double>> left_series;
  /** 오른발의 시간 창별 지표. 키는 left_series와 같은 규칙으로 배열 이름 `<키>_right`에서 왔습니다. */
  std::map<std::string, std::vector<double>> right_series;
  /**
   * `maps`: 시간 창별 관류 지도(관류지수 %, 발 영역 격자). 순서는 [발(0 왼발, 1 오른발)][시간 창][행][열]이고
   * 발 영역 밖 칸은 NaN입니다. 없으면 비었습니다.
   *
   * @note 옛 형식은 영상의 발 영역(좌우반전 전)을 32 x 16으로 나눈 격자이고, 표준 발 틀 형식은 표준 발 틀을 40 x 15로 나눈 격자입니다.
   *       표준 발 틀 격자의 행은 발끝(0)에서 뒤꿈치까지 발 길이를 같은 간격으로 나누고, 열은 그 행의 발 폭을 같은 간격으로 나눕니다.
   */
  std::vector<double> maps;
  int32_t map_rows = 0;
  int32_t map_cols = 0;
  /** `map_valid`: 표준 발 틀 격자에서 PI를 계산하는 칸이면 1입니다. 순서는 [행][열]입니다. 없으면 비었습니다. */
  std::vector<uint8_t> map_valid;
  /**
   * `outline`: 표준 발 틀의 윤곽입니다. outline_u는 발끝 0에서 뒤꿈치 1까지의 길이 위치이고, outline_left와
   * outline_right는 그 위치의 왼쪽과 오른쪽 끝(발 길이를 1로 둔 가로 위치)입니다. 셋의 길이는 같습니다. 없으면 비었습니다.
   */
  std::vector<double> outline_u;
  std::vector<double> outline_left;
  std::vector<double> outline_right;

  /** 표준 발 틀 지도를 그릴 값(maps, map_valid, outline)이 모두 있으면 true입니다. */
  bool HasTemplateMap() const;

  /** 관류 지도 한 칸의 값입니다. 범위를 벗어나면 NaN입니다. */
  double MapAt(int32_t foot, int32_t window, int32_t row, int32_t col) const;
};

/**
 * 분석 결과 배열 파일의 경로입니다.
 *
 * @param output_dir 분석 요청의 결과 폴더
 * @param name 분석 요청의 결과 이름
 * @return `<output_dir>/<name>_perfusion.npz`. 구분자는 '/'입니다
 */
std::string PerfusionDetailPath(const std::string& output_dir, const std::string& name);

/**
 * 읽은 `.npz` 내용에서 결과 창 값을 꺼냅니다.
 *
 * @param contents ReadNpz 또는 ReadNpzBytes의 결과
 * @param error 꺼내지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 꺼낸 값. `time_s`, `template_left`, `template_right`가 없거나 1차원이 아니거나 비었으면, 두 템플릿 길이가
 *         다르면, `template_corr`, `n_light_ref`, 지표 배열 중 길이가 시간 창 수와 다른 것이 있으면 nullopt
 * @note 이름이 `_left`, `_right`로 끝나는 1차원 배열 중 템플릿이 아닌 것을 모두 지표로 읽습니다. 없는 지표는 맵에 없습니다.
 */
std::optional<PerfusionDetail> PerfusionDetailOf(const NpzContents& contents, std::string* error = nullptr);

/**
 * 분석 결과 배열 파일을 읽습니다. ReadNpz 뒤 PerfusionDetailOf와 같습니다.
 *
 * @param path `<name>_perfusion.npz` 경로(UTF-8)
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 읽은 값. 파일을 열지 못하거나 형식이 틀리거나 PerfusionDetailOf가 실패하면 nullopt
 * @note 파일 입출력이 있으므로 화면 스레드에서 부르지 않습니다.
 */
std::optional<PerfusionDetail> ReadPerfusionDetail(const std::string& path, std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_PERFUSION_DETAIL_H_
