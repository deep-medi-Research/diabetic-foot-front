namespace DiabeticFoot.Core.Tests.Support;

/// <summary>
/// C++ std::mt19937과 같은 출력을 내는 난수 엔진입니다. 원본 테스트가 엔진 출력을 직접 써서 입력을 만들기 때문에
/// 같은 seed에서 같은 입력이 나오도록 표준 알고리즘을 그대로 둡니다.
/// </summary>
internal sealed class Mt19937
{
    private const int N = 624;
    private const int M = 397;
    private readonly uint[] _state = new uint[N];
    private int _index = N;

    public Mt19937(uint seed = 5489)
    {
        _state[0] = seed;
        for (int i = 1; i < N; i++)
        {
            _state[i] = 1812433253u * (_state[i - 1] ^ (_state[i - 1] >> 30)) + (uint)i;
        }
    }

    public uint Next()
    {
        if (_index >= N)
        {
            Twist();
        }
        uint y = _state[_index++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }

    private void Twist()
    {
        for (int i = 0; i < N; i++)
        {
            uint x = (_state[i] & 0x80000000u) + (_state[(i + 1) % N] & 0x7fffffffu);
            uint next = x >> 1;
            if ((x & 1) != 0)
            {
                next ^= 0x9908b0dfu;
            }
            _state[i] = _state[(i + M) % N] ^ next;
        }
        _index = 0;
    }
}
