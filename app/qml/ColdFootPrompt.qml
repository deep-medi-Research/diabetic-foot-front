import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 촬영 버튼을 눌렀는데 미리보기 열화상으로 본 발이 차가울 때 이대로 찍을지 묻는 안내입니다(CaptureController::footWarmthState).
// 기본 선택은 기다렸다 찍기(취소)입니다. Enter와 Esc 모두 취소이고, 진행은 버튼을 직접 눌러야 합니다.
Popup {
    id: prompt
    required property CaptureController capture
    readonly property string title: capture.coldFootPrompt
    readonly property color tone: Theme.settling

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.CloseOnEscape

    onTitleChanged: title !== "" ? open() : close()
    // Esc로 닫으면 안내 상태도 지웁니다.
    onClosed: if (capture.coldFootPrompt !== "") capture.cancelColdFootPrompt()

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
        Keys.onReturnPressed: prompt.capture.cancelColdFootPrompt()
        Keys.onEnterPressed: prompt.capture.cancelColdFootPrompt()

        Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: prompt.tone
            }
            Label {
                text: "발이 차가움"
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
            text: prompt.title
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
            // 제목과 같은 첫 문장은 빼고 괄호 안의 값만 씁니다.
            readonly property string values: {
                const t = prompt.capture.footWarmthText
                return t.indexOf("(") >= 0 ? t.substring(t.indexOf("(") + 1, t.lastIndexOf(")")) : ""
            }
            text: (values !== "" ? values + ". " : "") + "발이 차가우면 발바닥 혈류(맥박 신호)가 약해 재측정이 될 수 있습니다. "
                  + "양말을 신고 5~10분 기다린 뒤 측정하기를 권합니다."
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
                    text: "이대로 녹화"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.startWithColdFoot()
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
                    text: "기다렸다 찍기"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.background
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.cancelColdFootPrompt()
                }
            }
        }
    }
}
