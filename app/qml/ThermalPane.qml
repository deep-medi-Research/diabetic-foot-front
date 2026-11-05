import QtQuick
import QtQuick.Controls
import HwLab

// 열화상 미리보기와 열화상 스트림 상태입니다. 영상을 누르면 색상표가 아이언, 흑백, 레인보우, 고대비 순서로 바뀝니다.
Rectangle {
    id: pane
    required property ThermalView thermal
    // 이번 녹화가 진행 중이거나 끝난 뒤인지. 기록 장 수를 보일지 정합니다
    required property bool showRecording
    // 열화상 좌우 발 영역과 각 상자의 발 이름. RGB 영역과 따로 정합니다(보정값이 없어 옮겨 쓰지 않음)
    required property FootRegions regions
    required property string leftFoot
    required property string rightFoot
    property bool editable: true
    // 영상을 좌우로 뒤집어 그릴지(Main.qml mirrorPreview). 녹화 원시값은 뒤집지 않습니다.
    property bool mirror: false
    signal footClicked()
    // 렌즈 보정 버튼. 렌즈를 가렸는지 묻는 안내를 엽니다
    signal lensShadingClicked()

    readonly property string connState: thermal.state
    readonly property color stateColor: connState === "receiving" ? Theme.text
                                      : connState === "searching" ? Theme.settling
                                      : connState === "lost" ? Theme.mismatch : Theme.textMuted

    radius: Theme.radius
    color: Theme.previewBackground
    clip: true

    Image {
        id: image
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: false
        mirror: pane.mirror
        visible: pane.thermal.available && pane.thermal.previewRevision > 0
        // 수신 중이 아니면 마지막 영상을 흐리게 두어 지금 영상이 아님을 보입니다.
        opacity: pane.connState === "receiving" ? 1 : 0.35
        source: pane.thermal.previewRevision > 0 ? "image://thermal/" + pane.thermal.previewRevision : ""
        MouseArea {
            // 영상 영역만 누르게 합니다. 여백을 눌러 색상표가 바뀌면 의도하지 않은 전환이 됩니다.
            x: (image.width - image.paintedWidth) / 2
            y: (image.height - image.paintedHeight) / 2
            width: image.paintedWidth
            height: image.paintedHeight
            cursorShape: Qt.PointingHandCursor
            onClicked: pane.thermal.nextPalette()
        }
    }

    // 좌우 발 영역. 상자 밖을 누르면 위의 색상표 전환이 동작합니다.
    FootBoxes {
        regions: pane.regions
        picture: image
        leftFoot: pane.leftFoot
        rightFoot: pane.rightFoot
        editable: pane.editable
        mirror: pane.mirror
        visible: image.visible && pane.regions.frameWidth > 0 && image.paintedWidth > 0
        onFootClicked: pane.footClicked()
    }

    // 영상이 없을 때 가운데에 상태를 보입니다.
    Label {
        anchors.centerIn: parent
        visible: !image.visible
        text: pane.thermal.stateText
        font.family: Theme.font
        font.pixelSize: 15
        font.weight: Font.DemiBold
        color: pane.connState === "absent" ? Theme.textMuted : pane.stateColor
    }

    // 위: 장치와 실제 fps. 글자 뒤에 반투명 바탕을 두어 밝은 영상 위에서도 읽히게 합니다.
    Rectangle {
        anchors { fill: topInfo; margins: -8 }
        visible: topInfo.visible
        radius: 8
        color: "#b3000000"
    }
    Row {
        id: topInfo
        anchors { left: parent.left; top: parent.top; margins: 16 }
        spacing: 12
        visible: pane.thermal.available
        Label {
            text: pane.thermal.deviceName !== "" ? pane.thermal.deviceName : "열화상"
            font.family: Theme.font
            font.pixelSize: 13
            color: Theme.textMuted
        }
        Label {
            // 최근 31장의 수신 간격 중앙값으로 잰 fps입니다. 받지 않는 동안은 0입니다.
            text: pane.thermal.measuredFps.toFixed(1) + " fps"
            font.family: Theme.font
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
            color: Theme.text
        }
        // 렌즈 셰이딩 보정 상태입니다. 보정 없이 받는 영상(없음, 실패)은 눈에 띄는 색으로 보입니다.
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: pane.thermal.lensShadingText
            visible: text !== ""
            font.family: Theme.font
            font.pixelSize: 13
            color: pane.thermal.lensShading === "none" || pane.thermal.lensShading === "failed" ? Theme.settling
                                                                                                : Theme.textMuted
        }
        // 렌즈 보정 버튼. 미리보기에서 열화상을 받는 동안만 누를 수 있습니다(보정하는 동안 열화상이 멈춤).
        Rectangle {
            id: lensButton
            readonly property bool usable: pane.editable && pane.connState === "receiving" && !pane.thermal.lensShadingBusy
            anchors.verticalCenter: parent.verticalCenter
            visible: pane.thermal.lensShading !== "" || pane.thermal.lensShadingBusy
            implicitWidth: lensButtonLabel.implicitWidth + 16
            implicitHeight: 22
            radius: 6
            color: "transparent"
            border.width: 1
            border.color: Theme.line
            opacity: usable ? 1 : 0.4
            Label {
                id: lensButtonLabel
                anchors.centerIn: parent
                text: "렌즈 보정"
                font.family: Theme.font
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Theme.text
            }
            MouseArea {
                anchors.fill: parent
                enabled: lensButton.usable
                cursorShape: Qt.PointingHandCursor
                onClicked: pane.lensShadingClicked()
            }
        }
    }

    // 위 오른쪽: 연결 상태
    Rectangle {
        anchors { right: parent.right; top: parent.top; margins: 14 }
        visible: pane.thermal.available
        height: 30
        width: stateRow.width + 28
        radius: 15
        color: "#cc000000"
        Row {
            id: stateRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: pane.stateColor
            }
            Label {
                text: pane.thermal.stateText
                font.family: Theme.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
            }
        }
    }

    // 아래 왼쪽: 녹화 중 기록 장 수와 기록 주기
    Rectangle {
        anchors { fill: recordInfo; margins: -8 }
        visible: recordInfo.visible
        radius: 8
        color: "#b3000000"
    }
    Column {
        id: recordInfo
        anchors { left: parent.left; bottom: parent.bottom; margins: 18 }
        spacing: 2
        visible: pane.thermal.available && (pane.showRecording || pane.thermal.written > 0)
        Label {
            text: pane.thermal.written + "장 기록"
            font.family: Theme.font
            font.pixelSize: 22
            font.weight: Font.Bold
            font.features: { "tnum": 1 }
            color: Theme.text
        }
        Label {
            text: pane.thermal.recordHz > 0
                  ? "기록 주기 " + pane.thermal.recordPeriodMs.toFixed(1) + " ms (" + pane.thermal.recordHz.toFixed(1) + " Hz)"
                  : "받은 프레임 모두 기록"
            font.family: Theme.font
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            color: Theme.textMuted
        }
        Label {
            text: "열화상 누락 " + pane.thermal.missing + "   저장 누락 " + pane.thermal.dropped
            font.family: Theme.font
            font.pixelSize: 13
            font.features: { "tnum": 1 }
            color: pane.thermal.missing + pane.thermal.dropped > 0 ? Theme.mismatch : Theme.textMuted
        }
    }

    // 아래 오른쪽: 색상표와 표시 범위(원시값, 아래위 0.1% 제외)
    Rectangle {
        anchors { fill: paletteInfo; margins: -8 }
        visible: paletteInfo.visible
        radius: 8
        color: "#b3000000"
    }
    Column {
        id: paletteInfo
        anchors { right: parent.right; bottom: parent.bottom; margins: 18 }
        spacing: 2
        visible: image.visible
        Label {
            anchors.right: parent.right
            text: pane.thermal.paletteName
            font.family: Theme.font
            font.pixelSize: 13
            font.weight: Font.DemiBold
            color: Theme.text
        }
        Label {
            anchors.right: parent.right
            text: pane.thermal.rangeLow + " ~ " + pane.thermal.rangeHigh
            font.family: Theme.font
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            color: Theme.textMuted
        }
    }

    // 위: 열화상 오류(장치 열기, 파일 쓰기). RGB 녹화는 이 오류로 멈추지 않습니다
    Label {
        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 52; leftMargin: 16; rightMargin: 16 }
        visible: pane.thermal.available && pane.thermal.error !== "" && pane.connState !== "receiving"
        text: pane.thermal.error
        wrapMode: Text.Wrap
        font.family: Theme.font
        font.pixelSize: 12
        color: Theme.textMuted
    }
}
