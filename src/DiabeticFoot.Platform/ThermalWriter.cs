using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>
/// 열화상 원시값 파일(raw16)을 씁니다. 분석에 쓰는 원본입니다.
/// </summary>
/// <remarks>
/// 머리글 없이 프레임마다 width * height개의 uint16을 리틀 엔디언, 행 순서로 이어 붙입니다(기존 앱과 같은 형식).
/// 크기와 프레임 수는 파일에 없으므로 사이드카에 남깁니다. 프레임 수는 파일 크기를 프레임 바이트 수로 나눈 값과 같습니다.
/// 머리글이 없어 녹화 중 프로그램이 멈춰도 그때까지 쓴 프레임을 그대로 읽을 수 있습니다. 쓰기 스레드 한 곳에서만 씁니다.
/// </remarks>
public sealed class Raw16File : IDisposable
{
    private FileStream? _file;
    private int _width;
    private int _height;
    private long _frames;
    private bool _failed;

    public void Dispose()
    {
        if (_file != null) Close();
    }

    /// <summary>
    /// 파일을 만듭니다. 같은 이름의 파일이 있으면 덮어씁니다.
    /// </summary>
    /// <param name="path">파일 경로</param>
    /// <param name="width">프레임 폭</param>
    /// <param name="height">프레임 높이</param>
    /// <returns>만들었으면 true</returns>
    public bool Open(string path, int width, int height)
    {
        if (_file != null || width <= 0 || height <= 0) return false;
        try
        {
            _file = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.Read, 1 << 16);
        }
        catch (Exception e) when (ThermalFileIo.IsFileError(e))
        {
            return false;
        }
        _width = width;
        _height = height;
        _frames = 0;
        _failed = false;
        return true;
    }

    /// <summary>
    /// 프레임 하나를 씁니다.
    /// </summary>
    /// <param name="y16">Y16 형식 프레임. 크기가 Open과 같아야 합니다. 행 간격(RowStride0)이 폭보다 커도 됩니다</param>
    /// <returns>썼으면 true. 형식이나 크기가 다르면 쓰지 않고 false</returns>
    public bool WriteFrame(FrameView y16)
    {
        if (_file == null || _failed || !ThermalFileIo.IsFrameOf(y16, _width, _height)) return false;
        var rowBytes = _width * 2;
        var plane = y16.Plane0.Span;
        try
        {
            // 파일은 리틀 엔디언으로 정해져 있으므로 화소를 바이트로 받은 그대로 씁니다.
            if (y16.RowStride0 == rowBytes)
            {
                _file.Write(plane[..(rowBytes * _height)]);
            }
            else
            {
                for (var y = 0; y < _height; ++y) _file.Write(plane.Slice(y * y16.RowStride0, rowBytes));
            }
        }
        catch (Exception e) when (ThermalFileIo.IsFileError(e))
        {
            _failed = true;
            return false;
        }
        ++_frames;
        return true;
    }

    /// <summary>파일을 닫습니다. 열려 있지 않았거나 쓰기에 실패했으면 false입니다. 프레임이 0장이어도 파일은 남깁니다.</summary>
    public bool Close()
    {
        if (_file == null) return false;
        var file = _file;
        _file = null;
        try
        {
            file.Dispose();
        }
        catch (Exception e) when (ThermalFileIo.IsFileError(e))
        {
            return false;
        }
        return !_failed;
    }

    /// <summary>쓴 프레임 수입니다.</summary>
    public long FramesWritten => _frames;

    /// <summary>프레임 하나의 바이트 수(width * height * 2)입니다. 열기 전에는 0입니다.</summary>
    public long FrameBytes => (long)_width * _height * 2;
}

/// <summary>
/// 열화상을 사람이 보는 확인용 흑백 영상(무압축 AVI)으로 씁니다.
/// </summary>
/// <remarks>
/// 원시값을 표시 범위로 8비트 회색으로 펴고(<see cref="PixelFunctions.Y16ToGray8"/>) UYVY로 바꿔(<see cref="PixelFunctions.Gray8ToUyvy"/>)
/// <see cref="AviFile"/>로 씁니다. 표시 범위는 녹화 동안 바꾸지 않습니다. 프레임마다 범위를 다시 잡으면 같은 원시값이
/// 프레임마다 다른 회색이 되어 영상의 밝기 변화를 온도 변화로 읽을 수 없기 때문입니다.
/// 분석에는 쓰지 않습니다. 원본은 <see cref="Raw16File"/>입니다. 폭은 짝수여야 합니다(UYVY).
/// </remarks>
public sealed class ThermalPreviewFile : IDisposable
{
    private readonly AviFile _avi = new();
    private int _width;
    private int _height;
    private Y16Range? _range;
    private byte[] _gray = Array.Empty<byte>();
    private byte[] _uyvy = Array.Empty<byte>();

