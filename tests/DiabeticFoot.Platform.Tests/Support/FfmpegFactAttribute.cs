using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

/// <summary>FFmpeg 7.1 공유 라이브러리가 있는 컴퓨터에서만 돌리는 시험입니다. 환경 변수 DIABETICFOOT_FFMPEG_DIR로 폴더를 줄 수 있습니다.</summary>
public sealed class FfmpegFactAttribute : FactAttribute
{
    public FfmpegFactAttribute()
    {
        if (FfmpegTestLibraries.Create() is { } reader) reader.Dispose();
        else Skip = "FFmpeg 7.1 공유 라이브러리를 찾지 못했습니다(DIABETICFOOT_FFMPEG_DIR)";
    }
}

internal static class FfmpegTestLibraries
{
    private static readonly string?[] Candidates =
    [
        Environment.GetEnvironmentVariable("DIABETICFOOT_FFMPEG_DIR"),
        "/opt/homebrew/lib",
        "/usr/local/lib",
        null,
    ];

    public static IVideoReader? Create()
    {
        foreach (var directory in Candidates)
        {
            if (directory is { Length: 0 }) continue;
            if (VideoReaders.Create(directory, out _) is { } reader) return reader;
        }
        return null;
    }
}
