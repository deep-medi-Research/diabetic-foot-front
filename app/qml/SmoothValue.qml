import QtQuick

// 0.2초마다 바뀌는 값(촬영 장 수, 안정화 남은 프레임, 분석 진행 비율)을 화면에서 끊기지 않게 보이는 값입니다.
// 새 값이 오면 다음 값이 올 때까지(duration) 직선으로 따라갑니다. 정한 방향과 반대로 바뀌면(새 녹화로 장 수가 0이 됨 등)
// 따라가지 않고 바로 그 값이 됩니다.
QtObject {
    id: smooth
    // 따라갈 값입니다.
    property real goal: 0
    // 화면에 쓰는 값입니다.
    property real value: 0
    // 값이 늘어나는 쪽이면 true(장 수, 진행 비율), 줄어드는 쪽이면 false(남은 프레임)입니다.
    property bool rising: true
    // 다음 값이 오는 간격(밀리초)입니다. CaptureController, AnalysisController의 kPollMs와 같습니다.
    property int duration: 200

    property NumberAnimation follow: NumberAnimation {
        target: smooth
        property: "value"
        duration: smooth.duration
        easing.type: Easing.Linear
    }

    onGoalChanged: {
        follow.stop()
        if (rising ? goal < value : goal > value) {
            value = goal
            return
        }
        follow.from = value
        follow.to = goal
        follow.start()
    }
}
