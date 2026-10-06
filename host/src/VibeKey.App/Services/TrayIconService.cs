using System;
using System.Drawing;
using System.Windows;
using System.Windows.Forms;

namespace VibeKey.App.Services;

/// <summary>
/// 系统托盘图标。
///
/// PRD 6.5：自启时窗口默认最小化到托盘，不打扰用户；
/// 托盘图标左键展开状态，右键退出。
///
/// 用 WinForms 的 NotifyIcon——WPF 本身不带托盘组件，
/// 而 csproj 里已经开了 UseWindowsForms（本来就为了 SendKeys），
/// 直接复用不用再引包。
/// </summary>
public sealed class TrayIconService : IDisposable
{
    private readonly Window _mainWindow;
    private NotifyIcon? _icon;
    private bool _disposed;

    public TrayIconService(Window mainWindow)
    {
        _mainWindow = mainWindow ?? throw new ArgumentNullException(nameof(mainWindow));
    }

    /// <summary>当前是否已最小化到托盘。</summary>
    public bool IsInTray { get; private set; }

    public event EventHandler? ShowRequested;

    public event EventHandler? ExitRequested;

    public void Create()
    {
        if (_icon is not null)
        {
            return;
        }

        _icon = new NotifyIcon
        {
            Text = "VibeKey",
            Visible = true,
            Icon = LoadIcon(),
            ContextMenuStrip = BuildMenu(),
        };

        _icon.MouseClick += OnIconMouseClick;
    }

    /// <summary>最小化到托盘（自启时的默认行为）。</summary>
    public void MinimizeToTray()
    {
        if (_icon is null)
        {
            return;
        }

        _mainWindow.Hide();
        _mainWindow.ShowInTaskbar = false;
        IsInTray = true;
    }

    /// <summary>从托盘恢复窗口。</summary>
    public void ShowWindow()
    {
        _mainWindow.ShowInTaskbar = true;
        _mainWindow.Show();
        _mainWindow.WindowState = WindowState.Normal;
        _mainWindow.Activate();
        IsInTray = false;
    }

    private void OnIconMouseClick(object? sender, MouseEventArgs e)
    {
        if (e.Button == MouseButtons.Left)
        {
            ShowWindow();
            ShowRequested?.Invoke(this, EventArgs.Empty);
        }
    }

    private ContextMenuStrip BuildMenu()
    {
        var menu = new ContextMenuStrip();

        var showItem = new ToolStripMenuItem("展开", null, (_, _) =>
        {
            ShowWindow();
            ShowRequested?.Invoke(this, EventArgs.Empty);
        });

        var exitItem = new ToolStripMenuItem("退出", null, (_, _) =>
        {
            ExitRequested?.Invoke(this, EventArgs.Empty);
        });

        menu.Items.Add(showItem);
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add(exitItem);
        return menu;
    }

    private static Icon LoadIcon()
    {
        // 优先用应用自带的图标；没有的话用系统默认
        try
        {
            var path = System.IO.Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory, "vibekey.ico");
            if (System.IO.File.Exists(path))
            {
                return new Icon(path);
            }
        }
        catch (Exception)
        {
            // 回退到系统图标
        }

        return SystemIcons.Application;
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        if (_icon is not null)
        {
            _icon.MouseClick -= OnIconMouseClick;
            _icon.Visible = false;
            _icon.Dispose();
            _icon = null;
        }

        GC.SuppressFinalize(this);
    }
}