    public void Dispose() => _avi.Dispose();

    /// <summary>
    /// 파일을 만듭니다.
    /// </summary>
    /// <param name="path">파일 경로</param>
    /// <param name="width">프레임 폭. 짝수</param>
    /// <param name="height">프레임 높이</param>
    /// <param name="frameRate">AVI 머리글에 적을 fps. 재생 속도에만 쓰이고 프레임의 실제 시각은 사이드카에 있습니다</param>
    /// <param name="maxFrames">쓸 최대 프레임 수</param>
    /// <param name="range">표시 범위. null이면 처음 쓰는 프레임 전체의 최솟값과 최댓값(<see cref="PixelFunctions.FindRange"/>)으로 정합니다</param>
    /// <returns>만들었으면 true. 폭이 홀수이거나 파일을 만들지 못하면 false</returns>
    public bool Open(string path, int width, int height, int frameRate, long maxFrames, Y16Range? range)
    {
        if (width <= 0 || height <= 0 || width % 2 != 0) return false;
        if (!_avi.Open(path, new VideoFormat(PixelFormat.Uyvy, width, height, frameRate), maxFrames)) return false;
        _width = width;
        _height = height;
        _range = range;
        _gray = new byte[width * height];
        _uyvy = new byte[_gray.Length * 2];
        return true;
    }

    /// <summary>
    /// 프레임 하나를 씁니다.
    /// </summary>
    /// <param name="y16">Y16 형식 프레임. 크기가 Open과 같아야 합니다</param>
    /// <returns>썼으면 true. 형식이나 크기가 다르거나 maxFrames를 넘으면 false</returns>
    public bool WriteFrame(FrameView y16)
    {
        if (!ThermalFileIo.IsFrameOf(y16, _width, _height)) return false;
        _range ??= PixelFunctions.FindRange(y16, new Rect(0, 0, _width, _height));
        return PixelFunctions.Y16ToGray8(y16, _range.Value, _gray) &&
               PixelFunctions.Gray8ToUyvy(_gray, _width, _height, _uyvy) &&
               _avi.WriteFrame(_uyvy);
    }

    /// <summary>남은 색인을 쓰고 닫습니다. 일찍 멈춰도 그때까지 쓴 프레임으로 읽을 수 있는 파일이 됩니다.</summary>
    public bool Close() => _avi.Close();

    /// <summary>쓴 프레임 수입니다.</summary>
    public long FramesWritten => _avi.FramesWritten;

    /// <summary>녹화에 쓴 표시 범위입니다. 사이드카에 남깁니다. Open에 범위를 주지 않았고 아직 프레임을 쓰지 않았으면 null입니다.</summary>
    public Y16Range? Range => _range;
}

/// <summary>원시값 파일에서 영역을 잘라 다른 파일로 씁니다. 녹화 뒤 좌우 발을 따로 저장할 때 씁니다.</summary>
public static class ThermalWriter
{
    /// <summary>
    /// 원시값 파일에서 영역을 잘라 다른 원시값 파일로 씁니다.
    /// </summary>
    /// <param name="raw16Path">원본 원시값 파일</param>
    /// <param name="width">원본 프레임 폭</param>
    /// <param name="height">원본 프레임 높이</param>
    /// <param name="rect">자를 영역. 원본 프레임 안이어야 합니다</param>
    /// <param name="outPath">결과 원시값 파일. 영역 크기의 프레임이 같은 형식으로 이어집니다</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>옮긴 프레임 수. 원본 크기가 프레임 바이트 수의 배수가 아니거나 영역이 프레임 밖이면 null</returns>
    public static long? WriteRaw16Crop(string raw16Path, int width, int height, Rect rect, string outPath, out string error) =>
        CopyRegion(raw16Path, width, height, rect, outPath, npy: false, out error);

    /// <summary>
    /// 원시값 파일에서 영역을 잘라 NumPy .npy 파일로 씁니다. 관류 분석이 thermal_left.npy와 thermal_right.npy를 읽습니다.
    /// </summary>
    /// <param name="raw16Path">원본 원시값 파일</param>
    /// <param name="width">원본 프레임 폭</param>
    /// <param name="height">원본 프레임 높이</param>
    /// <param name="rect">자를 영역. 전체를 쓰려면 (0, 0, width, height)</param>
    /// <param name="npyPath">결과 .npy 파일</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>옮긴 프레임 수. 원본 크기가 프레임 바이트 수의 배수가 아니거나 영역이 프레임 밖이면 null</returns>
    /// <remarks>
    /// NumPy format 1.0입니다. 머리글의 dtype은 '&lt;u2', 모양은 (프레임 수, 영역 높이, 영역 폭)이고, 머리글 뒤의 자료는
    /// 원시값 파일과 같은 리틀 엔디언 uint16 행 순서입니다. 자료 시작 위치는 64바이트의 배수입니다.
    /// 원시값 파일에서 언제든 다시 만들 수 있는 사본입니다. 머리글에 프레임 수가 들어가므로 녹화가 끝난 뒤 만듭니다.
    /// </remarks>
    public static long? WriteNpy(string raw16Path, int width, int height, Rect rect, string npyPath, out string error) =>
        CopyRegion(raw16Path, width, height, rect, npyPath, npy: true, out error);

