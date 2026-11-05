pragma Singleton
import QtQuick

// 색은 상태와 데이터에만 씁니다. 버튼과 글자는 무채색입니다.
QtObject {
    readonly property color background: "#0b0c0e"
    readonly property color previewBackground: "#000000"
    readonly property color text: "#f2f3f5"
    readonly property color textMuted: "#8b9098"
    readonly property color line: "#24262b"
    readonly property color control: "#1a1c20"
    readonly property color controlActive: "#f2f3f5"
    readonly property color recording: "#ff453a"
    readonly property color settling: "#ffb020"
    readonly property color mismatch: "#ff453a"
    // RGB 패널 그래프의 채널 선 색입니다. 글자에는 쓰지 않습니다.
    readonly property color channelR: "#f0705f"
    readonly property color channelG: "#4cc38a"
    readonly property color channelB: "#6aa3ff"
    // 미리보기 위에 겹쳐 띄우는 패널 바탕입니다(background 0.88 불투명).
    readonly property color overlayPanel: "#e00b0c0e"
    readonly property string font: "Pretendard"
    readonly property int radius: 10
}
