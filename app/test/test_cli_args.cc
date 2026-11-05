#include <QFile>
#include <QTest>

#include <set>

#include "cli_args.h"

namespace {

QStringList Args(const QStringList& rest) { return QStringList{QStringLiteral("hwlab_app")} + rest; }

QStringList WithSpec(const QStringList& rest) { return Args({QStringLiteral("--spec"), QStringLiteral("a.json")}) + rest; }

}  // namespace

class CliArgsTest : public QObject {
  Q_OBJECT

 private slots:
  // 표의 이름은 겹치지 않고 --로 시작하며, 값 자리와 설명이 비어 있지 않습니다. Find가 첫 항목만 돌려주므로 겹치면 뒤 항목이 가려집니다.
  void TableIsWellFormed() {
    std::set<QString> names;
    for (const cli::ArgSpec& spec : cli::Arguments()) {
      const QString name = QString::fromUtf8(spec.name);
      QVERIFY2(name.startsWith(QStringLiteral("--")), qPrintable(name));
      QVERIFY2(names.insert(name).second, qPrintable(name));
      QVERIFY2(spec.help != nullptr && *spec.help != '\0', qPrintable(name));
      QVERIFY2(spec.value == nullptr || *spec.value != '\0', qPrintable(name));
    }
    QVERIFY(names.size() > 30);
  }

  // 표에 있는 인자는 값을 주든 안 주든 형식대로 주면 모두 받아들입니다.
  void AcceptsEveryArgumentInTable() {
    for (const cli::ArgSpec& spec : cli::Arguments()) {
      QStringList rest{QString::fromUtf8(spec.name)};
      if (spec.value != nullptr) rest << (rest[0] == QStringLiteral("--profile") ? QStringLiteral("qa") : QStringLiteral("값"));
      const auto problems = cli::Check(WithSpec(rest));
      QVERIFY2(!problems, qPrintable(rest.join(QLatin1Char(' ')) + QStringLiteral(": ") + problems.value_or(QString())));
    }
  }

  void RejectsUnknownArgument() {
    const auto problems = cli::Check(WithSpec({QStringLiteral("--fake"), QStringLiteral("--bogus")}));
    QVERIFY(problems);
    QCOMPARE(*problems, QStringLiteral("알 수 없는 인자입니다: --bogus"));
  }

  // 값이 뒤에 오는 인자는 마지막에 오거나 다른 인자가 바로 뒤에 오면 값이 빠진 것입니다.
  void RejectsMissingValue() {
    const auto last = cli::Check(WithSpec({QStringLiteral("--out")}));
    QVERIFY(last);
    QCOMPARE(*last, QStringLiteral("값이 빠진 인자입니다: --out <폴더>"));

    const auto before_option = cli::Check(WithSpec({QStringLiteral("--out"), QStringLiteral("--fake")}));
    QVERIFY(before_option);
    QCOMPARE(*before_option, QStringLiteral("값이 빠진 인자입니다: --out <폴더>"));
  }

  // 값은 인자 이름으로 오인하지 않습니다. 한글 값, 공백이 든 경로, 음수는 그대로 값입니다.
  void TakesValuesLiterally() {
    QVERIFY(!cli::Check(WithSpec({QStringLiteral("--patient"), QStringLiteral("홍 길동"), QStringLiteral("--out"),
                                  QStringLiteral("C:\\내 문서\\shots"), QStringLiteral("--fake-free-bytes"),
                                  QStringLiteral("-1")})));
  }

  // 값이 앞 인자에 먹혀 --fake를 잃어버리는 일이 없어야 하므로, 값 뒤의 인자는 다시 인자로 읽습니다.
  void ArgumentAfterValueIsCheckedToo() {
    const auto problems = cli::Check(WithSpec({QStringLiteral("--out"), QStringLiteral("x"), QStringLiteral("--bogus")}));
    QVERIFY(problems);
    QCOMPARE(*problems, QStringLiteral("알 수 없는 인자입니다: --bogus"));
  }

  void SuggestsSpaceInsteadOfEquals() {
    const auto problems = cli::Check(WithSpec({QStringLiteral("--out=C:\\shots")}));
    QVERIFY(problems);
    QVERIFY2(problems->contains(QStringLiteral("--out C:\\shots")), qPrintable(*problems));
  }

  // --profile 이름은 폴더와 레지스트리 키에 들어가므로 쓸 수 없는 글자는 거절합니다.
  void ChecksProfileName() {
    QVERIFY(!cli::Check(WithSpec({QStringLiteral("--profile"), QStringLiteral("qa-1")})));
    QVERIFY(!cli::Check(WithSpec({QStringLiteral("--profile"), QStringLiteral("default")})));
    const auto problems = cli::Check(WithSpec({QStringLiteral("--profile"), QStringLiteral("a/b")}));
    QVERIFY(problems);
    QVERIFY2(problems->contains(QStringLiteral("a/b")), qPrintable(*problems));
  }

  void RejectsBareWord() {
    const auto problems = cli::Check(WithSpec({QStringLiteral("fake")}));
    QVERIFY(problems);
    QVERIFY2(problems->startsWith(QStringLiteral("알 수 없는 인자입니다: fake")), qPrintable(*problems));
  }