    // NumPy format 1.0 머리글입니다. 읽는 쪽이 자료 시작을 64바이트 정렬로 가정하므로 공백으로 채우고 줄바꿈으로 끝냅니다.
    private static byte[] NpyHeader(long frames, int height, int width)
    {
        var dict = $"{{'descr': '<u2', 'fortran_order': False, 'shape': ({frames}, {height}, {width}), }}";
        const int prefixBytes = 10;  // 매직 6, 판 2, 머리글 길이 2
        var unpadded = prefixBytes + dict.Length + 1;
        dict += new string(' ', (unpadded + 63) / 64 * 64 - unpadded) + "\n";
        var header = new List<byte> { 0x93, (byte)'N', (byte)'U', (byte)'M', (byte)'P', (byte)'Y', 0x01, 0x00 };
        header.Add((byte)(dict.Length & 0xFF));
        header.Add((byte)((dict.Length >> 8) & 0xFF));
        header.AddRange(Encoding.ASCII.GetBytes(dict));
        return header.ToArray();
    }

    private static long? CopyRegion(string raw16Path, int width, int height, Rect rect, string outPath, bool npy, out string error)
    {
        error = "";
        if (width <= 0 || height <= 0 || !PixelFunctions.IsAligned(rect, PixelFormat.Y16, width, height))
        {
            error = "영역이 열화상 프레임 밖입니다";
            return null;
        }
        long bytes;
        try
        {
            bytes = new FileInfo(raw16Path).Length;
        }
        catch (Exception e) when (ThermalFileIo.IsFileError(e))
        {
            error = "원시값 파일을 열지 못했습니다: " + raw16Path;
            return null;
        }
        var frameBytes = (long)width * height * 2;
        // 크기가 맞지 않으면 모양을 잘못 알고 있는 것이고, 그대로 옮기면 행이 밀린 영상이 됩니다.
        if (bytes == 0 || bytes % frameBytes != 0)
        {
            error = $"원시값 파일 크기가 {width}x{height} 프레임의 배수가 아닙니다";
            return null;
        }
        var frames = bytes / frameBytes;

        FileStream input;
        FileStream output;
        try
        {
            input = new FileStream(raw16Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1 << 16);
            try
            {
                output = new FileStream(outPath, FileMode.Create, FileAccess.Write, FileShare.Read, 1 << 16);
            }
            catch
            {
                input.Dispose();
                throw;
            }
        }
        catch (Exception e) when (ThermalFileIo.IsFileError(e))
        {
            error = "파일을 열지 못했습니다: " + outPath;
            return null;
        }
        using (input)
        {
            try
            {
                if (npy) output.Write(NpyHeader(frames, rect.Height, rect.Width));
                var frame = new byte[frameBytes];
                var region = new byte[PixelFunctions.PackedBytes(PixelFormat.Y16, rect)];
                var view = new FrameView
                {
                    Format = PixelFormat.Y16,
                    Width = width,
                    Height = height,
                    Plane0 = frame,
                    RowStride0 = width * 2,
                    Stream = StreamKind.Thermal,
                };
                for (long i = 0; i < frames; ++i)
                {
                    try
                    {
                        input.ReadExactly(frame);
                    }
                    catch (EndOfStreamException)
                    {
                        output.Dispose();
                        error = "원시값을 끝까지 읽지 못했습니다";
                        return null;
                    }
                    if (!PixelFunctions.CopyRect(view, rect, region))
                    {
                        output.Dispose();
                        error = "영역을 자르지 못했습니다";
                        return null;
                    }
                    output.Write(region);
                }
                output.Dispose();
            }
            catch (Exception e) when (ThermalFileIo.IsFileError(e))
            {
                output.Dispose();
                error = "파일을 끝까지 쓰지 못했습니다: " + outPath;
                return null;
            }
        }
        return frames;
    }
}

internal static class ThermalFileIo
{
    public static bool IsFileError(Exception e) =>
        e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException;

    public static bool IsFrameOf(FrameView y16, int width, int height)
    {
        var rowBytes = (long)width * 2;
        return y16.Format == PixelFormat.Y16 && y16.Width == width && y16.Height == height && !y16.Plane0.IsEmpty &&
               y16.RowStride0 >= rowBytes && y16.Plane0.Length >= (long)(height - 1) * y16.RowStride0 + rowBytes;
    }
}
