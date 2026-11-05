import QtQuick
import HwLab

// RGB 패널의 채널 그래프 한 칸입니다. 가로축은 미리보기 시작부터의 초, 세로축은 채널 평균(0~255)입니다.
// 같은 패널의 그래프는 시간 창(viewport) 하나를 같이 써서, 한 그래프를 끌거나 휠을 굴리면 모두 같이 움직입니다.
// 겹쳐보기에서는 둘째 계열을 점선으로 그리고 오른쪽 세로축을 그 계열의 눈금으로 씁니다.
Canvas {
    id: chart
    // RgbPanel의 시간 창입니다. viewStart, viewEnd, span, panTo, zoom을 씁니다
    required property var viewport
    // RgbTrace::series의 결과입니다. 비어 있으면 그리지 않습니다
    property var primary: null
    property var secondary: null
    // "r", "g", "b" 중 하나입니다. 포화 배열 이름(satR 등)도 여기서 정합니다
    property string channel: "r"
    property color lineColor: Theme.channelR
    property bool timeLabels: false
    // 비어 있지 않으면 세로축 칸 왼쪽 위에 채널 글자를 씁니다. 패널이 좁아 제목 줄을 뺄 때 씁니다.
    property string title: ""

    readonly property bool dual: secondary !== null
    readonly property real leftGutter: 34
    readonly property real rightGutter: dual ? 34 : 4
    readonly property real plotTop: 3
    readonly property real plotBottom: height - (timeLabels ? 16 : 3)
    readonly property string satKey: "sat" + channel.toUpperCase()

    renderStrategy: Canvas.Cooperative
    onPrimaryChanged: requestPaint()
    onSecondaryChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onTimeLabelsChanged: requestPaint()
    onTitleChanged: requestPaint()
    Connections {
        target: chart.viewport
        function onViewEndChanged() { chart.requestPaint() }
        function onSpanChanged() { chart.requestPaint() }
    }

    function xOf(t) {
        const w = width - leftGutter - rightGutter
        return leftGutter + (t - viewport.viewStart) / (viewport.viewEnd - viewport.viewStart) * w
    }

    // 1, 2, 2.5, 5에 10의 거듭제곱을 곱한 간격 중 raw 이상인 가장 작은 값입니다.
    function niceStep(raw) {
        const base = Math.pow(10, Math.floor(Math.log10(raw)))
        for (const m of [1, 2, 2.5, 5, 10]) if (m * base >= raw) return m * base
        return 10 * base
    }

    // 보이는 구간의 값 범위에 위아래 12%를 더한 세로축입니다. 포화가 있으면 255를 범위에 넣습니다.
    function scaleOf(series) {
        if (!series || series.t.length === 0) return null
        const values = series[channel], sat = series[satKey], times = series.t
        let lo = Infinity, hi = -Infinity, saturated = false
        for (let i = 0; i < times.length; ++i) {
            if (times[i] < viewport.viewStart - 0.2 || times[i] > viewport.viewEnd + 0.2) continue
            lo = Math.min(lo, values[i]); hi = Math.max(hi, values[i])
            if (sat[i] > 0) saturated = true
        }
        if (lo === Infinity) return null
        if (saturated) hi = Math.max(hi, 255)
        const range = Math.max(hi - lo, 1)
        lo -= range * 0.12
        hi += range * 0.12
        // 눈금 3~4개를 목표로 하되, 낮은 그래프에서는 숫자가 겹치지 않게 13화소에 하나 이하로 줄입니다.
        const step = niceStep((hi - lo) / Math.max(1, Math.min(3.5, (plotBottom - plotTop) / 13)))
        const ticks = []
        for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) ticks.push(v)
        return { lo: lo, hi: hi, step: step, ticks: ticks, saturated: saturated }
    }

    function yOf(scale, v) { return plotTop + (scale.hi - v) / (scale.hi - scale.lo) * (plotBottom - plotTop) }

    function tickText(scale, v) { return scale.step < 1 ? v.toFixed(1) : String(Math.round(v)) }

    // 채널 값이 255인 화소가 있던 표본부터 다음 표본까지를 옅은 주황으로 칠합니다.
    function shadeSaturation(ctx, series) {
        const sat = series[satKey], times = series.t
        ctx.fillStyle = Theme.settling
        ctx.globalAlpha = 0.16
        let start = -1
        for (let i = 0; i <= times.length; ++i) {
            const on = i < times.length && sat[i] > 0
            if (on && start < 0) start = i
            if (!on && start >= 0) {
                const x0 = xOf(times[start])
                const x1 = i < times.length ? xOf(times[i]) : xOf(times[i - 1]) + 2
                ctx.fillRect(x0, plotTop, Math.max(2, x1 - x0), plotBottom - plotTop)
                start = -1
            }
        }
        ctx.globalAlpha = 1
    }

    function drawLine(ctx, series, scale, dashed) {
        const values = series[channel], times = series.t
        ctx.strokeStyle = lineColor
        ctx.lineWidth = 1.5
        ctx.setLineDash(dashed ? [4, 2] : [])
        ctx.beginPath()
        for (let i = 0; i < times.length; ++i) {
            const x = xOf(times[i]), y = yOf(scale, values[i])
            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
        }
        ctx.stroke()
        ctx.setLineDash([])
    }

    function drawSaturationLine(ctx, scale) {
        if (!scale.saturated) return
        const y = yOf(scale, 255)
        ctx.strokeStyle = Theme.settling
        ctx.lineWidth = 1
        ctx.setLineDash([3, 3])
        ctx.beginPath(); ctx.moveTo(leftGutter, y); ctx.lineTo(width - rightGutter, y); ctx.stroke()
        ctx.setLineDash([])
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const right = width - rightGutter
        const a = scaleOf(primary)
        const b = dual ? scaleOf(secondary) : null
        ctx.font = "10px " + Theme.font
        ctx.lineWidth = 1

        // 세로축: 왼쪽은 첫 계열, 오른쪽은 둘째 계열의 눈금입니다. 가로 격자선은 첫 계열(없으면 둘째) 눈금에 긋습니다.
        const grid = a || b
        if (grid) {
            ctx.strokeStyle = Theme.line
            for (const v of grid.ticks) {
                const y = Math.round(yOf(grid, v)) + 0.5
                ctx.beginPath(); ctx.moveTo(leftGutter, y); ctx.lineTo(right, y); ctx.stroke()
            }
        }
        ctx.fillStyle = Theme.textMuted
        ctx.textBaseline = "middle"
        if (a) {
            ctx.textAlign = "right"
            for (const v of a.ticks) ctx.fillText(tickText(a, v), leftGutter - 5, yOf(a, v))
        }
        if (b) {
            ctx.textAlign = "left"
            for (const v of b.ticks) ctx.fillText(tickText(b, v), right + 5, yOf(b, v))
        }
        if (title !== "") {
            ctx.fillStyle = lineColor
            ctx.textAlign = "left"
            ctx.textBaseline = "top"
            ctx.fillText(title, 0, plotTop)
        }

        // 가로축: 보이는 길이에 맞춘 간격의 정수 초입니다. 숫자는 구역의 마지막 그래프 아래에만 씁니다.
        const span = viewport.viewEnd - viewport.viewStart
        const step = span <= 6 ? 1 : span <= 14 ? 2 : span <= 30 ? 5 : 10
        ctx.textAlign = "center"
        ctx.textBaseline = "top"
        for (let t = Math.ceil(viewport.viewStart / step) * step; t <= viewport.viewEnd + 1e-6; t += step) {
            const x = Math.round(xOf(t)) + 0.5
            ctx.strokeStyle = Theme.line
            ctx.beginPath(); ctx.moveTo(x, plotTop); ctx.lineTo(x, plotBottom); ctx.stroke()
            if (timeLabels && t >= 0) {
                ctx.fillStyle = Theme.textMuted
                ctx.fillText(String(Math.round(t)), Math.min(Math.max(x, leftGutter + 6), right - 6), plotBottom + 4)
            }
        }

        ctx.save()
        ctx.beginPath(); ctx.rect(leftGutter, plotTop, right - leftGutter, plotBottom - plotTop); ctx.clip()
        if (a) shadeSaturation(ctx, primary)
        if (b) shadeSaturation(ctx, secondary)
        if (a) drawSaturationLine(ctx, a)
        if (b) drawSaturationLine(ctx, b)
        if (a) drawLine(ctx, primary, a, false)
        if (b) drawLine(ctx, secondary, b, true)
        ctx.restore()
    }

    // 가로로 끌거나 휠을 굴리면 시간 창을 옮기고, Ctrl을 누른 채 휠을 굴리면 보이는 길이를 5~60초에서 바꿉니다.
    MouseArea {
        anchors.fill: parent
        anchors.leftMargin: chart.leftGutter
        anchors.rightMargin: chart.rightGutter
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real pressX: 0
        property real pressEnd: 0
        onPressed: mouse => {
            pressX = mouse.x
            pressEnd = chart.viewport.viewEnd
        }
        onPositionChanged: mouse => {
            const perPx = (chart.viewport.viewEnd - chart.viewport.viewStart) / Math.max(1, width)
            chart.viewport.panTo(pressEnd - (mouse.x - pressX) * perPx)
        }
        onWheel: wheel => {
            const d = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
            if (wheel.modifiers & Qt.ControlModifier) {
                chart.viewport.zoom(d > 0 ? 0.8 : 1.25)
                return
            }
            // 위로 굴리면 과거로 갑니다. 한 칸(120)에 보이는 길이의 10%입니다.
            const span = chart.viewport.viewEnd - chart.viewport.viewStart
            chart.viewport.panTo(chart.viewport.viewEnd - d / 120 * span * 0.1)
        }
    }
}
