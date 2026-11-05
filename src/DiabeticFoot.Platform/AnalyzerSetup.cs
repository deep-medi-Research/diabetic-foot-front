namespace DiabeticFoot.Platform;

/// <summary>관류 분석 프로그램의 연구 코드 판 하나입니다. `&lt;analysis&gt;/variants/&lt;판&gt;/VARIANT.json`에서 읽습니다.</summary>
public sealed record AnalyzerVariant
{
    /// <summary>릴리즈 자산 하나입니다.</summary>
    public sealed record Asset
    {
        /// <summary>릴리즈 자산 이름(name)입니다.</summary>
        public string Name { get; init; } = "";

        /// <summary>판 폴더 안 파일의 SHA-256(sha256, 소문자 16진수)입니다.</summary>
        public string Sha256 { get; init; } = "";

        /// <summary>릴리즈에 올라온 날(uploaded, YYYY-MM-DD)입니다.</summary>
        public string Uploaded { get; init; } = "";
    }

    /// <summary>판 이름(id, 폴더 이름과 같음). 관류 분석 프로그램 인자 `--variant`와 결과 폴더 이름(PerfusionOutputDir)에 씁니다.</summary>
    public string Id { get; init; } = "";

    /// <summary>화면에 보이는 이름(label)입니다.</summary>
    public string Label { get; init; } = "";

    /// <summary>한 줄 설명(summary)입니다. 예: "규칙 판정, 템플릿 v7".</summary>
    public string Summary { get; init; } = "";

    /// <summary>연구 저장소 커밋(upstream.commit)입니다.</summary>
    public string UpstreamCommit { get; init; } = "";

    /// <summary>연구 저장소 커밋의 날짜(upstream.date, YYYY-MM-DD)입니다.</summary>
    public string UpstreamDate { get; init; } = "";

    /// <summary>모델 파일을 올린 연구 저장소 릴리즈 태그(release.tag)입니다.</summary>
    public string ReleaseTag { get; init; } = "";

    /// <summary>커프 단계 판정 파일(release.classifier)입니다.</summary>
    public Asset Classifier { get; init; } = new();

    /// <summary>맥동 템플릿(release.template)입니다.</summary>
    public Asset PulseTemplate { get; init; } = new();
}

/// <summary>배포 폴더에서 관류 분석 프로그램을 띄우는 방법과 연구 코드 판을 찾습니다.</summary>
public static class AnalyzerSetup
{
    /// <summary>
    /// 배포 폴더에서 관류 분석 프로그램을 띄우는 방법을 정합니다. 파일이 있는지는 보지 않습니다.
    /// 기본은 `&lt;appDir&gt;/python/python.exe`(Windows가 아니면 `&lt;appDir&gt;/python/bin/python3`)로
    /// `&lt;appDir&gt;/analysis/dfr_worker.py --idle-exit 0`을 낮은 우선순위로 띄웁니다. 환경 변수
    /// PYTHONUNBUFFERED=1, PYTHONUTF8=1, PYTHONIOENCODING=utf-8을 더하고, 작업 폴더는 스크립트가 있는 폴더입니다. 경로 구분자는 '/'입니다.
    /// 시스템 PATH의 파이썬으로 물러나지 않습니다. 패키지 판이 다른 파이썬으로 분석하면 결과가 달라질 수 있습니다.
    /// </summary>
    /// <param name="appDir">앱 실행 파일이 있는 폴더</param>
    /// <param name="pythonOverride">비어 있지 않으면 이 파이썬을 씁니다(개발용 명령줄 인자)</param>
    /// <param name="scriptOverride">비어 있지 않으면 이 스크립트를 씁니다(개발용 명령줄 인자)</param>
    public static ProcessLaunch Launch(string appDir, string pythonOverride, string scriptOverride)
    {
        var baseDir = appDir.TrimEnd('/', '\\');
        var bundledPython = baseDir + (OperatingSystem.IsWindows() ? "/python/python.exe" : "/python/bin/python3");
        var script = scriptOverride.Length == 0 ? baseDir + "/analysis/dfr_worker.py" : scriptOverride;
        var cut = script.LastIndexOfAny(['/', '\\']);
        return new ProcessLaunch
        {
            Program = pythonOverride.Length == 0 ? bundledPython : pythonOverride,
            // 0이면 관류 분석 프로그램이 스스로 끝나지 않습니다. 앱이 닫힐 때까지 띄워 두어 촬영마다 모델을 다시 올리지 않습니다.
            Arguments = [script, "--idle-exit", "0"],
            WorkingDir = cut < 0 ? "" : script[..cut],
            ExtraEnv = [("PYTHONUNBUFFERED", "1"), ("PYTHONUTF8", "1"), ("PYTHONIOENCODING", "utf-8")],
            LowPriority = true,
        };
    }

