// hwlab_app: 당뇨발 촬영 앱의 시작점입니다.
//
// 명령줄 인자의 이름, 값, 설명은 cli_args.cc의 표 하나가 원본입니다. hwlab_app --help로 전체를 보고, 같은 표가 docs/debugging.md에 들어갑니다.
// 인자를 더하면 그 표에도 더합니다. 표에 없는 인자는 시작할 때 거절됩니다.
//
// 다음 실행에 남기는 "기억한 값"은 --fake, --screenshot, --close-after, --light-remember, --ppg-remember 중 하나라도 준 시험 실행에서는
// 지난 값을 읽지 않고 바꾼 값도 남기지 않습니다. 장비 PC의 실제 값을 시험이 덮어쓰지 않게 하기 위함입니다(아래 trial).
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include "analysis_controller.h"
#include "analysis_variants.h"
#include "app_log.h"
#include "app_profile.h"
#include "app_version.h"
#include "capture_controller.h"
#include "cli_args.h"
#include "console_output.h"
#include "light_controller.h"
#include "result_view.h"
#include "scripted_ppg_device.h"
#include "late_rgb_camera.h"
#include "remembered_values.h"
#include "scripted_rgb_camera.h"
#include "scripted_thermal_camera.h"
#include "shutdown_watch.h"
#include "single_instance.h"
#include "hwlab/flow/capture_spec.h"
#include "hwlab/pixel/side_regions.h"
#include "hwlab/platform/analyzer.h"
#include "hwlab/platform/fake_camera.h"
#include "hwlab/platform/fake_serial_port.h"
#include "hwlab/platform/fake_thermal_camera.h"
#include "hwlab/platform/light.h"
#include "hwlab/platform/serial_port.h"
#include "hwlab/platform/thermal_camera.h"
#include "hwlab/record/shot.h"

// windows.h는 매크로(ERROR, min, max 등)가 앞 헤더와 겹치지 않게 마지막에 넣습니다.
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

// 창을 닫은 뒤 장치를 닫는 정리를 기다리는 최대 시간입니다(shutdown_watch.h).
constexpr int kTeardownLimitMs = 15000;

QString Arg(const QStringList& args, const QString& name) {
  const int i = args.indexOf(name);
  return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
}

// "x,y,w,h,x,y,w,h"를 좌우 영역으로 읽습니다. 숫자가 여덟 개가 아니면 nullopt입니다.
std::optional<hwlab::pixel::SideRegions> ParseRegions(const QString& text) {
  const QStringList parts = text.split(QLatin1Char(','));
  if (parts.size() != 8) return std::nullopt;
  int32_t v[8];
  for (int i = 0; i < 8; ++i) {
    bool ok = false;
    v[i] = parts[i].trimmed().toInt(&ok);
    if (!ok) return std::nullopt;
  }
  return hwlab::pixel::SideRegions{{v[0], v[1], v[2], v[3]}, {v[4], v[5], v[6], v[7]}};
}

// "VID:PID:일련번호"(16진수 VID, PID)를 읽습니다. 일련번호는 비워 둘 수 있습니다.
std::optional<hwlab::platform::LightPortIdentity> ParseLightIdentity(const QString& text) {
  const QStringList parts = text.split(QLatin1Char(':'));
  if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
  bool vid_ok = false;
  bool pid_ok = false;
  const uint vid = parts[0].toUInt(&vid_ok, 16);
  const uint pid = parts[1].toUInt(&pid_ok, 16);
  if (!vid_ok || !pid_ok || vid > 0xFFFF || pid > 0xFFFF) return std::nullopt;
  return hwlab::platform::LightPortIdentity{static_cast<uint16_t>(vid), static_cast<uint16_t>(pid),
                                            parts.size() == 3 ? parts[2].toStdString() : std::string()};
}

// 다음 실행에 남기지 않는 시험 실행인지입니다. 시험 실행에서 바꾼 값이 장비 PC의 실제 값을 덮어쓰지 않게 하기 위함입니다.
// --out은 설치 바로가기도 주므로 시험 실행을 가르는 데 쓰지 않습니다.
// 시험용으로 준 변환기(--light-remember, --ppg-remember)로 확인한 포트도 실제 실행에 남기지 않습니다.
bool IsTrialRun(const QStringList& args) {
  for (const char* name : {"--fake", "--screenshot", "--close-after", "--light-remember", "--ppg-remember"}) {
    if (args.contains(QLatin1String(name))) return true;
  }
  return false;
}

