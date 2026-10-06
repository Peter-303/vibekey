using System.Windows;
using System.Windows.Threading;
using VibeKey.App.Services;
using VibeKey.App.ViewModels;

namespace VibeKey.App;

public partial class App : System.Windows.Application
{
    private MainViewModel? _vm;
    private TrayIconService? _tray;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // 初始化主题：把色板（C# 创建的 SolidColorBrush）放进
        // Application.Resources 直接 Resources，样式字典（BaseStyles.xaml）
        // 放进 MergedDictionaries。
        ThemeManager.SetTheme(ThemeManager.Light);

        _vm = new MainViewModel();
        MainWindow = new Views.MainWindow { DataContext = _vm };

        // 托盘：自启时最小化到托盘（PRD 6.5）
        _tray = new TrayIconService(MainWindow);
        _tray.Create();
        _tray.ExitRequested += (_, _) => Shutdown();

        var minimized = Array.Exists(e.Args, a =>
            string.Equals(a, "--minimized", StringComparison.OrdinalIgnoreCase));
        if (minimized)
        {
            _tray.MinimizeToTray();
            _vm.Log("已在托盘待命（自启模式）");
        }
        else
        {
            MainWindow.Show();
        }
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _tray?.Dispose();
        _vm?.Disconnect();
        base.OnExit(e);
    }

    private void App_OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        _vm?.Log($"发生未处理异常：{e.Exception.Message}", isError: true);
        e.Handled = true;
    }
}
