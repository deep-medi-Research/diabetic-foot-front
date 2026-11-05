import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 녹화를 준비하거나 쓰는 중에 창을 닫을 때 묻습니다. 닫기를 고르면 녹화를 멈추고 지금까지 찍은 것을 저장한 뒤 닫습니다.
Popup {
    id: confirm
    required property CaptureController capture

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.CloseOnEscape

    // 확인을 띄운 사이 녹화가 끝나면 물을 이유가 없으므로 닫습니다.
    Connections {
        target: confirm.capture
        function onCanShootChanged() {
            if (confirm.visible && !confirm.capture.shotInProgress) confirm.close()
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

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "촬영 중입니다"
            font.family: Theme.font
            font.pixelSize: 17
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Label {
            Layout.topMargin: 10
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "닫으면 녹화를 멈추고 지금까지 찍은 영상과 파일을 저장한 뒤 닫습니다. 목표 장 수보다 짧은 촬영으로 남습니다."
            font.family: Theme.font
            font.pixelSize: 14
            lineHeight: 1.3
            color: Theme.textMuted
        }
        RowLayout {
            Layout.topMargin: 24
            Layout.alignment: Qt.AlignRight
            spacing: 8

            Rectangle {
                implicitWidth: keepLabel.implicitWidth + 40
                implicitHeight: 40
                radius: Theme.radius
                color: "transparent"
                border.width: 1
                border.color: Theme.line
                Label {
                    id: keepLabel
                    anchors.centerIn: parent
                    text: "계속 촬영"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: confirm.close()
                }
            }
            Rectangle {
                implicitWidth: stopLabel.implicitWidth + 40
                implicitHeight: 40
                radius: Theme.radius
                color: Theme.controlActive
                Label {
                    id: stopLabel
                    anchors.centerIn: parent
                    text: "멈추고 닫기"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.background
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        confirm.close()
                        confirm.capture.shutdown()
                    }
                }
            }
        }
    }
}
