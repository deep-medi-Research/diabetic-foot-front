import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 화면은 Capture 상태를 그리고 버튼을 넘기기만 합니다. 촬영 판단은 core와 platform이 합니다.
// 입력, 미리보기, 장치 상태, 촬영 버튼을 위에서 아래로 둡니다. 개발과 시험 기록에 쓰는 설정과 값은 설정 패널에 있습니다.
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
    readonly property string mode: capture.recording ? "recording" : capture.settling ? "settling" : "idle"
    readonly property bool fpsOff: capture.measuredFps > 0
                                   && Math.abs(capture.measuredFps - capture.targetFps) > capture.targetFps * 0.05
    // 설정 패널이 열린 동안만 영상 위에 개발용 값(장치 이름과 실제 fps, 발 상자 크기, 열화상 원시값 범위)을 그립니다.
    readonly property bool devInfo: settings.opened
    // 설정 패널을 연 채로 시작합니다(명령줄 시험 옵션 --open-settings). main이 넘깁니다.
    property bool openSettings: false
    Component.onCompleted: if (openSettings) settings.open()

    // 발 상자에서 채널 값이 255인 화소가 있으면 그 발과 채널입니다. 포화 화소는 맥박에 따른 밝기 변화가 기록되지 않아
    // 설정 패널을 닫아도 촬영 화면에 알립니다.
    property string saturationText: ""
    Connections {
        target: capture.rgb
        function onSampled() {
            const parts = []
            const boxes = [[capture.leftBoxFoot, 0], [capture.rightBoxFoot, 1]]
            for (const [foot, target] of boxes) {
                const latest = capture.rgb.latest(target)
                if (!latest.valid) continue
                for (const [ch, key] of [["R", "satR"], ["G", "satG"], ["B", "satB"]]) {
                    if (latest[key] > 0) parts.push(foot + " " + ch + " " + RgbText.saturation(latest[key]))
                }
            }
            root.saturationText = parts.length === 0 ? "" : "발 상자 포화: " + parts.join(", ") + ". 조명을 낮추거나 노출을 줄이십시오"
        }
    }

    // 촬영 버튼을 눌렀는데 열화상이 수신 중이 아니면 열립니다.
    ThermalPrompt { id: thermalPrompt; capture: root.capture }

    // 촬영 버튼을 눌렀는데 미리보기 열화상으로 본 발이 차가우면 열립니다.
    ColdFootPrompt { id: coldFootPrompt; capture: root.capture }

    // 보정 파일이 없을 때(열화상을 처음 받을 때 한 번)와 렌즈 보정을 누를 때 열립니다.
    LensShadingPrompt { id: lensShadingPrompt; capture: root.capture }

    // 창을 닫으면 먼저 카메라를 실행 전 상태로 되돌리고, 맞지 않은 항목이 있으면 알린 뒤 닫습니다.
    // 녹화를 준비하거나 쓰는 중이면 먼저 묻습니다. 닫으면 녹화를 멈추고 지금까지 찍은 것을 저장한 뒤 닫습니다.
    onClosing: close => {
        close.accepted = capture.closeReady
        if (close.accepted || capture.shuttingDown) return
        if (capture.shotInProgress) closeConfirm.open()
        else capture.shutdown()
    }
    CloseConfirm { id: closeConfirm; capture: root.capture }
    RestoreNotice { id: restoreNotice; capture: root.capture }

    SettingsPanel { id: settings; capture: root.capture; analysis: root.analysis }
    // 설정 패널이 열려 있으면 Esc는 관찰 영역이 있으면 지우고(RGB 패널의 지우기와 같음) 없으면 패널을 닫습니다.
    // 안내 창이 떠 있으면 Esc는 안내 창이 받으므로 끕니다.
    Shortcut {
        sequence: "Esc"
        enabled: settings.opened && !thermalPrompt.opened && !coldFootPrompt.opened && !lensShadingPrompt.opened
                 && !closeConfirm.opened && !restoreNotice.opened
        onActivated: capture.rgb.hasArea ? capture.rgb.clearArea() : settings.close()
    }

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
            font.pixelSize: 13
            color: Theme.mismatch
        }
    }

    // 장치 상태 한 칸입니다. 점의 색이 상태이고, 누를 수 있는 칸은 설정 패널을 엽니다(포트와 저장 위치는 거기서 고름).
    component StatusItem: Item {
        id: item
        required property string text
        required property color tone
        property bool opensSettings: false
        property real maxTextWidth: 480
        implicitWidth: itemRow.implicitWidth
        implicitHeight: itemRow.implicitHeight
        Row {
            id: itemRow
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8; height: 8; radius: 4
                color: item.tone
            }
            Label {
                width: Math.min(implicitWidth, item.maxTextWidth)
                elide: Text.ElideMiddle
                text: item.text + (item.opensSettings ? "  ›" : "")
                font.pixelSize: 14
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: Theme.text
            }
        }
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            enabled: item.opensSettings
            cursorShape: Qt.PointingHandCursor
            onClicked: settings.open()
        }
    }

    // 경고 한 줄입니다.
    component Warning: Label {
        Layout.fillWidth: true
        visible: text !== ""
        wrapMode: Text.Wrap
        font.pixelSize: 14
        font.weight: Font.DemiBold
        color: Theme.settling
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        // 1. 입력: 환자, 혈압, 발목 혈압 단계. 녹화 중에는 바꿀 수 없습니다.
        RowLayout {
            Layout.fillWidth: true
            spacing: 36
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: 420
                Layout.maximumWidth: 420
                spacing: 0
                enabled: capture.canEdit
                Label {
                    text: "촬영 정보"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    color: Theme.text
                }
                RowLayout {
                    Layout.topMargin: 10
                    Layout.fillWidth: true
                    spacing: 16
                    Field {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        label: "이름"
                        value: capture.shot.name
                        problem: capture.shot.nameError
                        onEdited: text => capture.shot.setName(text)
                    }
                    Field {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        label: "생년월일  예: 1955-11-03"
                        // 날짜 선택 대신 마스크를 씁니다. 날짜 선택은 비워 둘 수 없어 입력하지 않은 날짜가 그럴듯한 값으로 저장됩니다(기존 앱의 판단).
                        mask: "0000-00-00"
                        value: capture.shot.birth
                        problem: capture.shot.birthError
                        onEdited: text => capture.shot.setBirth(text)
                    }
                }
                // 저장 위치에 있는 최근 환자입니다. 누르면 이름과 생년월일을 함께 채웁니다. 지금 입력과 같은 환자는 밝게 보입니다.
                Flow {
                    Layout.topMargin: 10
                    Layout.fillWidth: true
                    visible: capture.shot.recentPatients.length > 0
                    spacing: 6
                    Label {
                        height: 34
                        verticalAlignment: Text.AlignVCenter
                        text: "최근 환자"
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                    Repeater {
                        model: capture.shot.recentPatients
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            readonly property bool current: modelData.name === capture.shot.name.trim() && modelData.birth === capture.shot.birth
                            width: chipText.implicitWidth + 20
                            height: 34
                            radius: height / 2
                            color: current ? Theme.controlActive : chipArea.containsMouse ? Theme.line : Theme.control
                            Row {
                                id: chipText
                                anchors.centerIn: parent
                                spacing: 6
                                Label {
                                    text: chip.modelData.name
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: chip.current ? Theme.background : Theme.text
                                }
                                Label {
                                    text: chip.modelData.birth
                                    font.pixelSize: 13
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
            }

            // 실험 초기에 잰 상완과 발목 혈압입니다. 커프 압력은 발목 혈압으로 계산하는 단계로 정합니다. 비워 두고 찍을 수 없습니다.
            // 이름이나 생년월일을 바꾸면 발목 혈압 단계와 함께 비워집니다. 앞 피험자의 혈압이 다음 피험자의 촬영에 남지 않게 하기 위함입니다.
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 34
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                spacing: 0
                enabled: capture.canEdit
                RowLayout {
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
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                    // 아래 줄의 "/"와 폭을 맞춰 머리글이 칸 위에 오게 합니다.
                    Label { text: "/"; font.pixelSize: 16; opacity: 0 }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: "이완기"
                        font.pixelSize: 13
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
            }

            // 발목 혈압으로 계산하는 단계입니다. 고르면 버튼 아래에 계산식과 값을 보이고 매니페스트에 단계와 값을 남깁니다.
            // 기본 선택이 없습니다. 단계 글자가 촬영 폴더의 가운데 단계라, 앞 값이 남아 있으면 다음 단계를 같은 폴더에 찍게 됩니다.
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 34
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                spacing: 0
                enabled: capture.canEdit
                Label {
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
                    font.pixelSize: 13
                    font.features: { "tnum": 1 }
                    color: capture.shot.ankleLevelError !== "" ? Theme.mismatch : Theme.textMuted
                }
            }

            Item { Layout.fillWidth: true }

            // 개발과 시험 기록용 설정 패널을 열고 닫습니다.
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: settingsLabel.implicitWidth + 32
                implicitHeight: 38
                radius: Theme.radius
                color: settings.opened ? Theme.controlActive : "transparent"
                border.width: 1
                border.color: Theme.line
                Label {
                    id: settingsLabel
                    anchors.centerIn: parent
                    text: "설정"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: settings.opened ? Theme.background : Theme.text
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settings.opened ? settings.close() : settings.open()
                }
            }
        }

        // 2. 미리보기: RGB와 열화상(4:3)을 같은 높이로 나란히 두고, PPG를 연결했으면 그 아래에 맥파를 그립니다.
        // 그 아래로 장치 상태 한 줄, 경고와 촬영 버튼을 이어 둡니다.
        Item {
            id: previewArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 240

            readonly property real gap: 12
            readonly property real rgbAspect: capture.rgbRegions.frameWidth > 0
                                              ? capture.rgbRegions.frameWidth / capture.rgbRegions.frameHeight : 16 / 9
            readonly property real thermalAspect: capture.thermalRegions.frameWidth > 0
                                                  ? capture.thermalRegions.frameWidth / capture.thermalRegions.frameHeight : 4 / 3
            // 녹화 중에 PPG가 끊겨도 미리보기 크기가 바뀌지 않게 기록하는 동안은 맥파 자리를 둡니다.
            readonly property bool showWave: capture.ppg !== null && (capture.ppg.connected || capture.ppg.recording)
            readonly property real waveHeight: 96
            // 아래 줄들은 미리보기가 아니라 이 칸의 폭을 따르므로, 그 높이로 미리보기 높이를 정해도 서로를 바꾸지 않습니다.
            readonly property real tileHeight: Math.max(0, Math.min(height - below.implicitHeight - gap - (showWave ? waveHeight + gap : 0),
                                                                    (width - gap) / (rgbAspect + thermalAspect)))

            Column {
                id: stack
                anchors { top: parent.top; horizontalCenter: parent.horizontalCenter }
                spacing: previewArea.gap

                Row {
                    spacing: previewArea.gap

                    Rectangle {
                        id: rgbFrame
                        width: previewArea.tileHeight * previewArea.rgbAspect
                        height: previewArea.tileHeight
                        radius: Theme.radius
                        color: Theme.previewBackground
                        clip: true

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
                            // 장치를 찾았지만 형식을 열지 못했으면(USB 2 속도 연결 등) 그 이유를 보입니다. 다시 꽂으면 저절로 이어집니다.
                            text: capture.cameraProblem !== ""
                                  ? "RGB 카메라(" + capture.deviceName + ")를 열지 못했습니다.\n" + capture.cameraProblem
                                  : "RGB 카메라(" + capture.deviceName + ")를 찾는 중입니다.\nUSB 케이블과 허브 전원을 확인하십시오. 연결되면 자동으로 이어집니다."
                            font.pixelSize: 15
                            color: capture.cameraProblem !== "" ? Theme.mismatch : Theme.settling
                        }

                        // 관찰 영역. 발 상자보다 아래에 두어 상자 위의 누름은 상자가 먼저 받습니다(규칙은 RgbAreaPicker 머리 주석).
                        // 설정 패널의 RGB 패널과 함께 쓰므로 패널이 열린 동안만 그리고 정합니다.
                        RgbAreaPicker {
                            id: areaPicker
                            visible: settings.opened
                            enabled: settings.opened
                            trace: capture.rgb
                            regions: capture.rgbRegions
                            picture: rgbImage
                            mirror: root.mirrorPreview
                        }

                        // 좌우 발 영역. 녹화 중에는 옮길 수 없습니다. 설정 패널의 RGB 패널에서 고른 발의 상자를 강조합니다.
                        FootBoxes {
                            regions: capture.rgbRegions
                            picture: rgbImage
                            leftFoot: capture.leftBoxFoot
                            rightFoot: capture.rightBoxFoot
                            editable: capture.canEdit
                            mirror: root.mirrorPreview
                            outline: settings.boxOutline
                            showSizes: root.devInfo
                            passShift: settings.opened
                            onFootClicked: capture.toggleFoot()
                            onBoxClicked: (frameX, frameY) => { if (settings.opened) areaPicker.pickAt(frameX, frameY) }
                        }

                        // 위: 장치와 실제 fps(설정 패널이 열린 동안)
                        Row {
                            anchors { left: parent.left; top: parent.top; margins: 16 }
                            spacing: 12
                            visible: root.devInfo
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
                            anchors { left: parent.left; top: parent.top; leftMargin: 10; topMargin: root.devInfo ? 44 : 14 }
                            visible: root.fpsOff && capture.measuredFps < capture.targetFps
                            width: Math.min(fpsHint.implicitWidth + 20, parent.width - 20 - statePill.width - 24)
                            height: fpsHint.implicitHeight + 10
                            radius: 8
                            color: "#cc000000"
                            Label {
                                id: fpsHint
                                anchors.centerIn: parent
                                width: parent.width - 20
                                text: "fps가 낮습니다(" + capture.measuredFps.toFixed(1) + " fps). 어두워서 노출이 길어졌을 수 있습니다. 조명을 켜거나 밝게 하십시오"
                                wrapMode: Text.Wrap
                                font.pixelSize: 13
                                color: Theme.settling
                            }
                        }
                        Rectangle {
                            id: statePill
                            anchors { right: parent.right; top: parent.top; margins: 14 }
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

                    ThermalPane {
                        id: thermalPane
                        width: previewArea.tileHeight * previewArea.thermalAspect
                        height: previewArea.tileHeight
                        thermal: capture.thermal
                        showRecording: root.mode !== "idle"
                        regions: capture.thermalRegions
                        leftFoot: capture.leftBoxFoot
                        rightFoot: capture.rightBoxFoot
                        editable: capture.canEdit
                        mirror: root.mirrorPreview
                        devInfo: root.devInfo
                        onFootClicked: capture.toggleFoot()
                        onLensShadingClicked: capture.askLensShading()
                    }
                }

                PpgWave {
                    width: rgbFrame.width + previewArea.gap + thermalPane.width
                    height: previewArea.waveHeight
                    visible: previewArea.showWave
                    ppg: capture.ppg
                }
            }

            Column {
                id: below
                anchors { top: stack.bottom; topMargin: previewArea.gap; left: parent.left; right: parent.right }
                spacing: previewArea.gap

                Flow {
                    width: parent.width
                    spacing: 28
                    StatusItem {
                        text: capture.measuredFps <= 0 ? (capture.cameraProblem !== "" ? "RGB 열지 못함" : "RGB 찾는 중")
                            : root.fpsOff ? "RGB fps 어긋남(" + capture.measuredFps.toFixed(1) + " fps)" : "RGB 수신 중"
                        tone: capture.measuredFps <= 0 ? (capture.cameraProblem !== "" ? Theme.mismatch : Theme.settling)
                            : root.fpsOff ? Theme.settling : Theme.text
                    }
                    StatusItem {
                        readonly property string key: capture.thermal.state
                        text: "열화상 " + capture.thermal.stateText
                              + (capture.thermal.warmupText !== "" ? ", " + capture.thermal.warmupText : "")
                        tone: key === "receiving" ? (capture.thermal.warmup === "" || capture.thermal.warmup === "ready" ? Theme.text : Theme.settling)
                            : key === "searching" ? Theme.settling : key === "lost" ? Theme.mismatch : Theme.textMuted
                    }
                    StatusItem {
                        visible: capture.light !== null
                        opensSettings: true
                        text: capture.light === null ? ""
                              : capture.light.connected ? "조명 " + capture.light.portName + ", " + capture.light.percent + "%"
                                                        : "조명 앱 밖에서 켬"
                        tone: capture.light === null || !capture.light.connected ? Theme.textMuted
                            : capture.light.confirmed ? Theme.text : Theme.settling
                    }
                    StatusItem {
                        visible: capture.ppg !== null
                        opensSettings: true
                        text: capture.ppg === null ? "" : capture.ppg.connected ? "PPG " + capture.ppg.portName
                              : capture.ppg.tone === "error" ? "PPG 오류" : "PPG 연결 안 함"
                        tone: capture.ppg === null ? Theme.textMuted
                            : capture.ppg.tone === "ok" ? Theme.text : capture.ppg.tone === "warn" ? Theme.settling
                            : capture.ppg.tone === "error" ? Theme.mismatch : Theme.textMuted
                    }
                }

                // 4. 경고와 촬영: 촬영을 막거나 촬영 내용을 바꾸는 상태, 설정 패널로 옮긴 값의 경고, 촬영 버튼, 녹화 결과와 관류 분석
                RowLayout {
                    width: parent.width
                    spacing: 32

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 6
                        Warning { text: capture.rgbRegions.problem; color: Theme.mismatch }
                        Warning { text: capture.thermalPlanText === "" ? capture.thermalRegions.problem : ""; color: Theme.mismatch }
                        Warning { text: capture.thermalPlanText }
                        // 미리보기 열화상으로 본 발 상태입니다. 차가우면 주의 색이고, 촬영 버튼을 누르면 진행할지 묻습니다.
                        Warning {
                            text: root.mode === "idle" ? capture.footWarmthText : ""
                            font.weight: Font.Normal
                            color: capture.footWarmthState === "cold" ? Theme.settling : Theme.textMuted
                        }
                        // 커프 단계 기준값을 맞춘 카메라 설정과 다르면 알립니다. 녹화는 막지 않습니다.
                        Warning { text: root.mode === "idle" ? capture.cameraSettingWarning : "" }
                        Warning { text: capture.rgb.stale ? "" : root.saturationText }
                        Warning { text: capture.readback.anyMismatch ? "카메라에서 다시 읽은 값이 보낸 값과 다릅니다. 설정에서 확인하십시오" : "" }
                        Warning {
                            text: capture.light !== null && capture.light.responseState === "not_seen" ? "조명: " + capture.light.responseText : ""
                        }
                        Warning { text: capture.light !== null ? capture.light.error : ""; color: Theme.mismatch }
                        Warning {
                            text: capture.ppg === null || (capture.ppg.tone !== "warn" && capture.ppg.tone !== "error") ? ""
                                  : capture.ppg.statusText.startsWith("PPG") ? capture.ppg.statusText : "PPG: " + capture.ppg.statusText
                            color: capture.ppg !== null && capture.ppg.tone === "error" ? Theme.mismatch : Theme.settling
                        }
                        // 저장 위치의 남은 공간. 10 GB 미만이면 노란색, 촬영 한 번을 저장하기에 모자라면 빨간색이며 촬영 버튼이 꺼집니다.
                        Warning {
                            text: capture.freeSpaceLevel !== "" ? capture.freeSpaceText : ""
                            color: capture.freeSpaceLevel === "short" ? Theme.mismatch : Theme.settling
                        }
                        // 앞 촬영을 분석하는 동안에는 촬영을 막고 이유를 적습니다. 안정화와 녹화 중에는 새 분석을 보내지 않지만(setHold)
                        // 이미 도는 분석은 멈추지 않아, 분석과 녹화가 CPU를 같이 쓰게 됩니다. 이 경우 프레임 누락이 없는지 실측하지 않았습니다.
                        Warning {
                            text: root.mode === "idle" && root.analysisBlocksShot
                                  ? "앞 촬영을 분석하는 중입니다. 분석이 끝나면 촬영할 수 있습니다"
                                    + (root.analysis.remainingText !== "" ? "(" + root.analysis.remainingText + ")" : "")
                                  : ""
                        }
                        // 촬영 버튼이 꺼진 이유입니다.
                        Warning { text: root.mode === "idle" ? capture.shot.missing : "" }
                        Warning { text: root.mode === "idle" ? capture.shot.toFix : ""; color: Theme.mismatch }
                        Warning { text: capture.error; color: Theme.mismatch }
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 0
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
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: 440
                        Layout.maximumWidth: 440
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: capture.lastResult !== ""
                            text: capture.lastResult
                            font.pixelSize: 13
                            color: Theme.textMuted
                        }
                        // 녹화 중 fps가 떨어진 구간입니다. 분석은 막지 않는 경고이며, 결과 창 프레임 간격 그래프에서 분석 구간과 겹치는지 봅니다.
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: capture.lastResultWarning !== ""
                            text: capture.lastResultWarning
                            font.pixelSize: 13
                            color: Theme.settling
                        }
                        // 분석 입력 확인 결과. 분석을 막는 이유가 있으면 빨간색, 경고만 있으면 주황색입니다.
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: capture.analysisText !== ""
                            text: capture.analysisText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: capture.analysisState === "blocked" ? Theme.mismatch
                                 : capture.analysisState === "warning" ? Theme.settling
                                 : capture.analysisState === "ready" ? Theme.text : Theme.textMuted
                        }
                        // 관류 분석 진행. 촬영이 끝나면 켜 둔 판 모두로 판 순서대로 분석합니다. 판 고르기와 다시 분석은 설정 패널에 있습니다.
                        Repeater {
                            model: root.analysis !== null ? root.analysis.lanes : []
                            delegate: ColumnLayout {
                                id: lane
                                required property AnalysisController modelData
                                Layout.fillWidth: true
                                visible: modelData.enabled
                                spacing: 4
                                // 판을 둘 이상 켜면 막대마다 판 이름을 적습니다. 판을 켜고 끄는 것은 설정 패널에서 합니다.
                                Label {
                                    Layout.fillWidth: true
                                    visible: root.analysis.enabledCount > 1
                                    text: lane.modelData.variantLabel
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    color: Theme.textMuted
                                }
                                AnalysisBar {
                                    Layout.fillWidth: true
                                    analysis: lane.modelData
                                    retryVisible: false
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
