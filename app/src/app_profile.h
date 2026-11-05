#ifndef HWLAB_APP_APP_PROFILE_H_
#define HWLAB_APP_APP_PROFILE_H_

#include <QString>
#include <QStringList>

// 개발 빌드와 설치본이 같은 PC에서 설정과 기록을 나눠 쓰지 않게 하는 프로필입니다.
//
// 설정(QSettings)과 앱 데이터 폴더는 QGuiApplication의 앱 이름을 따릅니다. 프로필이 있으면 앱 이름을 hwlab-<프로필>로 바꿔
// Windows는 레지스트리 HKCU\Software\deepmedi\hwlab-<프로필>과 %LOCALAPPDATA%\deepmedi\hwlab-<프로필>을 씁니다.
// 프로필이 없으면 앱 이름이 hwlab 그대로라 설치본의 위치와 레지스트리 키는 바뀌지 않습니다.
// 장치를 여는 실행이 하나만 돌게 하는 이름(single_instance.cc)은 프로필과 상관없이 같습니다. 카메라는 하나이기 때문입니다.
namespace profile {

enum class Source {
  kDefault,    // 프로필 없음. 설치본과 같은 위치
  kArgument,   // --profile로 정함
  kBuildTree,  // 빌드 폴더에서 실행해 자동으로 고름
};

struct Choice {
  QString name;  // 비어 있으면 프로필이 없습니다
  Source source = Source::kDefault;
};

/** 빌드 폴더에서 실행했을 때 자동으로 쓰는 프로필 이름입니다. */
inline QString BuildTreeName() { return QStringLiteral("dev"); }

/** --profile에 주면 프로필 없이 설치본과 같은 위치를 쓰는 이름입니다. */
inline QString DefaultName() { return QStringLiteral("default"); }

/** 프로필 이름으로 쓸 수 있는지입니다. 영문, 숫자, 점, 밑줄, 하이픈 32자까지입니다. 폴더 이름과 레지스트리 키에 들어가기 때문입니다. */
bool IsValidName(const QString& name);

/**
 * 실행 파일이 CMake 빌드 폴더에 있는지입니다. 실행 파일 폴더나 그 위 폴더에 CMakeCache.txt가 있으면 빌드 폴더입니다
 * (Ninja는 같은 폴더, Visual Studio 생성기는 Debug나 Release 폴더의 위). cmake --install로 만든 폴더에는 없습니다.
 */
bool IsBuildTreeDir(const QString& exe_dir);

/**
 * 이번 실행의 프로필을 정합니다. --profile이 올바른 값과 함께 있으면 그 값(default는 프로필 없음)이고, 없으면 빌드 폴더에서
 * 실행한 경우만 dev, 아니면 프로필 없음입니다. --profile 값이 잘못된 경우는 인자 검사가 알리므로 여기서는 없는 것으로 봅니다.
 */
Choice Resolve(const QStringList& args, const QString& exe_dir);

/** QGuiApplication에 줄 앱 이름입니다. 프로필이 없으면 hwlab입니다. */
QString ApplicationName(const QString& profile);

/** 창 제목에 보일 버전입니다. 프로필이 있으면 뒤에 붙입니다(예: "(개발 860ee33) 프로필 dev"). */
QString DisplayVersion(const QString& version, const QString& commit, const QString& profile);

/** 로그에 남길 설명입니다. */
QString Describe(const Choice& choice);

}  // namespace profile

#endif  // HWLAB_APP_APP_PROFILE_H_
