namespace DiabeticFoot.Platform;

/// <summary>LED 보드가 받는 명령과 직렬 조건입니다.</summary>
public static class LightProtocol
{
    /// <summary>LED 보드의 직렬 조건입니다. 115200bps 8N1, 흐름 제어 없음입니다(기존 앱 docs/LIGHT.md).</summary>
    public static readonly SerialConfig Serial = new(BaudRate: 115200);

    /// <summary>
    /// 밝기 명령을 다 내보낼 때까지 기다리는 시간입니다. 115200bps에서 2바이트는 0.2ms 안에 나가므로 넘으면 장치가
    /// 빠진 것으로 봅니다.
    /// </summary>
    public const int WriteTimeoutMs = 150;

    public const int MinPercent = 0;
    public const int MaxPercent = 100;

    const byte Header = 0xFE;

    /// <summary>
    /// 밝기를 절대값으로 정하는 명령을 만듭니다. 0보다 작으면 0, 100보다 크면 100으로 자릅니다.
    /// 첫 바이트는 항상 0xFE이고 둘째 바이트가 밝기(%)라 FE 00(0%)부터 FE 64(100%)까지입니다.
    /// </summary>
    /// <remarks>
    /// 보드에는 10%씩 올리고 내리는 명령(FE B1, FE B0)도 있지만 쓰지 않습니다. 현재 값을 읽는 명령이 없어 명령 하나가
    /// 빠지면 보낸 값과 실제 밝기가 달라지고 되돌릴 방법이 없기 때문입니다.
    /// </remarks>
    public static byte[] MakeCommand(int percent) => [Header, (byte)ClampPercent(percent)];

    /// <summary>0~100으로 자른 밝기입니다. MakeCommand가 보내는 값과 같습니다.</summary>
    public static int ClampPercent(int percent) => Math.Clamp(percent, MinPercent, MaxPercent);
}