// 로그 맨 앞에 남기는 시작 정보입니다. 이 로그가 어느 빌드의 어떤 실행이고 설정과 기록이 어디에 있는지 보입니다.
// 콘솔에는 로그 파일 경로 한 줄만 보이고 나머지는 파일에만 남습니다(명령 프롬프트에서 켠 오류 메시지가 묻히지 않게 하기 위함).
void LogStartup(const QStringList& args, const QString& data_dir, const profile::Choice& profile) {
  const auto detail = [](const QString& text) { app_log::InfoToFileOnly(text); };
  const QString version = QGuiApplication::applicationVersion();
  detail(QStringLiteral("hwlab_app %1 시작(PID %2)")
             .arg(version.isEmpty() ? QStringLiteral("(버전 없음)") : version)
             .arg(QCoreApplication::applicationPid()));
  detail(QStringLiteral("실행 파일: %1").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
  detail(QStringLiteral("인자: %1").arg(cli::MaskedForLog(args).mid(1).join(QLatin1Char(' '))));
  detail(QStringLiteral("Qt %1, 화면 플랫폼 %2, %3")
             .arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName(), QSysInfo::prettyProductName()));
  detail(QStringLiteral("프로필: %1").arg(profile::Describe(profile)));
  detail(QStringLiteral("앱 데이터 폴더: %1").arg(QDir::toNativeSeparators(data_dir)));
  detail(QStringLiteral("설정 위치: %1%2")
             .arg(QDir::toNativeSeparators(QSettings().fileName()),
                  IsTrialRun(args) ? QStringLiteral(" (시험 실행이라 이번에는 읽지도 쓰지도 않음)") : QString()));
  const QString log_file = app_log::CurrentFilePath();
  qInfo("로그 파일: %s",
        log_file.isEmpty() ? "없음(로그 폴더를 만들지 못함)" : qUtf8Printable(QDir::toNativeSeparators(log_file)));
}

// 촬영 단계, 저장, 오류, 분석 끝을 로그에 남깁니다. 모두 화면 스레드에서 받는 신호이고 프레임마다 오는 신호는 쓰지 않습니다.
void LogEvents(CaptureController* controller, AnalysisVariants* analysis, QObject* context) {
  QObject::connect(controller, &CaptureController::phaseChanged, context,
                   [controller] { qInfo("촬영 단계: %s", qUtf8Printable(controller->phase())); });
  QObject::connect(controller, &CaptureController::errorChanged, context, [controller] {
    if (!controller->error().isEmpty()) qWarning("촬영 오류: %s", qUtf8Printable(controller->error()));
  });
  QObject::connect(controller, &CaptureController::rgbSaved, context, [](const QString& folder, bool ready) {
    qInfo("RGB 영상 저장 끝: %s (분석 입력 %s)", qUtf8Printable(QDir::toNativeSeparators(folder)), ready ? "통과" : "통과하지 못함");
  });
  QObject::connect(controller, &CaptureController::shotSaved, context, [](const QString& folder, bool ready) {
    qInfo("촬영 저장 끝: %s (분석 입력 %s)", qUtf8Printable(QDir::toNativeSeparators(folder)), ready ? "통과" : "통과하지 못함");
  });
  QObject::connect(analysis, &AnalysisVariants::finished, context,
                   [](const QString& folder, bool ok, const QString& summary, const QString& variant) {
    if (ok) {
      qInfo("분석 성공(%s판): %s", qUtf8Printable(variant), qUtf8Printable(QDir::toNativeSeparators(summary)));
    } else {
      qWarning("분석 실패(%s판): %s. 분석 프로그램 출력은 그 폴더의 perfusion 아래 analyzer_log.txt에 있습니다", qUtf8Printable(variant),
               qUtf8Printable(QDir::toNativeSeparators(folder)));
    }
  });
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
  // Windows(D3D11)의 스레드 렌더 루프에서는 미리보기 영상이 바뀔 때마다 앱 메모리가 늘어 풀리지 않았습니다(2026-10-28 노트북,
  // Qt 6.8.1, 미리보기만 켠 채 초당 약 23MB, 5분에 약 6GB). 화면 스레드에서 그리는 basic 루프로는 2분 동안 늘지 않았습니다.
  // 시험하려고 환경 변수를 따로 정한 경우는 그 값을 씁니다. QGuiApplication을 만들기 전에 정해야 합니다.
  if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "basic");
