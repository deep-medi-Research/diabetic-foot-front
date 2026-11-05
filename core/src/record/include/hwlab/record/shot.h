#ifndef HWLAB_RECORD_SHOT_H_
#define HWLAB_RECORD_SHOT_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "hwlab/record/sidecar.h"
#include "hwlab/stream.h"

namespace hwlab::record {

/**
 * 촬영의 조명 정보입니다. 매니페스트의 lighting입니다. 출처(LightSource)는 sidecar.h에 있고 사이드카 light와 같은 값을 씁니다.
 *
 * @note brightness_percent는 표 2의 조명 밝기(LED 보드에 보내는 데 성공한 값)입니다. 앱이 보내지 않았으면 비웁니다.
 */
struct LightingInfo {
  LightSource source = LightSource::kUnknown;
  std::string product;                        // 조명 제품. 예: "4000K COB LED, 12V"
  std::optional<int32_t> brightness_percent;  // 앱이 보낸 밝기(%). 확인하지 못했으면 비움
};

/** 열화상 장치가 있는데도 이번 촬영에 열화상을 녹화하지 않은 이유입니다. */
enum class ThermalAbsence : uint8_t {
  kNone,          // 열화상을 녹화함, 또는 이유를 정하지 않음
  kDisabled,      // 실행할 때 열화상을 끔
  kNoDevice,      // 이 OS에 열화상 장치 구현이 없음
  kNotReceiving,  // 촬영 버튼을 누른 시점에 장치를 열지 못했거나 끊김
  kPreparing,     // 촬영 버튼을 누른 시점에 장치를 열고 첫 프레임을 기다리는 중(셔터 교정)
};

/**
 * 촬영 전에 한 부위에서 잰 혈압입니다. 0은 입력하지 않음입니다.
 *
 * @note 커프 압력은 발목 혈압으로 계산하는 발목 혈압 단계로 정합니다. 잰 혈압이 없으면 촬영 파일만으로 커프 압력을 알 수 없습니다.
 */
struct BloodPressure {
  int32_t systolic_mmhg = 0;
  int32_t diastolic_mmhg = 0;
};

/**
 * 받는 혈압 값의 하한(mmHg)입니다.
 *
 * @note 자릿수를 빠뜨린 입력(예: 80을 8로)을 거부하기 위함입니다.
 */
inline constexpr int32_t kMinBloodPressureMmhg = 20;

/**
 * 받는 혈압 값의 상한(mmHg)입니다.
 *
 * @note 혈압계 눈금의 끝(300mmHg)입니다.
 */
inline constexpr int32_t kMaxBloodPressureMmhg = 300;

/**
 * 발목 혈압 단계의 이름입니다. 화면의 버튼 순서와 같습니다.
 *
 * 0은 0mmHg, V는 발목 이완기 - 50, D는 발목 이완기, M은 (발목 수축기 + 발목 이완기) / 2, S는 발목 수축기입니다.
 * @note 관류 분석 프로그램이 내는 커프 단계(`cuff_level.level`, #72)와 같은 글자입니다. 대소문자를 구분합니다.
 */
inline constexpr std::array<std::string_view, 5> kAnkleLevels = {"0", "V", "D", "M", "S"};

/**
 * 발목 혈압 단계의 압력(mmHg)을 발목 혈압으로 계산합니다.
 *
 * @param level 단계 이름. kAnkleLevels 중 하나
 * @param ankle 발목에서 잰 혈압
 * @return 압력(mmHg). M은 0.5 단위일 수 있습니다. level이 목록에 없으면 nullopt
 * @note 계산식은 분석 쪽 요청(2026-10-29, #68)입니다. V는 발목 이완기가 50mmHg보다 낮으면 음수입니다.
 */
std::optional<double> AnkleLevelMmhg(std::string_view level, const BloodPressure& ankle);

/**
 * 촬영 한 번을 가리키는 정보입니다.
 *
 * @note 이름, 생년월일, 혈압은 개인정보라 매니페스트에만 남기고 사이드카에는 넣지 않습니다.
 */
/** 녹화 버튼을 누른 때의 두 발 열화상 대비와 사용자 선택입니다. 매니페스트의 foot_warmth입니다. */
struct FootWarmthRecord {
  int32_t left = 0;          // 환자 왼발 상자의 대비(원시값, pixel::ThermalContrast)
  int32_t right = 0;         // 환자 오른발 상자의 대비
  int32_t min_warm = 0;      // 따뜻하다고 본 아래 끝(pixel::kWarmContrastMin)
  bool cold_confirmed = false;  // 발이 차갑다는 안내에 사용자가 진행을 고름
};

struct ShotInfo {
  std::string patient_name;
  std::string birth_date;  // YYYYMMDD
  BloodPressure arm_pressure;    // 상완에서 잰 혈압
  BloodPressure ankle_pressure;  // 발목에서 잰 혈압
  std::string ankle_level;       // 발목 혈압 단계. kAnkleLevels 중 하나이고, 비어 있으면 고르지 않음
  int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;  // 촬영 시각(PC 현지 시각)
  // 화소 왼쪽(프레임의 x가 작은 쪽)에 찍히는 환자의 발. RGB와 열화상의 좌우 파일 이름을 이 값 하나로 정합니다
  FootSide pixel_left_foot = FootSide::kRight;
  LightingInfo lighting = {};  // 매니페스트의 lighting
  // 열화상을 녹화하지 않은 이유. 매니페스트의 thermal_absent_reason이 됩니다(열화상 역할이 배치에 없을 때만)
  ThermalAbsence thermal_absence = ThermalAbsence::kNone;
  // 열화상 없이 찍는다는 안내에 사용자가 진행을 고름. 장치 고장으로 빠진 촬영과 구분하기 위해 매니페스트에 남깁니다
  bool thermal_skip_confirmed = false;
  // 같은 날, 같은 환자, 같은 발목 혈압 단계에서 몇 번째 촬영인지(1부터). 0이면 세지 않았습니다. 재측정 기록에 씁니다(IRB 측정 실패
  // 사유는 앞 촬영 요약의 qc에 있습니다)
  int32_t attempt = 0;
  // 미리보기 열화상으로 판정한 두 발 대비. 열화상을 녹화하고 판정이 끝났을 때만 있습니다
  std::optional<FootWarmthRecord> foot_warmth;
  // 찍은 앱의 버전(vX.Y.Z, 개발 빌드는 빈 문자열)과 커밋. 같은 촬영을 어느 앱으로 찍었는지 촬영 파일만으로 알기 위함입니다
  std::string app_version;
  std::string app_commit;
  // 촬영 선언 파일의 이름과 SHA-256. 이름만으로는 같은 이름의 파일을 고친 경우를 구분하지 못합니다
  std::string spec_file;
  std::string spec_sha256;
};

/** 촬영 폴더 안 파일의 역할입니다. */
enum class FileRole : uint8_t {
  kLeftVideo,
  kLeftSidecar,
  kRightVideo,
  kRightSidecar,
  kThermalRaw,
  kThermalSidecar,
  kThermalPreview,  // 열화상을 볼 수 있게 옮긴 AVI
  kManifest,
  kFullVideo,    // 자르지 않은 전체 화면 영상
  kFullSidecar,  // 전체 화면 영상의 사이드카
  kThermalLeftArray,   // 왼발 열화상 원시값(.npy). 관류 분석 입력
  kThermalRightArray,  // 오른발 열화상 원시값(.npy). 관류 분석 입력
};

/**
 * 파일 이름 앞에 시각(ShotLayout::stem)을 넣는 역할인지 정합니다.
 *
 * @param role 파일 역할
 * @return 시각을 넣으면 true. 열화상 .npy(thermal_left.npy, thermal_right.npy)만 false입니다
 * @note 관류 분석 코드가 .npy 이름을 고정해 읽으므로 접두 없이 씁니다. 촬영 폴더가 촬영마다 하나라 같은 이름이 생기지 않습니다.
 */
bool UsesStemPrefix(FileRole role);

/**
 * 관류 분석에 넘기는 파일 역할인지 정합니다.
 *
 * @param role 파일 역할
 * @return 좌우 영상과 좌우 열화상 .npy이면 true
 */
bool IsAnalysisInput(FileRole role);

/** 촬영 폴더 안 파일 하나입니다. */
struct ShotFile {
  FileRole role = FileRole::kManifest;
  std::string name;  // 촬영 폴더 안의 파일 이름
};

/** 촬영 폴더와 파일 이름입니다. 경로 구분자는 '/'이고 OS에 맞게 바꾸는 것은 platform이 합니다. */
struct ShotLayout {
  std::string folder;  // 이름_생년월일/발목 혈압 단계/날짜_시각
  std::string stem;    // 폴더의 마지막 이름이자 파일 이름의 앞부분. 보통 날짜_시각(YYYYMMDD_HHMMSS)
  std::vector<ShotFile> files;

