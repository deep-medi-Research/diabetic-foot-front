namespace DiabeticFoot.Core.Tests.Support;

public class Mt19937Tests
{
    // C++ 표준이 정한 값입니다. 기본 seed 5489의 첫 출력과 10000번째 출력입니다.
    [Fact]
    public void MatchesStandardReferenceOutputs()
    {
        var engine = new Mt19937();

        Assert.Equal(3499211612u, engine.Next());
        for (int i = 2; i < 10000; i++)
        {
            engine.Next();
        }
        Assert.Equal(4123659995u, engine.Next());
    }
}
