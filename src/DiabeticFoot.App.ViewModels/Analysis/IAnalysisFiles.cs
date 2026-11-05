using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Analysis;

/// <summary>요약 JSON의 측정 품질 안내(qc)입니다. 촬영 화면은 결과 창을 열지 않아도 이 상태와 조치 문장을 보입니다.</summary>
/// <param name="Status">"ok", "check", "retake" 중 하나입니다.</param>
/// <param name="StatusKo">상태 문장입니다(예: "재측정 필요").</param>
/// <param name="ActionKo">조치 문장입니다(예: "양말을 신고 5~10분 기다려 발을 따뜻하게 한 뒤 재촬영하세요"). 없으면 빈 문자열입니다.</param>
public sealed record MeasurementQuality(string Status, string StatusKo, string ActionKo = "");

/// <summary>분석결과 폴더로 복사한 결과입니다.</summary>
/// <param name="Ok">복사했거나 같은 내용이 이미 있으면 true입니다.</param>
/// <param name="Text">화면 문장입니다. 복사한 파일 수, 재분석 번호, 실패 이유를 담습니다.</param>
/// <param name="Folder">분석결과 폴더입니다. 촬영 폴더 경로를 나누지 못했으면 빈 문자열입니다.</param>
public sealed record CollectOutcome(bool Ok, string Text, string Folder = "");

/// <summary>
/// 분석 막대가 촬영 폴더와 결과 파일에서 읽고 쓰는 일입니다. 매니페스트, 사이드카, 요약 JSON, 결과 복사는 파일 형식을 아는
/// platform의 몫이라 약속으로만 부릅니다(촬영 폴더와 결과 파일 이식이 끝나면 그 구현을 연결합니다).
/// </summary>
public interface IAnalysisFiles
{
    /// <summary>
    /// 촬영 폴더의 매니페스트와 사이드카로 분석 요청을 만듭니다. 결과 폴더(OutputDir)는 비워 두면 됩니다.
    /// 읽지 못하면 null이고 <paramref name="error"/>에 이유를 씁니다.
    /// </summary>
    PerfusionRequest? RequestFor(string shotFolder, out string error);

    /// <summary>요약 JSON의 측정 품질 안내를 읽습니다. 파일이 없거나 qc가 없으면 null입니다.</summary>
    MeasurementQuality? ReadQuality(string summaryPath);

    /// <summary>
    /// 분석 결과를 저장 위치의 분석결과 폴더로 복사합니다. 파일을 비교하고 복사하므로 화면 스레드 밖에서 부릅니다.
    /// 예외 대신 <see cref="CollectOutcome.Ok"/>가 false인 결과로 이유를 돌려줍니다.
    /// </summary>
    CollectOutcome Collect(string shotFolder, string variantId);
}
