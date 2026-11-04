namespace DiabeticFoot.Core.Tests.Support;

internal static class TestData
{
    public static string Path(params string[] parts) =>
        System.IO.Path.Combine(new[] { AppContext.BaseDirectory, "TestData" }.Concat(parts).ToArray());

    public static string ReadAllText(params string[] parts) => File.ReadAllText(Path(parts));
}