#endif
  QGuiApplication app(argc, argv);
  const QStringList args = app.arguments();
  const QString program = QFileInfo(args.value(0)).fileName();
  // 기억한 값을 남기는 QSettings의 이름입니다(RememberedValues). 앱 데이터 폴더(로그 포함)도 이 이름을 따릅니다.
  // 빌드 폴더에서 실행한 개발 빌드는 프로필 dev로 설치본과 설정과 기록을 나눕니다(app_profile.h). 설치본의 위치와 키는 그대로입니다.
  const profile::Choice profile = profile::Resolve(args, QCoreApplication::applicationDirPath());
  QGuiApplication::setOrganizationName(QStringLiteral("deepmedi"));
  QGuiApplication::setApplicationName(profile::ApplicationName(profile.name));
  const QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (cli::WantsHelp(args)) {
    WriteStdout(cli::HelpText(program, app_log::HelpFooter(data_dir)));
    return 0;
  }
  // 창 제목이 Qt.application.version으로 버전을 보입니다. 개발 빌드는 버전이 비어 있어 빌드한 커밋을 보입니다.
  // 노트북에 설치된 개발 빌드가 어느 코드인지 화면에서 확인하기 위함입니다.
  const QString version = QString::fromUtf8(kAppVersion);
  const QString commit = QString::fromUtf8(kAppCommit);
  QGuiApplication::setApplicationVersion(profile::DisplayVersion(version, commit, profile.name));
  // 이 줄부터 Qt 메시지는 콘솔과 로그 파일에 남습니다. main을 어떤 길로 나가도 남은 줄을 쓰고 끝납니다.
  app_log::Install(app_log::LogDirectory(data_dir));
  struct LogEnd {
    ~LogEnd() { app_log::Shutdown(); }
  } log_end;
  LogStartup(args, data_dir, profile);
  if (const auto problems = cli::Check(args)) {
    qCritical("%s\n모든 인자는 %s --help로 봅니다.", qUtf8Printable(*problems), qUtf8Printable(program));
    return 2;
  }
#ifdef _WIN32
  // 이 레포 밖의 설치 프로그램이 이 이름(Inno Setup의 AppMutex)으로 앱 실행 여부를 확인하고, 실행 중이면 덮어쓰지 않고 닫으라고 안내합니다.
  // 핸들은 프로세스가 끝날 때 Windows가 닫습니다.
  CreateMutexW(nullptr, FALSE, L"HWLabAppRunning");
