import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HwLab

// 관류 분석 상태입니다. 분석 판단은 AnalysisController와 platform::Analyzer가 하고 여기는 그리기만 합니다.
ColumnLayout {
    id: bar
    required property AnalysisController analysis
    // 완료만 이 막대에서 쓰는 색입니다. 나머지 상태 색은 Theme을 따릅니다.
    readonly property color doneColor: "#30d158"
    readonly property color tone: analysis.state === "failed" || analysis.state === "missing" ? Theme.mismatch
                                : analysis.state === "skipped" || analysis.state === "cancelled" ? Theme.settling
                                : analysis.state === "done" ? doneColor
                                : analysis.state === "busy" ? Theme.text : Theme.textMuted

    spacing: 0

    // 진행 비율은 0.2초마다 바뀝니다. 퍼센트 숫자와 막대가 끊기지 않게 따라가는 값을 씁니다.
    SmoothValue { id: shownProgress; goal: bar.analysis.progress }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            width: 8; height: 8; radius: 4
            color: bar.tone
            SequentialAnimation on opacity {
                running: bar.analysis.state === "busy" || bar.analysis.state === "starting"
                loops: Animation.Infinite
                NumberAnimation { to: 0.25; duration: 600 }
                NumberAnimation { to: 1; duration: 600 }
            }
        }
        Label {
            Layout.fillWidth: true
            text: bar.analysis.title
            elide: Text.ElideRight
            font.pixelSize: 14
            font.weight: Font.DemiBold
            color: bar.analysis.state === "busy" || bar.analysis.state === "idle" || bar.analysis.state === "waiting"
                   || bar.analysis.state === "starting" ? Theme.text : bar.tone
        }
        Label {
            visible: text !== ""
            text: bar.analysis.queued > 0 ? "대기 " + bar.analysis.queued + "건" : ""
            font.pixelSize: 12
            color: Theme.textMuted
        }
        // 분석 중에는 진행 비율, 그 밖에는 경과나 걸린 시간입니다.
        Label {
            visible: text !== ""
            text: bar.analysis.state === "busy" ? Math.round(shownProgress.value * 100) + "%" : bar.analysis.elapsedText
            font.pixelSize: bar.analysis.state === "busy" ? 14 : 12
            font.weight: bar.analysis.state === "busy" ? Font.DemiBold : Font.Normal
            font.features: { "tnum": 1 }
            color: bar.analysis.state === "busy" ? Theme.text : Theme.textMuted
        }
    }

    // 진행 막대. 분석 중에만 보이며, 단계와 세부 단계를 합친 비율(AnalysisController::progress)을 그립니다.
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: 10
        height: 6
        radius: 3
        color: Theme.line
        visible: bar.analysis.state === "busy"
        Rectangle {
            height: parent.height
            radius: parent.radius
            width: parent.width * Math.max(0.02, Math.min(1, shownProgress.value))
            color: Theme.text
        }
    }

    // 경과와 남은 시간 예상. 분석 중에만 보입니다.
    Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: bar.analysis.state === "busy"
        text: bar.analysis.elapsedText + (bar.analysis.remainingText !== "" ? ", " + bar.analysis.remainingText : "")
        font.pixelSize: 12
        font.features: { "tnum": 1 }
        color: Theme.textMuted
    }

    // 측정 품질 안내(요약 qc)입니다. 측정 완료는 초록, 확인 필요는 주의 색, 재측정 필요는 빨강입니다.
    Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: text !== ""
        text: bar.analysis.qualityText
        wrapMode: Text.Wrap
        font.pixelSize: 13
        font.weight: Font.DemiBold
        color: bar.analysis.qualityStatus === "retake" ? Theme.mismatch
             : bar.analysis.qualityStatus === "check" ? Theme.settling : bar.doneColor
    }

    Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: text !== ""
        text: bar.analysis.detail
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 5
        elide: Text.ElideRight
        font.pixelSize: 12
        color: Theme.textMuted
    }

    // 분석결과 폴더 복사 결과. 성공한 분석 뒤에만 보이며, 문장을 누르면 분석결과 폴더를 엽니다.
    Item {
        Layout.fillWidth: true
        Layout.topMargin: 6
        visible: bar.analysis.state === "done" && bar.analysis.collectText !== ""
        implicitHeight: collectRow.implicitHeight
        RowLayout {
            id: collectRow
            anchors.fill: parent
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: bar.analysis.collectText
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 4
                elide: Text.ElideRight
                font.pixelSize: 12
                color: bar.analysis.collectState === "failed" ? Theme.mismatch : Theme.textMuted
            }
            Label {
                visible: bar.analysis.collectFolder !== ""
                text: "›"
                font.pixelSize: 16
                color: Theme.textMuted
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: bar.analysis.collectFolder !== ""
            cursorShape: Qt.PointingHandCursor
            onClicked: bar.analysis.openCollectFolder()
        }
    }

    RowLayout {
        Layout.topMargin: 8
        spacing: 16
        visible: bar.analysis.canRetry || bar.analysis.canCancel
        Label {
            visible: bar.analysis.canRetry
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
                onClicked: bar.analysis.retry()
            }
        }
        Label {
            visible: bar.analysis.canCancel
            text: "취소"
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.underline: cancelArea.containsMouse
            color: Theme.text
            MouseArea {
                id: cancelArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.analysis.cancel()
            }
        }
    }
}
