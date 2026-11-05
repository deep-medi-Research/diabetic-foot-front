import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// RGB 미리보기 옆의 패널입니다. 발 상자 평균(왼발, 오른발, 겹쳐보기)과 관찰 영역의 채널별 평균, 포화 비율, 시간 그래프를 보입니다.
// 관찰 영역이 생기면 발 상자 평균을 위쪽 두 줄로 줄이고 그 아래에 관찰 영역을 보입니다. 값은 RgbTrace에서 옵니다.
Rectangle {
    id: panel
    required property RgbTrace trace
    // 화소 왼쪽 상자의 발 이름("왼발" 또는 "오른발")입니다. 패널은 상자가 아니라 환자의 발로 고릅니다
    required property string leftBoxFoot
    // 미리보기 위에 겹쳐 띄울 때 true입니다. 반투명 바탕에 둥근 모서리로 그립니다
    property bool floating: false
    // 0 왼발, 1 오른발, 2 겹쳐보기
    property int footMode: 0

    readonly property int leftFootTarget: leftBoxFoot === "왼발" ? 0 : 1
    readonly property int rightFootTarget: 1 - leftFootTarget
    // 미리보기 상자 테두리에 쓸 모양입니다. 화소 왼쪽 상자, 화소 오른쪽 상자 순서이고 FootBoxes.outline이 받습니다.
    readonly property var boxOutline: {
        const out = ["faint", "faint"]
        if (footMode === 2) {
            out[leftFootTarget] = "solid"
            out[rightFootTarget] = "dashed"
        } else {
            out[footMode === 0 ? leftFootTarget : rightFootTarget] = "solid"
        }
        return out
    }

    // 관찰 영역이 있으면 1입니다. 0.2초 동안 바뀌며 발 상자 구역을 줄이고 관찰 영역 구역을 띄웁니다.
    property real areaShown: trace.hasArea ? 1 : 0
    Behavior on areaShown { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

    color: floating ? Theme.overlayPanel : Theme.background
    radius: floating ? Theme.radius : 0
    clip: true

    // 패널의 빈 자리를 누르거나 휠을 굴려도 아래의 미리보기(발 상자, 관찰 영역)로 넘어가지 않게 받습니다.
    MouseArea {
        anchors.fill: parent
        onWheel: wheel => wheel.accepted = true
    }

    // 패널의 모든 그래프가 같이 쓰는 시간 창입니다. live이면 마지막 표본을 따라가고, 끌거나 휠로 옮기면 멈춥니다.
    QtObject {
        id: timeWindow
        property real span: 10
        property bool live: true
        property real heldEnd: 0
        // 기록이 span보다 짧으면 0초부터 span초까지를 보여 선이 왼쪽부터 차오르게 합니다.
        readonly property real latestEnd: Math.max(panel.trace.endS, span)
        readonly property real earliestEnd: Math.min(latestEnd, panel.trace.startS + span)
        readonly property real viewEnd: live ? latestEnd : Math.max(earliestEnd, Math.min(latestEnd, heldEnd))
        readonly property real viewStart: viewEnd - span

        function panTo(end) {
            const e = Math.max(earliestEnd, Math.min(latestEnd, end))
            if (e >= latestEnd - 1e-6) {
                live = true
            } else {
                heldEnd = e
                live = false
            }
        }
        function zoom(factor) {
            const end = viewEnd
            span = Math.max(5, Math.min(60, span * factor))
            if (!live) panTo(end)
        }
    }

    // 그래프와 숫자가 쓰는 값입니다. 표본이 올 때, 시간 창이나 고른 발이 바뀔 때 refresh가 다시 읽습니다.
    property var footPrimary: null
    property var footSecondary: null
    property var areaSeries: null
    property var leftLatest: ({ valid: false })
    property var rightLatest: ({ valid: false })
    property var areaLatest: ({ valid: false })
    readonly property var footLatest: footMode === 1 ? rightLatest : leftLatest
    // 카메라가 1초 넘게 프레임을 보내지 않으면 숫자는 마지막 값이므로 흐리게 그리고, 맨 아래 "실시간" 자리에 "멈춤"을 씁니다.
    readonly property real valueOpacity: trace.stale ? 0.4 : 1
    // 높이가 모자라면(작은 창에서 미리보기 위에 겹쳐 띄울 때) 큰 숫자 칸을 한 줄로 줄이고 그래프 제목 줄을 그래프 안에 씁니다.
    readonly property bool tight: content.height < 440

    // 앞쪽 1초를 더 받아 왼쪽 끝 선이 끊기지 않게 합니다.
    function refresh() {
        const from = timeWindow.viewStart - 1, to = timeWindow.viewEnd
        footPrimary = trace.series(footMode === 1 ? rightFootTarget : leftFootTarget, from, to)
        footSecondary = footMode === 2 ? trace.series(rightFootTarget, from, to) : null
        areaSeries = trace.hasArea ? trace.series(2, from, to) : null
        leftLatest = trace.latest(leftFootTarget)
        rightLatest = trace.latest(rightFootTarget)
        areaLatest = trace.latest(2)
    }
    Connections {
        target: panel.trace
        function onSampled() { panel.refresh() }
        function onAreaChanged() { panel.refresh() }
    }
    Connections {
        target: timeWindow
        function onViewEndChanged() { panel.refresh() }
        function onSpanChanged() { panel.refresh() }
    }
    onFootModeChanged: refresh()
    onLeftFootTargetChanged: refresh()
    Component.onCompleted: refresh()

    function valueText(latest, key) { return RgbText.value(latest, key) }
    function satText(v) { return RgbText.saturation(v) }
    // 겹쳐보기의 포화 한 줄입니다. 포화가 있는 발과 채널만 적습니다.
    function compareSatText() {
        const parts = []
        for (const [name, latest] of [["왼발", leftLatest], ["오른발", rightLatest]]) {
            if (!latest.valid) continue
            for (const [ch, key] of [["R", "satR"], ["G", "satG"], ["B", "satB"]]) {
                if (latest[key] > 0) parts.push(name + " " + ch + " " + satText(latest[key]))
            }
        }
        return parts.length === 0 ? "포화 없음" : "포화 " + parts.join(", ")
    }

    // 왼쪽 경계선. 미리보기 오른쪽 검은 여백에 붙일 때만 그립니다.
    Rectangle {
        visible: !panel.floating
        width: 1
        height: parent.height
        color: Theme.line
    }

    // 채널 숫자 세 칸: 글자, 큰 숫자, 포화 비율
    component ChannelNumbers: RowLayout {
        id: numbers
        property var latest: ({ valid: false })
        spacing: 6
        Repeater {
            model: [["R", "r", "satR"], ["G", "g", "satG"], ["B", "b", "satB"]]
            delegate: ColumnLayout {
                id: cell
                required property var modelData
                readonly property real sat: numbers.latest.valid ? numbers.latest[cell.modelData[2]] : 0
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 0
                Label { text: cell.modelData[0]; font.pixelSize: 11; color: Theme.textMuted }
                Label {
                    text: RgbText.value(numbers.latest, cell.modelData[1])
                    font.pixelSize: 24
                    font.weight: Font.Bold
                    font.features: { "tnum": 1 }
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: "포화 " + RgbText.saturation(cell.sat)
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    font.features: { "tnum": 1 }
                    color: cell.sat > 0 ? Theme.settling : Theme.textMuted
                }
            }
        }
    }

    // 겹쳐보기의 발 이름 앞 선 모양 범례: 왼발 실선, 오른발 점선
    component LineLegend: Canvas {
        property bool dashed: false
        implicitWidth: 14
        implicitHeight: 4
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Theme.text
            ctx.lineWidth = 2
            ctx.setLineDash(dashed ? [2, 1] : [])
            ctx.beginPath(); ctx.moveTo(0, 2); ctx.lineTo(width, 2); ctx.stroke()
        }
    }

    // 한 줄 숫자: R, G, B 평균과 세 채널 중 가장 큰 포화 비율. legend가 null이 아니면 앞에 선 모양 범례를 둡니다.
    component NumbersLine: RowLayout {
        id: numbersLine
        property var latest: ({ valid: false })
        property var legend: null
        // 앞에 쓰는 발 이름입니다. 관찰 영역이 있을 때 아래 관찰 영역 값과 헷갈리지 않게 어느 발 상자인지 적습니다.
        property string name: ""
        readonly property real sat: latest.valid ? Math.max(latest.satR, latest.satG, latest.satB) : 0
        spacing: 10
        LineLegend { visible: numbersLine.legend !== null; dashed: numbersLine.legend === true }
        Label { visible: numbersLine.name !== ""; text: numbersLine.name; font.pixelSize: 12; color: Theme.textMuted }
        Repeater {
            model: [["R", "r"], ["G", "g"], ["B", "b"]]
            delegate: Row {
                id: pair
                required property var modelData
                spacing: 3
                Label { anchors.baseline: value.baseline; text: pair.modelData[0]; font.pixelSize: 11; color: Theme.textMuted }
                Label {
                    id: value
                    text: RgbText.value(numbersLine.latest, pair.modelData[1])
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.text
                }
            }
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: "포화 " + RgbText.saturation(numbersLine.sat)
            elide: Text.ElideLeft
            font.pixelSize: 11
            font.features: { "tnum": 1 }
            color: numbersLine.sat > 0 ? Theme.settling : Theme.textMuted
        }
    }

    // 그래프 세 줄(R, G, B). 마지막 줄 아래에만 초 숫자를 씁니다.
    component ChannelCharts: ColumnLayout {
        id: charts
        required property var viewport
        property var primary: null
        property var secondary: null
        // true이면 채널 제목 줄을 빼고 그래프 왼쪽 위에 채널 글자를 씁니다.
        property bool compact: false
        spacing: compact ? 4 : 6
        Repeater {
            model: [["R", "r", Theme.channelR], ["G", "g", Theme.channelG], ["B", "b", Theme.channelB]]
            delegate: ColumnLayout {
                id: track
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2
                Row {
                    visible: !charts.compact
                    spacing: 6
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 10; height: 2; color: track.modelData[2] }
                    Label { text: track.modelData[0]; font.pixelSize: 11; color: Theme.textMuted }
                }
                RgbChart {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 18
                    title: charts.compact ? track.modelData[0] : ""
                    viewport: charts.viewport
                    primary: charts.primary
                    secondary: charts.secondary
                    channel: track.modelData[1]
                    lineColor: track.modelData[2]
                    timeLabels: track.index === 2
                }
            }
        }
    }

    Item {
        id: content
        anchors { fill: parent; leftMargin: 12; rightMargin: 12; topMargin: 14; bottomMargin: 12 }

        readonly property real sectionGap: 12
        // 관찰 영역이 있을 때 발 상자 구역이 차지하는 높이입니다.
        readonly property real compactHeight: footCompact.implicitHeight
        readonly property real bodyHeight: height - historyRow.height - sectionGap

        // 발 상자 구역. 관찰 영역이 생기면 높이를 줄이며 넘치는 부분을 가리고, 펼친 내용과 줄인 내용을 서로 바꿔 보입니다.
        Item {
            id: footSection
            width: parent.width
            height: content.bodyHeight + (content.compactHeight - content.bodyHeight) * panel.areaShown
            clip: true

            ColumnLayout {
                id: footFull
                width: parent.width
                height: content.bodyHeight
                opacity: 1 - panel.areaShown
                visible: opacity > 0
                spacing: 8

                Label {
                    text: "발 상자 평균"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                // 발 상자 안에서 그리는 방법(Shift+끌기)은 화면만 보고 알기 어려워 적어 둡니다(2026-10-27 실기).
                Label {
                    Layout.fillWidth: true
                    text: "미리보기를 누르거나 끌면 관찰 영역, 발 상자 안에서는 Shift+끌기"
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.textMuted
                }
                Segmented {
                    Layout.fillWidth: true
                    implicitHeight: 32
                    fontSize: 12
                    options: ["왼발", "오른발", "겹쳐보기"]
                    currentIndex: panel.footMode
                    onPicked: index => panel.footMode = index
                }
                ChannelNumbers {
                    Layout.fillWidth: true
                    visible: panel.footMode !== 2 && !panel.tight
                    opacity: panel.valueOpacity
                    latest: panel.footLatest
                }
                Repeater {
                    model: !panel.tight ? [] : panel.footMode === 2 ? [["left", false], ["right", true]] : [[panel.footMode === 1 ? "right" : "left", null]]
                    delegate: NumbersLine {
                        required property var modelData
                        Layout.fillWidth: true
                        opacity: panel.valueOpacity
                        latest: modelData[0] === "left" ? panel.leftLatest : panel.rightLatest
                        legend: modelData[1]
                        name: modelData[0] === "left" ? "왼발" : "오른발"
                    }
                }
                // 겹쳐보기: 발마다 한 줄
                GridLayout {
                    Layout.fillWidth: true
                    visible: panel.footMode === 2 && !panel.tight
                    opacity: panel.valueOpacity
                    columns: 4
                    rowSpacing: 2
                    columnSpacing: 6
                    Item { Layout.preferredWidth: 56; implicitHeight: 1 }
                    Repeater {
                        model: ["R", "G", "B"]
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: modelData
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                    }
                    Repeater {
                        model: [["왼발", false], ["오른발", true]]
                        delegate: Row {
                            required property var modelData
                            Layout.row: modelData[1] ? 2 : 1
                            Layout.column: 0
                            Layout.preferredWidth: 56
                            spacing: 6
                            LineLegend { anchors.verticalCenter: parent.verticalCenter; dashed: parent.modelData[1] }
                            Label { text: parent.modelData[0]; font.pixelSize: 12; color: Theme.textMuted }
                        }
                    }
                    Repeater {
                        model: 6
                        delegate: Label {
                            required property int index
                            Layout.row: index < 3 ? 1 : 2
                            Layout.column: index % 3 + 1
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: panel.valueText(index < 3 ? panel.leftLatest : panel.rightLatest, ["r", "g", "b"][index % 3])
                            font.pixelSize: 18
                            font.weight: Font.Bold
                            font.features: { "tnum": 1 }
                            color: Theme.text
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: panel.footMode === 2 && !panel.tight
                    opacity: panel.valueOpacity
                    text: panel.compareSatText()
                    elide: Text.ElideRight
                    font.pixelSize: 11
                    color: text === "포화 없음" ? Theme.textMuted : Theme.settling
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
                ChannelCharts {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    compact: panel.tight
                    viewport: timeWindow
                    primary: panel.footPrimary
                    secondary: panel.footSecondary
                }
            }

            // 관찰 영역이 있을 때의 두 줄: 제목과 작은 선택 버튼, 그리고 R, G, B 평균과 가장 큰 포화 비율
            ColumnLayout {
                id: footCompact
                width: parent.width
                opacity: panel.areaShown
                visible: opacity > 0
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: "발 상자 평균"
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }
                    Segmented {
                        implicitWidth: 140
                        implicitHeight: 24
                        fontSize: 11
                        options: ["왼발", "오른발", "겹쳐보기"]
                        currentIndex: panel.footMode
                        onPicked: index => panel.footMode = index
                    }
                }
                Repeater {
                    model: panel.footMode === 2 ? [["left", false], ["right", true]] : [[panel.footMode === 1 ? "right" : "left", null]]
                    delegate: NumbersLine {
                        required property var modelData
                        Layout.fillWidth: true
                        opacity: panel.valueOpacity
                        latest: modelData[0] === "left" ? panel.leftLatest : panel.rightLatest
                        legend: modelData[1]
                        name: modelData[0] === "left" ? "왼발" : "오른발"
                    }
                }
            }
        }

        // 관찰 영역 구역: 좌표, 지우기 버튼, 숫자, 그래프
        ColumnLayout {
            id: areaSection
            y: footSection.height + content.sectionGap
            width: parent.width
            height: Math.max(0, content.bodyHeight - footSection.height - content.sectionGap)
            opacity: panel.areaShown
            visible: opacity > 0
            spacing: 8

            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
            RowLayout {
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: "관찰 영역"; font.pixelSize: 14; font.weight: Font.DemiBold; color: Theme.text }
                    Label {
                        Layout.fillWidth: true
                        // 프레임 화소 좌표입니다(녹화 영상과 같은 좌표, 화면에서 뒤집어 그려도 그대로).
                        text: "x " + panel.trace.area.x + ", y " + panel.trace.area.y + ", "
                              + panel.trace.area.width + " x " + panel.trace.area.height
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                        color: Theme.textMuted
                    }
                }
                // 관찰 영역 지우기. Esc와 같습니다
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    width: 28
                    height: 28
                    radius: 8
                    color: clearMouse.containsMouse ? Theme.line : Theme.control
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
                        id: clearMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.trace.clearArea()
                    }
                }
            }
            ChannelNumbers {
                Layout.fillWidth: true
                visible: !panel.tight
                opacity: panel.valueOpacity
                latest: panel.areaLatest
            }
            NumbersLine {
                Layout.fillWidth: true
                visible: panel.tight
                opacity: panel.valueOpacity
                latest: panel.areaLatest
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
            ChannelCharts {
                Layout.fillWidth: true
                Layout.fillHeight: true
                compact: panel.tight
                viewport: timeWindow
                primary: panel.areaSeries
            }
        }

        // 맨 아래: 전체 기록 안에서 보이는 구간의 위치. 실시간이 아니면 지금으로 버튼을 보입니다.
        RowLayout {
            id: historyRow
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 22
            spacing: 8
            Item { Layout.preferredWidth: 26; implicitHeight: 1 }
            Rectangle {
                id: historyBar
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.control
                readonly property real total: Math.max(panel.trace.endS, panel.trace.startS + timeWindow.span) - panel.trace.startS
                readonly property real from: Math.max(0, Math.min(1, (timeWindow.viewStart - panel.trace.startS) / total))
                readonly property real to: Math.max(0, Math.min(1, (timeWindow.viewEnd - panel.trace.startS) / total))
                Rectangle {
                    x: historyBar.from * historyBar.width
                    width: Math.max(4, (historyBar.to - historyBar.from) * historyBar.width)
                    height: parent.height
                    radius: 2
                    color: Theme.textMuted
                }
                // 막대를 누르거나 끌면 그 자리를 가운데로 하는 구간으로 옮깁니다.
                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -8
                    anchors.bottomMargin: -8
                    cursorShape: Qt.PointingHandCursor
                    function jump(x) {
                        const at = panel.trace.startS + Math.max(0, Math.min(1, x / width)) * historyBar.total
                        timeWindow.panTo(at + timeWindow.span / 2)
                    }
                    onPressed: mouse => jump(mouse.x)
                    onPositionChanged: mouse => jump(mouse.x)
                }
            }
            Label {
                visible: timeWindow.live
                text: panel.trace.stale ? "멈춤" : "실시간"
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Rectangle {
                visible: !timeWindow.live
                implicitWidth: nowLabel.implicitWidth + 16
                implicitHeight: 22
                radius: 6
                color: Theme.controlActive
                Label {
                    id: nowLabel
                    anchors.centerIn: parent
                    text: "지금으로"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: Theme.background
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: timeWindow.live = true
                }
            }
        }
    }
}