  /** 역할에 맞는 파일 이름입니다. 없으면 빈 문자열입니다. */
  std::string NameOf(FileRole role) const;
};

/** 매니페스트에 남기는 장치 하나입니다. */
struct ShotDevice {
  StreamKind stream = StreamKind::kRgb;
  std::string name;  // 장치가 알려 준 이름
};

/** 촬영 정보 검증에서 찾은 문제입니다. */
enum class ShotProblem : uint8_t {
  kEmptyName,
  kBadNameCharacter,  // Windows 파일 이름에 쓸 수 없는 문자, 제어 문자, 앞의 공백, 끝의 점과 공백
  kBadBirthDate,
  kBadArmPressure,      // 상완 혈압이 없거나 받는 범위 밖이거나 수축기가 이완기보다 크지 않음
  kBadAnklePressure,    // 발목 혈압. 기준은 상완과 같음
  kUnknownAnkleLevel,   // 발목 혈압 단계를 고르지 않았거나 목록에 없음
  kNegativeAnkleLevel,  // 발목 혈압 단계의 압력이 0보다 작음(발목 이완기가 50mmHg보다 낮을 때의 v)
  kBadTime,
};

/**
 * 촬영 정보가 폴더와 파일 이름, 매니페스트를 만들 수 있는 값인지 검증합니다.
 *
 * @param info 촬영 정보
 * @return 찾은 문제 목록. 비어 있으면 통과입니다
 * @note 이름에 Windows가 거부하는 문자(< > : " / \ | ? *), 제어 문자, 앞의 공백, 끝의 점과 공백이 있으면 거부합니다.
 *       기존 앱은 이런 문자를 지우고 저장했지만, 지우면 입력한 이름과 폴더 이름이 달라지므로 거부하고 화면이 이유를 보여 줍니다.
 * @note 상완과 발목 혈압은 둘 다 있어야 합니다. 값은 kMinBloodPressureMmhg 이상 kMaxBloodPressureMmhg 이하이고
 *       수축기가 이완기보다 커야 합니다.
 * @note 발목 혈압 단계는 kAnkleLevels 중 하나여야 하고, 발목 혈압으로 계산한 압력이 0 이상이어야 합니다.
 */
std::vector<ShotProblem> ValidateShot(const ShotInfo& info);

/**
 * 촬영 폴더와 파일 이름을 만듭니다. 폴더는 이름_생년월일/발목 혈압 단계/날짜_시각입니다(예: 시험_19551103/D/20261022_101454).
 *
 * 파일 이름은 날짜_시각_역할.확장자입니다. 영상(전체 화면, 왼발, 오른발)의 확장자는 video_extension을 쓰고,
 * 매니페스트는 roles에 없어도 항상 넣습니다. 역할마다의 이름 규칙은 shot.cc의 한 함수(NameRuleOf)에 있고,
 * UsesStemPrefix가 false인 역할은 날짜_시각 없이 고정 이름(thermal_left.npy, thermal_right.npy)입니다.
 *
 * @param info 촬영 정보
 * @param video_extension 영상 파일 확장자(점 없이). 촬영 선언의 코덱에서 flow::ExtensionOf로 구합니다
 * @param roles 이번 촬영이 만드는 파일의 역할. 매니페스트의 파일 목록이 이 순서를 따릅니다
 * @return 폴더와 파일 목록. ValidateShot을 통과하지 않거나 video_extension이 비면 nullopt
 * @note 촬영 데이터를 올리는 NAS 서버의 구조와 같습니다. 기존 앱은 날짜/이름_생년월일/검사 기준/시각이었고(표 11),
 *       그 구조로 저장한 촬영은 SplitShotFolderPath가 함께 읽습니다.
 * @note 2026-10-30 전에는 가운데 단계가 검사 기준(수축기 혈압 대비 %, 0, 40, 80, 120)이었습니다. 커프 없이 찍은 촬영은
 *       전과 같이 0입니다. SplitShotFolderPath는 가운데 단계의 이름을 따지지 않아 두 폴더를 모두 읽습니다.
 */
std::optional<ShotLayout> LayoutOf(const ShotInfo& info, const std::string& video_extension,
                                   const std::vector<FileRole>& roles);

/**
 * 같은 폴더가 이미 있을 때 쓸 번호 붙은 배치를 만듭니다.
 *
 * 폴더의 마지막 이름과 날짜_시각으로 시작하는 파일 이름의 앞부분이 날짜_시각_번호가 됩니다
 * (예: 20260915_142233_2, 20260915_142233_2_full.mkv).
 * UsesStemPrefix가 false인 역할의 이름(thermal_left.npy)은 바꾸지 않습니다.
 * 기존 앱은 같은 초에 다시 찍으면 앞 촬영을 덮어썼습니다. 이 앱은 덮어쓰지 않고 번호를 붙입니다.
 *
 * @param layout LayoutOf의 결과
 * @param number 2 이상의 번호. 2보다 작으면 layout을 그대로 돌려줍니다
 * @return 번호 붙은 배치
 */
ShotLayout NumberLayout(const ShotLayout& layout, int32_t number);

/**
 * 촬영 폴더의 매니페스트(JSON)를 만듭니다. 환자 정보, 촬영 시각, 화소 왼쪽 발, 조명, 열화상 녹화 여부, 장치 목록,
 * 파일 목록, 분석 입력 역할 목록을 한 번만 담습니다.
 *
 * 열화상 녹화 여부(thermal_recorded)는 배치에 열화상 원시값 역할이 있는지로 정합니다. 없으면 thermal_absent_reason에
 * info.thermal_absence("disabled", "no_device", "not_receiving", "preparing", 정하지 않았으면 "unknown")와
 * thermal_skip_confirmed를 씁니다.
 *
 * 매니페스트는 녹화 전에 쓰므로 파일 목록과 분석 입력 목록은 이 촬영이 만들 파일입니다. 실제로 만들었는지는 파일이
 * 있는지로 확인합니다(CheckAnalysisInputs).
 *
 * @param info 촬영 정보
 * @param layout LayoutOf 또는 NumberLayout의 결과
 * @param devices 이번 촬영에 쓴 장치. 스트림 종류(rgb, thermal)와 이름을 남깁니다
 * @return JSON 문자열(UTF-8). 같은 입력에는 같은 바이트를 냅니다
 */
std::string BuildManifestJson(const ShotInfo& info, const ShotLayout& layout, const std::vector<ShotDevice>& devices);

/**
 * 분석 결과를 모으는 폴더 이름입니다. 저장 위치 바로 아래에 둡니다.
 *
 * @note 기존 앱은 "분석결과"였습니다. 경로를 영문으로 두어 압축, 메일, 다른 프로그램에서 이름이 깨지지 않게 합니다(표 11).
 */
inline constexpr char kCollectedResultFolder[] = "results";

/** 다시 분석한 결과를 모을 때 쓰는 번호의 상한입니다. 경로 길이 검사도 이 번호까지 셉니다. */
inline constexpr int32_t kMaxReanalysisNumber = 99;

/**
 * 분석결과 폴더에 모으는 결과 파일 이름의 앞부분을 만듭니다.
 *
 * 촬영 폴더 경로의 단계(이름_생년월일, 발목 혈압 단계, 날짜_시각, 같은 초 재촬영 번호)를 '_'로 이은 것입니다.
 * 예: 폴더 시험_19551103/0/20261020_102528이면 시험_19551103_0_20261020_102528, 번호 2이면 끝이 102528_2입니다.
 * 결과 파일은 이 앞부분 뒤에 관류 분석 프로그램이 정한 뒷부분(_summary.json, _perfusion.gif 등)을 이어 씁니다.
 * 같은 촬영을 다시 분석해 내용이 다른 결과는 덮어쓰지 않고 앞부분 끝에 _reanalysis2, _reanalysis3을 더한 이름으로 모읍니다.
 *
 * @param layout LayoutOf 또는 NumberLayout의 결과
 * @param reanalysis 몇 번째 모음인지. 1 이하이면 번호를 더하지 않고, 2 이상이면 끝에 `_reanalysis<번호>`를 더합니다
 * @return 앞부분. layout.folder가 비면 빈 문자열
 * @note 날짜_시각만 쓰면 다른 환자의 같은 시각 촬영이 같은 이름이 되므로 폴더 경로 전체를 씁니다.
 *       촬영 폴더 경로는 저장 위치 아래에서 촬영마다 다르므로 이 앞부분도 촬영마다 다릅니다.
 *       이전 배치(날짜/이름_생년월일/기준/시각)로 저장한 촬영은 그 단계 순서대로 날짜_이름_생년월일_기준_시각이 되므로,
 *       results 폴더에는 두 순서의 이름이 함께 있습니다.
 */
std::string CollectedResultPrefix(const ShotLayout& layout, int32_t reanalysis = 1);

/** 촬영 폴더 경로를 나눈 결과입니다. */
struct ShotFolderPath {
  std::string root;    // 저장 위치. 끝 구분자는 없습니다
  ShotLayout layout;   // folder와 stem만 채웁니다. files는 비어 있습니다
};

/**
 * 촬영 폴더 경로를 저장 위치와 폴더 배치로 나눕니다.
 *
 * 마지막 단계가 날짜_시각(YYYYMMDD_HHMMSS)이거나 그 뒤에 _번호가 붙은 모양이면 지금 배치(이름_생년월일/기준/날짜_시각)로 보고
 * 마지막 세 단계를, 아니면 이전 배치(날짜/이름_생년월일/기준/시각)로 보고 마지막 네 단계를 layout.folder로, 그 앞을 저장 위치로
 * 봅니다.
 *
 * @param path 촬영 폴더 경로(UTF-8). 구분자는 '/'와 '\\'를 모두 받고, 끝 구분자는 무시합니다
 * @return 나눈 결과. layout.folder의 구분자는 '/'입니다. 경로의 단계가 배치의 단계보다 적으면 nullopt
 * @note 분석이 끝난 촬영의 결과를 분석결과 폴더로 모을 때 씁니다. 그 사이 화면에서 저장 위치를 바꿔도 촬영 폴더의
 *       저장 위치를 씁니다. 이전 배치도 읽는 이유는 그 배치로 저장한 촬영을 결과 창과 다시 분석이 열기 위함입니다.
 */
std::optional<ShotFolderPath> SplitShotFolderPath(const std::string& path);

}  // namespace hwlab::record

#endif  // HWLAB_RECORD_SHOT_H_
