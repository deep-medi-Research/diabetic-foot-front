import QtQuick
import QtQuick.Controls
import HwLab

// 미리보기 영상 위의 좌우 발 영역 두 개입니다. 상자 안을 끌면 옮기고, 모서리를 끌면 크기를 바꾸고,
// 상자 위쪽의 발 이름을 누르면 두 상자의 발이 서로 바뀝니다. 좌표 계산과 격자 맞춤은 FootRegions가 합니다.
// 상자 안을 끌지 않고 누르면 상자를 옮기지 않고 boxClicked를 냅니다(RGB 미리보기의 관찰 영역 정하기).
Item {
    id: overlay
    required property FootRegions regions
    // 영역을 그릴 영상. 영상이 실제로 그려진 사각형(painted) 위에 상자를 둡니다
    required property Image picture
    required property string leftFoot
    required property string rightFoot
    property bool editable: true
    // 영상을 좌우로 뒤집어 그릴 때 true입니다. 영역 값(FootRegions)은 녹화 영상 좌표 그대로이고 화면 좌표만 뒤집습니다.
    property bool mirror: false
    // 상자 테두리 모양입니다. 화소 왼쪽 상자, 화소 오른쪽 상자 순서이고 "solid"(2화소 실선), "dashed"(2화소 점선),
    // "faint"(1화소 흐린 점선) 중 하나입니다. RGB 패널이 고른 발을 강조할 때 씁니다.
    property var outline: ["solid", "solid"]
    // true이면 발 이름 아래에 프레임 화소 크기(폭 × 높이)를 보입니다. 개발용 값이라 설정 패널이 열린 동안만 켭니다.
    property bool showSizes: true
    // true이면 Shift를 누른 채 상자 안을 누른 것을 받지 않고 아래 항목(RgbAreaPicker)으로 넘깁니다.
    property bool passShift: false
    signal footClicked()
    // 상자 안을 끌지 않고 눌렀습니다. 프레임 화소 좌표입니다.
    signal boxClicked(real frameX, real frameY)

    // 화면 x(이 항목 좌표)와 프레임 x 사이의 변환입니다.
    function frameX(screenX) { return (mirror ? width - screenX : screenX) / ratio }

    readonly property real ratio: regions.frameWidth > 0 ? picture.paintedWidth / regions.frameWidth : 0

    x: picture.x + (picture.width - picture.paintedWidth) / 2
    y: picture.y + (picture.height - picture.paintedHeight) / 2
    width: picture.paintedWidth
    height: picture.paintedHeight
    visible: regions.frameWidth > 0 && picture.paintedWidth > 0

    Repeater {
        model: 2
        delegate: Item {
            id: box
            required property int index
            readonly property rect area: index === 0 ? overlay.regions.left : overlay.regions.right
            readonly property bool ok: index === 0 ? overlay.regions.leftOk : overlay.regions.rightOk
            readonly property color tone: ok ? Theme.text : Theme.mismatch
            readonly property string look: overlay.outline[index] || "solid"

            x: overlay.mirror ? overlay.width - (area.x + area.width) * overlay.ratio : area.x * overlay.ratio
            y: area.y * overlay.ratio
            width: area.width * overlay.ratio
            height: area.height * overlay.ratio

            Rectangle {
                anchors.fill: parent
                visible: box.look === "solid"
                color: "transparent"
                border.width: 2
                border.color: box.tone
            }
            // 점선 테두리. Rectangle은 점선을 그리지 못해 Canvas로 그립니다.
            Canvas {
                id: dashedOutline
                anchors.fill: parent
                visible: box.look !== "solid"
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onVisibleChanged: requestPaint()
                Connections {
                    target: box
                    function onLookChanged() { dashedOutline.requestPaint() }
                    function onToneChanged() { dashedOutline.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const faint = box.look === "faint"
                    const w = faint ? 1 : 2
                    ctx.strokeStyle = faint && box.ok ? Theme.textMuted : box.tone
                    ctx.lineWidth = w
                    ctx.setLineDash(faint ? [4, 4] : [3, 2])
                    ctx.strokeRect(w / 2, w / 2, width - w, height - w)
                }
            }

            // 상자 안을 끌면 크기 그대로 옮깁니다. 누른 위치와 누른 시점의 영역을 기준으로 이동량을 셉니다.
            MouseArea {
                anchors.fill: parent
                enabled: overlay.editable
                cursorShape: Qt.SizeAllCursor
                property point start
                property rect startArea
                property bool moved: false
                onPressed: mouse => {
                    if (overlay.passShift && (mouse.modifiers & Qt.ShiftModifier)) {
                        mouse.accepted = false
                        return
                    }
                    start = mapToItem(overlay, mouse.x, mouse.y)
                    startArea = box.area
                    moved = false
                }
                onPositionChanged: mouse => {
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    // 손 떨림으로 상자가 움직이지 않게 4화소 넘게 움직여야 옮기기 시작합니다. 그 전에 놓으면 누르기입니다.
                    if (!moved && Math.hypot(p.x - start.x, p.y - start.y) < 4) return
                    moved = true
                    const dx = (p.x - start.x) / overlay.ratio
                    overlay.regions.moveBox(box.index,
                                            Math.round(startArea.x + (overlay.mirror ? -dx : dx)),
                                            Math.round(startArea.y + (p.y - start.y) / overlay.ratio))
                }
                onReleased: {
                    if (moved) {
                        overlay.regions.save()
                        return
                    }
                    overlay.boxClicked(overlay.frameX(start.x), start.y / overlay.ratio)
                }
            }

            // 네 모서리: 0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래
            Repeater {
                model: 4
                delegate: Rectangle {
                    id: handle
                    required property int index
                    width: 12
                    height: 12
                    x: (index % 2 === 1 ? box.width : 0) - width / 2
                    y: (index >= 2 ? box.height : 0) - height / 2
                    color: box.tone
                    visible: overlay.editable
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        cursorShape: handle.index === 0 || handle.index === 3 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                        onPositionChanged: mouse => {
                            const p = mapToItem(overlay, mouse.x, mouse.y)
                            // 뒤집어 그리면 화면 왼쪽 모서리가 프레임의 오른쪽 모서리입니다(0과 1, 2와 3이 바뀜).
                            const corner = overlay.mirror ? handle.index ^ 1 : handle.index
                            overlay.regions.resizeBox(box.index, corner,
                                                      Math.round(overlay.frameX(p.x)), Math.round(p.y / overlay.ratio))
                        }
                        onReleased: overlay.regions.save()
                    }
                }
            }

            // 위: 환자의 어느 발인지. 누르면 두 상자의 발이 바뀝니다. 미리보기 위쪽의 장치 이름 줄 아래에 둡니다.
            Rectangle {
                id: footPill
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 50 }
                width: footRow.width + 20
                height: 26
                radius: 13
                color: "#cc000000"
                Row {
                    id: footRow
                    anchors.centerIn: parent
                    spacing: 6
                    Label {
                        text: box.index === 0 ? overlay.leftFoot : overlay.rightFoot
                        font.family: Theme.font
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: box.tone
                    }
                    // 바꿀 수 있음을 보이는 좌우 화살표. 글꼴에 화살표 글리프가 없는 PC가 있어 직접 그립니다.
                    Canvas {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 10
                        visible: overlay.editable
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = Theme.textMuted
                            ctx.lineWidth = 1.4
                            ctx.beginPath()
                            ctx.moveTo(1, 3); ctx.lineTo(11, 3); ctx.moveTo(8, 0.5); ctx.lineTo(11, 3)
                            ctx.moveTo(11, 7); ctx.lineTo(1, 7); ctx.moveTo(4, 9.5); ctx.lineTo(1, 7)
                            ctx.stroke()
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: overlay.editable
                    cursorShape: Qt.PointingHandCursor
                    onClicked: overlay.footClicked()
                }
            }

            // 발 이름 아래: 프레임 화소 크기(폭 × 높이)
            Rectangle {
                anchors { horizontalCenter: parent.horizontalCenter; top: footPill.bottom; topMargin: 6 }
                visible: overlay.showSizes
                width: sizeLabel.implicitWidth + 16
                height: 20
                radius: 10
                color: "#cc000000"
                Label {
                    id: sizeLabel
                    anchors.centerIn: parent
                    text: box.area.width + " × " + box.area.height
                    font.family: Theme.font
                    font.pixelSize: 13
                    font.features: { "tnum": 1 }
                    color: box.ok ? Theme.textMuted : Theme.mismatch
                }
            }
        }
    }
}
