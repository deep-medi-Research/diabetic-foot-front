#include "cli_args.h"

#include <algorithm>

#include "app_profile.h"

namespace cli {

namespace {

// 표의 순서가 --help와 문서의 순서입니다. 설명은 한 줄이고, 자세한 동작은 docs/debugging.md에 있습니다.
constexpr ArgSpec kArguments[] = {
    {"--spec", "<선언 파일>", Group::kRun, "촬영 선언(JSON)입니다. --result가 아니면 꼭 필요하고, 설치 폴더의 specs에 있습니다", false},
    {"--out", "<폴더>", Group::kRun,
     "처음 저장 위치입니다. 화면에서 고른 위치를 기억하고 있으면 그 위치를 쓰고, 둘 다 없으면 현재 폴더의 captures입니다. 설치 바로가기는 문서\\DiabeticFoot를 줍니다", false},
    {"--profile", "<이름>", Group::kRun,
     "설정, 기록, 로그를 따로 두는 이름입니다. 빌드 폴더에서 실행하면 dev이고, default는 설치본과 같은 위치입니다", false},
    {"--help", nullptr, Group::kRun, "이 도움말을 보이고 끝납니다(-h, /?도 됩니다)", false},

    {"--no-thermal", nullptr, Group::kDevices, "열화상 카메라를 열지 않고 RGB만 녹화합니다", false},
    {"--save-full", nullptr, Group::kDevices, "좌우 발 영상과 함께 전체 화면 영상도 저장합니다. 기본은 좌우 발 영상만입니다", false},
    {"--lens-shading-folder", "<폴더>", Group::kDevices,
     "열화상 렌즈 셰이딩 보정 파일 폴더입니다. 기본은 앱 데이터 폴더의 thermal이고, --fake는 보정 기능이 없는 열화상입니다", false},
    {"--pixel-left-foot", "left|right", Group::kDevices, "화면 왼쪽 상자의 발을 정한 채로 시작합니다", false},
    {"--rgb-regions", "<x,y,w,h,x,y,w,h>", Group::kDevices,
     "RGB 좌우 영역(화소 왼쪽, 화소 오른쪽)을 정한 채로 시작합니다. 격자 맞춤 없이 그대로 검사합니다", false},
    {"--rgb-area", "<x,y,w,h>", Group::kDevices, "RGB 패널의 관찰 영역(프레임 화소)을 정한 채로 시작합니다. 관찰 영역 화면을 --screenshot으로 확인할 때 씁니다", false},
    {"--palette", "iron|gray|rainbow|high-contrast", Group::kDevices, "열화상 색상표를 정한 채로 시작합니다", false},

    {"--fake", nullptr, Group::kFake,
     "장비 없이 가짜 RGB, 열화상, PPG(COM6), LED(COM7)로 시작합니다. 화면에서 바꾼 값을 다음 실행에 남기지 않습니다", false},
    {"--fake-thermal-absent", nullptr, Group::kFake, "--fake에서 열화상 장치를 찾지 못하는 상태로 만듭니다. 열화상 없이 찍을지 묻는 안내를 시험할 때 씁니다", false},
    {"--fake-thermal-delay-ms", "<ms>", Group::kFake, "--fake에서 열화상을 연 뒤 첫 프레임이 이 시간만큼 늦게 오게 합니다. 같은 안내를 시험할 때 씁니다", false},
    {"--fake-thermal-warmup", "<분>", Group::kFake,
     "--fake의 열화상 카메라 온도를 켠 지 이만큼 지난 값에서 시작합니다. 0이면 식은 카메라이고, 주지 않으면 다 데워진 카메라(120분)입니다", false},
    {"--fake-free-bytes", "<바이트>", Group::kFake, "디스크 여유를 읽지 않고 이 값으로 봅니다. 디스크 부족 거부를 시험할 때 씁니다", false},
    {"--fake-restore-fails", nullptr, Group::kFake,
     "--fake에서 실행 전 색온도를 자동으로 두고 자동 색온도 명령을 실패시켜, 종료 때 카메라 복원 불일치 안내를 시험합니다", false},

    {"--light-port", "<포트>", Group::kPeripherals, "시작하면서 LED 포트를 엽니다. --fake에서는 가짜 포트(COM7)만 열립니다", false},
    {"--light-percent", "<%>", Group::kPeripherals, "--light-port로 연 뒤 보낼 밝기입니다. 기본은 50입니다", false},
    {"--light-remember", "<VID:PID:일련번호>", Group::kPeripherals,
     "기억한 LED 변환기를 정한 채로 시작합니다. 포트 제안을 시험할 때 쓰며 다음 실행에 남기지 않습니다", false},
    {"--ppg-port", "<포트>", Group::kPeripherals,
     "시작하면서 PPG 포트를 열고 장치를 시작합니다. --fake에서는 가짜 장치(COM6)만 열립니다", false},
    {"--ppg-remember", "<VID:PID:일련번호>", Group::kPeripherals,
     "기억한 PPG 변환기를 정한 채로 시작합니다. \"지난번 PPG\" 표시와 조명 칸의 확인을 시험할 때 쓰며 다음 실행에 남기지 않습니다", false},

    {"--patient", "<이름>", Group::kPatient, "환자 이름 칸을 채운 채로 시작합니다. 로그에는 값을 남기지 않습니다", true},
    {"--birth", "<0000-00-00>", Group::kPatient, "생년월일 칸을 채운 채로 시작합니다. 로그에는 값을 남기지 않습니다", true},
    {"--arm-pressure", "<수축기/이완기>", Group::kPatient, "상완 혈압 칸을 채운 채로 시작합니다(예: 120/80)", false},
    {"--ankle-pressure", "<수축기/이완기>", Group::kPatient, "발목 혈압 칸을 채운 채로 시작합니다(예: 135/84)", false},
    {"--ankle-level", "0|V|D|M|S", Group::kPatient, "발목 혈압 단계를 채운 채로 시작합니다", false},

    {"--analyzer-python", "<python.exe>", Group::kAnalysis,
     "배포 폴더의 python 대신 쓸 파이썬입니다. python처럼 이름만 주면 PATH에서 찾고, 상대 경로는 앱을 띄운 폴더 기준입니다", false},
    {"--analyzer-script", "<진입 스크립트>", Group::kAnalysis,
     "배포 폴더의 analysis 대신 쓸 분석 프로그램의 진입 스크립트입니다. 상대 경로는 앱을 띄운 폴더 기준입니다", false},
    {"--analyze", "<촬영 폴더>", Group::kAnalysis, "시작하자마자 그 촬영 폴더를 분석 대기열에 넣습니다", false},
    {"--no-prestream", nullptr, Group::kAnalysis, "녹화 중 미리 분석(첫 장 발 분할, 영상 미리 읽기)을 쓰지 않고 화면의 스위치도 숨깁니다", false},
    {"--presegment-threads", "<n>", Group::kAnalysis, "녹화 중 첫 장 분할에 쓸 스레드 수입니다. 기본은 2이고, 0이면 분석과 같은 모델을 같이 씁니다", false},

    {"--result", "<촬영 폴더>", Group::kResult, "분석이 끝난 촬영 폴더의 결과 창만 엽니다. 장치와 --spec을 쓰지 않습니다", false},
    {"--result-position", "<0~1>", Group::kResult, "되감기 막대의 처음 위치입니다(0 처음, 1 끝)", false},
    {"--result-variant", "<판>", Group::kResult, "처음 보일 분석 판 탭입니다", false},

    {"--screenshot", "<png>", Group::kCheck,
     "--after 뒤 화면을 이미지로 저장하고 끝납니다. 결과 창이 열려 있으면 <이름>_result.<확장자>도 저장합니다", false},
    {"--after", "<ms>", Group::kCheck, "--screenshot을 찍기까지의 시간입니다", false},
    {"--press-start", nullptr, Group::kCheck, "--after의 3분의 1 시점에 촬영 버튼을 누릅니다", false},
    {"--open-settings", nullptr, Group::kCheck, "설정 패널을 연 채로 시작합니다. 설정 패널을 --screenshot으로 확인할 때 씁니다", false},
    {"--close-after", "<ms>", Group::kCheck, "정한 시간 뒤 창을 닫습니다. 종료 때 카메라 복원 흐름을 시험할 때 씁니다", false},
};

bool IsOption(const QString& token) { return token.startsWith(QLatin1String("--")); }

QString Usage(const ArgSpec& spec) {
  const QString name = QString::fromUtf8(spec.name);
  return spec.value == nullptr ? name : name + QLatin1Char(' ') + QString::fromUtf8(spec.value);
}

// 한글은 터미널에서 두 칸을 차지하므로 칸 수로 셉니다.
int DisplayWidth(const QString& text) {
  int width = 0;
  for (const QChar ch : text) width += ch.unicode() >= 0x1100 ? 2 : 1;
  return width;
}

QString Padded(const QString& text, int width) {
  return text + QString(std::max(0, width - DisplayWidth(text)), QLatin1Char(' '));
}

// 공백에서 끊어 칸 수 안에 넣습니다. 한 낱말이 칸보다 길면 그대로 둡니다.
QStringList Wrap(const QString& text, int width) {
  QStringList lines;
  QString line;
  for (const QString& word : text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
    if (!line.isEmpty() && DisplayWidth(line) + 1 + DisplayWidth(word) > width) {
      lines << line;
      line.clear();
    }
    line += (line.isEmpty() ? QString() : QStringLiteral(" ")) + word;
  }
  if (!line.isEmpty()) lines << line;
  return lines;
}

constexpr int kUsageColumn = 34;  // 인자 이름과 값을 쓰는 칸 수. 넘으면 설명을 다음 줄에 씁니다
constexpr int kTextWidth = 78;    // 설명을 쓰는 칸 수

}  // namespace

std::span<const ArgSpec> Arguments() { return kArguments; }

QString GroupTitle(Group group) {
  switch (group) {
    case Group::kRun: return QStringLiteral("실행");
    case Group::kDevices: return QStringLiteral("장비와 녹화");
    case Group::kFake: return QStringLiteral("가짜 장치(--fake)");
    case Group::kPeripherals: return QStringLiteral("LED와 PPG");
    case Group::kPatient: return QStringLiteral("환자 정보 칸 채우기");
    case Group::kAnalysis: return QStringLiteral("관류 분석");
    case Group::kResult: return QStringLiteral("결과 창");
    case Group::kCheck: return QStringLiteral("화면 확인과 시험");
  }
  return {};
}

const ArgSpec* Find(const QString& name) {
  for (const ArgSpec& spec : kArguments) {
    if (name == QLatin1String(spec.name)) return &spec;
  }
  return nullptr;
}

bool WantsHelp(const QStringList& args) {
  for (int i = 1; i < args.size(); ++i) {
    if (args[i] == QLatin1String("--help") || args[i] == QLatin1String("-h") || args[i] == QLatin1String("/?")) return true;
  }
  return false;
}

std::optional<QString> Check(const QStringList& args) {
  QStringList problems;
  for (int i = 1; i < args.size(); ++i) {
    const QString& token = args[i];
    const ArgSpec* spec = IsOption(token) ? Find(token) : nullptr;
    if (spec == nullptr) {
      const int equals = token.indexOf(QLatin1Char('='));
      const ArgSpec* named = IsOption(token) && equals > 0 ? Find(token.left(equals)) : nullptr;
      if (named != nullptr && named->value != nullptr) {
        problems << QStringLiteral("알 수 없는 인자입니다: %1 (값은 =로 잇지 않고 띄어서 줍니다: %2 %3)")
                        .arg(token, token.left(equals), token.mid(equals + 1));
      } else if (IsOption(token)) {
        problems << QStringLiteral("알 수 없는 인자입니다: %1").arg(token);
      } else {
        problems << QStringLiteral("알 수 없는 인자입니다: %1 (인자는 --로 시작하고, 값은 앞 인자 바로 뒤에만 줍니다)").arg(token);
      }
      continue;
    }
    if (spec->value == nullptr) continue;
    if (i + 1 >= args.size() || IsOption(args[i + 1])) {
      problems << QStringLiteral("값이 빠진 인자입니다: %1").arg(Usage(*spec));
      continue;
    }
    ++i;
    if (token == QLatin1String("--profile") && !profile::IsValidName(args[i])) {
      problems << QStringLiteral("--profile 이름은 영문, 숫자, 점, 밑줄, 하이픈으로 32자까지입니다: %1").arg(args[i]);
    }
  }
  const bool has_result = args.contains(QStringLiteral("--result"));
  if (!has_result && !args.contains(QStringLiteral("--spec"))) {
    problems << QStringLiteral("--spec <선언 파일>이 필요합니다(결과 창만 여는 --result는 필요 없습니다)");
  }
  if (problems.isEmpty()) return std::nullopt;
  return problems.join(QLatin1Char('\n'));
}

QString HelpText(const QString& program, const QString& footer) {
  QString text;
  text += QStringLiteral("%1: 당뇨발 촬영 앱\n\n").arg(program);
  text += QStringLiteral("사용법\n");
  text += QStringLiteral("  %1 --spec <선언 파일> [--out <폴더>] [--fake] [인자...]\n").arg(program);
  text += QStringLiteral("  %1 --result <촬영 폴더> [--screenshot <png> --after <ms>]\n").arg(program);
  text += QStringLiteral("  %1 --help\n").arg(program);
  const QString indent = QString(2 + kUsageColumn + 2, QLatin1Char(' '));
  bool first = true;
  Group group = Group::kRun;
  for (const ArgSpec& spec : kArguments) {
    if (first || spec.group != group) {
      text += QStringLiteral("\n%1\n").arg(GroupTitle(spec.group));
      group = spec.group;
      first = false;
    }
    const QString usage = Usage(spec);
    const QStringList lines = Wrap(QString::fromUtf8(spec.help), kTextWidth);
    int next = 0;
    text += QStringLiteral("  ");
    if (DisplayWidth(usage) <= kUsageColumn) {
      text += Padded(usage, kUsageColumn) + QStringLiteral("  ") + lines.value(next++) + QLatin1Char('\n');
    } else {
      text += usage + QLatin1Char('\n');
    }
    for (; next < lines.size(); ++next) text += indent + lines[next] + QLatin1Char('\n');
  }
  if (!footer.isEmpty()) text += QLatin1Char('\n') + footer;
  return text;
}

QString MarkdownTable() {
  QString text;
  bool first = true;
  Group group = Group::kRun;
  for (const ArgSpec& spec : kArguments) {
    if (first || spec.group != group) {
      if (!first) text += QLatin1Char('\n');
      text += QStringLiteral("**%1**\n\n| 인자 | 설명 |\n| --- | --- |\n").arg(GroupTitle(spec.group));
      group = spec.group;
      first = false;
    }
    QString usage = Usage(spec);
    usage.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    text += QStringLiteral("| `%1` | %2 |\n").arg(usage, QString::fromUtf8(spec.help));
  }
  return text;
}

QStringList MaskedForLog(const QStringList& args) {
  QStringList out = args;
  for (int i = 1; i + 1 < out.size(); ++i) {
    const ArgSpec* spec = IsOption(out[i]) ? Find(out[i]) : nullptr;
    if (spec != nullptr && spec->secret) out[++i] = QStringLiteral("***");
  }
  return out;
}

}  // namespace cli
