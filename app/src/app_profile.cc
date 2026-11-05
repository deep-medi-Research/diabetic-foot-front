#include "app_profile.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace profile {

namespace {

// --profile의 값입니다. 다른 인자가 바로 뒤에 오면(값이 빠졌으면) 비어 있는 것으로 봅니다.
QString ArgumentValue(const QStringList& args) {
  const int i = args.indexOf(QStringLiteral("--profile"));
  if (i < 0 || i + 1 >= args.size() || args[i + 1].startsWith(QLatin1String("--"))) return QString();
  return args[i + 1];
}

}  // namespace

bool IsValidName(const QString& name) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9._-]{1,32}$"));
  return pattern.match(name).hasMatch();
}

bool IsBuildTreeDir(const QString& exe_dir) {
  const QDir dir(exe_dir);
  return QFileInfo::exists(dir.filePath(QStringLiteral("CMakeCache.txt"))) ||
         QFileInfo::exists(dir.filePath(QStringLiteral("../CMakeCache.txt")));
}

Choice Resolve(const QStringList& args, const QString& exe_dir) {
  const QString given = ArgumentValue(args);
  if (IsValidName(given)) return {given == DefaultName() ? QString() : given, Source::kArgument};
  if (IsBuildTreeDir(exe_dir)) return {BuildTreeName(), Source::kBuildTree};
  return {};
}

QString ApplicationName(const QString& profile) {
  return profile.isEmpty() ? QStringLiteral("hwlab") : QStringLiteral("hwlab-") + profile;
}

QString DisplayVersion(const QString& version, const QString& commit, const QString& profile) {
  const QString base = !version.isEmpty() ? version : !commit.isEmpty() ? QStringLiteral("(개발 %1)").arg(commit) : QString();
  if (profile.isEmpty()) return base;
  return (base.isEmpty() ? QString() : base + QLatin1Char(' ')) + QStringLiteral("프로필 ") + profile;
}

QString Describe(const Choice& choice) {
  switch (choice.source) {
    case Source::kArgument:
      return choice.name.isEmpty() ? QStringLiteral("없음(--profile default, 설치본과 같은 설정과 기록 위치)")
                                   : QStringLiteral("%1(--profile로 정함)").arg(choice.name);
    case Source::kBuildTree:
      return QStringLiteral("%1(빌드 폴더에서 실행해 자동으로 고름. 설치본과 설정과 기록을 나누지 않으려면 --profile default)")
          .arg(choice.name);
    case Source::kDefault:
      break;
  }
  return QStringLiteral("없음(설치본과 같은 설정과 기록 위치)");
}

}  // namespace profile
