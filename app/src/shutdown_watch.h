#ifndef HWLAB_APP_SHUTDOWN_WATCH_H_
#define HWLAB_APP_SHUTDOWN_WATCH_H_

#include <QString>

// 앱 종료의 단계 기록과 제한 시간입니다.
//
// 종료 중 장치 드라이버 호출(카메라, 열화상)이 돌아오지 않아 창이 닫히지 않거나, 창이 닫힌 뒤 프로세스가 장치를 잡은 채 남은
// 일이 있었습니다(2026-10-27 노트북, v0.1.8 두 개가 남음). 남은 프로세스는 다음 실행이 카메라를 열지 못하게 합니다.
// 그래서 단계마다 시각을 shutdown_log.txt에 남기고, 정한 시간 안에 끝나지 않으면 마지막 단계를 남긴 뒤 프로세스를 끝냅니다.

/** 기록 파일 위치를 정합니다. QGuiApplication과 앱 이름을 정한 뒤 한 번 부릅니다. */
void InitShutdownLog();

/** 단계 하나를 시각과 함께 남깁니다. 어느 스레드에서 불러도 됩니다. */
void LogShutdownStep(const QString& step);

/**
 * 지금부터 limit_ms 안에 프로세스가 끝나지 않으면, 마지막 단계를 남기고 프로세스를 끝냅니다.
 *
 * 다시 부르면 제한 시간을 새로 정합니다. 감시는 별도 스레드가 하므로 화면 스레드가 멈춰도 동작합니다.
 */
void ArmShutdownLimit(int limit_ms, const QString& what);

/** 제한 시간을 풉니다. 종료가 사람의 확인(복원 불일치 안내)을 기다리는 동안 부릅니다. */
void DisarmShutdownLimit();

/** 지금 제한 시간이 걸려 있으면 true입니다. */
bool ShutdownLimitArmed();

#endif  // HWLAB_APP_SHUTDOWN_WATCH_H_
