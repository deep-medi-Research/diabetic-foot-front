import QtQuick
import QtQuick.Layouts
import HwLab

// 값 하나를 -와 +로 조절합니다. 바꾼 값은 '적용'을 눌러야 카메라에 갑니다.
RowLayout {
    id: control
    required property string label
    required property int value
    required property int from
    required property int to
    required property bool changed  // 적용 전 값이 카메라 값과 다름
    property int step: 1
    property string suffix: ""
    signal edited(int value)

    spacing: 10
    opacity: enabled ? 1 : 0.4

    Text {
        Layout.fillWidth: true
        text: control.label
        font.family: Theme.font
        font.pixelSize: 14
        color: Theme.textMuted
    }
    component StepButton: Rectangle {
        property string glyph
        property int delta
        width: 32; height: 32; radius: 16
        color: area.pressed ? Theme.line : Theme.control
        Text { anchors.centerIn: parent; text: parent.glyph; color: Theme.text; font.family: Theme.font; font.pixelSize: 18 }
        MouseArea {
            id: area
            anchors.fill: parent
            enabled: control.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: control.edited(Math.max(control.from, Math.min(control.to, control.value + parent.delta)))
        }
    }
    StepButton { glyph: "−"; delta: -control.step }
    Text {
        Layout.preferredWidth: 78
        horizontalAlignment: Text.AlignHCenter
        text: control.value + control.suffix
        font.family: Theme.font
        font.pixelSize: 16
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 }
        color: control.changed ? Theme.settling : Theme.text
    }
    StepButton { glyph: "+"; delta: control.step }
}
