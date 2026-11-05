import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 촬영 버튼을 눌렀는데 열화상이 수신 중이 아닐 때 열화상 없이 찍을지 묻는 안내입니다.
// 기본 선택은 취소입니다. Enter와 Esc 모두 취소이고, 진행은 버튼을 직접 눌러야 합니다.
Popup {
    id: prompt
    required property CaptureController capture
    readonly property string kind: capture.thermalPrompt
    readonly property color tone: kind === "preparing" ? Theme.settling : Theme.mismatch

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.CloseOnEscape

    onKindChanged: kind !== "" ? open() : close()
    // Esc로 닫으면 안내 상태도 지웁니다.
    onClosed: if (capture.thermalPrompt !== "") capture.cancelThermalPrompt()

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle {
        color: Theme.background
        border.width: 1
        border.color: Theme.line
        radius: Theme.radius
    }

    contentItem: ColumnLayout {
        spacing: 0
        focus: true
        Keys.onReturnPressed: prompt.capture.cancelThermalPrompt()
        Keys.onEnterPressed: prompt.capture.cancelThermalPrompt()

        Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: prompt.tone
            }
            Label {
                text: prompt.kind === "preparing" ? "열화상 카메라 준비 중" : "열화상 카메라 인식 안 됨"
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: prompt.tone
            }
        }
        Label {
            Layout.topMargin: 12
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: prompt.kind === "preparing"
                  ? "열화상 카메라가 교정을 마치는 중입니다. 열화상 없이 촬영할까요?"
                  : "열화상 카메라가 인식되지 않았습니다. 열화상 없이 촬영할까요?"
            font.family: Theme.font
            font.pixelSize: 17
            font.weight: Font.DemiBold
            lineHeight: 1.25
            color: Theme.text
        }
        Label {
            Layout.topMargin: 10
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "커프 단계 추정은 RGB 영상만 쓰므로 열화상 없이 촬영해도 분석할 수 있습니다."
            font.family: Theme.font
            font.pixelSize: 13
            color: Theme.textMuted
        }
        RowLayout {
            Layout.topMargin: 24
            Layout.alignment: Qt.AlignRight
            spacing: 10

            // 진행: 테두리만 있는 버튼
            Rectangle {
                implicitWidth: goLabel.implicitWidth + 32
                implicitHeight: 40
                radius: Theme.radius
                color: "transparent"
                border.width: 1
                border.color: Theme.line
                Label {
                    id: goLabel
                    anchors.centerIn: parent
                    text: "열화상 없이 촬영"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.startWithoutThermal()
                }
            }
            // 취소: 기본 선택이라 채운 버튼
            Rectangle {
                implicitWidth: cancelLabel.implicitWidth + 40
                implicitHeight: 40
                radius: Theme.radius
                color: Theme.controlActive
                Label {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: "취소"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.background
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.cancelThermalPrompt()
                }
            }
        }
    }
}
