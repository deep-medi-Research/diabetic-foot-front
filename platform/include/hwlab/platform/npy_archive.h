#ifndef HWLAB_PLATFORM_NPY_ARCHIVE_H_
#define HWLAB_PLATFORM_NPY_ARCHIVE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hwlab::platform {

/**
 * numpy 배열 하나의 모양과 값입니다.
 *
 * 값은 C 순서(마지막 축이 가장 빨리 변함)로 펼쳐 double로 바꿔 둡니다. 0차원 배열은 shape가 비었고 값이 1개입니다.
 */
struct NpyArray {
  /** 축별 길이입니다. 0차원 배열이면 비었습니다. */
  std::vector<int64_t> shape;
  /** C 순서로 펼친 값. 개수는 shape 길이의 곱입니다. */
  std::vector<double> values;
};

/** ReadNpz의 결과입니다. 항목 이름은 `.npy`를 뗀 이름(np.savez의 키)입니다. */
struct NpzContents {
  /** 읽은 배열. 키는 항목 이름입니다. */
  std::map<std::string, NpyArray> arrays;
  /**
   * 읽지 않고 건너뛴 항목. 값은 건너뛴 이유입니다(예: 지원하지 않는 dtype `<U5`).
   *
   * @note 문자열 배열(`sides`, `cuff_side`)과 Fortran 순서 다차원 배열, `.npy`가 아닌 항목이 여기에 들어갑니다.
   */
  std::map<std::string, std::string> skipped;
};

/**
 * `.npy` 바이트 하나를 읽습니다.
 *
 * 머리 버전 1.0, 2.0, 3.0을 읽습니다. dtype은 리틀 엔디언 `<f8`, `<f4`, `<i8`, `<i4`만 값으로 바꿉니다.
 *
 * @param bytes `.npy` 파일 전체 바이트
 * @param skip_reason 형식은 맞지만 지원하지 않는 dtype이나 순서이면 이유를 쓰고 nullopt를 돌려줍니다. nullptr이면 쓰지 않습니다
 * @param error 형식이 틀리면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 읽은 배열. 형식이 틀리거나 지원하지 않으면 nullopt. 둘은 skip_reason과 error 중 어느 쪽이 채워졌는지로 구별합니다
 */
std::optional<NpyArray> ReadNpy(std::string_view bytes, std::string* skip_reason, std::string* error = nullptr);

/**
 * 메모리에 있는 `.npz`(ZIP) 바이트를 읽습니다.
 *
 * 중앙 디렉터리에서 항목의 크기와 위치를 읽으므로 데이터 기술자(data descriptor)가 붙은 항목도 읽습니다. 크기나 위치가
 * 32비트를 넘으면 zip64 레코드와 zip64 추가 필드를 읽습니다. 항목마다 CRC32를 확인합니다.
 *
 * @param bytes `.npz` 파일 전체 바이트
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return 배열과 건너뛴 항목. ZIP 구조가 틀리거나, 압축(deflate)되었거나 암호화된 항목이 있거나, CRC가 다르거나,
 *         `.npy` 형식이 틀린 항목이 있으면 nullopt
 * @note np.savez는 무압축(stored)으로 씁니다. np.savez_compressed가 쓴 파일은 zlib 없이 풀 수 없어 오류로 돌려줍니다.
 */
std::optional<NpzContents> ReadNpzBytes(std::string_view bytes, std::string* error = nullptr);

/**
 * `.npz` 파일을 읽습니다. 파일을 연 뒤 ReadNpzBytes와 같습니다.
 *
 * @param path_utf8 파일 경로(UTF-8). Windows에서도 한글 경로를 엽니다
 * @param error 읽지 못하면 이유를 씁니다. nullptr이면 쓰지 않습니다
 * @return ReadNpzBytes의 결과. 파일을 열지 못하면 nullopt
 * @note 파일 전체를 메모리로 읽습니다. 파일 입출력이 있으므로 화면 스레드에서 부르지 않습니다.
 */
std::optional<NpzContents> ReadNpz(const std::string& path_utf8, std::string* error = nullptr);

}  // namespace hwlab::platform

#endif  // HWLAB_PLATFORM_NPY_ARCHIVE_H_
