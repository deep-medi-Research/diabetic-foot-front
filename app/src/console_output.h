#ifndef HWLAB_APP_CONSOLE_OUTPUT_H_
#define HWLAB_APP_CONSOLE_OUTPUT_H_

#include <QString>

// 콘솔에 글을 씁니다.
//
// Windows의 앱은 창 프로그램(GUI 하위 시스템)이라 콘솔이 없고 표준 출력과 표준 오류가 어디에도 닿지 않습니다. 그래서 명령 프롬프트에서
// 켰을 때만 그 콘솔에 붙습니다. 바로가기나 탐색기에서 켠 실행은 붙을 콘솔이 없으므로 아무것도 하지 않습니다.
// 명령 프롬프트는 창 프로그램이 끝나기를 기다리지 않으므로 출력은 프롬프트 뒤에 나옵니다. 끝나기를 기다리고 종료 코드를 보려면
// start /wait를 쓰거나 출력을 | more 로 넘깁니다.

/**
 * 부모 프로세스의 콘솔에 붙습니다. 비어 있는 표준 출력과 표준 오류 핸들만 그 콘솔로 채우므로, 부모가 > 파일이나 파이프로 돌린
 * 출력은 그대로 둡니다. QGuiApplication을 만들기 전에 부릅니다. Windows가 아니면 아무것도 하지 않습니다.
 */
void AttachParentConsole();

/** 표준 출력에 씁니다. 콘솔이면 코드 페이지와 상관없이 읽히고, 파일이나 파이프면 UTF-8입니다. 쓸 곳이 없으면 버립니다. */
void WriteStdout(const QString& text);

/** 표준 오류에 씁니다. 규칙은 WriteStdout과 같습니다. */
void WriteStderr(const QString& text);

/** 디버거 출력(Windows의 OutputDebugString)에 씁니다. VS 출력 창과 DebugView에 보입니다. Windows가 아니면 아무것도 하지 않습니다. */
void WriteToDebugger(const QString& text);

#endif  // HWLAB_APP_CONSOLE_OUTPUT_H_
