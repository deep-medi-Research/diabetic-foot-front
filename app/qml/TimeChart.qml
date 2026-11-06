import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 촬영 30초를 가로축으로 둔 그래프입니다: 맥박 파형, PPG, 관류지수 변화, 부위별 관류지수 변화, 좌우 맥박 상관, 구간별 단계 점수,
// 구간별 판정 점수(있을 때), 프레임 간격.
// 모든 줄이 같은 시간 구간을 보이고, 세로선은 되감기 위치입니다. 그래프를 누르거나 끌면 그 시각으로 옮깁니다.
// 휠로 가로를 확대하고, Shift를 누른 채 끌어 보는 구간을 옮기고, 두 번 누르면 전체로 돌아갑니다. 값은 ResultView::chart에서 옵니다.
ColumnLayout {
    id: chart
    required property var values        // ResultView::chart (Item의 data와 다른 이름)
    required property real cursorS      // 되감기 위치(초)
    property color leftColor: "#5ac8fa"
    property color rightColor: "#bf5af2"
    signal seek(real seconds)

    readonly property real duration: Math.max(0.001, values.duration || 0)
    // 보이는 구간(초). 확대와 이동이 이 두 값을 바꿉니다.
    property real viewStart: 0
    property real viewEnd: duration
    onDurationChanged: { viewStart = 0; viewEnd = duration }

    readonly property real leftGutter: 64
    // 마우스를 올린 시각(초). 그래프 밖이면 -1이고, 그때 읽음 값은 재생 위치의 값입니다.
    property real hoverS: -1
    readonly property real readS: hoverS >= 0 ? hoverS : cursorS
    // 범례를 눌러 발별 선을 숨깁니다.
    property bool showLeft: true
    property bool showRight: true
    spacing: 0

    // times(구간 가운데 시각)나 fps 간격 값에서 시각 t에 가장 가까운 값의 위치입니다.
    function nearestIndex(values, times, fps, t) {
        if (!values || values.length === 0) return -1
        if (!times) return Math.max(0, Math.min(values.length - 1, Math.round(t * fps)))
        let best = 0
        for (let i = 1; i < times.length; ++i) if (Math.abs(times[i] - t) < Math.abs(times[best] - t)) best = i
        return best
    }
    // 반올림해서 0이 되는 음수는 "-0.000" 대신 "0.000"으로 씁니다.
    function fmt(v, digits) {
        if (v === null || v === undefined || isNaN(v)) return "-"
        const text = v.toFixed(digits)
        return Number(text) === 0 ? (0).toFixed(digits) : text
    }

    // PPG 측정 오류 값의 색입니다. 손가락 빠짐은 빨강, 신호 나쁨은 주황, 센서 연결 안 됨과 그 밖의 값은 회색입니다.
    function errorColor(code) { return code === "f" ? Theme.mismatch : code === "s" ? Theme.settling : Theme.textMuted }
    // 시각 t가 든 PPG 측정 오류 구간입니다. 없으면 undefined입니다.
    function ppgErrorAt(t) { return (values.ppgErrors || []).find(e => t >= e.start && t <= e.end) }

    function xOf(t, w) { return leftGutter + (t - viewStart) / (viewEnd - viewStart) * (w - leftGutter) }
    function tOf(x, w) { return viewStart + (x - leftGutter) / (w - leftGutter) * (viewEnd - viewStart) }
    function zoom(factor, centerS) {
        const span = Math.min(duration, Math.max(0.5, (viewEnd - viewStart) * factor))
        let start = centerS - (centerS - viewStart) * span / (viewEnd - viewStart)
        start = Math.max(0, Math.min(duration - span, start))
        viewStart = start
        viewEnd = start + span
    }
    function pan(deltaS) {
        const span = viewEnd - viewStart
        const start = Math.max(0, Math.min(duration - span, viewStart + deltaS))
        viewStart = start
        viewEnd = start + span
    }
    function repaint() {
        wave.requestPaint(); ppgTrack.requestPaint(); pi.requestPaint(); regionTrack.requestPaint(); corr.requestPaint()
        levels.requestPaint(); votes.requestPaint(); frames.requestPaint()
    }

    // 프레임 간격이 주기의 1.2배를 넘은 구간(fps 저하와 누락)을 옅은 주황 띠로 칠합니다. 분석 그래프와 겹치는지 보이려고 여러 줄에 같이 칠합니다.
    function shadeSlow(ctx, track, w) {
        const spans = values.slowSpans || []
        if (spans.length === 0) return
        ctx.fillStyle = Theme.settling
        ctx.globalAlpha = 0.18
        for (const s of spans) {
            const x0 = xOf(s[0], w), x1 = xOf(s[1], w)
            ctx.fillRect(x0, track.plotTop, Math.max(2, x1 - x0), track.plotBottom - track.plotTop)
        }
        ctx.globalAlpha = 1
    }
    // 분석 구간 하나 안의 느린 간격 수와 최저 fps 문장입니다. 고르면 빈 문자열입니다.
    function slowText(counts, fps, i) {
        if (!counts || i < 0 || i >= counts.length || !(counts[i] > 0)) return ""
        return "   느린 간격 " + counts[i] + ", 최저 " + fmt(fps[i], 1) + "fps"
    }
    onHoverSChanged: repaint()
    onShowLeftChanged: repaint()
    onShowRightChanged: repaint()
    onViewStartChanged: repaint()
    onViewEndChanged: repaint()
    onCursorSChanged: repaint()
    onValuesChanged: repaint()

    // 한 줄의 제목, 읽음 값, 범례입니다. 범례를 누르면 그 발의 선을 숨기거나 다시 보입니다.
    component TrackHeader: RowLayout {
        id: header
        property string title
        property string note
        property string readout
        property bool legend: true
        Layout.fillWidth: true
        Layout.leftMargin: chart.leftGutter
        spacing: 12
        Label {
            text: header.title
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.letterSpacing: 1
            color: Theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            text: header.note
            elide: Text.ElideRight
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Item { Layout.fillWidth: true; visible: header.note === "" }
        // 읽음 값: 마우스를 올린 시각, 아니면 재생 위치의 값
        Label {
            visible: text !== ""
            text: header.readout
            font.pixelSize: 12
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
            color: Theme.text
        }
        Repeater {
            model: header.legend ? [["왼발", chart.leftColor, "left"], ["오른발", chart.rightColor, "right"]] : []
            delegate: Item {
                id: item
                required property var modelData
                readonly property bool shown: modelData[2] === "left" ? chart.showLeft : chart.showRight
                implicitWidth: row.implicitWidth
                implicitHeight: row.implicitHeight
                opacity: shown ? 1 : 0.35
                Row {
                    id: row
                    spacing: 4
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 12; height: 3; radius: 1.5; color: item.modelData[1] }
                    Label { text: item.modelData[0]; font.pixelSize: 11; color: Theme.textMuted }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (item.modelData[2] === "left") chart.showLeft = !chart.showLeft
                        else chart.showRight = !chart.showRight
                    }
                }
            }
        }
    }

    // 그래프 한 칸의 공통 바탕: 세로축(눈금 숫자와 단위), 가로축(초 눈금), 되감기 위치, 마우스 조작.
    // 누르거나 끌면 재생 위치를 옮기고, 휠로 확대, Shift를 누른 채 끌거나 가로 휠로 보는 구간을 옮깁니다.
    component Track: Canvas {
        id: track
        property real yMin: -1
        property real yMax: 1
        property var yTicks: []
        property string yFormat: "%"   // "%": 관류지수, "p": 0~1 점수, "n": 정수
        property string yTitle: ""
        property var draw: function(ctx, w, h) {}
        readonly property real plotTop: 6
        readonly property real plotBottom: height - 20   // 아래 20px은 가로축 눈금 숫자
        Layout.fillWidth: true
        renderStrategy: Canvas.Cooperative
        function yOf(v) { return plotTop + (yMax - v) / (yMax - yMin) * (plotBottom - plotTop) }
        function tickText(v) {
            if (yFormat === "%") return v.toFixed(v === 0 ? 0 : v < 0.1 ? 3 : 2)
            if (yFormat === "p") return v.toFixed(1)
            return String(Math.round(v))
        }
        onPaint: {
            const ctx = getContext("2d")
            const w = width, h = height
            ctx.reset()
            ctx.fillStyle = Theme.previewBackground
            ctx.fillRect(chart.leftGutter, plotTop, w - chart.leftGutter, plotBottom - plotTop)
            ctx.font = "11px " + Theme.font
            // 세로축 단위: 왼쪽 끝에 세로로 씁니다.
            if (yTitle !== "") {
                ctx.save()
                ctx.translate(10, (plotTop + plotBottom) / 2)
                ctx.rotate(-Math.PI / 2)
                ctx.fillStyle = Theme.textMuted
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(yTitle, 0, 0)
                ctx.restore()
            }
            // 세로축 눈금
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (const v of yTicks) {
                const y = yOf(v)
                ctx.strokeStyle = Theme.line
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(chart.leftGutter, y); ctx.lineTo(w, y); ctx.stroke()
                ctx.fillStyle = Theme.textMuted
                ctx.fillText(tickText(v), chart.leftGutter - 6, y)
            }
            // 가로축 눈금: 보이는 구간 길이에 맞춰 0.5, 1, 2, 5초 간격
            const span = chart.viewEnd - chart.viewStart
            const step = span <= 4 ? 0.5 : span <= 10 ? 1 : span <= 20 ? 2 : 5
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            for (let t = Math.ceil(chart.viewStart / step - 1e-9) * step; t <= chart.viewEnd + 1e-6; t += step) {
                const x = chart.xOf(t, w)
                ctx.strokeStyle = Theme.line
                ctx.globalAlpha = 0.6
                ctx.beginPath(); ctx.moveTo(x, plotTop); ctx.lineTo(x, plotBottom); ctx.stroke()
                ctx.globalAlpha = 1
                ctx.fillStyle = Theme.textMuted
                ctx.fillText((step < 1 ? t.toFixed(1) : t.toFixed(0)) + "초", Math.min(Math.max(x, chart.leftGutter + 12), w - 14), plotBottom + 4)
            }
            ctx.save()
            ctx.beginPath(); ctx.rect(chart.leftGutter, plotTop, w - chart.leftGutter, plotBottom - plotTop); ctx.clip()
            draw(ctx, w, h)
            // 마우스를 올린 시각(점선)
            if (chart.hoverS >= 0) {
                const hx = chart.xOf(chart.hoverS, w)
                ctx.strokeStyle = Theme.textMuted
                ctx.setLineDash([3, 3])
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(hx, plotTop); ctx.lineTo(hx, plotBottom); ctx.stroke()
                ctx.setLineDash([])
            }
            // 되감기 위치
            const cx = chart.xOf(chart.cursorS, w)
            ctx.strokeStyle = Theme.text
            ctx.globalAlpha = 0.8
            ctx.lineWidth = 1.5
            ctx.beginPath(); ctx.moveTo(cx, plotTop); ctx.lineTo(cx, plotBottom); ctx.stroke()
            ctx.globalAlpha = 1
            ctx.restore()
        }
        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: chart.leftGutter
            hoverEnabled: true
            cursorShape: panning ? Qt.ClosedHandCursor : Qt.SizeHorCursor
            property real pressX: 0
            property real pressStart: 0
            property real pressSpan: 0
            property bool panning: false
            onPressed: mouse => {
                pressX = mouse.x
                pressStart = chart.viewStart
                pressSpan = chart.viewEnd - chart.viewStart
                panning = (mouse.modifiers & Qt.ShiftModifier) !== 0
                if (!panning) chart.seek(chart.tOf(mouse.x + chart.leftGutter, track.width))
            }
            onExited: chart.hoverS = -1
            onPositionChanged: mouse => {
                chart.hoverS = Math.max(0, Math.min(chart.duration, chart.tOf(mouse.x + chart.leftGutter, track.width)))
                if (!pressed) return
                if (!panning) {
                    chart.seek(Math.max(0, Math.min(chart.duration, chart.tOf(mouse.x + chart.leftGutter, track.width))))
                    return
                }
                // 누른 시점의 구간에서 끈 거리만큼 옮깁니다. 구간 폭은 바꾸지 않습니다.
                const perPx = pressSpan / (track.width - chart.leftGutter)
                const start = Math.max(0, Math.min(chart.duration - pressSpan, pressStart - (mouse.x - pressX) * perPx))
                chart.viewStart = start
                chart.viewEnd = start + pressSpan
            }
            onReleased: panning = false
            onDoubleClicked: { chart.viewStart = 0; chart.viewEnd = chart.duration }
            onWheel: wheel => {
                const span = chart.viewEnd - chart.viewStart
                if (wheel.angleDelta.x !== 0 || (wheel.modifiers & Qt.ShiftModifier)) {
                    const d = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : wheel.angleDelta.y
                    chart.pan(-d / 120 * span * 0.1)
                    return
                }
                chart.zoom(wheel.angleDelta.y > 0 ? 0.8 : 1.25, chart.tOf(wheel.x + chart.leftGutter, track.width))
            }
        }
    }

    // 선 하나를 그립니다. values는 times와 같은 길이이거나, times가 없으면 fps 간격입니다.
    function line(ctx, track, w, values, times, fps, color, width, dots) {
        if (!values || values.length === 0) return
        ctx.strokeStyle = color
        ctx.lineWidth = width
        ctx.beginPath()
        let started = false
        // 보이는 구간 밖의 점은 건너뛰고, 화면 1화소에 여러 점이 몰리면 솎아 그립니다.
        const n = values.length
        const tAt = i => times ? times[i] : i / fps
        const step = times ? 1 : Math.max(1, Math.floor(n * (chart.viewEnd - chart.viewStart) / chart.duration / (w - chart.leftGutter)))
        for (let i = 0; i < n; i += step) {
            const t = tAt(i)
            if (t < chart.viewStart - 1 || t > chart.viewEnd + 1) continue
            const v = values[i]
            if (v === null || v === undefined || isNaN(v)) { started = false; continue }
            const x = chart.xOf(t, w), y = track.yOf(v)
            if (!started) { ctx.moveTo(x, y); started = true } else ctx.lineTo(x, y)
        }
        ctx.stroke()
        if (dots) {
            ctx.fillStyle = color
            for (let i = 0; i < n; ++i) {
                const v = values[i]
                if (v === null || v === undefined || isNaN(v)) continue
                ctx.beginPath(); ctx.arc(chart.xOf(tAt(i), w), track.yOf(v), 3, 0, 2 * Math.PI); ctx.fill()
            }
        }
    }

    TrackHeader {
        title: "맥박 파형"
        note: chart.values.waveNote || ""
        legend: !!chart.values.hasWave
        readout: {
            if (!chart.values.hasWave) return ""
            const i = chart.nearestIndex(chart.values.waveLeft, null, chart.values.fps, chart.readS)
            const unit = chart.values.waveScaled ? "%" : ""
            const digits = chart.values.waveScaled ? 3 : 2
            return chart.fmt(chart.readS, 2) + "초   왼 " + chart.fmt(chart.values.waveLeft[i], digits) + unit + "   오 " + chart.fmt(chart.values.waveRight[i], digits) + unit
        }
    }
    Track {
        id: wave
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 3
        Layout.minimumHeight: 70
        // 관류 분석 프로그램이 AC(DC 대비 %)를 저장했으면 그 값의 축, 아니면 -1~1 상대값 축입니다.
        // Item에는 top 같은 FINAL 속성이 있어 그와 다른 이름을 씁니다.
        readonly property bool scaled: !!chart.values.waveScaled
        readonly property real waveTop: scaled ? (chart.values.waveMax || 0.1) : 1
        yMin: -waveTop * 1.08; yMax: waveTop * 1.08
        yTicks: [-waveTop, 0, waveTop]
        yFormat: scaled ? "%" : "n"
        yTitle: scaled ? "AC (%)" : "상대값"
        draw: function(ctx, w, h) {
            if (!chart.values.hasWave) return
            ctx.strokeStyle = Theme.line
            ctx.beginPath(); ctx.moveTo(chart.leftGutter, wave.yOf(0)); ctx.lineTo(w, wave.yOf(0)); ctx.stroke()
            // 가는 선: 조명 보정한 밝기 변화 전체(잡음 포함). 굵은 선: 그중 심박과 같은 박자인 성분(이 폭이 PI입니다).
            if (wave.scaled) {
                ctx.globalAlpha = 0.35
                if (chart.showLeft) chart.line(ctx, wave, w, chart.values.waveAcLeft, null, chart.values.fps, chart.leftColor, 1, false)
                if (chart.showRight) chart.line(ctx, wave, w, chart.values.waveAcRight, null, chart.values.fps, chart.rightColor, 1, false)
                ctx.globalAlpha = 1
            }
            if (chart.showLeft) chart.line(ctx, wave, w, chart.values.waveLeft, null, chart.values.fps, chart.leftColor, wave.scaled ? 2 : 1.5, false)
            if (chart.showRight) chart.line(ctx, wave, w, chart.values.waveRight, null, chart.values.fps, chart.rightColor, wave.scaled ? 2 : 1.5, false)
        }
        Label {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: chart.leftGutter / 2
            visible: !chart.values.hasWave
            text: chart.values.waveMissing || "파형 데이터가 없습니다"
            font.pixelSize: 12
            color: Theme.textMuted
        }
    }

    // PPG 센서: 촬영 때 PPG를 연결한 촬영에만 보입니다. 영상 맥박 파형과 박자를 견줄 수 있게 바로 아래에 둡니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: !!chart.values.hasPpg
        title: "PPG 센서"
        note: chart.values.ppgNote || ""
        legend: false
        readout: {
            if (!chart.values.hasPpg) return ""
            const i = chart.nearestIndex(chart.values.ppgValues, chart.values.ppgTimes, 0, chart.readS)
            const error = chart.ppgErrorAt(chart.values.ppgTimes[i])
            return chart.fmt(chart.values.ppgTimes[i], 2) + "초   " + chart.fmt(chart.values.ppgValues[i], 0)
                   + (error ? "   " + error.name : "")
        }
    }
    // RGB 녹화 구간 안 측정 오류를 값마다 시간과 비율로 적습니다. 네모의 색은 아래 PPG 줄에 칠한 구간의 색입니다.
    Flow {
        Layout.fillWidth: true
        Layout.leftMargin: chart.leftGutter
        Layout.topMargin: 2
        visible: !!chart.values.hasPpg
        spacing: 12
        Label {
            text: (chart.values.ppgErrorShares || []).length > 0 ? "녹화 구간 측정 오류" : "녹화 구간 측정 오류 없음"
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Repeater {
            model: chart.values.ppgErrorShares || []
            delegate: Row {
                id: share
                required property var modelData
                spacing: 4
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10; height: 10; radius: 2
                    color: chart.errorColor(share.modelData.code)
                }
                Label { text: share.modelData.text; font.pixelSize: 11; font.features: { "tnum": 1 }; color: Theme.text }
            }
        }
    }
    Track {
        id: ppgTrack
        visible: !!chart.values.hasPpg
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 2
        Layout.minimumHeight: 60
        yMin: chart.values.ppgMin || 0
        yMax: chart.values.ppgMax || 1
        yTicks: [chart.values.ppgMin || 0, ((chart.values.ppgMin || 0) + (chart.values.ppgMax || 1)) / 2, chart.values.ppgMax || 1]
        yFormat: "n"
        yTitle: "원시값"
        draw: function(ctx, w, h) {
            if (!chart.values.hasPpg) return
            // 측정 오류 구간을 값의 색으로 옅게 칠합니다. 측정 데이터 하나(약 48ms)만 오류여도 보이도록 폭을 1화소 이상으로 둡니다.
            ctx.globalAlpha = 0.3
            for (const e of (chart.values.ppgErrors || [])) {
                const x0 = chart.xOf(e.start, w)
                ctx.fillStyle = chart.errorColor(e.code)
                ctx.fillRect(x0, ppgTrack.plotTop, Math.max(1, chart.xOf(e.end, w) - x0), ppgTrack.plotBottom - ppgTrack.plotTop)
            }
            ctx.globalAlpha = 1
            chart.line(ctx, ppgTrack, w, chart.values.ppgValues, chart.values.ppgTimes, 0, "#ffd60a", 1.5, false)
        }
    }

    TrackHeader {
        Layout.topMargin: 12
        title: "관류지수 변화"
        note: chart.values.piNote || ""
        legend: !!chart.values.hasPi
        readout: {
            if (!chart.values.hasPi) return ""
            const i = chart.nearestIndex(chart.values.piLeft, chart.values.piTimes, 0, chart.readS)
            const c = chart.values.piTimes[i], half = (chart.values.voteWindow || 10) / 2
            return chart.fmt(c - half, 0) + "~" + chart.fmt(c + half, 0) + "초   왼 " + chart.fmt(chart.values.piLeft[i], 3) + "%   오 " + chart.fmt(chart.values.piRight[i], 3) + "%"
                   + chart.slowText(chart.values.piSlow, chart.values.piMinFps, i)
        }
    }
    Track {
        id: pi
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 2
        Layout.minimumHeight: 60
        yMin: 0
        yMax: chart.values.piMax || 0.1
        yTicks: [0, (chart.values.piMax || 0.1) / 2, chart.values.piMax || 0.1]
        yTitle: "PI (%)"
        draw: function(ctx, w, h) {
            if (!chart.values.hasPi) return
            // 읽는 시각이 속한 10초 계산 구간을 띠로 칠합니다. 점 하나가 이 구간 전체의 값입니다.
            const i = chart.nearestIndex(chart.values.piLeft, chart.values.piTimes, 0, chart.readS)
            const c = chart.values.piTimes[i], half = (chart.values.voteWindow || 10) / 2
            ctx.fillStyle = Theme.text
            ctx.globalAlpha = 0.06
            ctx.fillRect(chart.xOf(c - half, w), pi.plotTop, chart.xOf(c + half, w) - chart.xOf(c - half, w), pi.plotBottom - pi.plotTop)
            ctx.globalAlpha = 1
            chart.shadeSlow(ctx, pi, w)
            if (chart.showLeft) chart.line(ctx, pi, w, chart.values.piLeft, chart.values.piTimes, 0, chart.leftColor, 2, true)
            if (chart.showRight) chart.line(ctx, pi, w, chart.values.piRight, chart.values.piTimes, 0, chart.rightColor, 2, true)
        }
        Label {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: chart.leftGutter / 2
            visible: !chart.values.hasPi
            text: "구간별 관류지수가 없습니다"
            font.pixelSize: 12
            color: Theme.textMuted
        }
    }

    // 부위별 관류지수입니다. 발가락, 앞발, 발 중간, 뒤꿈치를 네 칸에 나란히 그리고, 세로축은 네 칸이 같습니다.
    // 칸마다 가로축은 다른 줄과 같은 보는 구간입니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: chart.values.hasRegions === true
        title: "부위별 관류지수 변화"
        note: "네 칸의 세로축이 같음"
        legend: false
        readout: {
            if (!chart.values.hasRegions) return ""
            const i = chart.nearestIndex(chart.values.piLeft, chart.values.piTimes, 0, chart.readS)
            const c = chart.values.piTimes[i], half = (chart.values.voteWindow || 10) / 2
            return chart.fmt(c - half, 0) + "~" + chart.fmt(c + half, 0) + "초"
        }
    }
    Canvas {
        id: regionTrack
        visible: chart.values.hasRegions === true
        Layout.topMargin: 4
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: 2
        Layout.minimumHeight: 70
        renderStrategy: Canvas.Cooperative
        readonly property real plotTop: 18
        readonly property real plotBottom: height - 18
        readonly property real gap: 24
        readonly property real panelWidth: (width - chart.leftGutter - 3 * gap) / 4
        function panelX(k) { return chart.leftGutter + k * (panelWidth + gap) }
        function xIn(k, t) { return panelX(k) + (t - chart.viewStart) / (chart.viewEnd - chart.viewStart) * panelWidth }
        function yOf(v) { return plotTop + (1 - v / (chart.values.regionMax || 0.1)) * (plotBottom - plotTop) }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!chart.values.hasRegions) return
            const top = chart.values.regionMax || 0.1
            ctx.font = "11px " + Theme.font
            // 세로축 단위와 눈금은 첫 칸 왼쪽에만 씁니다.
            ctx.save()
            ctx.translate(10, (plotTop + plotBottom) / 2)
            ctx.rotate(-Math.PI / 2)
            ctx.fillStyle = Theme.textMuted
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            ctx.fillText("PI (%)", 0, 0)
            ctx.restore()
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (const v of [0, top / 2, top]) {
                ctx.fillStyle = Theme.textMuted
                ctx.fillText(v.toFixed(v === 0 ? 0 : v < 0.1 ? 3 : 2), chart.leftGutter - 6, yOf(v))
            }
            const regions = chart.values.regions
            const times = chart.values.piTimes
            for (let k = 0; k < regions.length; ++k) {
                const x0 = panelX(k), x1 = x0 + panelWidth
                ctx.fillStyle = Theme.previewBackground
                ctx.fillRect(x0, plotTop, panelWidth, plotBottom - plotTop)
                ctx.strokeStyle = Theme.line
                ctx.lineWidth = 1
                for (const v of [0, top / 2, top]) { ctx.beginPath(); ctx.moveTo(x0, yOf(v)); ctx.lineTo(x1, yOf(v)); ctx.stroke() }
                ctx.fillStyle = Theme.text
                ctx.font = "600 11.5px " + Theme.font
                ctx.textAlign = "left"
                ctx.textBaseline = "alphabetic"
                ctx.fillText(regions[k].name, x0, plotTop - 5)
                // 가로축: 칸마다 처음, 가운데, 끝 시각
                ctx.font = "10.5px " + Theme.font
                ctx.fillStyle = Theme.textMuted
                ctx.textBaseline = "top"
                const mid = (chart.viewStart + chart.viewEnd) / 2
                for (const [t, align] of [[chart.viewStart, "left"], [mid, "center"], [chart.viewEnd, "right"]]) {
                    ctx.textAlign = align
                    ctx.fillText(t.toFixed(0) + "초", xIn(k, t), plotBottom + 4)
                }
                ctx.save()
                ctx.beginPath(); ctx.rect(x0, plotTop, panelWidth, plotBottom - plotTop); ctx.clip()
                for (const [series, color, shown] of [[regions[k].left, chart.leftColor, chart.showLeft], [regions[k].right, chart.rightColor, chart.showRight]]) {
                    if (!shown) continue
                    ctx.strokeStyle = color
                    ctx.fillStyle = color
                    ctx.lineWidth = 1.6
                    ctx.beginPath()
                    let started = false
                    for (let i = 0; i < series.length; ++i) {
                        const v = series[i]
                        if (v === null || v === undefined || isNaN(v)) { started = false; continue }
                        const x = xIn(k, times[i]), y = yOf(v)
                        if (!started) { ctx.moveTo(x, y); started = true } else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                    for (let i = 0; i < series.length; ++i) {
                        const v = series[i]
                        if (v === null || v === undefined || isNaN(v)) continue
                        ctx.beginPath(); ctx.arc(xIn(k, times[i]), yOf(v), 2.5, 0, 2 * Math.PI); ctx.fill()
                    }
                }
                // 되감기 위치
                ctx.strokeStyle = Theme.text
                ctx.globalAlpha = 0.8
                ctx.lineWidth = 1.5
                const cx = xIn(k, chart.cursorS)
                ctx.beginPath(); ctx.moveTo(cx, plotTop); ctx.lineTo(cx, plotBottom); ctx.stroke()
                ctx.globalAlpha = 1
                ctx.restore()
            }
        }
        // 칸 안을 누르거나 끌면 그 칸의 가로 위치로 재생 위치를 옮깁니다.
        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: chart.leftGutter
            cursorShape: Qt.SizeHorCursor
            function seekAt(x) {
                const px = x + chart.leftGutter
                const k = Math.max(0, Math.min(3, Math.floor((px - chart.leftGutter) / (regionTrack.panelWidth + regionTrack.gap))))
                const f = Math.max(0, Math.min(1, (px - regionTrack.panelX(k)) / regionTrack.panelWidth))
                chart.seek(chart.viewStart + f * (chart.viewEnd - chart.viewStart))
            }
            onPressed: mouse => seekAt(mouse.x)
            onPositionChanged: mouse => { if (pressed) seekAt(mouse.x) }
        }
    }

    // 시간 창별 좌우 맥박 상관입니다. 낮은 구간은 두 발에서 같은 심박을 찾지 못한 구간입니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: chart.values.hasCorr === true
        title: "좌우 맥박 상관"
        note: "구간마다 두 발에서 같은 심박을 찾았는지, 1에 가까울수록 같음"
        legend: false
        readout: {
            if (!chart.values.hasCorr) return ""
            const i = chart.nearestIndex(chart.values.corr, chart.values.corrTimes, 0, chart.readS)
            const c = chart.values.corrTimes[i], half = (chart.values.voteWindow || 10) / 2
            return chart.fmt(c - half, 0) + "~" + chart.fmt(c + half, 0) + "초   " + chart.fmt(chart.values.corr[i], 2)
        }
    }
    Track {
        id: corr
        visible: chart.values.hasCorr === true
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 1.3
        Layout.minimumHeight: 56
        yMin: 0; yMax: 1
        yTicks: [0, 0.5, 1]
        yFormat: "p"
        yTitle: "상관"
        // 좌우 발을 합친 값이라 발 색이 아닌 한 색을 씁니다(프레임 간격과 같은 색).
        readonly property color corrColor: "#e5e5ea"
        draw: function(ctx, w, h) {
            if (!chart.values.hasCorr) return
            chart.shadeSlow(ctx, corr, w)
            chart.line(ctx, corr, w, chart.values.corr, chart.values.corrTimes, 0, corr.corrColor, 1.6, true)
        }
    }

    // 커프 단계 점수 E(10초 구간별)입니다. 구간 평균이 결과 창의 단계 점수입니다. 단계 기준으로 나눈 네 띠에 단계 이름을 적습니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: chart.values.hasLevels === true
        title: "구간별 단계 점수"
        note: "10초 구간마다 E(0~3). 띠는 E가 속한 커프 단계"
        legend: false
        readout: {
            if (!chart.values.hasLevels) return ""
            const i = chart.nearestIndex(chart.values.levels, chart.values.levelTimes, 0, chart.readS)
            const c = chart.values.levelTimes[i]
            return chart.fmt(c - 5, 0) + "~" + chart.fmt(c + 5, 0) + "초   E " + chart.fmt(chart.values.levels[i], 2)
                   + ", " + levels.levelOf(chart.values.levels[i]) + "% 띠"
                   + chart.slowText(chart.values.levelSlow, chart.values.levelMinFps, i)
        }
    }
    Track {
        id: levels
        visible: chart.values.hasLevels === true
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 2.5
        Layout.minimumHeight: 76
        yMin: 0; yMax: 3
        yTicks: [0, 1, 2, 3]
        yFormat: "n"
        yTitle: "E"
        // 커프 단계 점과 선 색입니다.
        readonly property color levelColor: "#6f93e0"
        // E가 몇 번째 기준을 넘었는지로 단계(0, 40, 80, 120%)를 정합니다. 관류 분석 프로그램의 cuff_level과 같은 규칙입니다.
        function levelOf(e) {
            const cuts = chart.values.levelCuts || []
            let k = 0
            while (k < cuts.length && e >= cuts[k]) ++k
            return [0, 40, 80, 120][k]
        }
        draw: function(ctx, w, h) {
            if (!chart.values.hasLevels) return
            chart.shadeSlow(ctx, levels, w)
            // 단계 띠: 기준 사이를 번갈아 옅게 칠하고, 띠 오른쪽 안에 단계 이름을 적습니다.
            const edges = [0].concat(chart.values.levelCuts || []).concat([3])
            ctx.font = "10px " + Theme.font
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (let k = 0; k + 1 < edges.length; ++k) {
                const top = levels.yOf(edges[k + 1]), bottom = levels.yOf(edges[k])
                ctx.fillStyle = Theme.text
                ctx.globalAlpha = k % 2 === 0 ? 0.03 : 0.09
                ctx.fillRect(chart.leftGutter, top, w - chart.leftGutter, bottom - top)
                ctx.globalAlpha = 1
                ctx.fillStyle = Theme.textMuted
                if (bottom - top >= 9) ctx.fillText([0, 40, 80, 120][k] + "%", w - 4, (top + bottom) / 2)
            }
            // 구간은 5초씩 겹치므로 가운데 점과 구간 폭의 가는 선으로 그리고, 읽는 시각의 구간만 굵게 그립니다.
            // 값은 머리글에 읽는 구간 하나만 적습니다.
            const sel = chart.nearestIndex(chart.values.levels, chart.values.levelTimes, 0, chart.readS)
            for (let i = 0; i < chart.values.levels.length; ++i) {
                const t = chart.values.levelTimes[i], y = levels.yOf(chart.values.levels[i])
                ctx.strokeStyle = levels.levelColor
                ctx.globalAlpha = i === sel ? 1 : 0.35
                ctx.lineWidth = i === sel ? 3 : 1.5
                ctx.beginPath(); ctx.moveTo(chart.xOf(t - 5, w), y); ctx.lineTo(chart.xOf(t + 5, w), y); ctx.stroke()
                ctx.globalAlpha = 1
                ctx.fillStyle = levels.levelColor
                ctx.beginPath(); ctx.arc(chart.xOf(t, w), y, i === sel ? 5 : 4, 0, 2 * Math.PI); ctx.fill()
            }
        }
    }

    // 이진 판정 점수입니다. 판정이 있는 결과(2026-10-26 이전 결과 등)에만 보입니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: chart.values.hasVotes === true
        title: "구간별 판정 점수"
        note: chart.values.hasVotes ? "점선이 기준 " + chart.values.threshold.toFixed(2) + ", 기준 이상이면 PAD 의심" : ""
        legend: false
        readout: {
            if (!chart.values.hasVotes) return ""
            const i = chart.nearestIndex(chart.values.votes, chart.values.voteTimes, 0, chart.readS)
            const c = chart.values.voteTimes[i], half = (chart.values.voteWindow || 10) / 2
            return chart.fmt(c - half, 0) + "~" + chart.fmt(c + half, 0) + "초   " + chart.fmt(chart.values.votes[i], 2)
                   + chart.slowText(chart.values.voteSlow, chart.values.voteMinFps, i)
        }
    }
    Track {
        id: votes
        visible: chart.values.hasVotes === true
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 2
        Layout.minimumHeight: 60
        yMin: 0; yMax: 1.05
        yTicks: [0, 0.5, 1]
        yFormat: "p"
        yTitle: "점수"
        draw: function(ctx, w, h) {
            chart.shadeSlow(ctx, votes, w)
            if (!chart.values.hasVotes) return
            const y = votes.yOf(chart.values.threshold)
            ctx.strokeStyle = Theme.textMuted
            ctx.setLineDash([4, 4])
            ctx.beginPath(); ctx.moveTo(chart.leftGutter, y); ctx.lineTo(w, y); ctx.stroke()
            ctx.setLineDash([])
            // 점수 하나는 10초 구간 하나의 값입니다. 구간은 5초씩 겹치므로 막대 대신 가운데 점과 구간 폭의 가는 선으로 그리고,
            // 읽는 시각의 구간만 굵게 그립니다. 기준 이상은 판정 색(빨강), 미만은 초록입니다. 값은 머리글에 읽는 구간 하나만 적습니다.
            const half = (chart.values.voteWindow || 10) / 2
            const sel = chart.nearestIndex(chart.values.votes, chart.values.voteTimes, 0, chart.readS)
            for (let i = 0; i < chart.values.votes.length; ++i) {
                const t = chart.values.voteTimes[i], p = chart.values.votes[i]
                const color = p >= chart.values.threshold ? Theme.recording : "#30d158"
                const y = votes.yOf(p)
                ctx.strokeStyle = color
                ctx.globalAlpha = i === sel ? 1 : 0.35
                ctx.lineWidth = i === sel ? 3 : 1.5
                ctx.beginPath(); ctx.moveTo(chart.xOf(t - half, w), y); ctx.lineTo(chart.xOf(t + half, w), y); ctx.stroke()
                ctx.globalAlpha = 1
                ctx.fillStyle = color
                ctx.beginPath(); ctx.arc(chart.xOf(t, w), y, i === sel ? 5 : 4, 0, 2 * Math.PI); ctx.fill()
            }
        }
        Label {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: chart.leftGutter / 2
            visible: !chart.values.hasVotes
            text: "판정 점수가 없습니다"
            font.pixelSize: 12
            color: Theme.textMuted
        }
    }

    // 녹화 프레임 간격(왼발 RGB, 좌우 같음)입니다. 관류 분석 프로그램은 모든 프레임을 1/fps 간격으로 보므로, 간격이 늘어난 구간은
    // 분석 시간축이 실제와 어긋납니다. 가로축은 다른 줄과 같은 "프레임 번호 / fps"입니다.
    TrackHeader {
        Layout.topMargin: 12
        visible: chart.values.hasFrames === true
        title: "프레임 간격"
        note: "왼발 RGB(좌우 같음). 점선은 1.2배와 1.5배, 주황 띠는 1.2배를 넘은 구간"
        legend: false
        readout: {
            if (!chart.values.hasFrames) return ""
            const n = chart.values.frameIntervals.length
            const i = Math.max(0, Math.min(n - 1, Math.round(chart.readS * (chart.values.fps || 30)) - 1))
            const ms = chart.values.frameIntervals[i]
            return chart.fmt(chart.values.frameTimes[i], 2) + "초   " + chart.fmt(ms, 1) + "ms (" + chart.fmt(1000 / ms, 1) + "fps)"
        }
    }
    Track {
        id: frames
        visible: chart.values.hasFrames === true
        Layout.topMargin: 4
        Layout.fillHeight: true
        Layout.preferredHeight: 1.5
        Layout.minimumHeight: 50
        yMin: 20
        yMax: chart.values.frameMax || 55
        yTicks: [chart.values.framePeriod || 33.3, 50]
        yFormat: "n"
        yTitle: "ms"
        // 간격 선 색입니다. 좌우 발이 같은 값이라 발 색이 아닌 한 색을 씁니다.
        readonly property color intervalColor: "#e5e5ea"
        draw: function(ctx, w, h) {
            if (!chart.values.hasFrames) return
            chart.shadeSlow(ctx, frames, w)
            const period = chart.values.framePeriod || 33.333
            ctx.strokeStyle = Theme.textMuted
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])
            for (const k of [1.2, 1.5]) {
                const y = frames.yOf(period * k)
                ctx.beginPath(); ctx.moveTo(chart.leftGutter, y); ctx.lineTo(w, y); ctx.stroke()
            }
            ctx.setLineDash([])
            // 세로축보다 긴 간격(큰 누락)은 위 끝에 붙여 그립니다.
            const top = frames.yMax
            const clipped = chart.values.frameIntervals.map(v => Math.min(v, top))
            chart.line(ctx, frames, w, clipped, chart.values.frameTimes, 0, frames.intervalColor, 1, false)
        }
    }

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: chart.leftGutter
        Layout.topMargin: 2
        text: (chart.viewEnd - chart.viewStart < chart.duration - 0.01 ? "보는 구간 " + chart.viewStart.toFixed(1) + "~" + chart.viewEnd.toFixed(1) + "초.  " : "")
              + "끌어서 재생 위치 이동, 휠로 확대, Shift+끌기로 구간 이동, 두 번 눌러 전체"
        font.pixelSize: 11
        color: Theme.textMuted
    }
}
