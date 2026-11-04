using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 손으로 쓴 JSON 읽기를 System.Text.Json으로 바꿨으므로, 원래 받아들이던 입력과 거부하던 입력을 직접 확인합니다.
public class JsonReaderTests
{
    [Fact]
    public void ReadsFlatObjectOfScalars()
    {
        var members = new JsonReader("{\"s\": \"a\\\"b\", \"n\": 30.0, \"t\": true, \"f\": false, \"z\": null, \"nan\": NaN}").Read()!;
        Assert.Equal("a\"b", members["s"]);
        Assert.Equal("30.0", members["n"]);
        Assert.Equal("true", members["t"]);
        Assert.Equal("false", members["f"]);
        Assert.Equal("", members["z"]);
        Assert.Equal("", members["nan"]);
    }

    [Theory]
    [InlineData("")]
    [InlineData("{")]
    [InlineData("{\"a\": 1,}")]
    [InlineData("{\"a\": 1} x")]
    [InlineData("{\"a\": {\"b\": 1}}")]
    [InlineData("{\"a\": [1]}")]
    [InlineData("[1, 2]")]
    [InlineData("{\"a\": NaNx}")]
    [InlineData("{\"a\": 01}")]
    public void RejectsFlatObjectThatIsMalformedOrNested(string json) => Assert.Null(new JsonReader(json).Read());

    [Fact]
    public void FindsScalarsByKeyPathIncludingArrayIndex()
    {
        const string json = "{\"qc\": {\"items\": [{\"code\": \"a\", \"v\": 1.5}, {\"code\": \"b\", \"v\": NaN}]}, \"x\": [1, 2]}";
        var reader = new JsonReader(json);
        Assert.Equal(new JsonScalar(JsonKind.String, "b"), reader.FindScalar("qc", "items", "1", "code"));
        Assert.Equal(new JsonScalar(JsonKind.Number, "1.5"), reader.FindScalar("qc", "items", "0", "v"));
        Assert.Equal(JsonKind.NonFinite, reader.FindScalar("qc", "items", "1", "v")!.Value.Kind);
        Assert.Equal(new JsonScalar(JsonKind.Number, "2"), reader.FindScalar("x", "1"));
        Assert.Null(reader.FindScalar("qc", "items", "2", "code"));
        Assert.Null(reader.FindScalar("qc", "items", "01", "code"));
        Assert.Null(reader.FindScalar("qc"));
        Assert.Null(reader.FindScalar("missing"));
    }

    [Fact]
    public void FindStringIgnoresNonStrings()
    {
        var reader = new JsonReader("{\"a\": \"x\", \"b\": 1, \"c\": NaN, \"d\": \"NaN\"}");
        Assert.Equal("x", reader.FindString("a"));
        Assert.Null(reader.FindString("b"));
        Assert.Null(reader.FindString("c"));
        Assert.Equal("NaN", reader.FindString("d"));
    }

    [Fact]
    public void FindsFlatArraysOnly()
    {
        var reader = new JsonReader("{\"a\": [1, \"x\", null, -Infinity], \"b\": [1, {\"c\": 2}], \"e\": []}");
        var items = reader.FindArray("a")!;
        Assert.Equal(
            new[] { JsonKind.Number, JsonKind.String, JsonKind.Null, JsonKind.NonFinite },
            items.Select(item => item.Kind));
        Assert.Null(reader.FindArray("b"));
        Assert.Empty(reader.FindArray("e")!);
    }

    // 문법이 틀린 문서는 찾는 값이 앞에 있어도 읽지 못한 것으로 봅니다.
    [Fact]
    public void MalformedDocumentFindsNothing()
    {
        var reader = new JsonReader("{\"a\": \"x\", \"b\": ");
        Assert.Null(reader.FindString("a"));
        Assert.Null(new JsonReader("{\"a\": 1} trailing").FindScalar("a"));
    }

    [Fact]
    public void DuplicateKeysTakeTheLastValue()
    {
        var reader = new JsonReader("{\"a\": \"first\", \"a\": \"last\"}");
        Assert.Equal("last", reader.FindString("a"));
        Assert.Equal("last", reader.Read()!["a"]);
    }

    [Fact]
    public void RejectsNestingDeeperThanSixtyFour()
    {
        var deep = new string('[', 70) + new string(']', 70);
        Assert.Null(new JsonReader("{\"a\": " + deep + "}").FindArray("a"));
    }

    [Fact]
    public void DecodesSurrogatePairEscapes() =>
        Assert.Equal("\U0001F600", new JsonReader("{\"a\": \"\\ud83d\\ude00\"}").FindString("a"));
}
