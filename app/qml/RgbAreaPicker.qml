import QtQuick
import HwLab

// RGB 미리보기 위의 관찰 영역입니다. 끌면 사각형을 그리고, 끌지 않고 누르면 그 자리를 가운데로 40 x 40 화소 영역을 만듭니다.
// 새로 그리면 앞 영역을 바꿉니다. 녹화 중에도 그릴 수 있습니다(화면 표시에만 씁니다).
//
// 발 상자와 겹치는 자리의 규칙: 이 항목은 FootBoxes 아래에 둡니다. 상자를 옮길 수 있을 때(미리보기) 상자 안이나 모서리
// 손잡이를 끌면 상자를 옮기고, Shift를 누른 채 끌면 FootBoxes가 누름을 넘겨 상자 안에서도 관찰 영역을 그립니다.
// 상자 안을 끌지 않고 누르면 FootBoxes가 boxClicked로 알리고, Main이 pickAt으로 넘깁니다.
// 상자를 옮길 수 없을 때(안정화, 녹화 중)는 FootBoxes가 누름을 받지 않아 어디서든 그립니다.
Item {
    id: picker
    required property RgbTrace trace
    required property FootRegions regions
    // 영역을 그릴 영상. FootBoxes와 같이 영상이 실제로 그려진 사각형 위에 둡니다
    required property Image picture
    // 영상을 좌우로 뒤집어 그릴 때 true입니다. 관찰 영역 값은 녹화 영상 좌표 그대로이고 화면 좌표만 뒤집습니다.
    property bool mirror: false

    // 누르기만 했을 때 만드는 영역의 한 변(프레임 화소)입니다.
    readonly property int clickSide: 40
    // 이 거리(화면 화소) 이상 움직여야 끌기로 봅니다.
    readonly property real dragThreshold: 4

    readonly property real ratio: regions.frameWidth > 0 ? picture.paintedWidth / regions.frameWidth : 0

    x: picture.x + (picture.width - picture.paintedWidth) / 2
    y: picture.y + (picture.height - picture.paintedHeight) / 2
    width: picture.paintedWidth
    height: picture.paintedHeight
    visible: regions.frameWidth > 0 && picture.paintedWidth > 0

    // 화면 좌표(이 항목 기준)와 프레임 화소 사이의 변환입니다.
    function frameX(screenX) { return (mirror ? width - screenX : screenX) / ratio }
    function frameY(screenY) { return screenY / ratio }
    function screenRect(r) {
        return Qt.rect(mirror ? width - (r.x + r.width) * ratio : r.x * ratio, r.y * ratio, r.width * ratio, r.height * ratio)
    }

    // 프레임 화소 (fx, fy)를 가운데로 하는 clickSide 크기 영역을 만듭니다. 프레임 가장자리에서는 안으로 밀어 크기를 지킵니다.
    function pickAt(fx, fy) {
        const side = Math.min(clickSide, regions.frameWidth, regions.frameHeight)
        const x = Math.max(0, Math.min(regions.frameWidth - side, Math.round(fx - side / 2)))
        const y = Math.max(0, Math.min(regions.frameHeight - side, Math.round(fy - side / 2)))
        trace.setArea(x, y, side, side)
    }

    // 정해진 관찰 영역: 흰 2화소 선 바깥에 검은 1화소 선을 둘러 밝은 발 위에서도 보이게 합니다.
    Item {
        id: shown
        readonly property rect area: picker.screenRect(picker.trace.area)
        visible: picker.trace.hasArea && !drawing.active
        x: area.x
        y: area.y
        width: area.width
        height: area.height
        Rectangle { anchors.fill: parent; anchors.margins: -1; color: "transparent"; border.width: 1; border.color: "black" }
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: 2; border.color: "white" }
    }

    // 끄는 중인 사각형
    Item {
        id: drawing
        property bool active: false
        property point from
        property point to
        visible: active
        x: Math.min(from.x, to.x)
        y: Math.min(from.y, to.y)
        width: Math.abs(to.x - from.x)
        height: Math.abs(to.y - from.y)
        Rectangle { anchors.fill: parent; anchors.margins: -1; color: "transparent"; border.width: 1; border.color: "black" }
        Rectangle { anchors.fill: parent; color: "transparent"; border.width: 2; border.color: "white" }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        property point pressAt
        property bool dragged: false
        onPressed: mouse => {
            pressAt = Qt.point(mouse.x, mouse.y)
            dragged = false
        }
        onPositionChanged: mouse => {
            const x = Math.max(0, Math.min(picker.width, mouse.x))
            const y = Math.max(0, Math.min(picker.height, mouse.y))
            if (!dragged && Math.hypot(x - pressAt.x, y - pressAt.y) < picker.dragThreshold) return
            dragged = true
            drawing.from = pressAt
            drawing.to = Qt.point(x, y)
            drawing.active = true
        }
        onReleased: mouse => {
            drawing.active = false
            if (!dragged) {
                picker.pickAt(picker.frameX(pressAt.x), picker.frameY(pressAt.y))
                return
            }
            const x = Math.max(0, Math.min(picker.width, mouse.x))
            const y = Math.max(0, Math.min(picker.height, mouse.y))
            const fx0 = picker.frameX(pressAt.x), fx1 = picker.frameX(x)
            const fy0 = picker.frameY(pressAt.y), fy1 = picker.frameY(y)
            const left = Math.round(Math.min(fx0, fx1)), top = Math.round(Math.min(fy0, fy1))
            const w = Math.round(Math.max(fx0, fx1)) - left, h = Math.round(Math.max(fy0, fy1)) - top
            if (w >= 1 && h >= 1) picker.trace.setArea(left, top, w, h)
        }
        onCanceled: drawing.active = false
    }
}
