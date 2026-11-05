import QtQuick
import HwLab

// 촬영 버튼입니다. 둘레 고리가 녹화 진행(쓴 장 수 / 목표)을 보여줍니다.
Item {
    id: control
    required property string mode     // idle, settling, recording
    required property real progress   // 쓴 장 수 / 목표
    signal clicked()

    width: 84; height: 84

    Canvas {
        id: ring
        anchors.fill: parent
        property real progress: control.progress
        property string mode: control.mode
        onProgressChanged: requestPaint()
        onModeChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = width / 2
            ctx.lineWidth = 4
            ctx.strokeStyle = Theme.line
            ctx.beginPath(); ctx.arc(c, c, c - 3, 0, Math.PI * 2); ctx.stroke()
            if (mode === "recording" && progress > 0) {
                ctx.strokeStyle = Theme.recording
                ctx.beginPath()
                ctx.arc(c, c, c - 3, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * progress)
                ctx.stroke()
            }
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: control.mode === "idle" ? 62 : 30
        height: width
        radius: control.mode === "idle" ? width / 2 : 7
        color: control.mode === "settling" ? Theme.settling : Theme.recording
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on radius { NumberAnimation { duration: 160 } }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