    /// <summary>판 하나를 올리는 관류 분석 프로그램 실행 정보입니다. Launch의 인자 끝에 `--variant &lt;판&gt;`을 더한 것입니다.</summary>
    public static ProcessLaunch VariantLaunch(ProcessLaunch launch, string variantId) =>
        launch with { Arguments = [.. launch.Arguments, "--variant", variantId] };

    /// <summary>
    /// 관류 분석 프로그램 폴더의 연구 코드 판을 찾습니다. `variants/` 아래 `VARIANT.json`에 id가 있고 폴더 이름과 같은 판을 판 이름 순으로 돌려줍니다.
    /// 읽지 못한 판이 있거나 판이 하나도 없으면 error에 이유를 씁니다.
    /// </summary>
    /// <param name="analysisDir">`dfr_worker.py`가 있는 폴더</param>
    public static List<AnalyzerVariant> FindVariants(string analysisDir, out string error)
    {
        var variants = new List<AnalyzerVariant>();
        var problems = new List<string>();
        var root = Path.Combine(analysisDir, "variants");
        IEnumerable<string> folders;
        try
        {
            folders = Directory.Exists(root) ? Directory.GetDirectories(root) : [];
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            folders = [];
        }
        foreach (var folderPath in folders)
        {
            var file = Path.Combine(folderPath, "VARIANT.json");
            if (!File.Exists(file)) continue;
            var folder = Path.GetFileName(folderPath);
            string json;
            try
            {
                json = File.ReadAllText(file);
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                json = "";
            }
            var reader = new JsonReader(json);
            string At(params string[] path) => reader.FindString(path) ?? "";
            var id = At("id");
            if (id.Length == 0 || id != folder)
            {
                problems.Add($"판 정보를 읽지 못했거나 id가 폴더 이름과 다릅니다: variants/{folder}/VARIANT.json");
                continue;
            }
            variants.Add(new AnalyzerVariant
            {
                Id = id,
                Label = At("label") is { Length: > 0 } label ? label : id,
                Summary = At("summary"),
                UpstreamCommit = At("upstream", "commit"),
                UpstreamDate = At("upstream", "date"),
                ReleaseTag = At("release", "tag"),
                Classifier = ReadAsset(At, "classifier"),
                PulseTemplate = ReadAsset(At, "template"),
            });
        }
        variants.Sort((a, b) => string.CompareOrdinal(a.Id, b.Id));
        if (variants.Count == 0) problems.Add("연구 코드 판이 없습니다: " + analysisDir + "/variants");
        error = string.Join("; ", problems);
        return variants;
    }

    /// <summary>
    /// 관류 분석 프로그램 실행 파일과 스크립트가 있는지 확인합니다.
    /// 문제가 없으면 null이고, 파이썬이나 스크립트가 없거나 크기가 0이면 경로를 포함한 이유입니다.
    /// </summary>
    public static string? FilesProblem(ProcessLaunch launch)
    {
        if (Check("파이썬", launch.Program) is { } python) return python;
        if (launch.Arguments.Count == 0) return "분석 스크립트 경로가 없습니다";
        return Check("분석 스크립트", launch.Arguments[0]);
    }

    private static AnalyzerVariant.Asset ReadAsset(Func<string[], string> at, string role) => new()
    {
        Name = at(["release", role, "name"]),
        Sha256 = at(["release", role, "sha256"]),
        Uploaded = at(["release", role, "uploaded"]),
    };

    private static string? Check(string what, string path)
    {
        if (path.Length == 0 || !File.Exists(path)) return $"{what} 없음: {path}";
        return new FileInfo(path).Length == 0 ? $"{what} 크기 0: {path}" : null;
    }
}
