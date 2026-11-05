using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;

namespace DiabeticFoot.FakeAnalyzer.Tests;

/// <summary>`.npz` 안의 `.npy` 배열 하나입니다.</summary>
internal sealed record NpyEntry(string Descr, long[] Shape, byte[] Data)
{
    public bool IsNumeric => Descr is "<f8" or "<f4" or "<i8" or "<i4";

    public double[] Values()
    {
        int width = Descr[2] - '0';
        var values = new double[Data.Length / width];
        for (int i = 0; i < values.Length; i++)
        {
            ReadOnlySpan<byte> at = Data.AsSpan(i * width, width);
            values[i] = Descr switch
            {
                "<f8" => BinaryPrimitives.ReadDoubleLittleEndian(at),
                "<f4" => BinaryPrimitives.ReadSingleLittleEndian(at),
                "<i8" => BinaryPrimitives.ReadInt64LittleEndian(at),
                "<i4" => BinaryPrimitives.ReadInt32LittleEndian(at),
                _ => throw new InvalidOperationException($"숫자 dtype이 아닙니다: {Descr}"),
            };
        }
        return values;
    }
}

/// <summary>
/// 앱의 npz 읽기(NpyArchive)가 받는 범위인지 확인하며 읽습니다. 무압축 항목, 암호화 없음, CRC 일치, `.npy` 머리 버전 1~3,
/// C 순서만 통과합니다.
/// </summary>
internal static partial class NpzFile
{
    [GeneratedRegex(@"'descr':\s*'([^']*)'")]
    private static partial Regex DescrPattern();

    [GeneratedRegex(@"'fortran_order':\s*(True|False)")]
    private static partial Regex FortranPattern();

    [GeneratedRegex(@"'shape':\s*\(([^)]*)\)")]
    private static partial Regex ShapePattern();

    public static Dictionary<string, NpyEntry> Read(string path)
    {
        byte[] zip = File.ReadAllBytes(path);
        int end = zip.Length - 22;
        while (end >= 0 && BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(end)) != 0x06054b50) end--;
        Assert.True(end >= 0, "ZIP 끝 레코드가 없습니다");
        int count = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(end + 10));
        int at = (int)BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(end + 16));
        var arrays = new Dictionary<string, NpyEntry>();
        for (int i = 0; i < count; i++)
        {
            Assert.Equal(0x02014b50u, BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(at)));
            int flags = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(at + 8));
            int method = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(at + 10));
            uint crc = BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(at + 16));
            int packed = (int)BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(at + 20));
            int unpacked = (int)BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(at + 24));
            int nameLength = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(at + 28));
            int extraLength = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(at + 30));
            int commentLength = BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(at + 32));
            int local = (int)BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(at + 42));
            string name = Encoding.UTF8.GetString(zip, at + 46, nameLength);
            Assert.True(method == 0, $"{name}: 압축 방식 {method}. 앱은 무압축(0)만 읽습니다");
            Assert.True((flags & 1) == 0, $"{name}: 암호화된 항목");
            Assert.Equal(packed, unpacked);
            Assert.Equal(0x04034b50u, BinaryPrimitives.ReadUInt32LittleEndian(zip.AsSpan(local)));
            int start = local + 30 + BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(local + 26)) +
                        BinaryPrimitives.ReadUInt16LittleEndian(zip.AsSpan(local + 28));
            byte[] data = zip.AsSpan(start, packed).ToArray();
            Assert.True(crc == Crc32(data), $"{name}: CRC가 다릅니다");
            Assert.EndsWith(".npy", name);
            arrays[name[..^4]] = ReadNpy(name, data);
            at += 46 + nameLength + extraLength + commentLength;
        }
        return arrays;
    }

    static NpyEntry ReadNpy(string name, byte[] npy)
    {
        Assert.True(npy.AsSpan(0, 6).SequenceEqual(new byte[] { 0x93, (byte)'N', (byte)'U', (byte)'M', (byte)'P', (byte)'Y' }),
                    $"{name}: .npy 시작 바이트가 아닙니다");
        int major = npy[6];
        Assert.InRange(major, 1, 3);
        int headerLength = major == 1 ? BinaryPrimitives.ReadUInt16LittleEndian(npy.AsSpan(8)) : (int)BinaryPrimitives.ReadUInt32LittleEndian(npy.AsSpan(8));
        int headerStart = major == 1 ? 10 : 12;
        string header = Encoding.Latin1.GetString(npy, headerStart, headerLength);
        string descr = DescrPattern().Match(header).Groups[1].Value;
        Assert.Equal("False", FortranPattern().Match(header).Groups[1].Value);
        long[] shape = ShapePattern().Match(header).Groups[1].Value
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(long.Parse)
            .ToArray();
        byte[] data = npy[(headerStart + headerLength)..];
        int width = descr.StartsWith("<U", StringComparison.Ordinal) ? 4 * int.Parse(descr[2..]) : descr[2] - '0';
        Assert.Equal(shape.Aggregate(1L, (a, b) => a * b) * width, data.Length);
        return new NpyEntry(descr, shape, data);
    }

    public static uint Crc32(ReadOnlySpan<byte> bytes)
    {
        uint crc = 0xFFFFFFFFu;
        foreach (byte b in bytes)
        {
            crc ^= b;
            for (int k = 0; k < 8; k++) crc = (crc & 1) != 0 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        }
        return crc ^ 0xFFFFFFFFu;
    }
}

