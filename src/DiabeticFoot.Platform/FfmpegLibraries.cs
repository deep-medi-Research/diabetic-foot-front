using FFmpeg.AutoGen;

namespace DiabeticFoot.Platform;

/// <summary>
/// FFmpeg 공유 라이브러리를 한 번 불러옵니다. 바인딩(FFmpeg.AutoGen 9.0.1.1)과 주 버전이 다른 라이브러리는 구조체 배치가 달라 쓰지 않습니다.
/// 바인딩이 기대하는 주 버전은 libavcodec 63, libavformat 63, libavutil 61이고, 출하하는 FFmpeg 공유 라이브러리(avcodec-63, avformat-63, avutil-61)와 같습니다.
/// </summary>
internal static class FfmpegLibraries
{
    private static readonly object Gate = new();
    private static bool _loaded;

    public static bool TryLoad(string? directory, out string error)
    {
        lock (Gate)
        {
            error = "";
            if (_loaded) return true;
            try
            {
                if (!string.IsNullOrEmpty(directory)) ffmpeg.RootPath = directory;
                DynamicallyLoadedBindings.Initialize();
                var codec = ffmpeg.avcodec_version() >> 16;
                var format = ffmpeg.avformat_version() >> 16;
                var util = ffmpeg.avutil_version() >> 16;
                if (codec != ffmpeg.LIBAVCODEC_VERSION_MAJOR || format != ffmpeg.LIBAVFORMAT_VERSION_MAJOR || util != ffmpeg.LIBAVUTIL_VERSION_MAJOR)
                {
                    error = $"libavcodec {ffmpeg.LIBAVCODEC_VERSION_MAJOR}, libavformat {ffmpeg.LIBAVFORMAT_VERSION_MAJOR}, libavutil {ffmpeg.LIBAVUTIL_VERSION_MAJOR}인 FFmpeg가 필요합니다. 찾은 것은 libavcodec {codec}, libavformat {format}, libavutil {util}입니다";
                    return false;
                }
                _loaded = true;
                return true;
            }
            catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException or NotSupportedException or InvalidOperationException)
            {
                error = "FFmpeg 공유 라이브러리를 불러오지 못했습니다: " + e.Message;
                return false;
            }
        }
    }
}
