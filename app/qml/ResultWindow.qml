import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 관류 분석 결과 창입니다. 커프 단계와 관류지수를 보이고, 되감기 막대 하나로 RGB 좌우, 열화상, 관류 지도를 같은 시각으로 옮깁니다.
ApplicationWindow {
    id: win
    width: 1440
    height: 860
    title: "관류 분석 결과  " + result.shotName
    color: Theme.background
    font.family: Theme.font

    // ViewModel입니다. ResultView가 만들며, 이 창의 모든 상태는 여기서 옵니다.
    required property ResultView result

    // v8 결과(녹화 전체 요약 그림이 있는 결과)는 세 열 배치입니다: 왼쪽 숫자, 가운데 재생 시점을 따르는 영상과 그래프, 오른쪽 요약 그림.
    readonly property bool figureColumn: result.summaryFigures.length > 0

    // 이진 판정 색입니다. 색은 판정 상태에만 씁니다. 커프 단계는 좋고 나쁨을 정하지 않으므로 기본 글자색입니다.
    readonly property color padColor: Theme.recording
    readonly property color normalColor: "#30d158"
    readonly property color verdictColor: result.verdict === 2 ? padColor
                                        : result.verdict === 1 ? normalColor : Theme.textMuted

    // 재생: 실제 시간으로 장 번호를 정해 넘깁니다. 디코딩이 늦으면 중간 장을 건너뛰고 시간은 맞춥니다.
    property bool playing: false
    property real playStartMs: 0
    property int playStartPos: 0
    readonly property real fps: win.result.chart.fps || 30
    function togglePlay() {
        if (win.result.frameCount <= 1) return
        if (!playing && win.result.position >= win.result.frameCount - 1) win.result.setPosition(0)
        playStartMs = Date.now()
        playStartPos = win.result.position
        playing = !playing
    }
    function seekFrame(frame) {
        win.result.setPosition(frame)
        // 재생 중에 옮기면 그 위치부터 이어서 재생합니다.
        playStartMs = Date.now()
        playStartPos = win.result.position
    }
    Timer {
        interval: 33
        repeat: true
        running: win.playing && win.visible
        onTriggered: {
            const next = win.playStartPos + Math.floor((Date.now() - win.playStartMs) / 1000 * win.fps)
            if (next >= win.result.frameCount - 1) {
                win.result.setPosition(win.result.frameCount - 1)
                win.playing = false
            } else {
                win.result.setPosition(next)
            }
        }
    }
    onClosing: playing = false
    Shortcut { sequence: "Space"; onActivated: win.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: win.seekFrame(win.result.position - 1) }
    Shortcut { sequence: "Right"; onActivated: win.seekFrame(win.result.position + 1) }

    component Caption: Label {
        font.pixelSize: 12
        font.weight: Font.DemiBold
        font.letterSpacing: 1
        color: Theme.textMuted
    }
    component Key: Label {
        font.pixelSize: 13
        color: Theme.textMuted
    }
    component Value: Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignRight
        font.pixelSize: 15
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 }
        color: Theme.text
        elide: Text.ElideRight
    }
    component Rule: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 18
        Layout.bottomMargin: 18
        implicitHeight: 1
        color: Theme.line
    }
    // 폴더 경로 한 줄: 위에 이름, 아래에 경로. 경로 자체를 누르면 폴더를 엽니다.
    component FolderLink: ColumnLayout {
        id: link
        property string name
        property string path
        signal activated()
        Layout.fillWidth: true
        spacing: 2
        Label {
            text: link.name
            font.pixelSize: 11
            color: Theme.textMuted
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: pathRow.implicitHeight
            RowLayout {
                id: pathRow
                anchors.fill: parent
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: link.path
                    elide: Text.ElideMiddle
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                Label {
                    text: "›"
                    font.pixelSize: 16
                    color: Theme.textMuted
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: link.activated()
            }
        }
    }
    // 커프 단계 계기: 네 칸의 반원에서 추정 단계 칸만 칠하고 가운데에 단계를 크게 씁니다.
    // 칸은 v9가 커프 상태 0, V, D, O(폐색)이고 그 전 판이 0, 40, 80, 120%입니다(ResultView::cuffGaugeLabels).
    component CuffGauge: Canvas {
        id: gauge
        property int level: -1   // ResultView::cuffLevelIndex
        property var labels: []
        property string value: "-"
        property string caption: ""
        implicitWidth: 300
        implicitHeight: 158
        onLevelChanged: requestPaint()
        onLabelsChanged: requestPaint()
        onValueChanged: requestPaint()
        onCaptionChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2, cy = 140, r = 110
            ctx.lineWidth = 10
            for (let k = 0; k < 4; ++k) {
                // 왼쪽 첫 칸에서 오른쪽 끝 칸으로 45도씩, 칸 사이를 3도 띄웁니다.
                const a0 = Math.PI + (k * 45 + 2) * Math.PI / 180
                const a1 = Math.PI + (k * 45 + 43) * Math.PI / 180
                ctx.strokeStyle = k === level ? chartView.leftColor : Theme.line
                ctx.beginPath(); ctx.arc(cx, cy, r, a0, a1); ctx.stroke()
                const am = Math.PI + (k * 45 + 22.5) * Math.PI / 180
                ctx.fillStyle = k === level ? chartView.leftColor : Theme.textMuted
                ctx.font = (k === level ? "600 " : "") + "12px " + Theme.font
                ctx.textAlign = "center"
                ctx.textBaseline = "middle"
                ctx.fillText(k < labels.length ? labels[k] : "", cx + (r + 20) * Math.cos(am), cy + (r + 20) * Math.sin(am))
            }
            ctx.fillStyle = Theme.text
            // "정상 (경계)"는 56px이면 반원 안쪽(지름 200px)을 넘어 글자를 줄입니다.
            ctx.font = "bold " + (value.length > 4 ? 36 : 56) + "px " + Theme.font
            ctx.textBaseline = "alphabetic"
            ctx.fillText(value, cx, cy - 18)
            ctx.fillStyle = Theme.textMuted
            ctx.font = "12px " + Theme.font
            ctx.fillText(caption, cx, cy + 6)
        }
    }
    // 부위별 관류지수 막대: 가운데 부위 이름을 두고 왼발 막대는 왼쪽으로, 오른발 막대는 오른쪽으로 뻗습니다.
    component PiBars: ColumnLayout {
        id: bars
        property var rows: []
        property real maxValue: 0.1   // 막대 가로 끝 PI(%). Item의 top과 다른 이름입니다
        spacing: 9
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; text: "왼발"; font.pixelSize: 12; font.weight: Font.DemiBold; color: chartView.leftColor }
            Item { Layout.preferredWidth: 64 }
            Label { Layout.fillWidth: true; text: "오른발"; font.pixelSize: 12; font.weight: Font.DemiBold; color: chartView.rightColor }
        }
        Repeater {
            model: bars.rows
            delegate: RowLayout {
                id: barRow
                required property var modelData
                required property int index
                readonly property bool whole: index === 0
                // 숫자 칸(48px)과 간격을 뺀 반쪽 폭이 막대의 가장 긴 길이입니다.
                readonly property real reach: Math.max(10, (bars.width - 64) / 2 - 56)
                function lengthOf(v) { return isNaN(v) ? 0 : Math.max(1, v / bars.maxValue * reach) }
                function textOf(v) { return isNaN(v) ? "-" : v.toFixed(3) }
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6
                    Item { Layout.fillWidth: true }
                    Label { text: barRow.textOf(barRow.modelData.left); font.pixelSize: 13; font.weight: barRow.whole ? Font.Bold : Font.Normal; font.features: { "tnum": 1 }; color: barRow.whole ? Theme.text : Theme.textMuted }
                    Rectangle { implicitWidth: barRow.lengthOf(barRow.modelData.left); implicitHeight: barRow.whole ? 8 : 6; color: chartView.leftColor; opacity: barRow.whole ? 1 : 0.65 }
                }
                Label {
                    Layout.preferredWidth: 64
                    horizontalAlignment: Text.AlignHCenter
                    text: barRow.modelData.name
                    font.pixelSize: 13
                    font.weight: barRow.whole ? Font.Bold : Font.Normal
                    color: barRow.whole ? Theme.text : Theme.textMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: 6
                    Rectangle { implicitWidth: barRow.lengthOf(barRow.modelData.right); implicitHeight: barRow.whole ? 8 : 6; color: chartView.rightColor; opacity: barRow.whole ? 1 : 0.65 }
                    Label { text: barRow.textOf(barRow.modelData.right); font.pixelSize: 13; font.weight: barRow.whole ? Font.Bold : Font.Normal; font.features: { "tnum": 1 }; color: barRow.whole ? Theme.text : Theme.textMuted }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }
    // 영상 한 칸: 위에 이름, 아래에 영상. 영상이 없으면 이유를 가운데에 보입니다.
    component Pane: ColumnLayout {
        id: pane
        property string caption
        property string detail
        property string note
        default property alias content: holder.data
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Caption { text: pane.caption }
            Item { Layout.fillWidth: true }
            Label {
                text: pane.detail
                font.pixelSize: 12
                font.features: { "tnum": 1 }
                color: Theme.textMuted
            }
        }
        Rectangle {
            id: holder
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.previewBackground
            clip: true
            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: pane.note !== ""
                text: pane.note
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.textMuted
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: win.figureColumn ? 20 : 24
        spacing: win.figureColumn ? 24 : 32

        // 왼쪽: 판정, 관류지수, 분석 품질. 창 높이가 모자라면 이 열만 스크롤합니다.
        ScrollView {
            id: side
            Layout.preferredWidth: win.figureColumn ? 320 : 350
            Layout.maximumWidth: win.figureColumn ? 320 : 350
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
        ColumnLayout {
            width: side.availableWidth - 14
            spacing: 0

            Caption { text: "관류 분석" }
            Label {
                Layout.topMargin: 2
                Layout.fillWidth: true
                text: win.result.shotName
                elide: Text.ElideRight
                font.pixelSize: 20
                font.weight: Font.Bold
                color: Theme.text
            }
            // 분석 판 탭. 판 이름을 눌러 그 판의 결과로 바꿉니다. 분석을 기다리는 판은 이름 뒤에 "분석 중"을 붙입니다.
            Row {
                Layout.topMargin: 12
                // 판이 하나뿐이면 고를 것이 없어 숨깁니다.
                visible: win.result.variantTabs.length > 1
                spacing: 18
                Repeater {
                    model: win.result.variantTabs
                    delegate: Item {
                        id: tab
                        required property var modelData
                        readonly property bool current: modelData.id === win.result.currentVariant
                        implicitWidth: tabText.implicitWidth
                        implicitHeight: tabText.implicitHeight + 6
                        Label {
                            id: tabText
                            text: tab.modelData.label + (tab.modelData.done ? "" : " 분석 중")
                            font.pixelSize: 15
                            font.weight: tab.current ? Font.Bold : Font.Normal
                            color: tab.current ? Theme.text : Theme.textMuted
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            visible: tab.current
                            color: Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.result.currentVariant = tab.modelData.id
                        }
                    }
                }
            }

            // 측정 품질 안내(요약 qc): 측정 완료는 초록, 확인 필요는 주의 색, 재측정 필요는 빨강이고 그 아래에 할 일을 적습니다.
            Label {
                Layout.topMargin: 18
                Layout.fillWidth: true
                visible: win.result.loaded && win.result.qualityStatus !== ""
                text: win.result.qualityStatusText
                font.pixelSize: 18
                font.weight: Font.Bold
                color: win.result.qualityStatus === "retake" ? Theme.mismatch
                     : win.result.qualityStatus === "check" ? Theme.settling : win.normalColor
            }
            Label {
                Layout.topMargin: 4
                Layout.fillWidth: true
                visible: win.result.loaded && win.result.qualityAction !== ""
                text: win.result.qualityAction
                wrapMode: Text.Wrap
                font.pixelSize: 14
                color: Theme.text
            }
            Label {
                Layout.topMargin: 24
                Layout.fillWidth: true
                visible: !gauge.visible
                text: win.result.loaded ? win.result.verdictText : win.result.awaiting ? "분석 중" : win.result.error
                wrapMode: Text.Wrap
                font.pixelSize: win.result.loaded || win.result.awaiting ? 28 : 14
                font.weight: Font.Bold
                color: !win.result.loaded ? Theme.textMuted : win.result.hasCuffLevel ? Theme.text : win.verdictColor
            }
            CuffGauge {
                id: gauge
                Layout.topMargin: 14
                Layout.alignment: Qt.AlignHCenter
                visible: win.result.loaded && win.result.cuffLevelIndex >= 0
                level: win.result.cuffLevelIndex
                labels: win.result.cuffGaugeLabels
                value: win.result.cuffGaugeValue
                caption: win.result.cuffGaugeCaption
            }
            // 촬영 때 입력한 값(v9는 발목 혈압 단계, 그 전 판은 검사 기준)과 추정 단계입니다. 같으면 발 색, 다르면 주의 색으로 적습니다.
            RowLayout {
                Layout.topMargin: 8
                Layout.fillWidth: true
                visible: win.result.loaded && win.result.criterionText !== ""
                Key { text: win.result.criterionLabel }
                Item { Layout.fillWidth: true }
                Label { text: win.result.criterionText; font.pixelSize: 15; font.weight: Font.Bold; font.features: { "tnum": 1 }; color: Theme.text }
                Label {
                    visible: text !== ""
                    text: win.result.criterionNote
                    font.pixelSize: 13
                    color: win.result.criterionMatches ? chartView.leftColor : Theme.settling
                }
            }
            Label {
                Layout.topMargin: 4
                Layout.fillWidth: true
                visible: win.result.hasCuffLevel && !gauge.visible
                text: win.result.cuffSchemeText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: Theme.textMuted
            }
            // 규칙 판정은 강한 발의 맥박 SNR이 기준보다 낮으면 단계를 내지 않습니다.
            Label {
                Layout.topMargin: 8
                Layout.fillWidth: true
                visible: win.result.cuffQualityFailed
                text: win.result.cuffQualityFailText
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.settling
            }
            // 단계 기준은 고정 카메라 설정의 촬영으로 정했습니다. 다른 설정이면 주의 색으로 알립니다.
            Label {
                Layout.topMargin: 8
                Layout.fillWidth: true
                visible: win.result.hasCuffLevel && !win.result.cuffFixedSetting
                text: "커프 단계 기준값을 맞춘 카메라 설정(수동, 게인 1, 노출 -5 또는 -9)으로 찍은 촬영이 아니어서 참고용입니다."
                wrapMode: Text.Wrap
                font.pixelSize: 13
                color: Theme.settling
            }

            // 녹화 직후 연 창이면 분석 진행을 여기 보입니다. 분석이 끝나면 이 자리에 판정이 들어옵니다.
            // 결과 창만 따로 연 경우(--result)에는 분석 진행 객체가 없으므로 막대를 만들지 않습니다.
            Loader {
                id: barLoader
                Layout.topMargin: 16
                Layout.fillWidth: true
                readonly property AnalysisController lane: win.result.analysis !== null
                                                           ? win.result.analysis.lane(win.result.currentVariant) : null
                active: win.result.awaiting && lane !== null
                visible: active
                sourceComponent: AnalysisBar { analysis: barLoader.lane }
            }

            // 로지스틱 회귀 판정(v6): 세 확률의 합 E와 단계 기준입니다.
            GridLayout {
                Layout.topMargin: 16
                Layout.fillWidth: true
                visible: win.result.hasCuffLevel && !win.result.cuffRuleBased
                columns: 2
                rowSpacing: 8
                columnSpacing: 16
                Key { text: "단계 점수 E" }
                Value { text: win.result.cuffScoreText }
                // 구간별 단계 점수 그래프가 있으면 기준을 띠로 보이므로 이 줄은 뺍니다. 구간별 값이 없는 결과에만 보입니다.
                Key { text: "단계 기준"; visible: win.result.chart.hasLevels !== true }
                Value { text: win.result.cuffCutsText; visible: win.result.chart.hasLevels !== true }
                Key { text: "카메라 설정" }
                Value { text: win.result.cuffSettingText; font.pixelSize: 13 }
            }
            // 규칙 판정(v7, v8): 위 줄부터 차례로 보고 처음 기준에 든 규칙의 단계를 냅니다. 기준에 든 줄은 밝게 보입니다.
            // 커프 상태 판정(v9): 네 상태 확률 줄이고, 고른 상태 줄이 밝습니다.
            GridLayout {
                Layout.topMargin: 16
                Layout.fillWidth: true
                visible: win.result.cuffRuleBased
                columns: 3
                rowSpacing: 8
                columnSpacing: 16
                Repeater {
                    model: win.result.cuffRuleRows.length * 3
                    delegate: Label {
                        required property int index
                        readonly property var row: win.result.cuffRuleRows[Math.floor(index / 3)]
                        readonly property int column: index % 3
                        Layout.fillWidth: column === 0
                        horizontalAlignment: column === 0 ? Text.AlignLeft : Text.AlignRight
                        text: row[column]
                        font.pixelSize: column === 1 ? 15 : 13
                        font.weight: row[3] && column === 1 ? Font.DemiBold : Font.Normal
                        color: row[3] ? Theme.text : Theme.textMuted
                    }
                }
                // 기준 칸이 빈 v9 줄의 읽는 법입니다. 줄을 바꿔 써서 왼쪽 칸 폭을 넓히지 않습니다.
                Label {
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    visible: text !== ""
                    text: win.result.cuffModelText
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: Theme.textMuted
                }
                // v9 상태 확률 줄은 값 칸이 "0.80" 너비라, 품질 줄을 이름과 값 두 줄로 나눠 세 칸 전체 폭에 씁니다.
                Key { Layout.columnSpan: win.result.cuffStates ? 3 : 1; text: "강한 발 맥박 품질" }
                // 기준까지 읽히게 줄을 바꿔 씁니다. 너비를 1로 두어 긴 문장이 왼쪽 칸을 넓히지 않게 합니다.
                Value {
                    Layout.columnSpan: win.result.cuffStates ? 3 : 2
                    Layout.preferredWidth: 1
                    horizontalAlignment: win.result.cuffStates ? Text.AlignLeft : Text.AlignRight
                    text: win.result.cuffQualityText
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    elide: Text.ElideNone
                }
                // 사이드카에 카메라 설정이 있으면 아래 "RGB 카메라 설정" 표에 자세히 보입니다.
                Key { visible: win.result.cameraRows.length === 0; text: "카메라 설정" }
                Value { visible: win.result.cameraRows.length === 0; Layout.columnSpan: 2; text: win.result.cuffSettingText; font.pixelSize: 13 }
            }
            // 이진 PAD 판정입니다. 2026-10-26 이전 결과이거나 판정을 켜고 분석한 결과에만 있습니다.
            Caption { Layout.topMargin: 16; visible: win.result.hasCuffLevel && win.result.hasPadVerdict; text: "PAD 판정" }
            Label {
                Layout.topMargin: 4
                visible: win.result.hasCuffLevel && win.result.hasPadVerdict
                text: win.result.padVerdictText
                font.pixelSize: 15
                font.weight: Font.DemiBold
                color: win.verdictColor
            }
            GridLayout {
                Layout.topMargin: 16
                Layout.fillWidth: true
                visible: win.result.hasPadVerdict
                columns: 2
                rowSpacing: 8
                columnSpacing: 16
                Key { text: "판정 점수" }
                Value { text: win.result.scoreText }
                Key { text: "기준" }
                Value { text: win.result.thresholdText }
                Key { text: "판정 측 발" }
                Value { text: win.result.affectedFootText }
                Key { text: "모델" }
                Value { text: win.result.modelText; font.pixelSize: 13 }
            }
            Label {
                Layout.topMargin: 8
                Layout.fillWidth: true
                visible: win.result.windowVotesText !== ""
                text: win.result.windowVotesText
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
                color: Theme.textMuted
            }

            Rule { visible: win.result.loaded }
            Caption { visible: win.result.loaded; text: "관류지수(PI)" }
            // 부위별 막대(PI %). 첫 줄이 발 전체이고, 막대 색은 그래프의 발 색과 같습니다. 막대 길이는 모든 줄이 같은 비율입니다.
            PiBars {
                Layout.topMargin: 12
                Layout.fillWidth: true
                visible: win.result.loaded
                rows: win.result.loaded ? win.result.piBars : []
                maxValue: win.result.piBarMax
            }
            GridLayout {
                Layout.topMargin: 12
                Layout.fillWidth: true
                visible: win.result.loaded
                columns: 2
                rowSpacing: 8
                columnSpacing: 16
                Key { text: "좌우 비 (왼발 / 오른발)" }
                Value { text: win.result.piRatioText; font.pixelSize: 18 }
                Key { text: "더 낮은 발" }
                Value { text: win.result.lowerFootText }
                // v6, v7 결과는 분석 품질 표에 같은 값이 있습니다.
                Key { visible: win.figureColumn; text: "좌우 맥박 상관 (30초 전체)" }
                Value { visible: win.figureColumn; text: win.result.templateCorrText }
            }

            // 녹화 때 RGB 카메라에 적용한 설정입니다. 커프 단계 기준값을 맞춘 설정과 다른 값은 주의 색입니다.
            Rule { visible: win.result.cameraRows.length > 0 }
            RowLayout {
                Layout.fillWidth: true
                visible: win.result.cameraRows.length > 0
                Caption { text: "RGB 카메라 설정" }
                Item { Layout.fillWidth: true }
                Label { text: win.result.cameraNote; font.pixelSize: 11; color: Theme.textMuted }
            }
            GridLayout {
                Layout.topMargin: 10
                Layout.fillWidth: true
                visible: win.result.cameraRows.length > 0
                columns: 3
                rowSpacing: 6
                columnSpacing: 12
                Repeater {
                    model: win.result.cameraRows.length * 3
                    delegate: Label {
                        required property int index
                        readonly property var row: win.result.cameraRows[Math.floor(index / 3)]
                        readonly property int column: index % 3
                        Layout.fillWidth: column === 0
                        horizontalAlignment: column === 1 ? Text.AlignRight : Text.AlignLeft
                        text: column === 0 ? row[0] : column === 1 ? row[1] : row[2]
                        font.pixelSize: 13
                        font.features: { "tnum": 1 }
                        font.weight: column === 1 && row[3] ? Font.DemiBold : Font.Normal
                        color: column === 1 ? (row[3] ? Theme.settling : Theme.text) : Theme.textMuted
                    }
                }
            }

            Rule { visible: win.result.loaded }
            Caption { visible: win.result.loaded; text: "열화상" }
            Label {
                Layout.topMargin: 8
                Layout.fillWidth: true
                visible: win.result.loaded
                text: win.result.thermalText
                wrapMode: Text.Wrap
                font.pixelSize: 14
                // 판정에서 열화상을 뺐으면 주의 색으로 보입니다.
                color: win.result.thermalExcluded ? Theme.settling : Theme.text
            }

            // v8 결과는 관류 분석 프로그램이 동작을 바꾼 줄만 있고, 없으면 이 절을 숨깁니다.
            readonly property bool qualityShown: win.result.loaded && (!win.figureColumn || win.result.qualityRows.length > 0)
            Rule { visible: parent.qualityShown }
            Caption { visible: parent.qualityShown; text: "분석 품질" }
            Label {
                Layout.topMargin: 4
                Layout.fillWidth: true
                visible: parent.qualityShown
                text: win.result.qualityStatus !== "" ? "관류 분석 프로그램의 측정 품질 검사입니다. 기준을 벗어난 항목은 주황색, 계산하지 않은 항목은 흐린 색입니다"
                                                      : "검증된 합격 기준은 아직 없습니다. 관류 분석 프로그램이 계산 방식을 바꾼 항목만 주황색입니다"
                wrapMode: Text.Wrap
                font.pixelSize: 11
                color: Theme.textMuted
            }
            Repeater {
                model: win.result.loaded ? win.result.qualityRows : []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    spacing: 1
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Key { text: modelData[0] }
                        Value { text: modelData[1]; font.pixelSize: 13; color: modelData[3] ? Theme.settling : modelData[4] ? Theme.textMuted : Theme.text }
                    }
                    // 관류 분석 프로그램 규칙에 걸린 항목만 주의 색입니다. 그 밖의 값에는 검증된 기준이 없어 좋고 나쁨을 표시하지 않습니다.
                    Label {
                        Layout.fillWidth: true
                        text: modelData[2]
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: modelData[3] ? Theme.settling : Theme.textMuted
                    }
                }
            }

            Item { Layout.preferredHeight: 24 }

            Label {
                Layout.fillWidth: true
                text: win.result.disclaimer
                wrapMode: Text.Wrap
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
            }
            // 관류 분석 프로그램이 쓴 촬영 폴더 안 결과와, 저장 위치 아래로 모은 분석결과 폴더입니다.
            FolderLink {
                Layout.topMargin: 14
                name: "분석 출력"
                path: win.result.resultFolder
                onActivated: win.result.openFolder()
            }
            FolderLink {
                Layout.topMargin: 8
                visible: win.result.collectFolder !== ""
                name: "결과 모음 폴더(results)"
                path: win.result.collectFolder
                onActivated: win.result.openCollectFolder()
            }
            Label {
                Layout.topMargin: 2
                Layout.fillWidth: true
                visible: win.result.collectText !== ""
                text: win.result.collectText
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 12
                color: Theme.textMuted
            }
            Label {
                Layout.topMargin: 4
                visible: win.result.analyzerText !== ""
                text: win.result.analyzerText
                font.pixelSize: 11
                color: Theme.textMuted
            }
        }
        }

        // 오른쪽: 같은 시각의 RGB 좌우, 열화상, 관류 지도, 시간축 그래프와 되감기 막대
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // v8 결과는 그래프 줄이 늘어 영상 줄을 낮춥니다.
                Layout.preferredHeight: win.figureColumn ? 2.2 : 5
                spacing: 12

                // 촬영 화면과 같이 환자 왼발을 왼쪽에 둡니다.
                Repeater {
                    model: ["left", "right"]
                    delegate: Pane {
                        id: footPane
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        caption: "RGB " + (modelData === "left" ? "왼발" : "오른발")
                        note: win.result.hasVideo ? "" : win.result.videoNote
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            // 디코딩을 화면 스레드 밖에서 하고, 다음 장이 준비될 때까지 앞 장을 둡니다. 끌기와 재생이 멈추지 않게 합니다.
                            asynchronous: true
                            retainWhileLoading: true
                            // 촬영 화면과 같게 좌우반전합니다. 영상 왼쪽이 오른발인 촬영이면 발 하나의 영상도 뒤집어야 화면과 같습니다.
                            mirror: win.result.firstFoot === "right"
                            visible: win.result.hasVideo
                            // 결과를 읽을 때마다 바뀌는 번호를 넣어, 새 촬영을 읽으면 위치가 같아도 주소가 바뀌게 합니다. 주소가 같으면 앞 촬영의 장이 남습니다.
                            source: win.result.hasVideo ? "image://result/" + win.result.loadRevision + "/" + footPane.modelData + "/" + win.result.position : ""
                        }
                    }
                }
                Pane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    caption: win.result.thermalCaption
                    detail: win.result.thermalRangeText
                    note: win.result.hasThermal ? "" : win.result.thermalNote
                    Image {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: true
                        retainWhileLoading: true
                        smooth: false
                        // 영상 왼쪽이 오른발인 촬영은 뒤집어 왼발이 왼쪽에 오게 합니다(촬영 화면과 같음).
                        mirror: win.result.firstFoot === "right"
                        visible: win.result.hasThermal
                        source: win.result.hasThermal ? "image://result/" + win.result.loadRevision + "/thermal/" + win.result.position : ""
                    }
                }
                Pane {
                    id: mapPane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 2
                    // 관류 분석 프로그램이 만든 GIF를 그대로 보입니다. 관류 분석 프로그램은 발 영상을 좌우반전하지 않은 원래 방향으로 그리므로,
                    // 화면 배치(pixel_left_foot)와 관계없이 왼쪽 그림이 왼발입니다.
                    caption: "관류 지도(왼쪽이 왼발)"
                    detail: win.result.hasGif && win.result.chart.piTimes ? "10초 구간 " + (win.result.chart.piTimes[win.result.gifFrame] - 5).toFixed(0) + "~" + (win.result.chart.piTimes[win.result.gifFrame] + 5).toFixed(0) + "초" : ""
                    note: win.result.hasGif ? "" : "관류 지도 GIF가 없습니다"
                    AnimatedImage {
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        visible: win.result.hasGif
                        source: win.result.gifUrl
                        playing: false
                        currentFrame: win.result.gifFrame
                    }
                }
                // 같은 10초 구간의 PI를 표준 발 틀(v8)에 그린 지도입니다. 왼쪽이 왼발이고 오른발은 좌우반전입니다.
                Pane {
                    visible: win.result.hasTemplateMap
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1.6
                    caption: "표준 발 PI 지도"
                    detail: win.result.templateMapRangeText
                    Image {
                        anchors.fill: parent
                        anchors.margins: 6
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        asynchronous: true
                        retainWhileLoading: true
                        source: win.result.hasTemplateMap ? "image://result/" + win.result.loadRevision + "/tmap/" + win.result.gifFrame : ""
                    }
                }
            }

            TimeChart {
                id: chartView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 6
                values: win.result.chart
                cursorS: win.result.positionSeconds
                onSeek: seconds => win.seekFrame(Math.round(seconds * win.fps))
            }

            // 재생 버튼과 되감기 막대. 스페이스로 재생과 멈춤, 좌우 화살표로 한 장씩 옮깁니다.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    width: 40
                    height: 40
                    radius: 20
                    enabled: win.result.frameCount > 1
                    opacity: enabled ? 1 : 0.4
                    color: playArea.containsMouse ? Theme.text : Theme.controlActive
                    Canvas {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        property bool playing: win.playing
                        onPlayingChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.fillStyle = Theme.background
                            if (playing) {
                                ctx.fillRect(2, 1, 4, 14)
                                ctx.fillRect(10, 1, 4, 14)
                            } else {
                                ctx.beginPath(); ctx.moveTo(3, 1); ctx.lineTo(15, 8); ctx.lineTo(3, 15); ctx.closePath(); ctx.fill()
                            }
                        }
                    }
                    MouseArea {
                        id: playArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: win.togglePlay()
                    }
                }
                Slider {
                    id: scrub
                    Layout.fillWidth: true
                    enabled: win.result.frameCount > 1
                    from: 0
                    to: Math.max(0, win.result.frameCount - 1)
                    stepSize: 1
                    value: win.result.position
                    onMoved: win.seekFrame(Math.round(value))
                    background: Rectangle {
                        x: scrub.leftPadding
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: scrub.availableWidth
                        height: 4
                        radius: 2
                        color: Theme.line
                        Rectangle {
                            width: scrub.visualPosition * parent.width
                            height: parent.height
                            radius: 2
                            color: Theme.textMuted
                        }
                    }
                    handle: Rectangle {
                        x: scrub.leftPadding + scrub.visualPosition * (scrub.availableWidth - width)
                        y: scrub.topPadding + scrub.availableHeight / 2 - height / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: Theme.controlActive
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: win.result.timeText
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    font.features: { "tnum": 1 }
                    color: Theme.text
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: win.result.frameText
                    font.pixelSize: 12
                    font.features: { "tnum": 1 }
                    color: Theme.textMuted
                }
            }
        }

        // 오른쪽: 녹화 전체를 요약한 관류 분석 프로그램 그림(v8). 그림 속 글자는 이 크기에서 읽기 어려워 핵심 값을 제목 줄에
        // 씁니다. 그림을 누르면 원본 크기로 엽니다.
        ColumnLayout {
            visible: win.figureColumn
            Layout.preferredWidth: 460
            Layout.maximumWidth: 460
            Layout.fillHeight: true
            spacing: 14
            Repeater {
                model: win.result.summaryFigures
                delegate: ColumnLayout {
                    id: figure
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // 그림 비율대로 높이를 나눕니다. 발 분할 그림은 세로로 길어 높이를 300으로 묶습니다.
                    readonly property real ratio: image.sourceSize.width > 0 ? image.sourceSize.height / image.sourceSize.width : 0.6
                    Layout.preferredHeight: Math.min(index === 0 ? 300 : 1000, 460 * ratio) + 26
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Caption { text: figure.modelData.title }
                        Item { Layout.fillWidth: true }
                        Label { text: figure.modelData.detail; font.pixelSize: 12; font.features: { "tnum": 1 }; color: Theme.textMuted }
                    }
                    Image {
                        id: image
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        fillMode: Image.PreserveAspectFit
                        horizontalAlignment: Image.AlignHCenter
                        verticalAlignment: Image.AlignTop
                        asynchronous: true
                        mipmap: true
                        source: figure.modelData.url
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(figure.modelData.url)
                        }
                    }
                }
            }
        }
    }
}