#endif
  // 플랫폼 기본 모양 대신 모든 PC에서 같게 보이는 스타일을 씁니다.
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  // 글꼴을 앱에 넣어 PC에 Pretendard가 없어도 같게 보이게 합니다(SIL OFL, fonts/Pretendard-LICENSE.txt).
  for (const char* name : {"Regular", "SemiBold", "Bold"}) {
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/HwLab/fonts/Pretendard-%1.otf").arg(QLatin1String(name)));
  }
  // font.family를 빠뜨린 글자도 같은 글꼴로 그리도록 기본 글꼴을 정합니다.
  QGuiApplication::setFont(QFont(QStringLiteral("Pretendard")));
  // 결과 창만 띄우는 개발용 실행입니다. 장치를 열기 전에 끝내 촬영 중인 다른 프로그램의 장치를 건드리지 않습니다.
  if (args.contains(QStringLiteral("--result"))) {
    QQmlApplicationEngine engine;
    ResultView result_view(&engine);
    QString result_error;
    if (!result_view.Open(Arg(args, QStringLiteral("--result")), &result_error)) {
      qWarning("분석 결과를 읽지 못함: %s", qUtf8Printable(result_error));
      if (result_view.Window() == nullptr) return 1;
    }
    if (args.contains(QStringLiteral("--result-variant"))) result_view.setCurrentVariant(Arg(args, QStringLiteral("--result-variant")));
    if (args.contains(QStringLiteral("--result-position"))) {
      const double fraction = std::clamp(Arg(args, QStringLiteral("--result-position")).toDouble(), 0.0, 1.0);
      result_view.setPosition(static_cast<int>(std::lround(fraction * (result_view.frameCount() - 1))));
    }
    const QString result_shot = Arg(args, QStringLiteral("--screenshot"));
    if (!result_shot.isEmpty()) {
      QTimer::singleShot(Arg(args, QStringLiteral("--after")).toInt(), &app, [&] {
        result_view.Window()->grabWindow().save(result_shot);
        app.quit();
      });
    }
    return app.exec();
  }
  const QString spec_path = Arg(args, QStringLiteral("--spec"));
  QByteArray spec_text;
  {
    // 읽은 뒤 바로 닫습니다. 열어 두면 앱이 도는 동안 선언 파일을 바꿀 수 없습니다.
    QFile file(spec_path);
    if (!file.open(QIODevice::ReadOnly)) {
      qCritical("선언 파일을 열지 못함: %s", qUtf8Printable(spec_path));
      return 2;
    }
    spec_text = file.readAll();
  }
  std::string error;
  const auto spec = hwlab::flow::ParseCaptureSpec(spec_text.toStdString(), &error);
  if (!spec) {
    qCritical("선언 파일 오류: %s", error.c_str());
    return 2;
  }
  const bool fake = args.contains(QStringLiteral("--fake"));
  // 장치를 여는 실행은 하나만 돌게 합니다. 이미 있으면 그 창을 보이거나, 종료 중에 멈춘 실행을 끝낼지 묻습니다.
  if (!fake && ClaimDevices() != InstanceClaim::kAlone) return 0;
  InitShutdownLog();
  // 아래에서 만드는 장치와 화면이 모두 사라진 뒤(main을 나갈 때) 마지막으로 남습니다.
  struct TeardownEnd {
    ~TeardownEnd() { LogShutdownStep(QStringLiteral("정리 끝")); }
  } teardown_end;
  // 다음 실행에 남길지는 여기서만 정합니다.
  const bool trial = IsTrialRun(args);
  RememberedValues remembered = trial ? RememberedValues::ThisRunOnly() : RememberedValues::Saved();
  QString out_dir = ShotForm::RememberedRoot(remembered);
  if (out_dir.isEmpty()) out_dir = Arg(args, QStringLiteral("--out"));
  if (out_dir.isEmpty()) out_dir = QDir::current().filePath(QStringLiteral("captures"));

  std::unique_ptr<hwlab::platform::RgbCamera> camera;
  if (fake) {
    hwlab::platform::FakeCameraScript script;
    script.width = spec->width;
    script.height = spec->height;
    script.initial = {false, -5, 4, false, 4600, 60, true};
    script.wall_period_us = 1'000'000 / spec->settings.sdk_frame_rate;
    // 가짜 열화상과 같은 PC 시계를 써야 열화상을 RGB 녹화 구간으로 고를 수 있습니다.
    script.pc_clock = true;
    const bool restore_fails = args.contains(QStringLiteral("--fake-restore-fails"));
    if (restore_fails) script.initial.white_balance_auto = true;
    camera = std::make_unique<hwlab::platform::FakeRgbCamera>(script);
    if (restore_fails) {
      camera = std::make_unique<ScriptedRgbCamera>(std::move(camera), hwlab::flow::CommandKind::kWhiteBalanceAuto);
    }
  } else {
#ifdef HWLAB_HAS_DEVICE_CAMERA
    // 카메라가 없어도 앱은 뜹니다. 화면이 "RGB 카메라를 찾는 중"을 보이고, 연결되면 Capture가 다시 열기로 이어 받습니다.
    camera = std::make_unique<LateRgbCamera>(
        spec->device, [device = spec->device, w = spec->width, h = spec->height,
                       fps = spec->settings.sdk_frame_rate](std::string* problem) {
          return hwlab::platform::RgbCamera::Open(device, w, h, fps, problem);
        });
#else
    error = "이 OS에는 장비 카메라 구현이 없습니다. --fake를 쓰십시오";
    qCritical("카메라 열기 실패: %s", error.c_str());
    return 1;
#endif
  }

  // 열화상 카메라는 열지 않은 채로 넘기고 Capture가 수신 스레드에서 엽니다(장치 탐색과 셔터 교정에 수 초).
  std::unique_ptr<hwlab::platform::ThermalCamera> thermal;
  QString thermal_absent;
  hwlab::record::ThermalAbsence absence = hwlab::record::ThermalAbsence::kNone;
  if (args.contains(QStringLiteral("--no-thermal"))) {
    thermal_absent = QStringLiteral("열화상 끔");
    absence = hwlab::record::ThermalAbsence::kDisabled;
  } else if (fake) {
    hwlab::platform::FakeThermalScript script;
    script.pc_clock = true;
    thermal = std::make_unique<hwlab::platform::FakeThermalCamera>(script);
    const bool absent = args.contains(QStringLiteral("--fake-thermal-absent"));
    const int delay_ms = Arg(args, QStringLiteral("--fake-thermal-delay-ms")).toInt();
    if (absent || delay_ms > 0) thermal = std::make_unique<ScriptedThermalCamera>(std::move(thermal), absent, delay_ms);
  } else {
    thermal = hwlab::platform::MakeDeviceThermalCamera();
    if (!thermal) {
      thermal_absent = QStringLiteral("열화상 카메라 없음");
      absence = hwlab::record::ThermalAbsence::kNoDevice;
    }
  }
  // 렌즈 셰이딩 보정 파일은 앱 데이터 폴더에 둡니다. 설치 폴더 밖이라 업데이트해도 남고, 같은 PC의 같은 카메라는 같은 파일을 씁니다.
  QString lens_shading_folder = Arg(args, QStringLiteral("--lens-shading-folder"));
  if (lens_shading_folder.isEmpty() && !fake) {
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    lens_shading_folder = data.filePath(QStringLiteral("thermal"));
  }
  if (thermal && !lens_shading_folder.isEmpty()) thermal->SetLensShadingFolder(lens_shading_folder.toStdString());
  // LED 포트를 연결하지 않으면 사람이 LED 보드에 전원만 넣고 찍은 것으로 매니페스트와 사이드카에 남깁니다.
  hwlab::record::LightingInfo lighting;
  lighting.source = hwlab::record::LightSource::kExternal;
  lighting.product = "4000K COB LED, 12V";
  const auto videos = args.contains(QStringLiteral("--save-full")) ? hwlab::platform::SavedVideos::kFullAndFeet
                                                                   : hwlab::platform::SavedVideos::kFeet;

  // LED 조명입니다. 포트는 사람이 고르고 LED 밝기가 바뀌는 것을 보고 확인합니다(PPG 장치도 같은 변환기를 쓸 수 있음).
  std::unique_ptr<hwlab::platform::Light> light_device;
  std::function<std::vector<hwlab::platform::SerialPortInfo>()> list_ports;
  if (fake) {
    light_device = std::make_unique<hwlab::platform::Light>(
        hwlab::platform::FakeSerialPort::Opener("COM7", std::make_shared<hwlab::platform::FakeSerialLine>()));
    list_ports = [] {
      return std::vector<hwlab::platform::SerialPortInfo>{{"COM6", "가짜 PPG 장치", 0x0403, 0x6001, "FAKEPPG1A"},
                                                          {"COM7", "가짜 LED 보드", 0x0403, 0x6001, "FAKELED1A"}};
    };
  } else {
#ifdef HWLAB_HAS_DEVICE_CAMERA
    light_device = std::make_unique<hwlab::platform::Light>(hwlab::platform::OpenSerialPort);
    list_ports = hwlab::platform::ListSerialPorts;
#else
    light_device = std::make_unique<hwlab::platform::Light>(
        [](const std::string&, const hwlab::platform::SerialConfig&, std::string* why) {
          if (why != nullptr) *why = "이 OS에는 직렬 포트 구현이 없습니다";
          return std::unique_ptr<hwlab::platform::SerialPort>();
        });
#endif
  }
  const auto remembered_light = ParseLightIdentity(Arg(args, QStringLiteral("--light-remember")));
  const QString remembered_ppg = Arg(args, QStringLiteral("--ppg-remember"));
  LightController light(std::move(light_device), list_ports, lighting.product, &remembered, remembered_light);

  // PPG 장치. 포트는 조명과 같은 목록에서 사람이 고릅니다(같은 종류의 USB 직렬 변환기를 쓸 수 있음).
  hwlab::platform::SerialPortOpener ppg_opener;
  std::unique_ptr<ScriptedPpgDevice> fake_ppg_device;  // ppg보다 먼저 만들어 ppg가 중지 명령을 보낸 뒤에 거둡니다
  if (fake) {
    const auto ppg_line = std::make_shared<hwlab::platform::FakeSerialLine>();
    fake_ppg_device = std::make_unique<ScriptedPpgDevice>(ppg_line);
    ppg_opener = hwlab::platform::FakeSerialPort::Opener("COM6", ppg_line);
  } else {
#ifdef HWLAB_HAS_DEVICE_CAMERA
    ppg_opener = hwlab::platform::OpenSerialPort;
#else
    ppg_opener = [](const std::string&, const hwlab::platform::SerialConfig&, std::string* why) {
      if (why != nullptr) *why = "이 OS에는 직렬 포트 구현이 없습니다";
      return std::unique_ptr<hwlab::platform::SerialPort>();
    };
#endif
  }
  PpgController ppg(ppg_opener, list_ports, &remembered, remembered_ppg);
  // 포트는 공유 없이 열리므로 두 칸이 서로 연 포트를 알려 같은 포트를 고르지 못하게 합니다.
  const auto show_ppg_ports = [&light, &ppg] {
    light.SetPpgPorts(ppg.connected() ? ppg.portName() : QString(), ppg.LastPpgPort());
  };
  QObject::connect(&ppg, &PpgController::stateChanged, &light, show_ppg_ports);
  QObject::connect(&ppg, &PpgController::portsChanged, &light, show_ppg_ports);
  QObject::connect(&light, &LightController::stateChanged, &ppg,
                   [&light, &ppg] { ppg.SetLightPort(light.connected() ? light.portName() : QString()); });

  CaptureController controller(std::move(camera), *spec, out_dir, &remembered, std::move(thermal), thermal_absent,
                               absence, videos, lighting);
  // 시작 중에 끝나는 경로(인자 오류, 시작 실패)에서도 장치를 닫는 소멸자에 제한 시간을 겁니다. 이 객체는 장치보다 먼저 사라집니다.
  struct TeardownLimit {
    ~TeardownLimit() {
      if (ShutdownLimitArmed()) return;
      LogShutdownStep(QStringLiteral("장치 닫기 시작"));
      ArmShutdownLimit(kTeardownLimitMs, QStringLiteral("장치 닫기 %1초").arg(kTeardownLimitMs / 1000));
    }
  } teardown_limit;
  controller.AttachLight(&light);
  controller.AttachPpg(&ppg);
  controller.SetSpecSource(QFileInfo(spec_path).fileName(),
                           QString::fromLatin1(QCryptographicHash::hash(spec_text, QCryptographicHash::Sha256).toHex()));
  const QString foot = Arg(args, QStringLiteral("--pixel-left-foot"));
  if (foot == QStringLiteral("left")) controller.SetPixelLeftFoot(hwlab::record::FootSide::kLeft);
  if (foot == QStringLiteral("right")) controller.SetPixelLeftFoot(hwlab::record::FootSide::kRight);
  if (args.contains(QStringLiteral("--rgb-regions"))) {
    const auto regions = ParseRegions(Arg(args, QStringLiteral("--rgb-regions")));
    if (!regions) {
      qCritical("--rgb-regions는 x,y,w,h,x,y,w,h 형식입니다");
      return 2;
    }
    controller.rgbRegions()->SetRegions(*regions);
  }
  if (args.contains(QStringLiteral("--rgb-area"))) {
    const QStringList parts = Arg(args, QStringLiteral("--rgb-area")).split(QLatin1Char(','));
    int v[4] = {0, 0, 0, 0};
    bool ok = parts.size() == 4;
    for (int i = 0; ok && i < 4; ++i) v[i] = parts[i].trimmed().toInt(&ok);
    if (!ok) {
      qCritical("--rgb-area는 x,y,w,h 형식입니다");
      return 2;
    }
    controller.rgb()->setArea(v[0], v[1], v[2], v[3]);
  }
  if (args.contains(QStringLiteral("--patient"))) controller.shot()->setName(Arg(args, QStringLiteral("--patient")));
  if (args.contains(QStringLiteral("--birth"))) controller.shot()->setBirth(Arg(args, QStringLiteral("--birth")));
  // 이름과 생년월일을 바꾸면 혈압 칸이 비워지므로 그 뒤에 채웁니다.
  if (args.contains(QStringLiteral("--arm-pressure"))) {
    const QStringList parts = Arg(args, QStringLiteral("--arm-pressure")).split(QLatin1Char('/'));
    controller.shot()->setArmSystolic(parts.value(0));
    controller.shot()->setArmDiastolic(parts.value(1));
  }
  if (args.contains(QStringLiteral("--ankle-pressure"))) {
    const QStringList parts = Arg(args, QStringLiteral("--ankle-pressure")).split(QLatin1Char('/'));
    controller.shot()->setAnkleSystolic(parts.value(0));
    controller.shot()->setAnkleDiastolic(parts.value(1));
  }
  if (args.contains(QStringLiteral("--ankle-level"))) {
    controller.shot()->setAnkleLevel(Arg(args, QStringLiteral("--ankle-level")));
  }
  if (args.contains(QStringLiteral("--palette"))) {
    const QString name = Arg(args, QStringLiteral("--palette"));
    const QStringList names = {QStringLiteral("gray"), QStringLiteral("iron"), QStringLiteral("rainbow"),
                               QStringLiteral("high-contrast")};
    if (!names.contains(name)) {
      qCritical("--palette는 iron, gray, rainbow, high-contrast 중 하나입니다");
      return 2;
    }
    // names의 순서는 pixel::Palette의 선언 순서입니다.
    controller.thermal()->SetPalette(static_cast<hwlab::pixel::Palette>(names.indexOf(name)));
  }
  if (args.contains(QStringLiteral("--fake-free-bytes"))) {
    controller.SetFreeBytesOverride(Arg(args, QStringLiteral("--fake-free-bytes")).toLongLong());
  }
  QString start_error;
  if (!controller.Start(&start_error)) {
    qCritical("시작 실패: %s", qUtf8Printable(start_error));
    return 1;
  }
  if (args.contains(QStringLiteral("--light-port"))) {
    const QString port = Arg(args, QStringLiteral("--light-port"));
    const QString percent = Arg(args, QStringLiteral("--light-percent"));
    // PPG 장치 스레드가 포트 목록을 보낸 뒤에 열어야 지난번 PPG 장치로 연결한 포트인지 알고 확인을 받습니다.
    QObject::connect(
        &ppg, &PpgController::portsChanged, &light,
        [&light, port, percent] { light.connectPort(port, percent.isEmpty() ? 50 : percent.toInt()); },
        Qt::SingleShotConnection);
  }
  if (args.contains(QStringLiteral("--ppg-port"))) ppg.connectPort(Arg(args, QStringLiteral("--ppg-port")));

  // 관류 분석 프로그램. 촬영이 저장되면 분석 입력 확인을 통과한 촬영을 자동으로 분석하고, 안정화와 녹화 중에는 새 요청을 보내지 않습니다.
  hwlab::platform::ProcessLaunch analyzer_launch =
      hwlab::platform::AnalyzerLaunch(QCoreApplication::applicationDirPath().toStdString(),
                                      Arg(args, QStringLiteral("--analyzer-python")).toStdString(),
                                      Arg(args, QStringLiteral("--analyzer-script")).toStdString());
  // 녹화 중 첫 장 미리 분할은 노트북에서 프레임 누락이 늘지 않는지 실측하기 전까지 인자로만 켭니다.
  // 녹화 중 영상 미리 읽기는 미리 분할한 발 상자를 쓰므로 미리 분할도 켭니다. 같은 첫 장 신호로 시작합니다.
  // 녹화 중 미리 분석은 관류 분석 프로그램이 늘 할 수 있게 띄우고, 실제로 요청할지는 화면 스위치가 정합니다.
  // 스위치를 바꿔도 프로그램을 다시 띄우지 않고 다음 녹화부터 적용합니다.
  const bool prestream_available = !args.contains(QStringLiteral("--no-prestream"));
  if (prestream_available) {
    const QString threads = Arg(args, QStringLiteral("--presegment-threads"));
    analyzer_launch.arguments.push_back("--presegment-threads");
    analyzer_launch.arguments.push_back(threads.isEmpty() ? std::string("2") : threads.toStdString());
    analyzer_launch.arguments.push_back("--prestream");
  }
  // 분석 판(analysis/variants)마다 관류 분석 프로그램을 하나씩 띄웁니다. 두 판은 모듈 이름이 같아 한 프로세스에 올리지 못합니다.
  std::string variants_error;
  std::vector<hwlab::platform::AnalyzerVariant> variants =
      hwlab::platform::FindAnalyzerVariants(analyzer_launch.working_dir, &variants_error);
  if (!variants_error.empty()) qWarning("분석 판: %s", variants_error.c_str());
  std::vector<std::unique_ptr<AnalysisController>> lanes;
  for (auto& variant : variants) {
    auto launch = hwlab::platform::AnalyzerVariantLaunch(analyzer_launch, variant.id);
    lanes.push_back(std::make_unique<AnalysisController>(std::move(launch), std::move(variant)));
  }
  // 판을 찾지 못해도 분석 칸이 이유를 보이도록 판 없이 하나 띄웁니다. 관류 분석 프로그램이 --variant가 없다고 알립니다.
  if (lanes.empty()) lanes.push_back(std::make_unique<AnalysisController>(analyzer_launch));
  AnalysisVariants analysis(std::move(lanes), prestream_available);
  analysis.UseRemembered(&remembered);
  QObject::connect(&controller, &CaptureController::rgbSaved, &analysis, &AnalysisVariants::onRgbSaved);
  QObject::connect(&controller, &CaptureController::shotSaved, &analysis, &AnalysisVariants::onShotSaved);
  QObject::connect(&controller, &CaptureController::firstFramesWritten, &analysis,
                   &AnalysisVariants::onFirstFramesWritten);
  QObject::connect(&controller, &CaptureController::phaseChanged, &analysis,
                   [&] { analysis.setHold(controller.recording() || controller.settling()); });
  analysis.Start();
  LogEvents(&controller, &analysis, &app);
  if (args.contains(QStringLiteral("--analyze"))) analysis.onShotSaved(Arg(args, QStringLiteral("--analyze")), true);

  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("preview"), new PreviewProvider(&controller));
  engine.addImageProvider(QStringLiteral("thermal"), new ThermalImageProvider(controller.thermal()));
  // 화면 최상위의 required property로 넘깁니다. 전역 이름(context property)을 쓰지 않아 QML에서 타입 검사가 됩니다.
  engine.setInitialProperties({{QStringLiteral("capture"), QVariant::fromValue(&controller)},
                               {QStringLiteral("analysis"), QVariant::fromValue(&analysis)}});
  engine.loadFromModule("HwLab", "Main");
  if (engine.rootObjects().isEmpty()) return 1;

  // 분석이 끝나면 결과 창을 띄웁니다. 분석결과 폴더 복사는 AnalysisController가 화면 스레드 밖에서 하고 결과를 알립니다.
  // 다음 촬영의 안정화나 녹화 중에 끝난 분석은 결과 창을 바로 띄우지 않고 녹화가 끝난 뒤 띄웁니다.
  // 결과 창이 촬영 화면을 가리면 녹화 중지 버튼을 누르기 어렵기 때문입니다.
  ResultView result_view(&engine);
  result_view.SetAnalysis(&analysis);
  QString pending_result;
  const auto open_result = [&](const QString& folder) {
    QString open_error;
    // 켜 둔 다른 판이 아직 분석 중이면 그 판의 탭을 "분석 중"으로 엽니다.
    if (!result_view.Open(folder, &open_error, !analysis.Finished(folder))) {
      qWarning("분석 결과를 읽지 못함: %s", qUtf8Printable(open_error));
    }
  };
  // 분석 입력 확인을 통과한 촬영은 녹화가 끝나 저장되면 결과 창을 바로 엽니다. 영상과 열화상을 먼저 보이고 분석 값은
  // 끝나는 대로 채웁니다. 저장은 녹화가 끝난 뒤에 일어나므로 촬영 중에는 열리지 않습니다.
  QObject::connect(&controller, &CaptureController::shotSaved, &result_view, [&](const QString& folder, bool ready) {
    if (!ready || controller.recording() || controller.settling()) return;
    QString open_error;
    // 분석은 RGB가 저장되면 바로 시작하므로, 열화상 배열을 쓰는 동안 끝났을 수 있습니다. 그러면 "분석 중"으로 열지 않습니다.
    if (!result_view.Open(folder, &open_error, !analysis.Finished(folder)) && !open_error.isEmpty()) {
      qInfo("결과 창을 분석 전에 엶: %s", qUtf8Printable(open_error));
    }
  });
  // 관류 지도 GIF와 그림은 분석 완료 뒤에 그립니다. 다 그리면 결과 창이 GIF를 다시 읽습니다.
  QObject::connect(&analysis, &AnalysisVariants::figuresReady, &result_view,
                   [&](const QString& folder, const QString&) { result_view.Refresh(folder); });
  QObject::connect(&analysis, &AnalysisVariants::finished, &result_view,
                   [&](const QString& folder, bool ok, const QString&, const QString&) {
    // 결과 창이 이 촬영을 보이고 있으면 실패여도 다시 읽어 "분석 중" 문장을 지웁니다.
    if (result_view.Refresh(folder)) return;
    if (!ok) return;
    if (controller.recording() || controller.settling()) {
      pending_result = folder;
      return;
    }
    open_result(folder);
  });
  QObject::connect(&controller, &CaptureController::phaseChanged, &result_view, [&] {
    if (pending_result.isEmpty() || controller.recording() || controller.settling()) return;
    const QString folder = pending_result;
    pending_result.clear();
    open_result(folder);
  });
  QObject::connect(&analysis, &AnalysisVariants::collected, &result_view,
                   [&](const QString& folder, const QString& text) { result_view.SetCollectText(folder, text); });
  // 결과 창은 메인 창에 딸린 창입니다. 종료 흐름(카메라 복원, 복원 안내)이 끝나 메인 창이 닫히면 결과 창도 닫고 앱을 끝냅니다.
  // 결과 창이 열린 채 남으면 마지막 창이 닫히지 않아 앱이 끝나지 않습니다.
  auto* main_window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  // 연결의 문맥을 result_view로 두어, 엔진이 메인 창을 지울 때(result_view가 먼저 사라진 뒤)는 부르지 않습니다.
  QObject::connect(main_window, &QWindow::visibleChanged, &result_view, [&](bool visible) {
    if (visible) return;
    result_view.Close();
    QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
  });
  // 앱을 닫기 시작하면 카메라 복원과 함께 관류 분석 프로그램을 끝냅니다. 진행 중인 분석과 대기열은 취소합니다.
  QObject::connect(&controller, &CaptureController::shutdownChanged, &analysis, [&] {
    if (controller.shuttingDown()) analysis.Shutdown();
  });

  const QString shot = Arg(args, QStringLiteral("--screenshot"));
  if (!shot.isEmpty()) {
    const int after = Arg(args, QStringLiteral("--after")).toInt();
    if (args.contains(QStringLiteral("--press-start"))) {
      QTimer::singleShot(after / 3, &controller, &CaptureController::startRecording);
      // 실험(디스크 연속 촬영): 빈 지그에서 뜨는 발 차가움 안내를 "이대로 녹화"로 넘깁니다. 안내가 없으면 아무 일도 하지 않습니다.
      QTimer::singleShot(after / 3 + 1500, &controller, &CaptureController::startWithColdFoot);
    }
    QTimer::singleShot(after, &app, [&] {
      main_window->grabWindow().save(shot);
      // 분석이 끝나 결과 창이 열려 있으면 같은 이름 뒤에 _result를 더한 파일로 함께 저장합니다.
      if (result_view.Window() != nullptr) {
        const QFileInfo info(shot);
        result_view.Window()->grabWindow().save(info.dir().filePath(info.completeBaseName() + QStringLiteral("_result.") +
                                                                     info.suffix()));
      }
      app.quit();
    });
  }
  if (args.contains(QStringLiteral("--close-after"))) {
    QTimer::singleShot(Arg(args, QStringLiteral("--close-after")).toInt(), &app, [&] {
      main_window->close();
    });
  }
  const int code = app.exec();
  // 창을 닫은 뒤에도 장치를 닫는 소멸자(카메라, 열화상, 직렬 포트)가 드라이버 호출에서 멈출 수 있습니다.
  // 멈추면 창 없는 프로세스가 장치를 잡은 채 남으므로 제한 시간을 겁니다. 끝 단계는 teardown_end가 남깁니다.
  LogShutdownStep(QStringLiteral("창 닫힘, 장치 닫기 시작"));
  ArmShutdownLimit(kTeardownLimitMs, QStringLiteral("창을 닫은 뒤 정리 %1초").arg(kTeardownLimitMs / 1000));
  return code;
}
