using System;
using System.IO;
using Microsoft.Win32;

namespace VibeKey.App.Services;

/// <summary>
/// 开机自启管理。
///
/// PRD 6.5：勾选"开机自动启动"后在 Windows 注册表
/// HKCU\Software\Microsoft\Windows\CurrentVersion\Run 写入启动项；
/// 取消勾选即删除。自启时最小化到托盘，不打扰用户。
///
/// 用 HKCU 而不是 HKLM：本应用不需要管理员权限（PRD 6.6 的安装向导
/// 也不要求提权），写入用户自己的注册表项就够了。
/// </summary>
public sealed class AutoStartService
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "VibeKey";

    /// <summary>当前是否已设置开机自启。</summary>
    public bool IsEnabled()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: false);
        var value = key?.GetValue(ValueName) as string;
        return !string.IsNullOrEmpty(value);
    }

    /// <summary>获取当前自启时指向的可执行文件路径（未设置返回 null）。</summary>
    public string? CurrentPath()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: false);
        return key?.GetValue(ValueName) as string;
    }

    /// <summary>
    /// 设置开机自启。
    /// </summary>
    /// <param name="exePath">要启动的可执行文件完整路径。</param>
    /// <param name="arguments">附加参数，默认传 "--minimized" 让它直接最小化到托盘。</param>
    public void Enable(string exePath, string arguments = "--minimized")
    {
        if (string.IsNullOrWhiteSpace(exePath))
        {
            throw new ArgumentException("可执行文件路径不能为空", nameof(exePath));
        }

        if (!File.Exists(exePath))
        {
            throw new FileNotFoundException($"可执行文件不存在：{exePath}", exePath);
        }

        var cmd = string.IsNullOrWhiteSpace(arguments)
            ? $"\"{exePath}\""
            : $"\"{exePath}\" {arguments}";

        using var key = Registry.CurrentUser.CreateSubKey(RunKeyPath, writable: true);
        key.SetValue(ValueName, cmd, RegistryValueKind.String);
    }

    /// <summary>取消开机自启。即使之前没设置过也能安全调用。</summary>
    public void Disable()
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, writable: true);
        key?.DeleteValue(ValueName, throwOnMissingValue: false);
    }

    /// <summary>当前正在运行的本应用的可执行文件路径。
    /// 不用 Assembly.Location——单文件发布（PublishSingleFile）下它返回空串。</summary>
    public static string CurrentExePath
        => System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName
           ?? throw new InvalidOperationException("无法确定当前可执行文件路径");
}
