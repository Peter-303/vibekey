using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using VibeKey.App.ViewModels;

namespace VibeKey.App.Views;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm = new();

    public MainWindow()
    {
        InitializeComponent();
        DataContext = _vm;
        Loaded += OnLoaded;
        Closing += OnClosing;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // 启动时刷新一次 COM 口列表，让默认选中第一个
        _vm.RefreshPorts();
    }

    private void OnClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        _vm.Disconnect();
    }

    // ---------------- 连接 ----------------

    /// <summary>扫描设备（配网用）。异步方法由事件处理器触发，不阻塞 UI。</summary>
    private async void OnScanDevices(object sender, RoutedEventArgs e)
    {
        await _vm.ScanDevicesAsync();
    }

    // PasswordBox 出于安全不支持绑定，只能在事件里同步给 ViewModel
    private void OnWifiPasswordChanged(object sender, RoutedEventArgs e)
    {
        _vm.WifiPassword = WifiPasswordBox.Password;
    }

    private async void OnStartProvisioning(object sender, RoutedEventArgs e)
    {
        await _vm.StartProvisioningAsync();
    }

    private void OnConnect(object sender, RoutedEventArgs e)
    {
        _vm.Connect();
    }

    private void OnDisconnect(object sender, RoutedEventArgs e)
    {
        _vm.Disconnect();
    }

    // ---------------- 语音 ----------------

    private void OnVoiceStart(object sender, RoutedEventArgs e)
    {
        _vm.VoiceStart();
    }

    private void OnVoiceStop(object sender, RoutedEventArgs e)
    {
        _vm.VoiceStop();
    }

    // ---------------- 烧录 ----------------

    private void OnBrowseFirmware(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = "选择固件镜像",
            Filter = "ESP32 固件 (*.bin)|*.bin|所有文件 (*.*)|*.*",
            CheckFileExists = true,
        };
        if (dlg.ShowDialog(this) == true)
        {
            _vm.SetFirmwarePath(dlg.FileName);
        }
    }

    private async void OnStartFlashing(object sender, RoutedEventArgs e)
    {
        // PRD 6.3：烧录入口需二次确认
        var result = System.Windows.MessageBox.Show(
            "这将擦除当前固件并烧录新固件。\n\n"
            + $"镜像：{_vm.FirmwarePath}\n"
            + "过程：进入下载模式 -> 擦除 -> 写入 -> 校验 -> 重启\n\n"
            + "烧录过程中请勿拔 USB 线。确定继续？",
            "烧录固件",
            System.Windows.MessageBoxButton.YesNo,
            System.Windows.MessageBoxImage.Warning);

        if (result == System.Windows.MessageBoxResult.Yes)
        {
            await _vm.StartFlashingAsync();
        }
    }

    private void OnEnterDfu(object sender, RoutedEventArgs e)
    {
        // PRD 6.3：烧录入口需二次确认
        var result = System.Windows.MessageBox.Show(
            "这将擦除当前固件并进入下载模式。\n\n" +
            "· 固件镜像需另行准备（本期从上位机发起，写入由后续版本实现）\n" +
            "· 设备会重启并重新枚举为新的 COM 口\n" +
            "· 拔线会导致烧录中断\n\n" +
            "确定继续？",
            "进入烧录模式",
            System.Windows.MessageBoxButton.YesNo,
            System.Windows.MessageBoxImage.Warning);

        if (result == System.Windows.MessageBoxResult.Yes)
        {
            _vm.RequestEnterDfu();
        }
    }

    private void OnFlashHelp(object sender, RoutedEventArgs e)
    {
        System.Windows.MessageBox.Show(
            "命令行烧录（面向开发者）\n\n" +
            "1. 安装 ESP-IDF，然后：\n" +
            "     idf.py set-target esp32s3\n" +
            "     idf.py build\n" +
            "     idf.py -p COMx flash monitor\n\n" +
            "2. 或直接用 esptool：\n" +
            "     esptool.py --port COMx write_flash 0x10000 build/vibekey.bin\n\n" +
            "硬件默认运行在工作态，普通按键、串口误触、WiFi 报文都不能触发烧录态；\n" +
            "只有上位机通过 USB 下发带 magic word 的指令才会进入下载模式。\n\n" +
            "详细说明见 docs/00-技术设计文档.md §7.1",
            "烧录教程",
            System.Windows.MessageBoxButton.OK,
            System.Windows.MessageBoxImage.Information);
    }

    // ---------------- 映射 ----------------

    private void OnSaveMappings(object sender, RoutedEventArgs e)
    {
        _vm.SaveMappings();
    }

    private void OnExportMappings(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.SaveFileDialog
        {
            Title = "导出映射配置",
            Filter = "VibeKey 映射配置 (*.json)|*.json|所有文件 (*.*)|*.*",
            FileName = "vibekey-mappings.json",
            DefaultExt = ".json",
            AddExtension = true,
        };
        if (dlg.ShowDialog(this) == true)
        {
            _vm.ExportMappings(dlg.FileName);
        }
    }

    private void OnImportMappings(object sender, RoutedEventArgs e)
    {
        var dlg = new Microsoft.Win32.OpenFileDialog
        {
            Title = "导入映射配置",
            Filter = "VibeKey 映射配置 (*.json)|*.json|所有文件 (*.*)|*.*",
            CheckFileExists = true,
        };
        if (dlg.ShowDialog(this) == true)
        {
            if (System.Windows.MessageBox.Show(
                    "导入会覆盖当前所有按键映射，确定继续？",
                    "导入映射配置",
                    System.Windows.MessageBoxButton.YesNo,
                    System.Windows.MessageBoxImage.Question) == System.Windows.MessageBoxResult.Yes)
            {
                _vm.ImportMappings(dlg.FileName);
            }
        }
    }

    // ---------------- 日志 ----------------

    private void OnClearLog(object sender, RoutedEventArgs e)
    {
        _vm.ClearLog();
    }
}
