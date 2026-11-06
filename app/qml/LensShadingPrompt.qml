import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 열화상 렌즈 셰이딩 보정 안내입니다. 보정 파일이 없을 때("missing"), 불러오지 못했을 때("unreadable"), 렌즈 보정 버튼을
// 눌렀을 때("confirm") 열립니다.
// 렌즈를 가리지 않고 보정하면 영상이 보정 전보다 고르지 않게 되므로 기본 선택은 보정하지 않는 쪽입니다.
// 판을 렌즈와 정면으로 대면 데워진 렌즈가 판에 비쳐 보정값에 섞이므로(실기 2026-10-27) 기울여 대라고 안내합니다.
// Enter와 Esc 모두 보정하지 않고 닫으며, 보정은 버튼을 직접 눌러야 합니다.
Popup {
    id: prompt
    required property CaptureController capture
    readonly property string kind: capture.lensShadingPrompt
    // 앱이 먼저 띄운 안내(보정 파일 없음, 불러오지 못함)는 나중에 하도록 고를 수 있습니다.
    readonly property bool recommended: kind === "missing" || kind === "unreadable"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.CloseOnEscape

    onKindChanged: kind !== "" ? open() : close()
    // Esc로 닫으면 안내 상태도 지웁니다.
    onClosed: if (capture.lensShadingPrompt !== "") capture.cancelLensShadingPrompt()

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
        Keys.onReturnPressed: prompt.capture.cancelLensShadingPrompt()
        Keys.onEnterPressed: prompt.capture.cancelLensShadingPrompt()

        Row {
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: Theme.settling
            }
            Label {
                text: prompt.kind === "missing" ? "렌즈 보정값 없음" : prompt.kind === "unreadable" ? "렌즈 보정값을 불러오지 못함" : "렌즈 보정"
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.settling
            }
        }
        Label {
            Layout.topMargin: 12
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: prompt.kind === "missing"
                  ? "이 열화상 카메라의 렌즈 보정값이 없습니다. 열화상 카메라가 데워진 뒤 렌즈 앞을 판으로 기울여 가리고 보정하십시오. 나중에 하려면 열화상 칸의 렌즈 보정을 누릅니다."
                  : prompt.kind === "unreadable"
                  ? "저장된 렌즈 보정값을 불러오지 못해 보정 없이 받고 있습니다. 열화상 카메라가 데워진 뒤 렌즈 앞을 판으로 기울여 가리고 다시 보정하십시오. 나중에 하려면 열화상 칸의 렌즈 보정을 누릅니다."
                  : "열화상 렌즈 앞을 판으로 기울여 가렸습니까?"
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
            text: "판: 나무나 두꺼운 종이처럼 반사가 없고 열이 잘 퍼지지 않는 실온의 평평한 판을 씁니다.\n"
                  + "대는 방법: 판을 렌즈와 정면으로 마주 보지 않게 30도 이상 기울여 화면 전체를 덮습니다. "
                  + "렌즈가 보는 면에는 손이나 카메라가 닿지 않게 합니다.\n"
                  + "시점: 열화상 칸에 \"예열 완료\"가 보인 뒤에 보정합니다. 카메라를 USB에서 뽑아 두어 식었으면 꽂아 둔 카메라보다 오래 걸립니다.\n\n"
                  + "판을 정면으로 대면 데워진 렌즈가 판에 비치고, 가리지 않으면 그때 보이던 장면이 보정값에 섞여 "
                  + "영상이 오히려 고르지 않게 됩니다. 보정값은 저장되어 다음에 앱을 켤 때 그대로 적용됩니다. "
                  + "보정하는 약 2초 동안 열화상이 멈춥니다."
            font.family: Theme.font
            font.pixelSize: 13
            lineHeight: 1.2
            color: Theme.textMuted
        }
        Label {
            Layout.topMargin: 12
            Layout.fillWidth: true
            visible: prompt.capture.thermal.warmup !== ""
            wrapMode: Text.Wrap
            text: "지금 열화상 카메라: " + prompt.capture.thermal.warmupText + ", " + prompt.capture.thermal.warmupDetail
            font.family: Theme.font
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: prompt.capture.thermal.warmup === "ready" ? Theme.text : Theme.settling
        }
        RowLayout {
            Layout.topMargin: 24
            Layout.alignment: Qt.AlignRight
            spacing: 10

            // 보정: 테두리만 있는 버튼
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
                    text: "기울여 가렸습니다, 보정"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.correctLensShading()
                }
            }
            // 보정하지 않음: 기본 선택이라 채운 버튼
            Rectangle {
                implicitWidth: cancelLabel.implicitWidth + 40
                implicitHeight: 40
                radius: Theme.radius
                color: Theme.controlActive
                Label {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: prompt.recommended ? "나중에" : "취소"
                    font.family: Theme.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.background
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: prompt.capture.cancelLensShadingPrompt()
                }
            }
        }
    }
}
