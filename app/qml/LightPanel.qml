import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// LED 조명입니다. 포트는 사람이 고르고, LED 밝기가 바뀌는 것을 보고 연결을 확인합니다.
// 밝기는 미리보기에서만 바꾸며, 포트를 연결하지 않으면 조명은 앱 밖에서 켠 것으로 기록합니다.
ColumnLayout {
    id: panel
    required property LightController light
    required property bool editable  // 미리보기 중이면 true

    // 화면에서 고친 밝기입니다. '적용'이나 포트 연결 전에는 보드에 보내지 않습니다.
    property int draftPercent: light.percent >= 0 ? light.percent : 50
    // 포트 목록은 연결하지 않았을 때 펼치고, 연결한 뒤에는 접습니다. 상태 줄을 누르면 연결이 바뀔 때까지 그 선택을 따릅니다.
    property bool toggled: false
    property bool toggledOpen: false
    readonly property bool listOpen: toggled ? toggledOpen : !light.connected
    readonly property bool connectedNow: light.connected
    onConnectedNowChanged: toggled = false
    readonly property bool canSend: editable && light.canChange
    readonly property color stateColor: light.connected ? (light.confirmed ? Theme.text : Theme.settling) : Theme.textMuted

    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "조명"
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: Theme.textMuted
        }
        // 포트 목록을 다시 읽습니다. 포트를 열지 않습니다.
        Label {
            text: "↻"
            font.pixelSize: 15
            color: Theme.textMuted
            visible: panel.listOpen
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.light.refreshPorts()
            }
        }
    }

    // 상태 줄. 누르면 포트 목록을 펼치거나 접습니다.
    Item {
        Layout.topMargin: 4
        Layout.fillWidth: true
        implicitHeight: stateRow.implicitHeight
        RowLayout {
            id: stateRow
            anchors.fill: parent
            spacing: 8
            Rectangle {
                width: 8; height: 8; radius: 4
                color: panel.stateColor
            }
            Label {
                Layout.fillWidth: true
                text: panel.light.connected
                      ? panel.light.portName + "  " + panel.light.percent + "%"
                      : "앱 밖에서 켬"
                elide: Text.ElideRight
                font.pixelSize: 14
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: Theme.text
            }
            // 저장 위치와 같은 펼침 표시입니다. 펼치면 아래를 가리킵니다.
            Label {
                text: "›"
                rotation: panel.listOpen ? 90 : 0
                font.pixelSize: 18
                color: Theme.textMuted
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                panel.toggledOpen = !panel.listOpen
                panel.toggled = true
            }
        }
    }

    // 포트 목록. 행을 누르면 그 포트를 열고 밝기를 보냅니다. 기억한 변환기와 맞는 포트는 앞에 점을 둡니다.
    // PPG 칸이 연 포트는 누를 수 없고, 지난번 PPG 장치로 연결한 포트는 열기 전에 묻습니다.
    ColumnLayout {
        Layout.topMargin: 8
        Layout.fillWidth: true
        spacing: 2
        visible: panel.listOpen

        Repeater {
            model: panel.light.ports
            delegate: Rectangle {
                id: portRow
                required property var modelData
                readonly property bool open: panel.light.connected && panel.light.portName === modelData.name
                readonly property bool usable: panel.canSend && !modelData.ppgOpen
                Layout.fillWidth: true
                implicitHeight: portColumn.implicitHeight + 10
                radius: 6
                color: portArea.containsMouse && usable ? Theme.control : "transparent"
                opacity: usable || open ? 1 : 0.4

                ColumnLayout {
                    id: portColumn
                    anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; leftMargin: 6; rightMargin: 6 }
                    spacing: 1
                    RowLayout {
                        spacing: 8
                        Label {
                            text: portRow.modelData.name
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Label {
                            text: portRow.modelData.vidPid
                            visible: text !== ""
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                            color: Theme.textMuted
                        }
                        Label {
                            text: portRow.open ? "연결됨"
                                  : portRow.modelData.ppgOpen ? "PPG에서 사용 중"
                                  : portRow.modelData.lastPpg ? "지난번 PPG"
                                  : portRow.modelData.suggested ? "기억한 변환기" : ""
                            visible: text !== ""
                            font.pixelSize: 12
                            color: portRow.open ? Theme.text : portRow.modelData.ppgOpen ? Theme.textMuted : Theme.settling
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: portRow.modelData.description
                              + (portRow.modelData.serial !== "" ? "  " + portRow.modelData.serial : "")
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        color: Theme.textMuted
                    }
                }
                MouseArea {
                    id: portArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: portRow.usable && !portRow.open
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.light.connectPort(portRow.modelData.name, panel.draftPercent)
                }
            }
        }
        // 지난번 PPG 장치로 연결한 포트를 골랐을 때입니다. 열면 바로 밝기 명령을 보내므로 사람이 LED 보드인지 판단합니다.
        ColumnLayout {
            Layout.topMargin: 4
            Layout.fillWidth: true
            visible: panel.light.pendingPort !== ""
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: "지난번 PPG 장치로 연결한 변환기입니다(" + panel.light.pendingPort + "). 조명으로 열면 이 포트에 밝기 명령을 보냅니다."
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.settling
            }
            RowLayout {
                spacing: 16
                Label {
                    text: "조명으로 열기"
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    color: Theme.text
                    opacity: panel.canSend ? 1 : 0.4
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        enabled: panel.canSend
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.light.openPending(panel.draftPercent)
                    }
                }
                Label {
                    text: "취소"
                    font.pixelSize: 12
                    color: Theme.textMuted
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.light.cancelPending()
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: panel.light.ports.length === 0
            text: "직렬 포트가 없습니다"
            font.pixelSize: 12
            color: Theme.textMuted
        }
        // 연결한 포트를 닫습니다. LED는 마지막 밝기로 남습니다.
        Label {
            visible: panel.light.connected
            text: "연결 끊기"
            font.pixelSize: 12
            color: Theme.textMuted
            opacity: panel.editable ? 1 : 0.4
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                enabled: panel.editable
                cursorShape: Qt.PointingHandCursor
                onClicked: panel.light.disconnectPort()
            }
        }
    }

    // 밝기. 포트를 연결하기 전에는 연결할 때 보낼 값입니다.
    Stepper {
        Layout.topMargin: 10
        Layout.fillWidth: true
        enabled: panel.editable
        label: "밝기"
        value: panel.draftPercent; from: 0; to: 100; step: 5; suffix: "%"
        changed: panel.light.connected && panel.draftPercent !== panel.light.percent
        onEdited: v => panel.draftPercent = v
    }
    Rectangle {
        Layout.topMargin: 8
        Layout.fillWidth: true
        height: 34
        radius: Theme.radius
        color: Theme.controlActive
        visible: panel.light.connected && panel.draftPercent !== panel.light.percent && panel.canSend
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
            onClicked: panel.light.apply(panel.draftPercent)
        }
    }

    // 사람이 LED 밝기가 바뀌는 것을 본 뒤 누릅니다. 누르면 이 변환기를 다음 실행에 기억합니다.
    Item {
        Layout.topMargin: 8
        Layout.fillWidth: true
        visible: panel.light.connected
        implicitHeight: confirmRow.implicitHeight
        RowLayout {
            id: confirmRow
            anchors.fill: parent
            spacing: 8
            Rectangle {
                width: 14; height: 14; radius: 3
                color: "transparent"
                border.width: 1
                border.color: panel.light.confirmed ? Theme.text : Theme.settling
                Label {
                    anchors.centerIn: parent
                    visible: panel.light.confirmed
                    text: "✓"
                    font.pixelSize: 11
                    color: Theme.text
                }
            }
            Label {
                Layout.fillWidth: true
                text: panel.light.confirmed ? "LED 보드로 확인함" : "LED 밝기가 바뀌는 것을 봤으면 누르십시오"
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: panel.light.confirmed ? Theme.textMuted : Theme.settling
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: !panel.light.confirmed
            cursorShape: Qt.PointingHandCursor
            onClicked: panel.light.confirm()
        }
    }

    Label {
        Layout.topMargin: 6
        Layout.fillWidth: true
        visible: panel.light.connected
        text: panel.light.responseText
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: panel.light.responseState === "not_seen" ? Theme.settling : Theme.textMuted
    }
    Label {
        Layout.topMargin: 6
        Layout.fillWidth: true
        visible: !panel.editable
        text: "밝기는 미리보기에서만 바꿀 수 있습니다"
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.textMuted
    }
    Label {
        Layout.topMargin: 6
        Layout.fillWidth: true
        visible: panel.light.error !== ""
        text: panel.light.error
        wrapMode: Text.Wrap
        font.pixelSize: 12
        color: Theme.mismatch
    }
}
