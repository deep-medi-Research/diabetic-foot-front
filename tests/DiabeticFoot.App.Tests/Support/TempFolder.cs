namespace DiabeticFoot.App.Tests.Support;

/// <summary>테스트마다 따로 쓰는 임시 폴더입니다. 경로에 한글이 들어 있습니다.</summary>
internal sealed class TempFolder : IDisposable
{
    public TempFolder()
    {
        Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "app-tests", Guid.NewGuid().ToString("N"), "저장 위치");
        Directory.CreateDirectory(Path);
    }

    public string Path { get; }

    public string Make(params string[] parts)
    {
        var folder = System.IO.Path.Combine([Path, .. parts]);
        Directory.CreateDirectory(folder);
        return folder;
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(System.IO.Path.GetDirectoryName(Path)!, recursive: true);
        }
        catch (IOException)
        {
        }
    }
}
