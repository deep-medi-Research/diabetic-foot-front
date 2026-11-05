import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import HwLab

// 화면은 Capture 상태를 그리고 버튼을 넘기기만 합니다. 촬영 판단은 core와 platform이 합니다.
ApplicationWindow {
    id: root
    // 최대화로 엽니다. 크기는 최대화를 풀었을 때의 크기입니다.
    width: 1280
    height: 760
    visibility: Window.Maximized

    // 녹화 장 수와 안정화 남은 프레임은 0.2초마다 바뀝니다. 숫자, 막대, 촬영 버튼 고리가 6장씩 끊겨 움직이지 않게 따라가는 값을 씁니다.
    SmoothValue { id: shownWritten; goal: capture.written }
    SmoothValue { id: shownSettle; goal: capture.settleRemaining; rising: false }
    // 릴리즈 빌드는 버전(vX.Y.Z)을 붙입니다. 현장에서 어느 버전인지 창 제목으로 확인합니다.
    title: Qt.application.version ? "당뇨발 촬영 " + Qt.application.version : "당뇨발 촬영"
    color: Theme.background
    font.family: Theme.font

    // ViewModel입니다. main이 넘기며, 이 화면의 모든 상태는 여기서 옵니다.
    required property CaptureController capture
    // 관류 분석 상태입니다. 분석 판마다 하나씩 띄운 분석의 묶음이며 main이 넘깁니다.
    required property AnalysisVariants analysis
    // 켜 둔 판 중 하나라도 분석 중이거나 분석할 촬영이 대기 중이면 true입니다. 촬영 버튼을 막는 데 씁니다.
    readonly property bool analysisBlocksShot: analysis.blocksShot
    // 카메라가 발바닥을 아래에서 찍어 영상 왼쪽이 환자 오른발이면, 미리보기를 좌우로 뒤집어 환자 왼발이 화면 왼쪽에 오게 합니다.
    // 발 상자의 발 이름을 바꾸면(toggleFoot) 이 값도 따라 바뀌므로 화면은 늘 왼발이 왼쪽입니다.
    readonly property bool mirrorPreview: capture.leftBoxFoot === "오른발"

    // 화면에서 고친 값입니다. '적용'을 누르기 전에는 카메라에 가지 않습니다.
    property bool draftAuto: capture.autoMode
    property int draftExposure: capture.exposureStep
    property int draftGain: capture.gain
    property int draftWb: capture.whiteBalance
    readonly property bool dirty: draftAuto !== capture.autoMode || draftExposure !== capture.exposureStep
                                  || draftGain !== capture.gain || draftWb !== capture.whiteBalance
    readonly property string mode: capture.recording ? "recording" : capture.settling ? "settling" : "idle"
    readonly property bool fpsOff: capture.measuredFps > 0
                                   && Math.abs(capture.measuredFps - capture.targetFps) > capture.targetFps * 0.05

    // 촬영 버튼을 눌렀는데 열화상이 수신 중이 아니면 열립니다.
    ThermalPrompt { capture: root.capture }

    // 촬영 버튼을 눌렀는데 미리보기 열화상으로 본 발이 차가우면 열립니다.
    ColdFootPrompt { capture: root.capture }

    // 보정 파일이 없을 때(열화상을 처음 받을 때 한 번)와 렌즈 보정 버튼을 누를 때 열립니다.
    LensShadingPrompt { capture: root.capture }

    // 창을 닫으면 먼저 카메라를 실행 전 상태로 되돌리고, 맞지 않은 항목이 있으면 알린 뒤 닫습니다.
    // 녹화를 준비하거나 쓰는 중이면 먼저 묻습니다. 닫으면 녹화를 멈추고 지금까지 찍은 것을 저장한 뒤 닫습니다.
    onClosing: close => {
        close.accepted = capture.closeReady
        if (close.accepted || capture.shuttingDown) return
        if (capture.shotInProgress) closeConfirm.open()
        else capture.shutdown()
    }
    CloseConfirm { id: closeConfirm; capture: root.capture }
    RestoreNotice { capture: root.capture }

    // 한 부위의 수축기와 이완기 혈압 줄입니다. 칸 이름은 위의 머리글 줄(수축기, 이완기)이 대신하고,
    // 두 칸의 문제 문구는 줄 아래에 한 번만 보입니다.
    component PressureRow: ColumnLayout {
        id: pressureRow
        required property string site
        required property string systolic
        required property string diastolic
        required property string problem
        signal systolicEdited(string text)
        signal diastolicEdited(string text)
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                Layout.preferredWidth: 72
                text: pressureRow.site
                font.pixelSize: 14
                color: Theme.text
                opacity: enabled ? 1 : 0.4
            }
            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: ""
                mask: "000"
                value: pressureRow.systolic
                problem: ""
                marked: pressureRow.problem !== ""
                onEdited: text => pressureRow.systolicEdited(text)
            }
            Label { text: "/"; font.pixelSize: 16; color: Theme.textMuted }
            Field {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                label: ""
                mask: "000"
                value: pressureRow.diastolic
                problem: ""
                marked: pressureRow.problem !== ""
                onEdited: text => pressureRow.diastolicEdited(text)
            }
        }
        Label {
            Layout.fillWidth: true
            visible: pressureRow.problem !== ""
            text: pressureRow.problem
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.mismatch
        }
    }

    // 저장 위치를 누르면 열립니다.
    FolderDialog {
        id: folderDialog
        title: "저장 위치"
        currentFolder: capture.shot.rootUrl
        onAccepted: capture.shot.setRootUrl(selectedFolder)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 24

        // 왼쪽: 환자 정보, 저장 위치, 조명, PPG. 녹화 중에는 바꿀 수 없습니다. 창 높이가 모자라면 PPG 위쪽만 스크롤하고,
        // PPG는 열 아래에 고정해 창 모드에서도 실시간 맥파가 가려지지 않게 합니다.
        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.maximumWidth: 260
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0
        Flickable {
            id: leftScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property bool overflow: contentHeight > height
            contentHeight: leftColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: leftScroll.overflow ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        ColumnLayout {
            id: leftColumn
            // 스크롤 막대가 보이면 그 폭만큼 비웁니다.
            width: leftScroll.width - (leftScroll.overflow ? 12 : 0)
            spacing: 0
            enabled: capture.canEdit

            Label {
                text: "환자"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.letterSpacing: 1
                color: Theme.textMuted
            }
            Label {
                Layout.topMargin: 2
                text: "촬영 정보"
                font.pixelSize: 20
                font.weight: Font.Bold
                color: Theme.text
            }
            Field {
                Layout.topMargin: 18
                Layout.fillWidth: true
                label: "이름"
                value: capture.shot.name
                problem: capture.shot.nameError
                onEdited: text => capture.shot.setName(text)
            }
            Field {
                Layout.topMargin: 16
                Layout.fillWidth: true
                label: "생년월일  예: 1955-11-03"
                // 날짜 선택 대신 마스크를 씁니다. 날짜 선택은 비워 둘 수 없어 입력하지 않은 날짜가 그럴듯한 값으로 저장됩니다(기존 앱의 판단).
                mask: "0000-00-00"
                value: capture.shot.birth
                problem: capture.shot.birthError
                onEdited: text => capture.shot.setBirth(text)
            }
            // 저장 위치에 있는 최근 환자입니다. 누르면 이름과 생년월일을 함께 채웁니다. 지금 입력과 같은 환자는 밝게 보입니다.
            Label {
                Layout.topMargin: 14
                visible: capture.shot.recentPatients.length > 0
                text: "최근 환자"
                font.pixelSize: 12
                color: Theme.textMuted
            }
            Flow {
                Layout.topMargin: 6
                Layout.fillWidth: true
                visible: capture.shot.recentPatients.length > 0
                spacing: 6
                Repeater {
                    model: capture.shot.recentPatients
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool current: modelData.name === capture.shot.name.trim() && modelData.birth === capture.shot.birth
                        width: chipText.implicitWidth + 20
                        height: chipText.implicitHeight + 12
                        radius: height / 2
                        color: current ? Theme.controlActive : chipArea.containsMouse ? Theme.line : Theme.control
                        Column {
                            id: chipText
                            anchors.centerIn: parent
                            Label {
                                text: chip.modelData.name
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: chip.current ? Theme.background : Theme.text
                            }
                            Label {
                                text: chip.modelData.birth
                                font.pixelSize: 11
                                font.features: { "tnum": 1 }
                                color: chip.current ? Theme.background : Theme.textMuted
                            }
                        }
                        MouseArea {
                            id: chipArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: capture.shot.pickPatient(chip.modelData.name, chip.modelData.birth)
                        }
                        ToolTip.visible: chipArea.containsMouse
                        ToolTip.text: "마지막 촬영 " + chip.modelData.last
                        ToolTip.delay: 500
                    }
                }
            }
            // 실험 초기에 잰 상완과 발목 혈압입니다. 커프 압력은 발목 혈압으로 계산하는 단계로 정합니다. 비워 두고 찍을 수 없습니다.
            // 이름이나 생년월일을 바꾸면 발목 혈압 단계와 함께 비워집니다. 앞 피험자의 혈압이 다음 피험자의 촬영에 남지 않게 하기 위함입니다.
            RowLayout {
                Layout.topMargin: 16
                Layout.fillWidth: true
                spacing: 8
                opacity: enabled ? 1 : 0.4
                Label {
                    Layout.preferredWidth: 72
                    text: "혈압 (mmHg)"
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "수축기"
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                // 아래 줄의 "/"와 폭을 맞춰 머리글이 칸 위에 오게 합니다.
                Label { text: "/"; font.pixelSize: 16; opacity: 0 }
                Label {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: "이완기"
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
            }
            PressureRow {
                Layout.topMargin: 2
                Layout.fillWidth: true
                site: "상완"
                systolic: capture.shot.armSystolic
                diastolic: capture.shot.armDiastolic
                problem: capture.shot.armPressureError
                onSystolicEdited: text => capture.shot.setArmSystolic(text)
                onDiastolicEdited: text => capture.shot.setArmDiastolic(text)
            }
            PressureRow {
                Layout.topMargin: 4
                Layout.fillWidth: true
                site: "발목"
                systolic: capture.shot.ankleSystolic
                diastolic: capture.shot.ankleDiastolic
                problem: capture.shot.anklePressureError
                onSystolicEdited: text => capture.shot.setAnkleSystolic(text)
                onDiastolicEdited: text => capture.shot.setAnkleDiastolic(text)
            }
            // 발목 혈압으로 계산하는 단계입니다. 고르면 버튼 아래에 계산식과 값을 보이고 매니페스트에 단계와 값을 남깁니다.
            // 기본 선택이 없습니다. 단계 글자가 촬영 폴더의 가운데 단계라, 앞 값이 남아 있으면 다음 단계를 같은 폴더에 찍게 됩니다.
            Label {
                Layout.topMargin: 16
                text: "발목 혈압 단계"
                font.pixelSize: 13
                color: Theme.textMuted
            }
            Segmented {
                Layout.topMargin: 6
                Layout.fillWidth: true
                options: capture.shot.ankleLevels
                currentIndex: capture.shot.ankleLevels.indexOf(capture.shot.ankleLevel)
                onPicked: index => capture.shot.setAnkleLevel(capture.shot.ankleLevels[index])
            }
            Label {
                Layout.topMargin: 4
                Layout.fillWidth: true
                visible: text !== ""
                text: capture.shot.ankleLevelError !== "" ? capture.shot.ankleLevelError : capture.shot.ankleLevelText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                font.features: { "tnum": 1 }
                color: capture.shot.ankleLevelError !== "" ? Theme.mismatch : Theme.textMuted
            }

            Rectangle { Layout.fillWidth: true; Layout.topMargin: 22; Layout.bottomMargin: 18; height: 1; color: Theme.line }

            Label {
                text: "저장 위치"
                font.pixelSize: 13
                color: Theme.textMuted
            }
            // 경로 자체를 누르면 폴더를 고릅니다.
            Item {
                Layout.topMargin: 4
                Layout.fillWidth: true
                implicitHeight: rootRow.implicitHeight
                RowLayout {
                    id: rootRow
                    anchors.fill: parent
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: capture.shot.root
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
            // 저장 위치의 남은 공간. 10 GB 미만이면 노란색, 촬영 한 번을 저장하기에 모자라면 빨간색이며 촬영 버튼이 꺼집니다.
            Label {
                Layout.topMargin: 6
                Layout.fillWidth: true
                visible: capture.freeSpaceLevel !== ""
                text: capture.freeSpaceText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: capture.freeSpaceLevel === "short" ? Theme.mismatch : Theme.settling
            }
            Label {
                Layout.topMargin: 6
                Layout.fillWidth: true
                visible: capture.shot.folderPreview !== ""
                text: capture.shot.folderPreview
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 12
                color: Theme.textMuted
            }
            Label {
                Layout.topMargin: 6
                Layout.fillWidth: true
                text: capture.savedVideosText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.textMuted
            }
            Label {
                Layout.topMargin: 2
                Layout.fillWidth: true
                text: capture.light ? capture.light.summary : capture.lightingText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.textMuted
            }
            // LED 조명. 포트를 연결하지 않으면 앱 밖에서 켠 조명으로 기록합니다.
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 18; Layout.bottomMargin: 14; height: 1; color: Theme.line; visible: capture.light !== null }
            LightPanel {
                Layout.fillWidth: true
                visible: capture.light !== null
                light: capture.light
                editable: capture.canEdit
            }
        }
        }
        // PPG 장치. 연결하지 않으면 PPG 없이 촬영합니다. 스크롤 영역 밖에 두어 늘 보입니다.
        Rectangle { Layout.fillWidth: true; Layout.topMargin: 14; Layout.bottomMargin: 14; height: 1; color: Theme.line; visible: capture.ppg !== null }
        Loader {
            Layout.fillWidth: true
            active: capture.ppg !== null
            visible: active
            sourceComponent: PpgPanel { ppg: capture.ppg; editable: capture.canEdit; showWave: !ppgWave.visible }
        }
        }

        // 미리보기: 위는 RGB, 아래는 열화상(4:3)
        ColumnLayout {
            id: previews
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 480
            spacing: 12

            Rectangle {
                id: rgbFrame
                Layout.fillWidth: true
                // RGB는 프레임 가로세로비로 두되 열화상 자리를 45% 남깁니다.
                Layout.preferredHeight: Math.min(width / frameAspect, (previews.height - previews.spacing) * 0.55)
                radius: Theme.radius
                color: Theme.previewBackground
                clip: true

                // 넓은 창에서는 높이에 맞춘 영상 오른쪽에 검은 여백이 남습니다. 영상을 왼쪽에 붙이고 남은 폭에 RGB 패널을 둡니다.
                // 남은 폭이 패널에 모자라면 영상을 가운데에 두고 패널을 영상 오른쪽 위에 겹쳐 띄웁니다.
                readonly property real frameAspect: capture.rgbRegions.frameWidth > 0
                                                    ? capture.rgbRegions.frameWidth / capture.rgbRegions.frameHeight : 16 / 9
                readonly property real fitWidth: Math.min(width, height * frameAspect)
                readonly property bool sidePanel: width - fitWidth >= 240

                // Esc를 누르면 관찰 영역을 지웁니다(패널의 X 버튼과 같음). 안내 창이 떠 있으면 Esc는 안내 창을 닫는 데 쓰므로 끕니다.
                Shortcut {
                    sequence: "Esc"
                    enabled: capture.rgb.hasArea && !rgbFrame.Overlay.overlay.visible
                    onActivated: capture.rgb.clearArea()
                }

                Item {
                    id: rgbView
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                    width: rgbFrame.sidePanel ? rgbFrame.fitWidth : parent.width

                    Image {
                        id: rgbImage
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        // 첫 미리보기가 오기 전에는 요청하지 않습니다.
                        source: capture.previewRevision > 0 ? "image://preview/" + capture.previewRevision : ""
                        // 환자 왼발이 화면 왼쪽에 오게 뒤집어 그립니다. 녹화 영상은 뒤집지 않습니다.
                        mirror: root.mirrorPreview
                    }

                    // RGB 프레임이 오지 않을 때(앱을 켤 때 카메라가 없거나 연결이 끊김) 이유를 보입니다. 연결되면 저절로 이어집니다.
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 48
                        visible: capture.measuredFps <= 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        // 장치를 찾았지만 형식을 열지 못했으면(USB 2 포트 등) 그 이유를 보입니다. 다시 꽂으면 저절로 이어집니다.
                        text: capture.cameraProblem !== ""
                              ? "RGB 카메라(" + capture.deviceName + ")를 열지 못했습니다.\n" + capture.cameraProblem
                              : "RGB 카메라(" + capture.deviceName + ")를 찾는 중입니다.\nUSB 케이블과 허브 전원을 확인하십시오. 연결되면 자동으로 이어집니다."
                        font.pixelSize: 15
                        color: capture.cameraProblem !== "" ? Theme.mismatch : Theme.settling
                    }

                    // 관찰 영역. 발 상자보다 아래에 두어 상자 위의 누름은 상자가 먼저 받습니다(규칙은 RgbAreaPicker 머리 주석).
                    RgbAreaPicker {
                        id: areaPicker
                        trace: capture.rgb
                        regions: capture.rgbRegions
                        picture: rgbImage
                        mirror: root.mirrorPreview
                    }

                    // 좌우 발 영역. 녹화 중에는 옮길 수 없습니다. 패널에서 고른 발의 상자를 강조합니다.
                    FootBoxes {
                        regions: capture.rgbRegions
                        picture: rgbImage
                        leftFoot: capture.leftBoxFoot
                        rightFoot: capture.rightBoxFoot
                        editable: capture.canEdit
                        mirror: root.mirrorPreview
                        outline: rgbPanel.boxOutline
                        passShift: true
                        onFootClicked: capture.toggleFoot()
                        onBoxClicked: (frameX, frameY) => areaPicker.pickAt(frameX, frameY)
                    }

                    // 위: 장치와 실제 fps, 상태
                    Row {
                        anchors { left: parent.left; top: parent.top; margins: 16 }
                        spacing: 12
                        Label {
                            text: capture.deviceName
                            font.pixelSize: 13
                            color: Theme.textMuted
                        }
                        Label {
                            // 받은 프레임 수로 잰 fps입니다. 선언한 fps와 5% 넘게 다르면 주황색입니다.
                            text: capture.measuredFps.toFixed(1) + " fps"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                            color: root.fpsOff ? Theme.settling : Theme.text
                        }
                    }
                    // 실제 fps가 낮으면 이유를 보입니다. 어두우면 자동 맞춤이 노출 시간을 1/30초보다 길게 늘려 15fps가 됩니다(2026-10-16 실측).
                    // 게인을 올리면 잡음도 같이 커지므로 조명을 밝히는 것이 해결입니다.
                    Rectangle {
                        anchors { left: parent.left; top: parent.top; leftMargin: 10; topMargin: 44 }
                        visible: root.fpsOff && capture.measuredFps < capture.targetFps
                        width: fpsHint.implicitWidth + 20
                        height: fpsHint.implicitHeight + 10
                        radius: 8
                        color: "#cc000000"
                        Label {
                            id: fpsHint
                            anchors.centerIn: parent
                            text: "fps가 낮습니다. 어두워서 노출이 길어졌을 수 있습니다. 조명을 켜거나 밝게 하십시오"
                            font.pixelSize: 12
                            color: Theme.settling
                        }
                    }
                    Rectangle {
                        // RGB 패널을 영상 위에 겹쳐 띄우면 패널 왼쪽으로 옮겨 가리지 않게 합니다.
                        anchors { right: parent.right; top: parent.top; margins: 14; rightMargin: rgbPanel.floating ? rgbPanel.width + 26 : 14 }
                        height: 30
                        width: pill.width + 28
                        radius: 15
                        color: "#cc000000"
                        Row {
                            id: pill
                            anchors.centerIn: parent
                            spacing: 8
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 8; height: 8; radius: 4
                                color: root.mode === "recording" ? Theme.recording
                                     : root.mode === "settling" ? Theme.settling : Theme.textMuted
                                SequentialAnimation on opacity {
                                    running: root.mode === "recording"
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 600 }
                                    NumberAnimation { to: 1; duration: 600 }
                                }
                            }
                            Label {
                                text: root.mode === "settling" ? "안정화 " + Math.round(shownSettle.value) + "프레임 남음" : capture.phase
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }
                        }
                    }

                    // 아래: 녹화 진행
                    Column {
                        anchors { left: parent.left; bottom: parent.bottom; margins: 18 }
                        spacing: 2
                        visible: root.mode !== "idle" || capture.written > 0
                        Label {
                            text: Math.round(shownWritten.value) + " / " + capture.target
                            font.pixelSize: 30
                            font.weight: Font.Bold
                            font.features: { "tnum": 1 }
                            color: Theme.text
                        }
                        Label {
                            text: "카메라 단 누락 " + capture.missing + "   저장 누락 " + capture.dropped
                            font.pixelSize: 13
                            color: capture.missing + capture.dropped > 0 ? Theme.mismatch : Theme.textMuted
                        }
                    }
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom }
                        height: 3
                        width: parent.width * shownWritten.value / Math.max(1, capture.target)
                        color: Theme.recording
                        visible: root.mode === "recording"
                    }
                }

                // 두 발 상자와 관찰 영역의 채널별 평균, 포화 비율, 시간 그래프. 조명을 맞출 때 포화가 0%인 가장 밝은 값을 찾는 데 씁니다
                // (포화 화소는 맥박에 따른 밝기 변화가 기록되지 않음).
                RgbPanel {
                    id: rgbPanel
                    trace: capture.rgb
                    leftBoxFoot: capture.leftBoxFoot
                    floating: !rgbFrame.sidePanel
                    anchors {
                        right: parent.right
                        top: parent.top
                        bottom: parent.bottom
                        rightMargin: floating ? 12 : 0
                        topMargin: floating ? 12 : 0
                        bottomMargin: floating ? 12 : 0
                    }
                    width: floating ? 268 : parent.width - rgbFrame.fitWidth
                }
            }

            // 열화상은 RGB 영상과 같은 폭으로 두어 두 영상의 가운데를 맞추고, 남는 오른쪽(RGB 패널 아래)에 PPG 맥파를 크게 그립니다.
            // RGB 패널을 영상 위에 겹쳐 띄울 때(좁은 창)는 열화상이 전체 폭을 쓰고 맥파는 왼쪽 PPG 칸에 그대로 둡니다.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
            ThermalPane {
                Layout.fillWidth: !rgbFrame.sidePanel
                Layout.preferredWidth: rgbFrame.sidePanel ? rgbFrame.fitWidth : -1
                Layout.fillHeight: true
                thermal: capture.thermal
                showRecording: root.mode !== "idle"
                regions: capture.thermalRegions
                leftFoot: capture.leftBoxFoot
                rightFoot: capture.rightBoxFoot
                editable: capture.canEdit
                mirror: root.mirrorPreview
                onFootClicked: capture.toggleFoot()
                onLensShadingClicked: capture.askLensShading()
            }
            PpgWave {
                id: ppgWave
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: rgbFrame.sidePanel && capture.ppg !== null
                ppg: capture.ppg
            }
            }
        }

        // 오른쪽
        ColumnLayout {
            Layout.preferredWidth: 320
            Layout.maximumWidth: 320
            Layout.fillWidth: false
            Layout.fillHeight: true
            spacing: 0

            // 다시 읽은 값과 설정입니다. 창 높이가 모자라면 이 부분만 스크롤하고, 아래의 촬영 버튼, 결과, 분석 막대는 항상 보입니다.
            Flickable {
                id: settingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                Layout.bottomMargin: 12
                readonly property bool overflow: contentHeight > height
                contentHeight: settingsColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: settingsScroll.overflow ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

                ColumnLayout {
                    id: settingsColumn
                    // 스크롤 막대가 보이면 그 폭만큼 비워 증감 버튼을 가리지 않습니다.
                    width: settingsScroll.width - (settingsScroll.overflow ? 14 : 0)
                    spacing: 0

                    Label {
                        text: "카메라"
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.topMargin: 2
                        text: "카메라에 적용된 값"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: Theme.text
                    }
                    // 주기적으로 읽지 않으므로 읽는 시점을 함께 적습니다. 자동 맞춤 중 펌웨어가 바꾸는 노출은 여기 보이지 않습니다.
                    Label {
                        Layout.topMargin: 4
                        Layout.fillWidth: true
                        text: "설정을 보낼 때마다 카메라에서 다시 읽은 값입니다. 보낸 값과 다르면 빨간색으로 표시합니다."
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                    ColumnLayout {
                        Layout.topMargin: 14
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: capture.readback
                            delegate: ReadbackRow { Layout.fillWidth: true }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.topMargin: 22; Layout.bottomMargin: 22; height: 1; color: Theme.line }

                    Label {
                        text: "설정"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: Theme.text
                    }
                    Segmented {
                        Layout.topMargin: 14
                        Layout.fillWidth: true
                        enabled: capture.canEdit
                        options: ["자동 맞춤", "직접 지정"]
                        currentIndex: root.draftAuto ? 0 : 1
                        onPicked: index => root.draftAuto = index === 0
                    }
                    ColumnLayout {
                        Layout.topMargin: 16
                        Layout.fillWidth: true
                        spacing: 12
                        enabled: capture.canEdit
                        Stepper {
                            Layout.fillWidth: true
                            label: "노출 단계"
                            enabled: !root.draftAuto
                            value: root.draftExposure; from: -13; to: -1
                            changed: root.draftExposure !== capture.exposureStep
                            onEdited: v => root.draftExposure = v
                        }
                        Stepper {
                            Layout.fillWidth: true
                            label: "게인"
                            value: root.draftGain; from: 0; to: 255
                            changed: root.draftGain !== capture.gain
                            onEdited: v => root.draftGain = v
                        }
                        Stepper {
                            Layout.fillWidth: true
                            label: "색온도"
                            value: root.draftWb; from: 2800; to: 10000; step: 100; suffix: " K"
                            changed: root.draftWb !== capture.whiteBalance
                            onEdited: v => root.draftWb = v
                        }
                    }
                    Rectangle {
                        Layout.topMargin: 14
                        Layout.fillWidth: true
                        height: 38
                        radius: Theme.radius
                        color: Theme.controlActive
                        visible: root.dirty && capture.canEdit
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
                            onClicked: capture.applySettings(root.draftAuto, root.draftExposure, root.draftGain, root.draftWb)
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
                                border.color: capture.rememberSettings ? Theme.text : Theme.textMuted
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8; height: 8; radius: 2
                                    color: Theme.text
                                    visible: capture.rememberSettings
                                }
                            }
                            Label {
                                text: "이 설정 기억 (다음 실행에도 사용)"
                                font.pixelSize: 13
                                color: capture.rememberSettings ? Theme.text : Theme.textMuted
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: capture.rememberSettings = !capture.rememberSettings
                        }
                    }
                    Label {
                        Layout.topMargin: 4
                        visible: capture.startedWithRememberedSettings
                        text: "기억한 설정으로 시작했습니다(촬영 선언 파일 대신)"
                        font.pixelSize: 12
                        color: Theme.textMuted
                    }
                }
            }

            // 촬영을 막거나 촬영 내용을 바꾸는 상태: 발 영역 오류, 열화상 영역 오류, 열화상 없음
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                visible: text !== ""
                text: capture.rgbRegions.problem
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.mismatch
            }
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 6
                visible: text !== "" && capture.thermalPlanText === ""
                text: capture.thermalRegions.problem
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.mismatch
            }
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: text !== ""
                text: capture.thermalPlanText
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.settling
            }

            // 미리보기 열화상으로 본 발 상태입니다. 차가우면 주의 색이고, 촬영 버튼을 누르면 진행할지 묻습니다.
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && text !== ""
                text: capture.footWarmthText
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: capture.footWarmthState === "cold" ? Theme.settling : Theme.textMuted
            }
            // 커프 단계 기준값을 맞춘 카메라 설정과 다르면 알립니다. 녹화는 막지 않습니다.
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && text !== ""
                text: capture.cameraSettingWarning
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.settling
            }

            // 앞 촬영을 분석하는 동안에는 촬영을 막고 이유를 적습니다. 안정화와 녹화 중에는 새 분석을 보내지 않지만(setHold)
            // 이미 도는 분석은 멈추지 않아, 분석과 녹화가 CPU를 같이 쓰게 됩니다. 이 경우 프레임 누락이 없는지 실측하지 않았습니다.
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && root.analysisBlocksShot
                text: "앞 촬영을 분석하는 중입니다. 분석이 끝나면 촬영할 수 있습니다"
                      + (root.analysis.remainingText !== "" ? "(" + root.analysis.remainingText + ")" : "")
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.settling
            }

            // 촬영 버튼이 꺼진 이유를 버튼 바로 위에 둡니다. 왼쪽 칸 맨 아래에 두면 칸이 길 때 스크롤해야 보입니다.
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && capture.shot.missing !== ""
                text: capture.shot.missing
                wrapMode: Text.Wrap
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.settling
            }
            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && capture.shot.toFix !== ""
                text: capture.shot.toFix
                wrapMode: Text.Wrap
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.mismatch
            }

            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                visible: root.mode === "idle" && capture.freeSpaceLevel === "short"
                text: capture.freeSpaceText
                wrapMode: Text.Wrap
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.mismatch
            }

            RecordButton {
                Layout.alignment: Qt.AlignHCenter
                // 환자 정보가 갖춰지지 않았거나 앞 촬영을 분석하는 중이면 촬영을 막습니다. 녹화 중의 중지는 막지 않습니다.
                enabled: root.mode !== "idle" || (capture.canShoot && !root.analysisBlocksShot)
                opacity: enabled ? 1 : 0.3
                mode: root.mode
                progress: shownWritten.value / Math.max(1, capture.target)
                onClicked: capture.canEdit ? capture.startRecording() : capture.stopRecording()
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                text: root.mode !== "idle" ? "중지" : capture.savingShot ? "저장 중" : "촬영"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: capture.lastResult !== ""
                text: capture.lastResult
                font.pixelSize: 13
                color: Theme.textMuted
            }
            // 녹화 중 fps가 떨어진 구간입니다. 분석은 막지 않는 경고이며, 결과 창 프레임 간격 그래프에서 분석 구간과 겹치는지 봅니다.
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 2
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: capture.lastResultWarning !== ""
                text: capture.lastResultWarning
                font.pixelSize: 13
                color: Theme.settling
            }
            // 분석 입력 확인 결과. 분석을 막는 이유가 있으면 빨간색, 경고만 있으면 주황색입니다.
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 8
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: capture.analysisText !== ""
                text: capture.analysisText
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: capture.analysisState === "blocked" ? Theme.mismatch
                     : capture.analysisState === "warning" ? Theme.settling
                     : capture.analysisState === "ready" ? Theme.text : Theme.textMuted
            }
            // 관류 분석 진행. 촬영이 끝나면 켜 둔 판 모두로 판 순서대로 분석합니다.
            Rectangle { Layout.fillWidth: true; Layout.topMargin: 14; Layout.bottomMargin: 12; height: 1; color: Theme.line }
            Repeater {
                model: root.analysis.lanes
                delegate: ColumnLayout {
                    id: lane
                    required property AnalysisController modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.topMargin: index === 0 ? 0 : 12
                    spacing: 6
                    // 판 고르기. 줄을 눌러 켜고 끕니다. 마지막으로 켜 둔 판은 끄지 않습니다. 다음 촬영부터 적용합니다.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: variantRow.implicitHeight
                        visible: root.analysis.lanes.length > 1
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
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.analysis.setLaneEnabled(lane.modelData.variantId, !lane.modelData.enabled)
                        }
                    }
                    AnalysisBar {
                        Layout.fillWidth: true
                        visible: lane.modelData.enabled
                        analysis: lane.modelData
                    }
                }
            }
            // 녹화 중 미리 분석 스위치. 줄 전체를 눌러 켜고 끕니다. 다음 녹화부터 적용합니다.
            Item {
                Layout.topMargin: 10
                Layout.alignment: Qt.AlignHCenter
                visible: root.analysis !== null && root.analysis.prestreamAvailable
                implicitWidth: prestreamRow.implicitWidth
                implicitHeight: prestreamRow.implicitHeight
                Row {
                    id: prestreamRow
                    spacing: 8
                    // 켜짐은 손잡이가 오른쪽에 있고 밝은 색입니다.
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30; height: 16; radius: 8
                        color: root.analysis && root.analysis.prestream ? Theme.text : "transparent"
                        border.width: 1.5
                        border.color: root.analysis && root.analysis.prestream ? Theme.text : Theme.textMuted
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: root.analysis && root.analysis.prestream ? parent.width - width - 3 : 3
                            width: 10; height: 10; radius: 5
                            color: root.analysis && root.analysis.prestream ? Theme.background : Theme.textMuted
                        }
                    }
                    Label {
                        text: "녹화 중 미리 분석 " + (root.analysis && root.analysis.prestream ? "켬" : "끔")
                        font.pixelSize: 13
                        color: root.analysis && root.analysis.prestream ? Theme.text : Theme.textMuted
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.analysis.prestream = !root.analysis.prestream
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 4
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: root.analysis !== null && root.analysis.prestreamAvailable
                text: root.analysis && root.analysis.prestream
                      ? "녹화하는 동안 분석을 미리 해 결과가 약 15초 빨리 나옵니다. 녹화 누락이 생기면 끄세요."
                      : "녹화가 끝난 뒤 영상을 다시 읽어 분석합니다."
                font.pixelSize: 12
                color: Theme.textMuted
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 6
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                visible: capture.error !== ""
                text: capture.error
                font.pixelSize: 13
                color: Theme.mismatch
            }
        }
    }
}
