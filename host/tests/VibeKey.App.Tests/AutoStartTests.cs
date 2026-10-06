using System;
using System.IO;
using VibeKey.App.Services;
using Xunit;

namespace VibeKey.App.Tests;

/// <summary>
/// 开机自启测试。
///
/// 直接读写当前用户的 HKCU Run 键——测试前后都把状态还原，
/// 不会影响用户机器上的真实自启设置。
/// </summary>
public sealed class AutoStartTests : IDisposable
{
    private readonly AutoStartService _svc = new();
    private readonly string? _originalPath;
    private readonly bool _originalEnabled;

    public AutoStartTests()
    {
        _originalEnabled = _svc.IsEnabled();
        _originalPath = _svc.CurrentPath();
    }

    [Fact]
    public void Enable_Then_Disable_RoundTrip()
    {
        var fakeExe = Path.Combine(Path.GetTempPath(), "vibekey-test.exe");
        File.WriteAllText(fakeExe, "fake");
        try
        {
            _svc.Enable(fakeExe, "--minimized");
            Assert.True(_svc.IsEnabled());
            Assert.Contains(fakeExe, _svc.CurrentPath());
            Assert.Contains("--minimized", _svc.CurrentPath());

            _svc.Disable();
            Assert.False(_svc.IsEnabled());
            Assert.Null(_svc.CurrentPath());
        }
        finally
        {
            File.Delete(fakeExe);
        }
    }

    [Fact]
    public void Enable_Without_Arguments_Has_No_Trailing_Args()
    {
        var fakeExe = Path.Combine(Path.GetTempPath(), "vibekey-test.exe");
        File.WriteAllText(fakeExe, "fake");
        try
        {
            _svc.Enable(fakeExe, "");
            Assert.Equal($"\"{fakeExe}\"", _svc.CurrentPath());
        }
        finally
        {
            _svc.Disable();
            File.Delete(fakeExe);
        }
    }

    [Fact]
    public void Enable_Rejects_Missing_File()
    {
        Assert.Throws<FileNotFoundException>(() => _svc.Enable(@"C:\path\that\does\not\exist.exe"));
    }

    [Fact]
    public void Enable_Rejects_Empty_Path()
    {
        Assert.Throws<ArgumentException>(() => _svc.Enable(""));
        Assert.Throws<ArgumentException>(() => _svc.Enable("   "));
    }

    [Fact]
    public void Disable_Is_Idempotent()
    {
        _svc.Disable();
        _svc.Disable();
        Assert.False(_svc.IsEnabled());
    }

    [Fact]
    public void CurrentExePath_Points_To_Running_Process()
    {
        var path = AutoStartService.CurrentExePath;
        Assert.False(string.IsNullOrWhiteSpace(path));
        Assert.True(File.Exists(path));
    }

    public void Dispose()
    {
        if (_originalEnabled && !string.IsNullOrEmpty(_originalPath))
        {
            var fakeExe = _originalPath.Trim('"').Split('"')[0];
            if (File.Exists(fakeExe))
            {
                _svc.Enable(fakeExe);
            }
        }
        else
        {
            _svc.Disable();
        }
    }
}
