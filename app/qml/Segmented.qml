import QtQuick
import HwLab

// 여러 칸 중 하나를 고르는 버튼입니다.
Rectangle {
    id: control
    required property var options
    required property int currentIndex
    // RGB 패널처럼 좁은 자리는 글자와 모서리를 줄여 씁니다.
    property int fontSize: 14
    signal picked(int index)

    implicitHeight: 38
    radius: height >= 30 ? Theme.radius : 7
    color: Theme.control
    opacity: enabled ? 1 : 0.4

    Row {
        anchors.fill: parent
        anchors.margins: 3
        Repeater {
            model: control.options
            delegate: Rectangle {
                width: (control.width - 6) / control.options.length
                height: control.height - 6
                radius: control.radius - 3
                color: index === control.currentIndex ? Theme.controlActive : "transparent"
                Behavior on color { ColorAnimation { duration: 120 } }
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    font.family: Theme.font
                    font.pixelSize: control.fontSize
                    font.weight: Font.DemiBold
                    color: index === control.currentIndex ? Theme.background : Theme.textMuted
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: control.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.picked(index)
                }
            }
        }
    }
}