  void ReportsEveryProblemOnItsOwnLine() {
    const auto problems = cli::Check(Args({QStringLiteral("--bogus"), QStringLiteral("--out")}));
    QVERIFY(problems);
    const QStringList lines = problems->split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines[0].contains(QStringLiteral("--bogus")));
    QVERIFY(lines[1].contains(QStringLiteral("--out")));
    QVERIFY(lines[2].contains(QStringLiteral("--spec")));
  }

  void SpecIsRequiredUnlessResult() {
    const auto none = cli::Check(Args({QStringLiteral("--fake")}));
    QVERIFY(none);
    QVERIFY(none->contains(QStringLiteral("--spec")));
    QVERIFY(!cli::Check(Args({QStringLiteral("--result"), QStringLiteral("folder")})));
  }

  void WantsHelpForCommonSpellings() {
    for (const char* spelling : {"--help", "-h", "/?"}) {
      QVERIFY2(cli::WantsHelp(Args({QStringLiteral("--spec"), QStringLiteral("a.json"), QString::fromLatin1(spelling)})),
               spelling);
    }
    QVERIFY(!cli::WantsHelp(WithSpec({QStringLiteral("--fake")})));
    QVERIFY(!cli::WantsHelp(QStringList{QStringLiteral("/?")}));  // 첫 항목은 실행 파일 경로입니다
  }

  // 도움말에는 모든 인자의 이름이 한 번 이상 나오고, 줄이 터미널 폭을 크게 넘지 않습니다.
  void HelpListsEveryArgumentWithinWidth() {
    const QString help = cli::HelpText(QStringLiteral("hwlab_app.exe"), QStringLiteral("기록 파일\n  로그 폴더: X\n"));
    for (const cli::ArgSpec& spec : cli::Arguments()) {
      QVERIFY2(help.contains(QString::fromUtf8(spec.name)), spec.name);
    }
    QVERIFY(help.contains(QStringLiteral("hwlab_app.exe --spec <선언 파일>")));
    QVERIFY(help.contains(QStringLiteral("기록 파일\n  로그 폴더: X")));
    for (const QString& line : help.split(QLatin1Char('\n'))) {
      int width = 0;
      for (const QChar ch : line) width += ch.unicode() >= 0x1100 ? 2 : 1;
      QVERIFY2(width <= 120, qPrintable(line));
    }
  }

  void HelpWithoutFooterEndsAfterLastArgument() {
    const QString help = cli::HelpText(QStringLiteral("hwlab_app"), QString());
    QVERIFY(help.endsWith(QStringLiteral("\n")));
    QVERIFY(!help.endsWith(QStringLiteral("\n\n")));
  }

  // 환자 이름과 생년월일은 로그에 남지 않게 값만 가립니다. 다른 인자는 그대로입니다.
  void MasksPatientValuesInLog() {
    const QStringList masked = cli::MaskedForLog(
        Args({QStringLiteral("--patient"), QStringLiteral("홍길동"), QStringLiteral("--birth"), QStringLiteral("1955-11-03"),
              QStringLiteral("--out"), QStringLiteral("C:\\shots"), QStringLiteral("--fake")}));
    QCOMPARE(masked, Args({QStringLiteral("--patient"), QStringLiteral("***"), QStringLiteral("--birth"), QStringLiteral("***"),
                           QStringLiteral("--out"), QStringLiteral("C:\\shots"), QStringLiteral("--fake")}));
  }

  // 인자 값이 인자 이름처럼 생겨도 마지막 인자가 값 없이 끝나도 가리기가 범위를 넘지 않습니다.
  void MaskingHandlesTrailingSecretWithoutValue() {
    QCOMPARE(cli::MaskedForLog(Args({QStringLiteral("--patient")})), Args({QStringLiteral("--patient")}));
  }

  // docs/debugging.md의 인자 표는 cli_args.cc의 표에서 만든 것과 같아야 합니다. 어긋나면 HWLAB_UPDATE_DOCS=1로 이 시험을 돌려 고칩니다.
  void DocsTableMatchesTable() {
    const QString path = QStringLiteral(HWLAB_DOCS_DIR "/debugging.md");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    const QString begin = QStringLiteral("<!-- hwlab-args:begin -->\n");
    const QString end = QStringLiteral("<!-- hwlab-args:end -->");
    const qsizetype from = text.indexOf(begin);
    const qsizetype to = text.indexOf(end);
    QVERIFY2(from >= 0 && to > from, "docs/debugging.md에 hwlab-args 표시가 없습니다");
    const QString current = text.mid(from + begin.size(), to - from - begin.size());
    const QString expected = cli::MarkdownTable() + QLatin1Char('\n');
    if (current == expected) return;
    if (qEnvironmentVariableIsSet("HWLAB_UPDATE_DOCS")) {
      QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
      file.write((text.left(from + begin.size()) + expected + text.mid(to)).toUtf8());
      return;
    }
    QFAIL("docs/debugging.md의 인자 표가 cli_args.cc의 표와 다릅니다. HWLAB_UPDATE_DOCS=1로 test_cli_args를 돌리면 고칩니다");
  }

  // 마크다운 표의 칸 구분자와 섞이지 않게 값 자리의 |를 이스케이프합니다.
  void MarkdownEscapesPipes() {
    const QString table = cli::MarkdownTable();
    QVERIFY(table.contains(QStringLiteral("| `--palette iron\\|gray\\|rainbow\\|high-contrast` |")));
    QVERIFY(table.contains(QStringLiteral("**실행**\n\n| 인자 | 설명 |\n| --- | --- |\n| `--spec <선언 파일>` |")));
    for (const cli::ArgSpec& spec : cli::Arguments()) {
      QVERIFY2(table.contains(QString::fromUtf8(spec.name)), spec.name);
    }
  }
};

QTEST_GUILESS_MAIN(CliArgsTest)
#include "test_cli_args.moc"
