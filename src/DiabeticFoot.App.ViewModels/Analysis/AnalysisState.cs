namespace DiabeticFoot.App.Analysis;

/// <summary>관류 분석 막대가 보이는 상태입니다.</summary>
public enum AnalysisState
{
    /// <summary>관류 분석 프로그램 파일이 없습니다. 촬영은 할 수 있고 분석만 하지 않습니다.</summary>
    Missing,

    /// <summary>관류 분석 프로그램이 모델을 올리는 중입니다.</summary>
    Starting,

    /// <summary>요청을 기다립니다.</summary>
    Idle,

    /// <summary>대기열에 촬영이 있고 아직 시작하지 않았습니다.</summary>
    Waiting,

    /// <summary>분석 중입니다.</summary>
    Busy,

    /// <summary>마지막 분석이 성공했습니다.</summary>
    Done,

    /// <summary>마지막 분석을 취소했습니다.</summary>
    Cancelled,

    /// <summary>마지막 분석이 실패했거나 관류 분석 프로그램이 멈췄습니다.</summary>
    Failed,

    /// <summary>분석 입력 확인에서 막혀 분석하지 않았습니다.</summary>
    Skipped,
}

/// <summary>측정 품질 상태(요약 qc.status)입니다.</summary>
public enum QualityLevel
{
    /// <summary>끝난 분석이 없거나 요약에 qc가 없습니다.</summary>
    None,

    /// <summary>측정 완료입니다.</summary>
    Ok,

    /// <summary>확인이 필요합니다.</summary>
    Check,

    /// <summary>재측정이 필요합니다.</summary>
    Retake,
}

/// <summary>분석결과 폴더 복사 상태입니다.</summary>
public enum CollectState
{
    None,
    Copying,
    Done,
    Failed,
}
