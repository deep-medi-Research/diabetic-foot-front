#include "shot_form.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>
#include <QStringList>

#include <algorithm>

#include "remembered_values.h"

namespace {

constexpr char kRootKey[] = "shot/root";

// Windows가 폴더 이름에 받지 않는 글자입니다. 화면에 그 글자를 그대로 보여 주려고 찾습니다.
QString ForbiddenCharacters(const QString& name) {
  QString found;
  for (const QChar c : name) {
    const bool bad = c.unicode() < 0x20 || c.unicode() == 0x7F || QStringLiteral("<>:\"/\\|?*").contains(c);
    if (!bad || found.contains(c)) continue;
    if (!found.isEmpty()) found += QStringLiteral(" ");
    found += c.unicode() < 0x20 || c.unicode() == 0x7F ? QStringLiteral("제어 문자") : QString(c);
  }
  return found;
}

QString DigitsOf(const QString& text) {
  QString digits;
  for (const QChar c : text) {
    if (c != QLatin1Char('-') && c != QLatin1Char(' ')) digits += c;
  }
  return digits;
}

bool IsAllDigits(const QString& text) {
  return std::all_of(text.begin(), text.end(), [](QChar c) { return c.isDigit(); });
}

// 명령줄에서 숫자 여덟 자리로 받으면 화면 입력 형식(0000-00-00)에 맞춥니다.
QString BirthText(const QString& text) {
  if (text.size() == 8 && IsAllDigits(text)) {
    return text.left(4) + QLatin1Char('-') + text.mid(4, 2) + QLatin1Char('-') + text.mid(6, 2);
  }
  return text;
}

// 혈압 칸의 입력 마스크가 비운 자리를 공백으로 채우므로 공백을 뺍니다.
QString PressureDigits(const QString& text) { return QString(text).remove(QLatin1Char(' ')); }

int32_t PressureMmhg(const QString& text) {
  bool ok = false;
  const int value = PressureDigits(text).toInt(&ok);
  return ok ? value : 0;
}

// 버튼 아래에 값과 함께 보여 어떤 혈압으로 계산했는지 알 수 있게 합니다. 0은 혈압과 관계없어 비웁니다.
QString AnkleLevelFormula(const QString& level) {
  if (level == QLatin1String("V")) return QStringLiteral("발목 이완기 - 50");
  if (level == QLatin1String("D")) return QStringLiteral("발목 이완기");
  if (level == QLatin1String("M")) return QStringLiteral("(발목 수축기 + 이완기) / 2");
  if (level == QLatin1String("S")) return QStringLiteral("발목 수축기");
  return {};
}

// 최근 환자 목록의 길이와 이전 배치에서 읽는 날짜 폴더 수입니다. 저장 위치에 촬영이 많아도 화면을 열 때 짧게 끝나게 합니다.
constexpr int kRecentPatients = 8;
constexpr int kRecentDays = 60;

}  // namespace

ShotForm::ShotForm(QString root, RememberedValues* remembered_values, QObject* parent)
    : QObject(parent), root_(std::move(root)), remembered_values_(remembered_values) {
  Revalidate();
  refreshRecent();
}

void ShotForm::pickPatient(const QString& name, const QString& birth) {
  const QString next_birth = BirthText(birth);
  if (name.trimmed() != name_.trimmed() || next_birth != birth_) ClearPressures();
  name_ = name;
  birth_ = next_birth;
  Revalidate();
}

void ShotForm::refreshRecent() {
  // 폴더 이름 규칙: 저장 위치 아래 "이름_생년월일8자리"(이전 배치는 날짜 yyyyMMdd 아래). 이름에 '_'가 있어도 마지막 '_' 뒤 여덟 자리를 생년월일로 봅니다.
  static const QRegularExpression day_pattern(QStringLiteral("^\\d{8}$"));
  static const QRegularExpression patient_pattern(QStringLiteral("^(.+)_(\\d{8})$"));
  QDir root(root_);
  struct Seen {
    QString name;
    QString birth;
    QDateTime last;
  };
  std::vector<Seen> seen;
  const auto add = [&seen](const QRegularExpressionMatch& m, const QDateTime& last) {
    const QString name = m.captured(1);
    const QString digits = m.captured(2);
    const QString birth = digits.left(4) + QLatin1Char('-') + digits.mid(4, 2) + QLatin1Char('-') + digits.mid(6, 2);
    auto it = std::find_if(seen.begin(), seen.end(), [&](const Seen& x) { return x.name == name && x.birth == birth; });
    if (it == seen.end()) {
      seen.push_back({name, birth, last});
    } else if (last > it->last) {
      it->last = last;
    }
  };
  // 환자 폴더의 수정 시각은 촬영 폴더를 만들 때 맞춥니다(CreateShotFolder). 환자 폴더를 열지 않아 환자가 많아도 짧게 끝납니다.
  for (const QFileInfo& patient : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const auto m = patient_pattern.match(patient.fileName());
    if (m.hasMatch()) add(m, patient.lastModified());
  }
  QStringList days = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
  days = days.filter(day_pattern);
  for (const QString& day : days.mid(0, kRecentDays)) {
    const QDir day_dir(root.filePath(day));
    for (const QFileInfo& info : day_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const auto m = patient_pattern.match(info.fileName());
      if (m.hasMatch()) add(m, info.lastModified());
    }
  }
  std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.last > b.last; });
  QVariantList list;
  for (size_t i = 0; i < seen.size() && static_cast<int>(i) < kRecentPatients; ++i) {
    QVariantMap item;
    item[QStringLiteral("name")] = seen[i].name;
    item[QStringLiteral("birth")] = seen[i].birth;
    item[QStringLiteral("last")] = seen[i].last.toString(QStringLiteral("M/d HH:mm"));
    list.append(item);
  }
  if (list == recent_) return;
  recent_ = list;
  emit recentChanged();
}

