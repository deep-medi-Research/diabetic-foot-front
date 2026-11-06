import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 미리보기 아래에 미리보기 폭으로 그리는 PPG 맥파입니다. 왼쪽에 상태 글자, 오른쪽에 최근 5초 맥파를 둡니다.
// 포트 고르기는 설정 패널의 PpgPanel이 맡고, 그 패널은 작은 맥파를 숨깁니다. 값과 색은 PpgPanel의 맥파와 같습니다.
Rectangle {
    id: pane
    required property PpgController ppg
    readonly property bool hasWave: ppg.connected && ppg.wave.length > 1

    radius: Theme.radius
    color: Theme.previewBackground

    RowLayout {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16; topMargin: 12; bottomMargin: 12 }
        spacing: 16

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260
            spacing: 4
            Label {
                text: "PPG"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Label {
                Layout.fillWidth: true
                text: pane.ppg.connected ? pane.ppg.statusText : "연결 안 함. 설정에서 포트를 고릅니다"
                wrapMode: Text.Wrap
                font.pixelSize: 13
                font.features: { "tnum": 1 }
                color: Theme.textMuted
            }
            Label {
                visible: pane.hasWave
                text: "최근 5초"
                font.pixelSize: 13
                color: Theme.textMuted
            }
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
    }
}
