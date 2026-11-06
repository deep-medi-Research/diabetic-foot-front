import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// PPG 장치입니다. 지난번 PPG 변환기가 꽂혀 있으면 앱이 자동으로 열고(PpgController::MaybeAutoConnect), 아니면 사람이 포트를
// 고릅니다. 연결하면 바로 측정을 시작해 파형이 오는지 상태 줄과 최근 5초 맥파로 보입니다.
// 촬영 버튼을 누르면 기록을 시작하고 저장할 때 촬영 폴더에 <촬영 이름>_ppg.csv로 씁니다. 연결하지 않으면 PPG 없이 찍습니다.
ColumnLayout {
    id: panel
    required property PpgController ppg
    required property bool editable  // 미리보기 중이면 true
    // false이면 작은 맥파를 숨깁니다. 열화상 오른쪽의 PpgWave가 크게 그릴 때입니다.
    property bool showWave: true

    // 포트 목록은 연결하지 않았을 때 펼치고, 연결한 뒤에는 접습니다. 상태 줄을 누르면 연결이 바뀔 때까지 그 선택을 따릅니다.
    property bool toggled: false
    property bool toggledOpen: false
    readonly property bool listOpen: toggled ? toggledOpen : !ppg.connected
    readonly property bool connectedNow: ppg.connected
    onConnectedNowChanged: toggled = false
    readonly property color stateColor: ppg.tone === "ok" ? "#30d158"
                                      : ppg.tone === "warn" ? Theme.settling
                                      : ppg.tone === "error" ? Theme.mismatch : Theme.textMuted

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "PPG"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: Theme.textMuted
        }
        // 포트 목록을 다시 읽습니다. 포트를 열지 않습니다.
        Label {
            text: "↻"
            font.pixelSize: 15
            color: Theme.textMuted
            visible: panel.listOpen
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.ppg.refreshPorts()
            }
        }
    }

    // 상태 줄. 누르면 포트 목록을 펼치거나 접습니다.
    Item {
        Layout.topMargin: 4
        Layout.fillWidth: true
        implicitHeight: stateRow.implicitHeight
        RowLayout {
            id: stateRow
            anchors.fill: parent
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                color: panel.stateColor
                // 기록 중에는 깜빡여 촬영과 함께 기록되고 있음을 보입니다.
                SequentialAnimation on opacity {
                    running: panel.ppg.recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.3; duration: 500 }
                    NumberAnimation { to: 1; duration: 500 }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: panel.ppg.connected ? panel.ppg.portName : "연결 안 함(PPG 없이 촬영)"
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    visible: panel.ppg.connected || panel.ppg.tone === "error"
                    text: panel.ppg.statusText
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                    font.features: { "tnum": 1 }
                    color: panel.ppg.tone === "ok" ? Theme.textMuted : panel.stateColor
                }
            }
            Label {
                text: "›"
                rotation: panel.listOpen ? 90 : 0
                font.pixelSize: 18
                color: Theme.textMuted
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.toggledOpen = !panel.listOpen
                panel.toggled = true
            }
        }
    }

    // 최근 5초 맥파. 손가락 센서 원시값을 100Hz로 평균하고 높이를 맞춘 값(ppg.wave)을 그리기만 합니다.
    // 마지막 점을 오른쪽 끝에 두므로 장치를 다시 시작한 직후에는 오른쪽부터 차오릅니다. 색은 결과 창 PPG 줄과 같습니다.
    Canvas {
        id: waveCanvas
        Layout.topMargin: 6
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        visible: panel.showWave && panel.ppg.connected && panel.ppg.wave.length > 1
        readonly property var points: panel.ppg.wave
        onPointsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const n = points.length
            if (n < 2) return
            const step = width / (panel.ppg.wavePoints - 1)
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

    // 포트 목록. 행을 누르면 그 포트를 엽니다. 지난번에 측정 데이터를 받은 변환기는 "지난번 PPG"로 표시합니다.
    // 조명 칸이 연 포트는 누를 수 없습니다.
    ColumnLayout {
        Layout.topMargin: 8
        Layout.fillWidth: true
        spacing: 2
        visible: panel.listOpen

        Repeater {
            model: panel.ppg.ports
            delegate: Rectangle {
                id: portRow
                required property var modelData
                readonly property bool open: panel.ppg.connected && panel.ppg.portName === modelData.name
                readonly property bool usable: panel.editable && !modelData.lightOpen
                Layout.fillWidth: true
                implicitHeight: portColumn.implicitHeight + 10
                radius: 6
                color: portArea.containsMouse && usable ? Theme.control : "transparent"
                opacity: usable || open ? 1 : 0.4

                ColumnLayout {
                    id: portColumn
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 6; rightMargin: 6 }
                    spacing: 1
                    RowLayout {
                        spacing: 8
                        Label { text: portRow.modelData.name; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.text }
                        Label {
                            text: portRow.modelData.vidPid
                            visible: text !== ""
                            font.pixelSize: 13
                            font.features: { "tnum": 1 }
                            color: Theme.textMuted
                        }
                        Label {
                            text: portRow.open ? "연결됨"
                                  : portRow.modelData.lightOpen ? "조명에서 사용 중"
                                  : portRow.modelData.suggested ? "지난번 PPG" : ""
                            visible: text !== ""
                            font.pixelSize: 13
                            color: portRow.open ? Theme.text : portRow.modelData.lightOpen ? Theme.textMuted : Theme.settling
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: portRow.modelData.description + (portRow.modelData.serial !== "" ? "  " + portRow.modelData.serial : "")
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }
                MouseArea {
                    id: portArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: portRow.usable && !portRow.open
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.ppg.connectPort(portRow.modelData.name)
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: panel.ppg.ports.length === 0
            text: "직렬 포트가 없습니다"
            font.pixelSize: 13
            color: Theme.textMuted
        }
        Label {
            visible: panel.ppg.connected
            text: "연결 끊기"
            font.pixelSize: 13
            color: Theme.textMuted
            opacity: panel.editable ? 1 : 0.4
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                enabled: panel.editable
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.ppg.disconnectPort()
            }
        }
    }
}
