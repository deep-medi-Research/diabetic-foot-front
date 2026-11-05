#ifndef HWLAB_APP_CLI_ARGS_H_
#define HWLAB_APP_CLI_ARGS_H_

#include <QString>
#include <QStringList>

#include <optional>
#include <span>

// hwlab_app이 받는 명령줄 인자의 표입니다. 인자 검사, --help 출력, docs/debugging.md의 인자 표가 모두 이 표 하나에서 나옵니다.
// 인자를 더하거나 빼면 이 표만 고칩니다. 표에 없는 인자는 실행할 때 거절되므로, 빠뜨리면 바로 드러납니다.
namespace cli {

/** --help와 문서에서 인자를 묶는 순서입니다. */
enum class Group { kRun, kDevices, kFake, kPeripherals, kPatient, kAnalysis, kResult, kCheck };

struct ArgSpec {
  const char* name;   // "--out"
  const char* value;  // 값 자리 "<폴더>". 값을 받지 않는 인자는 nullptr입니다
  Group group;
  const char* help;   // 한 줄 설명
  bool secret;        // 값을 로그에 남기지 않습니다(환자 정보)
};

/** 모든 인자를 --help와 문서에 나오는 순서로 돌려줍니다. */
std::span<const ArgSpec> Arguments();

/** 묶음의 제목입니다. */
QString GroupTitle(Group group);

/** 이름(예: "--out")으로 찾습니다. 없으면 nullptr입니다. */
const ArgSpec* Find(const QString& name);

/** --help, -h, /? 중 하나가 있으면 true입니다. 다른 인자보다 먼저 봅니다. */
bool WantsHelp(const QStringList& args);

/**
 * 인자를 표와 견줍니다. 모르는 인자, 값이 빠진 인자, 필요한 인자(--result가 아니면 --spec)가 없는 경우를 줄마다 하나씩 담아
 * 돌려주고, 문제가 없으면 nullopt입니다.
 *
 * @param args QCoreApplication::arguments(). 첫 항목은 실행 파일 경로입니다
 */
std::optional<QString> Check(const QStringList& args);

/**
 * --help 본문입니다.
 *
 * @param program 사용법 줄에 쓸 실행 파일 이름
 * @param footer 인자 표 뒤에 덧붙일 글(기록 파일 위치). 비어 있으면 붙이지 않습니다
 */
QString HelpText(const QString& program, const QString& footer);

/** docs/debugging.md에 들어가는 인자 표(마크다운)입니다. 문서와 어긋나면 시험이 실패합니다. */
QString MarkdownTable();

/** 로그에 남길 인자입니다. 환자 정보 인자(secret)의 값은 ***로 가립니다. */
QStringList MaskedForLog(const QStringList& args);

}  // namespace cli

#endif  // HWLAB_APP_CLI_ARGS_H_
