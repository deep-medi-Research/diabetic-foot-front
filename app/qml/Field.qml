import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 라벨, 입력 칸, 문제 문구를 함께 표시합니다. 값은 바깥에서 받고 편집한 글자는 edited 신호로 전달합니다.
ColumnLayout {
    id: field
    required property string label
    required property string value
    required property string problem  // 빈 문자열이면 문제 없음
    // 문제 문구를 칸 밖에서 한 번만 보일 때(칸 여럿이 문구 하나를 함께 씀) 밑줄만 빨갛게 하는 데 씁니다.
    property bool marked: problem !== ""
    property string mask: ""
    signal edited(string text)

    spacing: 2
    opacity: enabled ? 1 : 0.4

    // 칸 이름을 위의 머리글 줄이 대신할 때(혈압 칸) 빈 문자열을 주면 이름 줄을 두지 않습니다.
    Label {
        visible: field.label !== ""
        text: field.label
        font.pixelSize: 13
        color: Theme.textMuted
    }
    TextField {
        id: input
        Layout.fillWidth: true
        text: field.value
        inputMask: field.mask
        color: Theme.text
        selectionColor: Theme.line
        selectedTextColor: Theme.text
        font.pixelSize: 17
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 }
        leftPadding: 0
        rightPadding: 0
        topPadding: 4
        bottomPadding: 6
        // 상자 대신 밑줄 하나만 둡니다. 밑줄 색은 입력 상태(잘못됨, 입력 중)만 나타냅니다.
        background: Item {
            implicitHeight: 34
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: input.activeFocus || field.marked ? 2 : 1
                color: field.marked ? Theme.mismatch : input.activeFocus ? Theme.text : Theme.line
            }
        }
        onTextEdited: field.edited(text)
    }
    Label {
        Layout.fillWidth: true
        visible: field.problem !== ""
        text: field.problem
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.mismatch
    }
}
