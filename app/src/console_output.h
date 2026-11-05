#ifndef HWLAB_APP_CONSOLE_OUTPUT_H_
#define HWLAB_APP_CONSOLE_OUTPUT_H_

#include <QString>

// 콘솔에 글을 씁니다.
//
// Windows의 앱은 창 프로그램(GUI 하위 시스템)이라 콘솔이 없고 표준 출력과 표준 오류가 어디에도 닿지 않습니다. 그래서 쓸 때마다
// 부모 프로세스의 콘솔(명령 프롬프트)에 잠깐 붙었다가 떼고, 붙을 콘솔이 없으면(바로가기와 탐색기로 켠 실행) 아무것도 하지 않습니다.
// 계속 붙어 있지 않는 이유는, 붙어 있는 동안에는 그 콘솔에서 누른 Ctrl+C가 앱을 바로 끝내고(카메라 복원 없이) 콘솔 창을 닫아도
// 앱이 같이 끝나기 때문입니다. 지금까지 이 앱은 콘솔과 떨어져 있었으므로 그 동작을 바꾸지 않습니다.
// 부모가 > 파일이나 파이프로 돌린 출력은 붙지 않고 그 핸들에 그대로 씁니다.
// 명령 프롬프트는 창 프로그램이 끝나기를 기다리지 않으므로 출력은 프롬프트 뒤에 나옵니다. 끝나기를 기다리고 종료 코드를 보려면
// start /wait를 쓰거나 출력을 | more 로 넘깁니다.

/** 표준 출력에 씁니다. 콘솔이면 코드 페이지와 상관없이 읽히고, 파일이나 파이프면 UTF-8입니다. 쓸 곳이 없으면 버립니다. 어느 스레드에서나 부릅니다. */
void WriteStdout(const QString& text);

/** 표준 오류에 씁니다. 규칙은 WriteStdout과 같습니다. */
void WriteStderr(const QString& text);

/** 디버거 출력(Windows의 OutputDebugString)에 씁니다. VS 출력 창과 DebugView에 보입니다. Windows가 아니면 아무것도 하지 않습니다. */
void WriteToDebugger(const QString& text);

#endif  // HWLAB_APP_CONSOLE_OUTPUT_H_