QString ShotForm::RememberedRoot(const RememberedValues& remembered_values) {
  return remembered_values.Value(QLatin1String(kRootKey)).toString();
}

QVariantList ShotForm::ankleLevels() const {
  QVariantList list;
  for (const std::string_view level : hwlab::record::kAnkleLevels) {
    list.append(QString::fromLatin1(level.data(), static_cast<qsizetype>(level.size())));
  }
  return list;
}

QString ShotForm::root() const { return QDir::toNativeSeparators(root_); }

void ShotForm::setName(const QString& text) {
  if (name_ == text) return;
  if (text.trimmed() != name_.trimmed()) ClearPressures();
  name_ = text;
  Revalidate();
}

void ShotForm::setBirth(const QString& text) {
  const QString next = BirthText(text);
  if (birth_ == next) return;
  birth_ = next;
  ClearPressures();
  Revalidate();
}

void ShotForm::setArmSystolic(const QString& text) { SetPressureText(&arm_systolic_, text); }

void ShotForm::setArmDiastolic(const QString& text) { SetPressureText(&arm_diastolic_, text); }

void ShotForm::setAnkleSystolic(const QString& text) { SetPressureText(&ankle_systolic_, text); }

void ShotForm::setAnkleDiastolic(const QString& text) { SetPressureText(&ankle_diastolic_, text); }

void ShotForm::setAnkleLevel(const QString& level) {
  if (ankle_level_ == level) return;
  ankle_level_ = level;
  Revalidate();
}

void ShotForm::SetPressureText(QString* field, const QString& text) {
  if (*field == text) return;
  *field = text;
  Revalidate();
}

void ShotForm::ClearPressures() {
  arm_systolic_.clear();
  arm_diastolic_.clear();
  ankle_systolic_.clear();
  ankle_diastolic_.clear();
  ankle_level_.clear();
}

void ShotForm::setRootUrl(const QUrl& url) {
  const QString path = url.toLocalFile();
  if (path.isEmpty() || path == root_) return;
  root_ = path;
  remembered_values_->SetValue(QLatin1String(kRootKey), root_);
  Revalidate();
  refreshRecent();
}

hwlab::record::ShotInfo ShotForm::InfoAt(const QDateTime& when) const {
  hwlab::record::ShotInfo info;
  info.patient_name = name_.trimmed().toStdString();
  info.birth_date = DigitsOf(birth_).toStdString();
  info.arm_pressure = {PressureMmhg(arm_systolic_), PressureMmhg(arm_diastolic_)};
  info.ankle_pressure = {PressureMmhg(ankle_systolic_), PressureMmhg(ankle_diastolic_)};
  info.ankle_level = ankle_level_.toStdString();
  const QDate d = when.date();
  const QTime t = when.time();
  info.year = d.year();
  info.month = d.month();
  info.day = d.day();
  info.hour = t.hour();
  info.minute = t.minute();
  info.second = t.second();
  return info;
}

