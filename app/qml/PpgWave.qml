import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 열화상 오른쪽 여백에 크게 그리는 PPG 맥파입니다. 위 RGB 패널과 같은 폭이라 미리보기 두 줄의 균형이 맞습니다.
// 이 칸이 보일 때 왼쪽 PpgPanel은 작은 맥파를 숨기고 연결과 포트 선택만 맡습니다. 값과 색은 PpgPanel의 맥파와 같습니다.
Rectangle {
    id: pane
    required property PpgController ppg
    readonly property bool hasWave: ppg.connected && ppg.wave.length > 1

    color: Theme.background

    // 왼쪽 경계선. RGB 패널의 경계선과 같은 선입니다.
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.line
    }

    ColumnLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 12; topMargin: 14; bottomMargin: 14 }
        spacing: 4

        Label {
            text: "PPG"
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Label {
            Layout.fillWidth: true
            text: pane.ppg.connected ? pane.ppg.statusText : "연결 안 함. 왼쪽 PPG 칸에서 포트를 고릅니다"
            wrapMode: Text.Wrap
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            color: Theme.textMuted
        }

        // 최근 5초 맥파. 마지막 점을 오른쪽 끝에 둡니다. 높이가 너무 크면 파형이 과장되어 보여 200화소까지만 씁니다.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Canvas {
                id: waveCanvas
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                height: Math.min(parent.height, 200)
                visible: pane.hasWave
                readonly property var points: pane.ppg.wave
                onPointsChanged: requestPaint()
                onHeightChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const n = points.length
                    if (n < 2) return
                    const step = width / (pane.ppg.wavePoints - 1)
                    ctx.strokeStyle = "#ffd60a"
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (let i = 0; i < n; ++i) {
                        const x = width - (n - 1 - i) * step
                        const y = height - 2 - points[i] * (height - 4)
                        if (i === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
            }
        }
        Label {
            visible: pane.hasWave
            text: "최근 5초"
            font.pixelSize: 11
            color: Theme.textMuted
        }
    }
}
