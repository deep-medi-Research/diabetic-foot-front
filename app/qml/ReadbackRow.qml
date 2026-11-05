import QtQuick
import QtQuick.Layouts
import HwLab

// 카메라에서 다시 읽은 값 한 줄입니다. 목표와 다르면 빨간색과 목표 값을 함께 보여줍니다.
RowLayout {
    id: row
    // 목록 모델(ReadbackModel)의 역할 이름과 같아 Repeater가 행마다 채웁니다.
    required property string name
    required property string value
    required property bool mismatch
    required property string target

    spacing: 8
    Text {
        Layout.fillWidth: true
        text: row.name
        font.family: Theme.font
        font.pixelSize: 14
        color: Theme.textMuted
    }
    Text {
        visible: row.mismatch && row.target !== ""
        text: "목표 " + row.target
        font.family: Theme.font
        font.pixelSize: 12
        color: Theme.textMuted
    }
    Text {
        text: row.value
        font.family: Theme.font
        font.pixelSize: 15
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 }
        color: row.mismatch ? Theme.mismatch : Theme.text
    }
}
