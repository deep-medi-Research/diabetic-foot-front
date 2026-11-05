using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>무압축 AVI(OpenDML) 파일입니다. <see cref="AviBuilder"/>가 돌려준 바이트를 실제 파일에 씁니다.</summary>
public sealed class AviFile : IVideoWriter
{
    private FileStream? _file;
    private AviBuilder? _builder;

    public void Dispose()
    {
        if (_file != null) Close();
    }

    public bool Open(string path, VideoFormat format, long maxFrames)
    {
        var builder = new AviBuilder(format, maxFrames);
        if (!builder.IsValid) return false;
        try
        {
            // 4MB 프레임을 그대로 넘기므로 .NET 버퍼를 거치지 않게 합니다.
            _file = new FileStream(path, FileMode.Create, FileAccess.ReadWrite, FileShare.Read, bufferSize: 1);
        }
        catch (Exception e) when (IsFileError(e))
        {
            return false;
        }
        _builder = builder;
        return Apply(builder.Start());
    }

    /// <summary>pixels는 행 사이 빈 바이트가 없는 <see cref="FrameBytes"/> 크기 이상이어야 합니다. 앞의 <see cref="FrameBytes"/> 바이트만 씁니다.</summary>
    public bool WriteFrame(ReadOnlyMemory<byte> pixels)
    {
        if (_file == null || _builder == null || pixels.Length < FrameBytes) return false;
        var output = _builder.BeginFrame();
        if (output == null || !Apply(output)) return false;
        try
        {
            _file.Write(pixels.Span[..(int)_builder.FrameBytes]);
            return true;
        }
        catch (Exception e) when (IsFileError(e))
        {
            return false;
        }
    }

    public bool Close()
    {
        if (_file == null) return false;
        var written = Apply(_builder!.Finish());
        var closed = true;
        try
        {
            _file.Dispose();
        }
        catch (Exception e) when (IsFileError(e))
        {
            closed = false;
        }
        _file = null;
        return written && closed;
    }

    public long FramesWritten => _builder?.FramesWritten ?? 0;

    /// <summary>프레임 하나의 바이트 수입니다. 열기 전에는 0입니다.</summary>
    public long FrameBytes => _builder?.FrameBytes ?? 0;

    private bool Apply(AviOutput output)
    {
        try
        {
            if (output.Append.Length > 0) _file!.Write(output.Append);
            if (output.Patches.Count == 0) return true;
            foreach (var patch in output.Patches)
            {
                // 4GB를 넘는 위치도 Seek의 64비트 위치로 옮깁니다.
                _file!.Seek((long)patch.Offset, SeekOrigin.Begin);
                _file.Write(patch.Bytes);
            }
            _file!.Seek(0, SeekOrigin.End);
            return true;
        }
        catch (Exception e) when (IsFileError(e))
        {
            return false;
        }
    }

    private static bool IsFileError(Exception e) =>
        e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException;
}