void ShotForm::Revalidate() {
  using hwlab::record::ShotProblem;
  const hwlab::record::ShotInfo info = InfoAt(QDateTime::currentDateTime());
  const auto problems = hwlab::record::ValidateShot(info);
  const auto has = [&](ShotProblem p) { return std::find(problems.begin(), problems.end(), p) != problems.end(); };

  QStringList missing;
  name_error_.clear();
  birth_error_.clear();

  if (has(ShotProblem::kEmptyName)) missing << QStringLiteral("이름");
  if (has(ShotProblem::kBadNameCharacter)) {
    const QString found = ForbiddenCharacters(name_.trimmed());
    name_error_ = found.isEmpty() ? QStringLiteral("이름 끝에 점을 쓸 수 없습니다")
                                  : QStringLiteral("폴더 이름에 쓸 수 없는 글자: %1").arg(found);
  }

  const QString digits = DigitsOf(birth_);
  const bool all_digits = std::all_of(digits.begin(), digits.end(), [](QChar c) { return c.isDigit(); });
  if (digits.isEmpty()) {
    missing << QStringLiteral("생년월일");
  } else if (!all_digits) {
    birth_error_ = QStringLiteral("생년월일은 숫자 여덟 자리입니다");
  } else if (digits.size() < 8) {
    // 입력하는 중에는 빨간 글자를 띄우지 않고 남은 입력으로만 알립니다.
    missing << QStringLiteral("생년월일 %1자리").arg(8 - digits.size());
  } else if (has(ShotProblem::kBadBirthDate)) {
    birth_error_ = QStringLiteral("없는 날짜입니다: %1").arg(birth_);
  }

  const auto check_pressure = [&](const QString& site, const QString& systolic_text, const QString& diastolic_text,
                                  bool bad, QString* error) {
    error->clear();
    const QString systolic = PressureDigits(systolic_text);
    const QString diastolic = PressureDigits(diastolic_text);
    if (systolic.isEmpty() && diastolic.isEmpty()) {
      missing << QStringLiteral("%1 혈압").arg(site);
    } else if (!IsAllDigits(systolic) || !IsAllDigits(diastolic)) {
      *error = QStringLiteral("혈압은 숫자로 씁니다");
    } else if (systolic.size() < 2 || diastolic.size() < 2) {
      // 혈압은 두 자리 이상이므로 한 자리 이하는 입력하는 중으로 보고 빨간 글자를 띄우지 않습니다.
      missing << QStringLiteral("%1 %2").arg(site, systolic.size() < 2 ? QStringLiteral("수축기") : QStringLiteral("이완기"));
    } else if (bad && systolic.toInt() <= diastolic.toInt()) {
      *error = QStringLiteral("수축기가 이완기보다 커야 합니다");
    } else if (bad) {
      *error = QStringLiteral("혈압은 %1~%2mmHg입니다")
                   .arg(hwlab::record::kMinBloodPressureMmhg)
                   .arg(hwlab::record::kMaxBloodPressureMmhg);
    }
  };
  check_pressure(QStringLiteral("상완"), arm_systolic_, arm_diastolic_, has(ShotProblem::kBadArmPressure),
                 &arm_pressure_error_);
  check_pressure(QStringLiteral("발목"), ankle_systolic_, ankle_diastolic_, has(ShotProblem::kBadAnklePressure),
                 &ankle_pressure_error_);

  ankle_level_error_.clear();
  ankle_level_text_.clear();
  const QString formula = AnkleLevelFormula(ankle_level_);
  if (ankle_level_.isEmpty()) {
    missing << QStringLiteral("발목 혈압 단계");
    ankle_level_text_ = QStringLiteral("V 이완기 - 50, D 이완기, M (수축기 + 이완기) / 2, S 수축기");
  } else if (has(ShotProblem::kUnknownAnkleLevel)) {
    ankle_level_error_ = QStringLiteral("발목 혈압 단계 %1은 목록에 없습니다").arg(ankle_level_);
  } else if (has(ShotProblem::kBadAnklePressure) && !formula.isEmpty()) {
    ankle_level_text_ = QStringLiteral("발목 혈압을 입력하면 계산합니다");
  } else if (has(ShotProblem::kNegativeAnkleLevel)) {
    ankle_level_error_ = QStringLiteral("V는 발목 이완기가 50mmHg 이상일 때 고를 수 있습니다");
  } else {
    const double mmhg = hwlab::record::AnkleLevelMmhg(info.ankle_level, info.ankle_pressure).value_or(0);
    ankle_level_text_ = formula.isEmpty() ? QStringLiteral("%1 mmHg").arg(QString::number(mmhg))
                                          : QStringLiteral("%1 = %2 mmHg").arg(formula, QString::number(mmhg));
  }

  missing_ = missing.isEmpty() ? QString() : QStringLiteral("입력할 것: %1").arg(missing.join(QStringLiteral(", ")));
  // 촬영 버튼 옆에서 꺼진 이유를 한 번에 보이기 위해, 빨간 글자가 뜬 칸 이름을 모읍니다.
  QStringList to_fix;
  if (!name_error_.isEmpty()) to_fix << QStringLiteral("이름");
  if (!birth_error_.isEmpty()) to_fix << QStringLiteral("생년월일");
  if (!arm_pressure_error_.isEmpty()) to_fix << QStringLiteral("상완 혈압");
  if (!ankle_pressure_error_.isEmpty()) to_fix << QStringLiteral("발목 혈압");
  if (!ankle_level_error_.isEmpty()) to_fix << QStringLiteral("발목 혈압 단계");
  to_fix_ = to_fix.isEmpty() ? QString() : QStringLiteral("고칠 것: %1").arg(to_fix.join(QStringLiteral(", ")));
  folder_preview_.clear();
  // 폴더 규칙은 core(LayoutOf)의 결과를 그대로 보여 주고, 아직 정해지지 않은 마지막 단계만 글자로 둡니다.
  if (const auto layout = hwlab::record::LayoutOf(info, "mkv", {})) {
    const std::string parent = layout->folder.substr(0, layout->folder.size() - layout->stem.size());
    folder_preview_ = QString::fromStdString(parent) + QStringLiteral("날짜_시각");
  }
  emit changed();
}