internal sealed record GifFile(int Width, int Height, bool Loops, IReadOnlyList<byte[]> Frames, IReadOnlyList<int> DelaysCs)
{
    /// <summary>GIF 구조를 읽고 장마다 LZW를 풀어 색표 번호를 꺼냅니다.</summary>
    public static GifFile Read(string path)
    {
        byte[] gif = File.ReadAllBytes(path);
        Assert.Equal("GIF89a", Encoding.ASCII.GetString(gif, 0, 6));
        int width = BinaryPrimitives.ReadUInt16LittleEndian(gif.AsSpan(6));
        int height = BinaryPrimitives.ReadUInt16LittleEndian(gif.AsSpan(8));
        int at = 13;
        if ((gif[10] & 0x80) != 0) at += 3 << ((gif[10] & 7) + 1);
        bool loops = false;
        var frames = new List<byte[]>();
        var delays = new List<int>();
        while (true)
        {
            byte introducer = gif[at++];
            if (introducer == 0x3B) break;
            if (introducer == 0x21)
            {
                byte label = gif[at++];
                if (label == 0xF9) delays.Add(BinaryPrimitives.ReadUInt16LittleEndian(gif.AsSpan(at + 2)));
                if (label == 0xFF && Encoding.ASCII.GetString(gif, at + 1, 11) == "NETSCAPE2.0") loops = true;
                at = SkipBlocks(gif, at, out _);
                continue;
            }
            Assert.Equal(0x2C, introducer);
            int w = BinaryPrimitives.ReadUInt16LittleEndian(gif.AsSpan(at + 4));
            int h = BinaryPrimitives.ReadUInt16LittleEndian(gif.AsSpan(at + 6));
            byte packedFields = gif[at + 8];
            at += 9;
            if ((packedFields & 0x80) != 0) at += 3 << ((packedFields & 7) + 1);
            int minCodeSize = gif[at++];
            at = SkipBlocks(gif, at, out byte[] data);
            byte[] indices = Lzw(data, minCodeSize);
            Assert.Equal(w * h, indices.Length);
            frames.Add(indices);
        }
        return new GifFile(width, height, loops, frames, delays);
    }

    static int SkipBlocks(byte[] gif, int at, out byte[] data)
    {
        var joined = new List<byte>();
        while (gif[at] != 0)
        {
            joined.AddRange(gif.AsSpan(at + 1, gif[at]).ToArray());
            at += 1 + gif[at];
        }
        data = joined.ToArray();
        return at + 1;
    }

    static byte[] Lzw(byte[] data, int minCodeSize)
    {
        int clear = 1 << minCodeSize, end = clear + 1;
        var table = new List<byte[]>();
        void Reset()
        {
            table.Clear();
            for (int i = 0; i < clear + 2; i++) table.Add(i < clear ? [(byte)i] : []);
        }
        Reset();
        int width = minCodeSize + 1, bits = 0, count = 0, at = 0;
        byte[]? previous = null;
        var output = new List<byte>();
        while (true)
        {
            while (count < width)
            {
                Assert.True(at < data.Length, "LZW 끝 번호 전에 데이터가 끝났습니다");
                bits |= data[at++] << count;
                count += 8;
            }
            int code = bits & ((1 << width) - 1);
            bits >>= width;
            count -= width;
            if (code == clear)
            {
                Reset();
                width = minCodeSize + 1;
                previous = null;
                continue;
            }
            if (code == end) return output.ToArray();
            byte[] entry = code < table.Count ? table[code]
                : code == table.Count && previous is not null ? [.. previous, previous[0]]
                : throw new InvalidDataException($"LZW 번호 {code}가 표({table.Count})에 없습니다");
            output.AddRange(entry);
            if (previous is not null && table.Count < 4096)
            {
                table.Add([.. previous, entry[0]]);
                if (table.Count == 1 << width && width < 12) width++;
            }
            previous = entry;
        }
    }
}

internal static class PngFile
{
    /// <summary>청크 CRC를 확인하고 IDAT를 풀어 크기를 확인합니다. 너비와 높이를 돌려줍니다.</summary>
    public static (int Width, int Height) Read(string path)
    {
        byte[] png = File.ReadAllBytes(path);
        Assert.True(png.AsSpan(0, 8).SequenceEqual(new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A }));
        int at = 8, width = 0, height = 0, colorType = -1;
        using var idat = new MemoryStream();
        while (at < png.Length)
        {
            int length = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(at));
            string type = Encoding.ASCII.GetString(png, at + 4, 4);
            ReadOnlySpan<byte> body = png.AsSpan(at + 4, 4 + length);
            Assert.True(BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(at + 8 + length)) == NpzFile.Crc32(body), $"{type} CRC가 다릅니다");
            ReadOnlySpan<byte> data = png.AsSpan(at + 8, length);
            if (type == "IHDR")
            {
                width = BinaryPrimitives.ReadInt32BigEndian(data);
                height = BinaryPrimitives.ReadInt32BigEndian(data[4..]);
                colorType = data[9];
                Assert.Equal(8, data[8]);
            }
            if (type == "IDAT") idat.Write(data);
            at += 12 + length;
            if (type == "IEND") break;
        }
        Assert.Equal(2, colorType);
        idat.Position = 0;
        using var raw = new MemoryStream();
        using (var zlib = new ZLibStream(idat, CompressionMode.Decompress)) zlib.CopyTo(raw);
        Assert.Equal(height * (1 + width * 3), raw.Length);
        return (width, height);
    }
}
