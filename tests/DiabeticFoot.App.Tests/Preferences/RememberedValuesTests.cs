using System.Runtime.Versioning;
using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Tests.Support;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Tests.Preferences;

public sealed class RememberedValuesTests
{
    // 이번 실행 동안만 두는 저장소는 처음에 비어 있고, 쓴 값을 형식별로 돌려줍니다.
    [Fact]
    public void ThisRunOnlyStartsEmptyAndReturnsWhatWasSet()
    {
        var values = RememberedValues.ThisRunOnly();
        Assert.False(values.IsSaved);
        Assert.Null(values.GetString("shot/root"));
        Assert.Null(values.GetBool("analysis/prestream"));
        Assert.Null(values.GetSize("regions/rgb/frame"));
        Assert.Null(values.GetRect("regions/rgb/left"));

        values.SetString("shot/root", @"C:\촬영");
        values.SetBool("analysis/prestream", false);
        values.SetSize("regions/rgb/frame", 1280, 720);
        values.SetRect("regions/rgb/left", new Rect(10, 20, 300, 600));

        Assert.Equal(@"C:\촬영", values.GetString("shot/root"));
        Assert.False(values.GetBool("analysis/prestream"));
        Assert.Equal((1280, 720), values.GetSize("regions/rgb/frame"));
        Assert.Equal(new Rect(10, 20, 300, 600), values.GetRect("regions/rgb/left"));
    }

    // 형식이 맞지 않는 글자는 없는 값으로 봅니다(Qt 앱이 이진 형식으로 남긴 크기와 사각형도 여기에 듭니다).
    [Theory]
    [InlineData("")]
    [InlineData("1280")]
    [InlineData("1280,")]
    [InlineData("a,b")]
    [InlineData("1,2,3")]
    [InlineData("99999999999,1")]
    public void UnparseableTextIsMissing(string text)
    {
        var store = new InMemoryValueStore();
        store.Set("k", text);
        var values = new RememberedValues(store);

        Assert.Null(values.GetSize("k"));
        Assert.Null(values.GetRect("k"));
    }

    [Theory]
    [InlineData("true", true)]
    [InlineData("false", false)]
    [InlineData("True", true)]
    [InlineData("1", null)]
    [InlineData("", null)]
    public void ReadsBoolOnlyFromTrueOrFalse(string text, bool? expected)
    {
        var store = new InMemoryValueStore();
        store.Set("k", text);

        Assert.Equal(expected, new RememberedValues(store).GetBool("k"));
    }

    // 저장소를 바꿔 끼워도 같은 코드가 돕니다. 값은 저장소에 문자열로 남습니다.
    [Fact]
    public void WritesCultureIndependentText()
    {
        var store = new InMemoryValueStore();
        var values = new RememberedValues(store);

        values.SetRect("r", new Rect(1, -2, 3, 4));
        values.SetBool("b", true);

        Assert.Equal("1,-2,3,4", store.Get("r"));
        Assert.Equal("true", store.Get("b"));
    }

    // 레지스트리에는 키의 '/'가 하위 키가 되고, 문자열 값으로 남습니다. 시험 키는 끝나면 지웁니다.
    [WindowsFact]
    [SupportedOSPlatform("windows")]
    public void RegistryStoreKeepsValuesUnderSubKeys()
    {
        var path = @"Software\deepmedi\hwlab-test-" + Guid.NewGuid().ToString("N");
        try
        {
            var first = new RememberedValues(new RegistryValueStore(path));
            Assert.True(first.IsSaved);
            Assert.Null(first.GetString("shot/root"));

            first.SetString("shot/root", @"D:\촬영 위치");
            first.SetRect("regions/rgb/left", new Rect(0, 0, 640, 720));

            var second = new RememberedValues(new RegistryValueStore(path));
            Assert.Equal(@"D:\촬영 위치", second.GetString("shot/root"));
            Assert.Equal(new Rect(0, 0, 640, 720), second.GetRect("regions/rgb/left"));
        }
        finally
        {
            Microsoft.Win32.Registry.CurrentUser.DeleteSubKeyTree(path, throwOnMissingSubKey: false);
        }
    }
}
