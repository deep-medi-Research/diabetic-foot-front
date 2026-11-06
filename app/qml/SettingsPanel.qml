import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HwLab

// 개발과 시험 기록에 쓰는 설정과 값입니다. 촬영 화면의 "설정"을 누르면 오른쪽에서 열립니다.
// 앱을 쓰는 사람은 이 값을 바꾸지 않습니다. 카메라 설정은 촬영 선언 파일과 기억한 값이 정합니다.
// 열린 채로 RGB 미리보기를 눌러 관찰 영역을 정할 수 있도록 화면을 막지 않습니다. 녹화 중에는 지금처럼 바꿀 수 없습니다.
Drawer {
    id: panel
    required property CaptureController capture
    required property AnalysisVariants analysis
    // RGB 패널이 고른 발의 상자 테두리입니다. 닫혀 있으면 두 상자 모두 실선입니다.
    readonly property var boxOutline: opened ? rgbPanel.boxOutline : ["solid", "solid"]

    // 화면에서 고친 값입니다. '적용'을 누르기 전에는 카메라에 가지 않습니다.
    property bool draftAuto: capture.autoMode
    property int draftExposure: capture.exposureStep
    property int draftGain: capture.gain
    property int draftWb: capture.whiteBalance
    readonly property bool dirty: draftAuto !== capture.autoMode || draftExposure !== capture.exposureStep
                                  || draftGain !== capture.gain || draftWb !== capture.whiteBalance

    edge: Qt.RightEdge
    width: Math.min(720, parent ? parent.width - 200 : 720)
    height: parent ? parent.height : 0
    modal: false
    interactive: false
    // Esc는 촬영 화면의 Shortcut이 관찰 영역 지우기와 나눠 맡습니다(Main.qml).
    closePolicy: Popup.NoAutoClose
    padding: 0
    background: Rectangle {
        color: Theme.background
        Rectangle { width: 1; height: parent.height; color: Theme.line }
        // 화면을 막지 않는 패널은 빈 자리의 누름을 아래 항목(촬영 버튼, 열화상 발 상자)으로 넘기므로 여기서 받습니다.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
    }

    component SectionTitle: Label {
        font.pixelSize: 13
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        color: Theme.textMuted
    }
    component Divider: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 20
        Layout.bottomMargin: 16
        height: 1
        color: Theme.line
    }

    FolderDialog {
        id: folderDialog
        title: "저장 위치"
        currentFolder: panel.capture.shot.rootUrl
        onAccepted: panel.capture.shot.setRootUrl(selectedFolder)
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 8
            Label {
                Layout.fillWidth: true
                text: "설정"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: Theme.text
            }
            // 닫기. 관찰 영역 지우기(RgbPanel)와 같은 모양이고, Esc로도 닫습니다.
            Rectangle {
                width: 28
                height: 28
                radius: 8
                color: closeMouse.containsMouse ? Theme.line : Theme.control
                Canvas {
                    anchors.centerIn: parent
                    width: 12
                    height: 12
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = Theme.textMuted
                        ctx.lineWidth = 1.6
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.moveTo(2, 2); ctx.lineTo(10, 10); ctx.moveTo(10, 2); ctx.lineTo(2, 10)
                        ctx.stroke()
                    }
                }
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.close()
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            text: "개발과 시험 기록에 쓰는 값입니다. 열려 있는 동안 영상 위에 장치 이름과 실제 fps, 발 상자 크기, 열화상 원시값 범위를 함께 보입니다."
            wrapMode: Text.Wrap
            font.pixelSize: 13
            color: Theme.textMuted
        }

        Flickable {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 12
            readonly property bool overflow: contentHeight > height
            contentHeight: columns.implicitHeight + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: scroll.overflow ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

            RowLayout {
                id: columns
                x: 20
                width: scroll.width - 40 - (scroll.overflow ? 12 : 0)
                spacing: 28

                // 왼쪽: 카메라, 렌즈 보정, 저장 위치
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 0

                    SectionTitle { text: "카메라에 적용된 값" }
                    // 주기적으로 읽지 않으므로 읽는 시점을 함께 적습니다. 자동 맞춤 중 펌웨어가 바꾸는 노출은 여기 보이지 않습니다.
                    Label {
                        Layout.topMargin: 4
                        Layout.fillWidth: true
                        text: "설정을 보낼 때마다 카메라에서 다시 읽은 값입니다. 보낸 값과 다르면 빨간색으로 표시합니다."
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                    ColumnLayout {
                        Layout.topMargin: 12
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: panel.capture.readback
                            delegate: ReadbackRow { Layout.fillWidth: true }
                        }
                    }

                    Divider {}
                    // 영상 위 개발용 값입니다. 열화상 칸은 이 패널에 가려 영상 위 값이 보이지 않을 수 있어 여기에도 적습니다.
                    SectionTitle { text: "장치" }
                    Label {
                        Layout.topMargin: 6
                        Layout.fillWidth: true
                        text: "RGB  " + panel.capture.deviceName + ", " + panel.capture.measuredFps.toFixed(1) + " fps"
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        font.features: { "tnum": 1 }
                        color: Theme.text
                    }
                    Label {
                        Layout.topMargin: 4
                        Layout.fillWidth: true
                        visible: panel.capture.thermal.available
                        text: "열화상  " + (panel.capture.thermal.deviceName !== "" ? panel.capture.thermal.deviceName + ", " : "")
                              + panel.capture.thermal.measuredFps.toFixed(1) + " fps, 원시값 " + panel.capture.thermal.rangeLow
                              + " ~ " + panel.capture.thermal.rangeHigh
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        font.features: { "tnum": 1 }
                        color: Theme.text
                    }

                    Divider {}
                    SectionTitle { text: "카메라 설정" }
                    Segmented {
                        Layout.topMargin: 12
                        Layout.fillWidth: true
                        enabled: panel.capture.canEdit
                        options: ["자동 맞춤", "직접 지정"]
                        currentIndex: panel.draftAuto ? 0 : 1
                        onPicked: index => panel.draftAuto = index === 0
                    }
                    ColumnLayout {
                        Layout.topMargin: 16
                        Layout.fillWidth: true
                        spacing: 12
                        enabled: panel.capture.canEdit
                        Stepper {
                            Layout.fillWidth: true
                            label: "노출 단계"
                            enabled: !panel.draftAuto
                            value: panel.draftExposure; from: -13; to: -1
                            changed: panel.draftExposure !== panel.capture.exposureStep
                            onEdited: v => panel.draftExposure = v
                        }
                        Stepper {
                            Layout.fillWidth: true
                            label: "게인"
                            value: panel.draftGain; from: 0; to: 255
                            changed: panel.draftGain !== panel.capture.gain
                            onEdited: v => panel.draftGain = v
                        }
                        Stepper {
                            Layout.fillWidth: true
                            label: "색온도"
                            value: panel.draftWb; from: 2800; to: 10000; step: 100; suffix: " K"
                            changed: panel.draftWb !== panel.capture.whiteBalance
                            onEdited: v => panel.draftWb = v
                        }
                    }
                    Rectangle {
                        Layout.topMargin: 14
                        Layout.fillWidth: true
                        height: 38
                        radius: Theme.radius
                        color: Theme.controlActive
                        visible: panel.dirty && panel.capture.canEdit
                        Label {
                            anchors.centerIn: parent
                            text: "적용"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.background
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.capture.applySettings(panel.draftAuto, panel.draftExposure, panel.draftGain,
                                                                   panel.draftWb)
                        }
                    }
                    // 적용한 설정을 다음 실행의 기본값으로 남깁니다. 줄 전체를 눌러 켜고 끕니다.
                    // 클릭 영역을 글자의 자식으로 두고 왼쪽으로 넓히면 네모를 눌렀을 때 반응하지 않아(2026-10-23 실기) 줄 위에 겹칩니다.
                    Item {
                        Layout.topMargin: 12
                        implicitWidth: rememberRow.implicitWidth
                        implicitHeight: rememberRow.implicitHeight
                        Row {
                            id: rememberRow
                            spacing: 8
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16; height: 16; radius: 4
                                color: "transparent"
                                border.width: 1.5
                                border.color: panel.capture.rememberSettings ? Theme.text : Theme.textMuted
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8; height: 8; radius: 2
                                    color: Theme.text
                                    visible: panel.capture.rememberSettings
                                }
                            }
                            Label {
                                text: "이 설정 기억 (다음 실행에도 사용)"
                                font.pixelSize: 13
                                color: panel.capture.rememberSettings ? Theme.text : Theme.textMuted
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.capture.rememberSettings = !panel.capture.rememberSettings
                        }
                    }
                    Label {
                        Layout.topMargin: 4
                        visible: panel.capture.startedWithRememberedSettings
                        text: "기억한 설정으로 시작했습니다(촬영 선언 파일 대신)"
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }

                    Divider {}
                    SectionTitle { text: "열화상 렌즈 보정" }
                    Label {
                        Layout.topMargin: 6
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: panel.capture.thermal.lensShadingText
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        color: Theme.text
                    }
                    // 렌즈를 가리고 보정하는 안내를 엽니다. 미리보기에서 열화상을 받는 동안만 누를 수 있습니다(보정하는 동안 열화상이 멈춤).
                    Rectangle {
                        readonly property bool usable: panel.capture.canEdit && panel.capture.thermal.state === "receiving"
                                                       && !panel.capture.thermal.lensShadingBusy
                        Layout.topMargin: 10
                        visible: panel.capture.thermal.lensShading !== "" || panel.capture.thermal.lensShadingBusy
                        implicitWidth: lensLabel.implicitWidth + 28
                        implicitHeight: 34
                        radius: Theme.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.line
                        opacity: usable ? 1 : 0.4
                        Label {
                            id: lensLabel
                            anchors.centerIn: parent
                            text: "렌즈 보정"
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: parent.usable
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.capture.askLensShading()
                        }
                    }

                    Divider {}
                    SectionTitle { text: "저장 위치" }
                    // 경로 자체를 누르면 폴더를 고릅니다.
                    Item {
                        Layout.topMargin: 6
                        Layout.fillWidth: true
                        implicitHeight: rootRow.implicitHeight
                        enabled: panel.capture.canEdit
                        RowLayout {
                            id: rootRow
                            anchors.fill: parent
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                text: panel.capture.shot.root
                                elide: Text.ElideMiddle
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                            Label {
                                text: "›"
                                font.pixelSize: 18
                                color: Theme.textMuted
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: folderDialog.open()
                        }
                    }
                    Label {
                        Layout.topMargin: 6
                        Layout.fillWidth: true
                        visible: panel.capture.shot.folderPreview !== ""
                        text: panel.capture.shot.folderPreview
                        wrapMode: Text.WrapAnywhere
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.topMargin: 6
                        Layout.fillWidth: true
                        text: panel.capture.savedVideosText
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.topMargin: 2
                        Layout.fillWidth: true
                        text: panel.capture.light ? panel.capture.light.summary : panel.capture.lightingText
                        wrapMode: Text.Wrap
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }

                // 오른쪽: 발 상자 RGB, 조명과 PPG 포트, 분석 판
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 0

                    // 두 발 상자와 관찰 영역의 채널별 평균, 포화 비율, 시간 그래프. 조명을 맞출 때 포화가 0%인 가장 밝은 값을 찾는 데
                    // 씁니다(포화 화소는 맥박에 따른 밝기 변화가 기록되지 않음). 관찰 영역은 열린 채로 RGB 미리보기를 눌러 정합니다.
                    RgbPanel {
                        id: rgbPanel
                        Layout.fillWidth: true
                        Layout.preferredHeight: 520
                        trace: panel.capture.rgb
                        leftBoxFoot: panel.capture.leftBoxFoot
                    }

                    // LED 조명. 포트를 연결하지 않으면 앱 밖에서 켠 조명으로 기록합니다.
                    Divider { visible: panel.capture.light !== null }
                    LightPanel {
                        Layout.fillWidth: true
                        enabled: panel.capture.canEdit
                        visible: panel.capture.light !== null
                        light: panel.capture.light
                        editable: panel.capture.canEdit
                    }
                    // PPG 장치. 연결하지 않으면 PPG 없이 촬영합니다. 맥파는 촬영 화면에 크게 그립니다.
                    Divider { visible: panel.capture.ppg !== null }
                    Loader {
                        Layout.fillWidth: true
                        active: panel.capture.ppg !== null
                        visible: active
                        sourceComponent: PpgPanel { ppg: panel.capture.ppg; editable: panel.capture.canEdit; showWave: false }
                    }

                    Divider {}
                    SectionTitle { text: "관류 분석" }
                    Repeater {
                        model: panel.analysis !== null ? panel.analysis.lanes : []
                        delegate: ColumnLayout {
                            id: lane
                            required property AnalysisController modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.topMargin: index === 0 ? 10 : 12
                            spacing: 6
                            // 판 고르기. 줄을 눌러 켜고 끕니다. 마지막으로 켜 둔 판은 끄지 않습니다. 다음 촬영부터 적용합니다.
                            Item {
                                Layout.fillWidth: true
                                implicitHeight: variantRow.implicitHeight
                                visible: panel.analysis !== null && panel.analysis.lanes.length > 1
                                RowLayout {
                                    id: variantRow
                                    width: parent.width
                                    spacing: 8
                                    // 켜짐은 칸 안을 채웁니다. 글꼴에 없는 기호를 쓰지 않으려고 도형으로 그립니다.
                                    Rectangle {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 2
                                        width: 14; height: 14; radius: 3
                                        color: "transparent"
                                        border.width: 1.5
                                        border.color: lane.modelData.enabled ? Theme.text : Theme.textMuted
                                        Rectangle {
                                            anchors.centerIn: parent
                                            visible: lane.modelData.enabled
                                            width: 8; height: 8; radius: 1.5
                                            color: Theme.text
                                        }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label {
                                            Layout.fillWidth: true
                                            text: lane.modelData.variantLabel + "  " + lane.modelData.variantSummary
                                            elide: Text.ElideRight
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            color: lane.modelData.enabled ? Theme.text : Theme.textMuted
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: lane.modelData.variantSource
                                            wrapMode: Text.Wrap
                                            font.pixelSize: 13
                                            color: Theme.textMuted
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: panel.analysis.setLaneEnabled(lane.modelData.variantId, !lane.modelData.enabled)
                                }
                            }
                            Label {
                                visible: lane.modelData.enabled && lane.modelData.canRetry
                                text: "다시 분석"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                font.underline: retryArea.containsMouse
                                color: Theme.text
                                MouseArea {
                                    id: retryArea
                                    anchors.fill: parent
                                    anchors.margins: -6
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: lane.modelData.retry()
                                }
                            }
                        }
                    }
                    // 녹화 중 미리 분석 스위치. 줄 전체를 눌러 켜고 끕니다. 다음 녹화부터 적용합니다.
                    Item {
                        Layout.topMargin: 14
                        visible: panel.analysis !== null && panel.analysis.prestreamAvailable
                        implicitWidth: prestreamRow.implicitWidth
                        implicitHeight: prestreamRow.implicitHeight
                        Row {
                            id: prestreamRow
                            spacing: 8
                            // 켜짐은 손잡이가 오른쪽에 있고 밝은 색입니다.
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 30; height: 16; radius: 8
                                color: panel.analysis && panel.analysis.prestream ? Theme.text : "transparent"
                                border.width: 1.5
                                border.color: panel.analysis && panel.analysis.prestream ? Theme.text : Theme.textMuted
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: panel.analysis && panel.analysis.prestream ? parent.width - width - 3 : 3
                                    width: 10; height: 10; radius: 5
                                    color: panel.analysis && panel.analysis.prestream ? Theme.background : Theme.textMuted
                                }
                            }
                            Label {
                                text: "녹화 중 미리 분석 " + (panel.analysis && panel.analysis.prestream ? "켬" : "끔")
                                font.pixelSize: 13
                                color: panel.analysis && panel.analysis.prestream ? Theme.text : Theme.textMuted
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: panel.analysis.prestream = !panel.analysis.prestream
                        }
                    }
                    Label {
                        Layout.topMargin: 4
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: panel.analysis !== null && panel.analysis.prestreamAvailable
                        text: panel.analysis && panel.analysis.prestream
                              ? "녹화하는 동안 분석을 미리 해 결과가 약 15초 빨리 나옵니다. 녹화 누락이 생기면 끄세요."
                              : "녹화가 끝난 뒤 영상을 다시 읽어 분석합니다."
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }
            }
        }
    }
}
