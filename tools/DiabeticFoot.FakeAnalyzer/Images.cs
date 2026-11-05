using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;

namespace DiabeticFoot.FakeAnalyzer;

internal sealed class IndexedImage(int width, int height, byte background)
{
    public int Width { get; } = width;
    public int Height { get; } = height;
    public byte[] Pixels { get; } = Enumerable.Repeat(background, width * height).ToArray();

    public void Set(int x, int y, byte color)
    {
        if (x >= 0 && x < Width && y >= 0 && y < Height) Pixels[y * Width + x] = color;
    }

    public void FillRect(int x0, int y0, int x1, int y1, byte color)
    {
        for (int y = y0; y < y1; y++)
        {
            for (int x = x0; x < x1; x++) Set(x, y, color);
        }
    }
}

// GIF와 PNG가 같은 16색을 씁니다. 1~12는 값이 작은 쪽(파랑)에서 큰 쪽(빨강)으로 갑니다.
internal static class Palette
{
    public const byte Background = 0;
    public const byte Gap = 13;
    public const byte White = 15;
    public const int RampSize = 12;

    public static readonly byte[] Rgb =
    [
        26, 28, 33,
        48, 18, 59, 62, 73, 179, 70, 129, 241, 50, 182, 234, 27, 226, 196, 70, 248, 132,
        156, 253, 60, 215, 226, 53, 250, 186, 57, 251, 128, 34, 228, 69, 11, 180, 22, 2,
        60, 64, 72,
        120, 120, 120,
        255, 255, 255,
    ];

    public static byte Ramp(double value, double top) =>
        (byte)(1 + Math.Clamp((int)Math.Round(value / top * (RampSize - 1)), 0, RampSize - 1));
}

internal static class Gif
{
    const int MinCodeSize = 4;

    public static void Write(string path, IReadOnlyList<IndexedImage> frames, int delayCentiseconds)
    {
        using var gif = new MemoryStream();
        IndexedImage first = frames[0];
        gif.Write("GIF89a"u8);
        WriteUInt16(gif, first.Width);
        WriteUInt16(gif, first.Height);
        gif.WriteByte(0xF3); // 전역 색표 있음, 색 깊이 8비트, 색표 16색
        gif.WriteByte(Palette.Background);
        gif.WriteByte(0);
        gif.Write(Palette.Rgb);
        gif.Write([0x21, 0xFF, 0x0B]);
        gif.Write("NETSCAPE2.0"u8);
        gif.Write([0x03, 0x01, 0x00, 0x00, 0x00]);
        foreach (IndexedImage frame in frames)
        {
            gif.Write([0x21, 0xF9, 0x04, 0x04]);
            WriteUInt16(gif, delayCentiseconds);
            gif.Write([0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00]);
            WriteUInt16(gif, frame.Width);
            WriteUInt16(gif, frame.Height);
            gif.WriteByte(0x00);
            WriteImageData(gif, frame.Pixels);
        }
        gif.WriteByte(0x3B);
        File.WriteAllBytes(path, gif.ToArray());
    }

    static void WriteUInt16(Stream stream, int value)
    {
        stream.WriteByte((byte)value);
        stream.WriteByte((byte)(value >> 8));
    }

    // LZW 표가 커지기 전에 지우기 번호를 넣어 번호 폭을 MinCodeSize + 1비트로 고정합니다. 압축은 되지 않지만 모든 GIF 디코더가 읽습니다.
    static void WriteImageData(Stream gif, byte[] pixels)
    {
        int clear = 1 << MinCodeSize;
        int width = MinCodeSize + 1;
        int run = clear - 2;
        var data = new List<byte>();
        int bits = 0, count = 0;
        void Code(int code)
        {
            bits |= code << count;
            count += width;
            while (count >= 8)
            {
                data.Add((byte)bits);
                bits >>= 8;
                count -= 8;
            }
        }
        Code(clear);
        for (int i = 0; i < pixels.Length; i++)
        {
            if (i > 0 && i % run == 0) Code(clear);
            Code(pixels[i]);
        }
        Code(clear + 1);
        if (count > 0) data.Add((byte)bits);
        gif.WriteByte(MinCodeSize);
        for (int at = 0; at < data.Count; at += 255)
        {
            int length = Math.Min(255, data.Count - at);
            gif.WriteByte((byte)length);
            gif.Write(data.GetRange(at, length).ToArray());
        }
        gif.WriteByte(0);
    }
}

internal static class Png
{
    public static void Write(string path, IndexedImage image)
    {
        var raw = new byte[image.Height * (1 + image.Width * 3)];
        for (int y = 0; y < image.Height; y++)
        {
            int row = y * (1 + image.Width * 3);
            for (int x = 0; x < image.Width; x++)
            {
                int color = image.Pixels[y * image.Width + x] * 3;
                Array.Copy(Palette.Rgb, color, raw, row + 1 + x * 3, 3);
            }
        }
        using var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true)) zlib.Write(raw);

        var header = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(header, image.Width);
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4), image.Height);
        header[8] = 8;
        header[9] = 2;
        using var png = new MemoryStream();
        png.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
        Chunk(png, "IHDR", header);
        Chunk(png, "IDAT", compressed.ToArray());
        Chunk(png, "IEND", []);
        File.WriteAllBytes(path, png.ToArray());
    }

    static void Chunk(Stream png, string type, byte[] data)
    {
        var length = new byte[4];
        BinaryPrimitives.WriteInt32BigEndian(length, data.Length);
        png.Write(length);
        byte[] body = [.. Encoding.ASCII.GetBytes(type), .. data];
        png.Write(body);
        var crc = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(crc, Crc32.Of(body));
        png.Write(crc);
    }
}

internal static class Crc32
{
    static readonly uint[] Table = Enumerable.Range(0, 256).Select(n =>
    {
        uint c = (uint)n;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        return c;
    }).ToArray();

    public static uint Of(ReadOnlySpan<byte> bytes)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in bytes) crc = Table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }
}
