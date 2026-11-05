#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "app_profile.h"
#include "remembered_values.h"

namespace {

QStringList Args(const QStringList& rest) { return QStringList{QStringLiteral("hwlab_app")} + rest; }

void Touch(const QString& path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
}

}  // namespace

class AppProfileTest : public QObject {
  Q_OBJECT

 private slots:
  void ValidNames() {
    for (const char* name : {"dev", "qa-2", "a.b_c", "A", "0123456789012345678901234567890a"}) {
      QVERIFY2(profile::IsValidName(QString::fromLatin1(name)), name);
    }
    for (const char* name : {"", "a b", "a/b", "a\\b", "..\\x", "한글", "01234567890123456789012345678901a", "a:b"}) {
      QVERIFY2(!profile::IsValidName(QString::fromLatin1(name)), name);
    }
  }

  // Ninja는 실행 파일과 같은 폴더, Visual Studio 생성기는 Debug 폴더의 위에 CMakeCache.txt가 있습니다.
  // cmake --install로 만든 폴더(설치본과 같은 모양)에는 없습니다.
  void BuildTreeIsRecognizedByCMakeCache() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QDir dir(root.path());
    Touch(dir.filePath(QStringLiteral("ninja/CMakeCache.txt")));
    Touch(dir.filePath(QStringLiteral("vs/CMakeCache.txt")));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("vs/Debug"))));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("installed/bin"))));
    QVERIFY(profile::IsBuildTreeDir(dir.filePath(QStringLiteral("ninja"))));
    QVERIFY(profile::IsBuildTreeDir(dir.filePath(QStringLiteral("vs/Debug"))));
    QVERIFY(!profile::IsBuildTreeDir(dir.filePath(QStringLiteral("installed/bin"))));
    QVERIFY(!profile::IsBuildTreeDir(dir.filePath(QStringLiteral("does/not/exist"))));
  }

  // 설치본(빌드 폴더가 아닌 곳, --profile 없음)은 프로필이 없고 앱 이름이 hwlab 그대로라 레지스트리 키와 데이터 폴더가 바뀌지 않습니다.
  void InstalledRunKeepsTheDefaultLocation() {
    QTemporaryDir root;
    const QString installed = QDir(root.path()).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(installed));
    const profile::Choice choice = profile::Resolve(Args({QStringLiteral("--spec"), QStringLiteral("a.json")}), installed);
    QVERIFY(choice.name.isEmpty());
    QCOMPARE(choice.source, profile::Source::kDefault);
    QCOMPARE(profile::ApplicationName(choice.name), QStringLiteral("hwlab"));
  }

  void BuildTreeRunUsesDevAutomatically() {
    QTemporaryDir root;
    Touch(QDir(root.path()).filePath(QStringLiteral("CMakeCache.txt")));
    const profile::Choice choice = profile::Resolve(Args({QStringLiteral("--fake")}), root.path());
    QCOMPARE(choice.name, QStringLiteral("dev"));
    QCOMPARE(choice.source, profile::Source::kBuildTree);
    QCOMPARE(profile::ApplicationName(choice.name), QStringLiteral("hwlab-dev"));
  }

  void ArgumentWinsOverBuildTree() {
    QTemporaryDir root;
    Touch(QDir(root.path()).filePath(QStringLiteral("CMakeCache.txt")));
    const profile::Choice named = profile::Resolve(Args({QStringLiteral("--profile"), QStringLiteral("qa")}), root.path());
    QCOMPARE(named.name, QStringLiteral("qa"));
    QCOMPARE(named.source, profile::Source::kArgument);

    const profile::Choice shared = profile::Resolve(Args({QStringLiteral("--profile"), QStringLiteral("default")}), root.path());
    QVERIFY(shared.name.isEmpty());
    QCOMPARE(shared.source, profile::Source::kArgument);
    QCOMPARE(profile::ApplicationName(shared.name), QStringLiteral("hwlab"));

    const profile::Choice installed = profile::Resolve(Args({QStringLiteral("--profile"), QStringLiteral("qa")}), QStringLiteral("/nowhere"));
    QCOMPARE(installed.name, QStringLiteral("qa"));
  }

  // 값이 빠졌거나 잘못된 --profile은 인자 검사가 알리고, 여기서는 --profile이 없는 것처럼 정합니다.
  void BrokenArgumentFallsBack() {
    QTemporaryDir root;
    Touch(QDir(root.path()).filePath(QStringLiteral("CMakeCache.txt")));
    for (const QStringList& rest : {QStringList{QStringLiteral("--profile")},
                                    QStringList{QStringLiteral("--profile"), QStringLiteral("--fake")},
                                    QStringList{QStringLiteral("--profile"), QStringLiteral("a/b")}}) {
      QCOMPARE(profile::Resolve(Args(rest), root.path()).name, QStringLiteral("dev"));
      QVERIFY(profile::Resolve(Args(rest), QStringLiteral("/nowhere")).name.isEmpty());
    }
  }

  void DisplayVersionAppendsProfile() {
    QCOMPARE(profile::DisplayVersion(QStringLiteral("v0.1.17"), QStringLiteral("abc1234"), QString()), QStringLiteral("v0.1.17"));
    QCOMPARE(profile::DisplayVersion(QString(), QStringLiteral("abc1234"), QString()), QStringLiteral("(개발 abc1234)"));
    QCOMPARE(profile::DisplayVersion(QString(), QString(), QString()), QString());
    QCOMPARE(profile::DisplayVersion(QString(), QStringLiteral("abc1234"), QStringLiteral("dev")),
             QStringLiteral("(개발 abc1234) 프로필 dev"));
    QCOMPARE(profile::DisplayVersion(QStringLiteral("v0.1.17"), QString(), QStringLiteral("qa")), QStringLiteral("v0.1.17 프로필 qa"));
    QCOMPARE(profile::DisplayVersion(QString(), QString(), QStringLiteral("dev")), QStringLiteral("프로필 dev"));
  }

  void DescribeNamesTheReason() {
    QVERIFY(profile::Describe({QStringLiteral("dev"), profile::Source::kBuildTree}).contains(QStringLiteral("빌드 폴더")));
    QVERIFY(profile::Describe({QStringLiteral("qa"), profile::Source::kArgument}).contains(QStringLiteral("--profile")));
    QVERIFY(profile::Describe({}).contains(QStringLiteral("설치본과 같은")));
  }

  // 프로필이 다르면 기억한 값(QSettings)과 앱 데이터 폴더가 갈라지고, 프로필이 없으면 설치본의 이름을 그대로 씁니다.
  void ProfilesDoNotShareRememberedValuesOrDataFolder() {
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir settings_root;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root.path());
    QCoreApplication::setOrganizationName(QStringLiteral("deepmedi"));

    QCoreApplication::setApplicationName(profile::ApplicationName(QString()));
    const QString installed_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    RememberedValues::Saved().SetValue(QStringLiteral("shot/root"), QStringLiteral("설치본의 저장 위치"));

    QCoreApplication::setApplicationName(profile::ApplicationName(QStringLiteral("dev")));
    const QString dev_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QVERIFY(!RememberedValues::Saved().Value(QStringLiteral("shot/root")).isValid());
    RememberedValues::Saved().SetValue(QStringLiteral("shot/root"), QStringLiteral("개발 빌드의 저장 위치"));

    QCoreApplication::setApplicationName(profile::ApplicationName(QString()));
    QCOMPARE(RememberedValues::Saved().Value(QStringLiteral("shot/root")).toString(), QStringLiteral("설치본의 저장 위치"));
    QVERIFY(installed_dir != dev_dir);
    QVERIFY(installed_dir.endsWith(QStringLiteral("/deepmedi/hwlab")));
    QVERIFY(dev_dir.endsWith(QStringLiteral("/deepmedi/hwlab-dev")));
    QStandardPaths::setTestModeEnabled(false);
  }
};

QTEST_GUILESS_MAIN(AppProfileTest)
#include "test_app_profile.moc"
