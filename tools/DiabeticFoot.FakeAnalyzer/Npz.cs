using System.Buffers.Binary;
using System.IO.Compression;
using System.Text;

namespace DiabeticFoot.FakeAnalyzer;

// 값은 C 순서(마지막 축이 가장 빨리 변함)의 리틀 엔디언 바이트입니다.
internal sealed record NpyArray(string Descr, long[] Shape, byte[] Data)
{
    public static NpyArray Float64(double[] values, params long[] shape)
    {
        var data = new byte[values.Length * 8];
        for (int i = 0; i < values.Length; i++) BinaryPrimitives.WriteDoubleLittleEndian(data.AsSpan(i * 8), values[i]);
        return new NpyArray("<f8", Checked(shape, values.Length), data);
    }

    public static NpyArray Float64Scalar(double value) => Float64([value]);

    public static NpyArray Int32(int[] values, params long[] shape)
    {
        var data = new byte[values.Length * 4];
        for (int i = 0; i < values.Length; i++) BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan(i * 4), values[i]);
        return new NpyArray("<i4", Checked(shape, values.Length), data);
    }

    public static NpyArray Int64(long[] values, params long[] shape)
    {
        var data = new byte[values.Length * 8];
        for (int i = 0; i < values.Length; i++) BinaryPrimitives.WriteInt64LittleEndian(data.AsSpan(i * 8), values[i]);
        return new NpyArray("<i8", Checked(shape, values.Length), data);
    }

    // numpy 문자열 배열(`<U길이`)은 글자마다 UTF-32 리틀 엔디언 4바이트이고, 짧은 값은 0으로 채웁니다.
    public static NpyArray Unicode(string[] values, params long[] shape)
    {
        int width = values.Max(v => v.Length);
        var data = new byte[values.Length * width * 4];
        for (int i = 0; i < values.Length; i++)
        {
            for (int j = 0; j < values[i].Length; j++)
            {
                BinaryPrimitives.WriteInt32LittleEndian(data.AsSpan((i * width + j) * 4), values[i][j]);
            }
        }
        return new NpyArray($"<U{width}", Checked(shape, values.Length), data);
    }

    static long[] Checked(long[] shape, int count)
    {
        long expected = shape.Aggregate(1L, (a, b) => a * b);
        if (expected != count) throw new ArgumentException($"모양 ({string.Join(", ", shape)})과 값 수 {count}가 다릅니다");
        return shape;
    }

    // 머리 버전 1.0이고, numpy와 같이 머리 끝을 64바이트 경계에 맞춥니다.
    public byte[] ToNpy()
    {
        string shape = Shape.Length switch
        {
            0 => "()",
            1 => $"({Shape[0]},)",
            _ => $"({string.Join(", ", Shape)})",
        };
        string header = $"{{'descr': '{Descr}', 'fortran_order': False, 'shape': {shape}, }}";
        int padded = (10 + header.Length + 1 + 63) / 64 * 64 - 10;
        header = header.PadRight(padded - 1) + "\n";
        var npy = new byte[10 + header.Length + Data.Length];
        npy[0] = 0x93;
        "NUMPY"u8.CopyTo(npy.AsSpan(1));
        npy[6] = 1;
        npy[7] = 0;
        BinaryPrimitives.WriteUInt16LittleEndian(npy.AsSpan(8), (ushort)header.Length);
        Encoding.ASCII.GetBytes(header, npy.AsSpan(10));
        Data.CopyTo(npy, 10 + header.Length);
        return npy;
    }
}

internal static class Npz
{
    // 같은 배열이면 같은 바이트가 나오도록 항목 시각을 고정합니다.
    static readonly DateTimeOffset EntryTime = new(2026, 1, 1, 0, 0, 0, TimeSpan.Zero);

    public static void Write(string path, IEnumerable<(string Name, NpyArray Array)> arrays)
    {
        using FileStream file = File.Create(path);
        using var zip = new ZipArchive(file, ZipArchiveMode.Create);
        foreach ((string name, NpyArray array) in arrays)
        {
            ZipArchiveEntry entry = zip.CreateEntry(name + ".npy", CompressionLevel.NoCompression);
            entry.LastWriteTime = EntryTime;
            using Stream stream = entry.Open();
            stream.Write(array.ToNpy());
        }
    }
}
