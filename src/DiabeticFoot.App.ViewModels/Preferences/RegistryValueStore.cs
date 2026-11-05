using System.Runtime.Versioning;
using System.Security;
using Microsoft.Win32;

namespace DiabeticFoot.App.Preferences;

/// <summary>
/// 현재 사용자의 레지스트리(HKCU\Software\deepmedi\hwlab)에 값을 남깁니다.
/// 키의 '/'는 하위 키 구분이고 마지막 조각이 값 이름입니다(예: "shot/root"는 하위 키 shot의 값 root).
/// </summary>
/// <remarks>
/// 위치와 문자열 형식이 Qt 앱(QSettings)과 같아 문자열과 bool 값은 Qt 앱이 남긴 값을 이어 읽습니다.
/// 크기와 사각형은 Qt 앱이 이진 형식으로 남겨 읽지 못하므로 처음 한 번 다시 정해집니다.
/// </remarks>
[SupportedOSPlatform("windows")]
public sealed class RegistryValueStore : IValueStore
{
    public const string DefaultPath = @"Software\deepmedi\hwlab";

    private readonly string _path;

    public RegistryValueStore(string path = DefaultPath)
    {
        _path = path;
    }

    public bool IsPersistent => true;

    public string? Get(string key)
    {
        var (subKey, name) = Split(key);
        try
        {
            using var opened = Registry.CurrentUser.OpenSubKey(subKey, writable: false);
            return opened?.GetValue(name) as string;
        }
        catch (Exception e) when (e is SecurityException or UnauthorizedAccessException or IOException)
        {
            return null;
        }
    }

    public void Set(string key, string value)
    {
        var (subKey, name) = Split(key);
        try
        {
            using var opened = Registry.CurrentUser.CreateSubKey(subKey, writable: true);
            opened.SetValue(name, value, RegistryValueKind.String);
        }
        catch (Exception e) when (e is SecurityException or UnauthorizedAccessException or IOException)
        {
        }
    }

    private (string SubKey, string Name) Split(string key)
    {
        var cut = key.LastIndexOf('/');
        if (cut < 0) return (_path, key);
        return (_path + "\\" + key[..cut].Replace('/', '\\'), key[(cut + 1)..]);
    }
}
