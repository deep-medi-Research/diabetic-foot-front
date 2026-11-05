import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 앱을 닫을 때 카메라를 실행 전 상태로 되돌리는 동안과, 되돌린 값이 맞지 않을 때의 안내입니다.
// 같은 카메라를 다른 사람도 쓰므로 맞지 않은 항목은 닫기 전에 사람이 확인하게 합니다.
Popup {
    id: notice
    required property CaptureController capture
    readonly property bool failed: capture.restoreReport !== ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.NoAutoClose
    visible: capture.shuttingDown || (capture.closeReady && failed)

    Connections {
        target: notice.capture
        // 문제가 없으면 바로 끝냅니다. 있으면 안내를 띄우고 사람이 닫기를 누를 때 끝냅니다.
        function onShutdownFinished() {
            if (!notice.failed) Qt.quit()
        }
    }

    Overlay.modal: Rectangle { color: "#99000000" }
    background: Rectangle {
        color: Theme.background
        border.width: 1
        border.color: Theme.line
        radius: Theme.radius
    }

    contentItem: ColumnLayout {
        spacing: 0

        Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: notice.failed ? Theme.mismatch : Theme.settling
            }
            Label {
                text: notice.failed ? "카메라 설정 복원 불일치"
                      : notice.capture.savingBeforeClose ? "촬영 저장 중" : "카메라 설정 복원 중"
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: notice.failed ? Theme.mismatch : Theme.settling
            }
        }
        Label {
            Layout.topMargin: 12
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: notice.failed
                  ? "카메라 설정 일부가 실행 전 값으로 돌아가지 않았습니다. 다음 사용자에게 알리거나 카메라 설정을 확인하십시오."
                  : notice.capture.savingBeforeClose
                    ? "녹화를 멈추고 지금까지 찍은 촬영을 저장하는 중입니다. 저장이 끝나면 카메라 설정을 되돌리고 닫습니다."
                    : "카메라 설정을 실행 전 값으로 되돌리는 중입니다."
            font.family: Theme.font
            font.pixelSize: 17
            font.weight: Font.DemiBold
            lineHeight: 1.25
            color: Theme.text
        }
        Label {
            Layout.topMargin: 10
            Layout.fillWidth: true
            visible: notice.failed
            wrapMode: Text.Wrap
            text: notice.capture.restoreReport
            font.family: Theme.font
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            lineHeight: 1.3
            color: Theme.textMuted
        }
        Rectangle {
            Layout.topMargin: 24
            Layout.alignment: Qt.AlignRight
            visible: notice.failed
            implicitWidth: closeLabel.implicitWidth + 40
            implicitHeight: 40
            radius: Theme.radius
            color: Theme.controlActive
            Label {
                id: closeLabel
                anchors.centerIn: parent
                text: "확인하고 닫기"
                font.family: Theme.font
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.background
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: Qt.quit()
            }
        }
    }
}
