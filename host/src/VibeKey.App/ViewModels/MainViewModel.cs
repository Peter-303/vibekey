using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Windows;
using VibeKey.App.Models;
using VibeKey.App.Services;

namespace VibeKey.App.ViewModels;

/// <summary>极简 INotifyPropertyChanged 基类（不引第三方包，减少还原风险）。</summary>
public abstract class ObservableObject : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(name);
        return true;
    }
}

/// <summary>一条日志。</summary>
public sealed class LogLine(DateTime time, string text, bool isError = false)
{
    public DateTime Time { get; } = time;

    public string Text { get; } = text;

    public bool IsError { get; } = isError;

    public string Stamp => Time.ToString("HH:mm:ss");

    /// <summary>完整一行，便于 ListBox 直接绑定。</summary>
    public string Line => $"{Stamp}  {Text}";
}

/// <summary>主窗口 ViewModel。</summary>
public sealed class MainViewModel : ObservableObject
{
    private readonly MappingStore _store = new();
    private readonly ActionExecutor _executor = new();
    private DeviceSession? _session;
    private IDeviceTransport? _transport;
    private bool _useSimulator = true;
    private string _comPort = "";
    private bool _isBusy;
    private string _lastAsrText = "";
    private KeyMapping? _selected;
    private PortJumpWatcher? _jumpWatcher;
    private System.Threading.Timer? _jumpTimer;
    private readonly DeviceDiscovery _discovery = new();
    private bool _isScanning;
    private string _wifiSsid = "";
    private string _wifiPassword = "";
    private bool _isProvisioning;
    private string _provisionStatus = "";
    private readonly AutoStartService _autoStart = new();
    private bool _autoStartEnabled;
    private string _firmwarePath = "";
    private bool _isFlashing;
    private string _flashStatus = "";
    private double _flashProgress;
    private DateTime _jumpStartedAt;

    public MainViewModel()
    {
        Profile = _store.Load();
        Mappings = new ObservableCollection<KeyMapping>(Profile.Mappings);
        Selected = Mappings.FirstOrDefault();

        _executor.VoiceRequested += OnVoiceRequested;

        Log("就绪。先用「模拟设备」试跑，不需要插硬件。");
    }

    public ObservableCollection<KeyMapping> Mappings { get; }

    public MappingProfile Profile { get; }

    public ObservableCollection<LogLine> Logs { get; } = new();

    /// <summary>
    /// UI 线程派发器。传输层事件在后台线程触发，而 Execute() 要调 SendKeys、
    /// 还要动 ObservableCollection，都必须回到 UI 线程。
    /// 全限定名以避免与 System.Windows.Forms.Application 冲突
    /// （csproj 里开了 UseWindowsForms 以复用 SendKeys）。
    /// </summary>
    private System.Windows.Threading.Dispatcher UiDispatcher =>
        System.Windows.Application.Current?.Dispatcher
        ?? System.Windows.Threading.Dispatcher.CurrentDispatcher;

    /// <summary>可用的 COM 口列表（PRD 6.1：CDC 设备枚举为 COMx）。</summary>
    public ObservableCollection<string> AvailablePorts { get; } = new();

    /// <summary>可选的动作类型（PRD 6.4 要求的五类）。</summary>
    public IReadOnlyList<ActionType> AvailableActions { get; } = new[]
    {
        ActionType.Text,
        ActionType.Hotkey,
        ActionType.OpenTarget,
        ActionType.Voice,
        ActionType.Clipboard,
    };

    public DeviceStatus Status { get; } = new();

    public KeyMapping? Selected
    {
        get => _selected;
        set => Set(ref _selected, value);
    }

    /// <summary>是否使用模拟设备（无硬件也能开发 UI）。</summary>
    public bool UseSimulator
    {
        get => _useSimulator;
        set
        {
            if (Set(ref _useSimulator, value))
            {
                OnPropertyChanged(nameof(CanConnect));
            }
        }
    }

    public string ComPort
    {
        get => _comPort;
        set => Set(ref _comPort, value);
    }

    public bool IsBusy
    {
        get => _isBusy;
        set => Set(ref _isBusy, value);
    }

    public string LastAsrText
    {
        get => _lastAsrText;
        set => Set(ref _lastAsrText, value);
    }

    /// <summary>是否正在扫描设备。</summary>
    public bool IsScanning
    {
        get => _isScanning;
        private set => Set(ref _isScanning, value);
    }

    /// <summary>扫描到的设备（AP 模式下应是 192.168.4.1）。</summary>
    public ObservableCollection<DiscoveredDevice> DiscoveredDevices { get; } = new();

    /// <summary>家庭 WiFi 的 SSID（配网输入）。</summary>
    public string WifiSsid
    {
        get => _wifiSsid;
        set => Set(ref _wifiSsid, value);
    }

    /// <summary>家庭 WiFi 的密码（配网输入）。</summary>
    public string WifiPassword
    {
        get => _wifiPassword;
        set => Set(ref _wifiPassword, value);
    }

    /// <summary>是否正在配网。</summary>
    public bool IsProvisioning
    {
        get => _isProvisioning;
        private set => Set(ref _isProvisioning, value);
    }

    /// <summary>配网当前阶段的状态描述，显示在界面上。</summary>
    public string ProvisionStatus
    {
        get => _provisionStatus;
        private set => Set(ref _provisionStatus, value);
    }

    /// <summary>是否开机自启。读取时从注册表取，写入时同步到注册表（PRD 6.5）。</summary>
    public bool AutoStartEnabled
    {
        get => _autoStartEnabled;
        set
        {
            if (Set(ref _autoStartEnabled, value))
            {
                ApplyAutoStart(value);
            }
        }
    }

    /// <summary>把当前的自启设置同步到注册表。</summary>
    private void ApplyAutoStart(bool enabled)
    {
        try
        {
            if (enabled)
            {
                _autoStart.Enable(AutoStartService.CurrentExePath);
                Log("已设置开机自启");
            }
            else
            {
                _autoStart.Disable();
                Log("已取消开机自启");
            }
        }
        catch (Exception ex)
        {
            Log($"设置自启失败：{ex.Message}", isError: true);
        }
    }

    /// <summary>启动时同步一次注册表里的当前状态到界面。</summary>
    public void SyncAutoStartFromRegistry()
    {
        _autoStartEnabled = _autoStart.IsEnabled();
        OnPropertyChanged(nameof(AutoStartEnabled));
    }

    /// <summary>要烧录的固件镜像文件路径。</summary>
    public string FirmwarePath
    {
        get => _firmwarePath;
        set => Set(ref _firmwarePath, value);
    }

    /// <summary>是否正在烧录。</summary>
    public bool IsFlashing
    {
        get => _isFlashing;
        private set => Set(ref _isFlashing, value);
    }

    /// <summary>烧录当前阶段的状态描述。</summary>
    public string FlashStatus
    {
        get => _flashStatus;
        private set => Set(ref _flashStatus, value);
    }

    /// <summary>烧录进度（0..1），用于进度条。</summary>
    public double FlashProgress
    {
        get => _flashProgress;
        private set => Set(ref _flashProgress, value);
    }

    public bool IsConnected => _session?.IsOnline == true;

    public bool CanConnect => UseSimulator || !string.IsNullOrWhiteSpace(ComPort);

    // ---------------- 连接 ----------------

    public void Connect()
    {
        RefreshPorts();

        if (_session is not null)
        {
            Disconnect();
        }

        _transport = UseSimulator ? new SimulatedTransport() : CreateUsbTransport(ComPort);
        _session = new DeviceSession(_transport);

        _session.KeyEventReceived += OnKeyEvent;
        _session.AsrTextReceived += OnAsrText;
        _session.AckReceived += OnAck;

        _session.Start();
        Log($"已连接：{_transport.Description}");
        OnPropertyChanged(nameof(IsConnected));
    }

    public void Disconnect()
    {
        if (_session is null)
        {
            return;
        }

        _session.KeyEventReceived -= OnKeyEvent;
        _session.AsrTextReceived -= OnAsrText;
        _session.AckReceived -= OnAck;
        _session.Stop();
        _session.Dispose();
        _session = null;
        _transport = null;
        Log("已断开");
        OnPropertyChanged(nameof(IsConnected));
    }

    /// <summary>
    /// 枚举系统串口。USB CDC 设备由 Windows 自带 usbser.sys 驱动枚举，
    /// 安装包不携带任何第三方驱动（PRD 6.6）。
    /// </summary>
    public void RefreshPorts()
    {
        var ports = System.IO.Ports.SerialPort.GetPortNames();
        Array.Sort(ports, StringComparer.OrdinalIgnoreCase);

        AvailablePorts.Clear();
        foreach (var p in ports)
        {
            AvailablePorts.Add(p);
        }

        if (AvailablePorts.Count > 0 && string.IsNullOrWhiteSpace(ComPort))
        {
            ComPort = AvailablePorts[0];
        }

        OnPropertyChanged(nameof(CanConnect));
    }

    private static IDeviceTransport CreateUsbTransport(string comPort)
    {
        // USB CDC 通道：按 VID 303A / PID 4001 枚举，断线自动重连（设计文档 §6.1）
        return new UsbCdcTransport();
    }

    // ---------------- 事件处理 ----------------

    private void OnKeyEvent(object? sender, char key)
    {
        var mapping = Mappings.FirstOrDefault(m => m.Key == key);
        if (mapping is null)
        {
            UiDispatcher.Invoke(() => Log($"收到未配置的按键 '{key}'", isError: true));
            return;
        }

        UiDispatcher.Invoke(() =>
        {
            Log($"按键 {mapping.Label}（'{key}'）→ {mapping.Describe()}");
            var result = _executor.Execute(mapping);
            if (result != ExecuteResult.Ok)
            {
                Log($"动作执行失败：{result}", isError: true);
            }
        });
    }

    private void OnAsrText(object? sender, AsrText text)
    {
        UiDispatcher.Invoke(() =>
        {
            LastAsrText = text.Text;
            Log($"ASR{(text.IsFinal ? "（最终）" : "（中间）")}：{text.Text}");
        });
    }

    private void OnAck(object? sender, Ack ack)
    {
        UiDispatcher.Invoke(() =>
        {
            if (ack.Ok)
            {
                Log($"指令已被设备接受（SEQ {ack.Seq}）");
            }
            else
            {
                Log($"设备拒绝指令（SEQ {ack.Seq}）：{ack.Reason}", isError: true);
            }
        });
    }

    private void OnVoiceRequested(object? sender, bool start)
    {
        if (_session is null)
        {
            Log("未连接设备，语音动作已忽略", isError: true);
            return;
        }

        if (start)
        {
            _session.VoiceStart();
        }
        else
        {
            _session.VoiceStop();
        }
    }
    /// <summary>
    /// 扫描一圈设备（约 2 秒）。用于配网：PC 连到设备的 AP 后点这个，
    /// 应能发现 192.168.4.1。
    /// </summary>
    public async Task ScanDevicesAsync()
    {
        if (IsScanning)
        {
            return;
        }

        IsScanning = true;
        DiscoveredDevices.Clear();
        Log("开始扫描 VibeKey 设备…");

        try
        {
            var found = await _discovery.ProbeOnceAsync(2000);
            foreach (var d in found)
            {
                DiscoveredDevices.Add(d);
            }

            if (found.Count == 0)
            {
                Log("没有发现设备。请确认 PC 已连到 VibeKey-xxxx 热点，"
                    + "或设备已连上家庭 WiFi 且与 PC 在同一网段。", isError: true);
            }
            else
            {
                Log($"发现 {found.Count} 台设备：" +
                    string.Join("、", found.Select(d => d.ToString())));
            }
        }
        catch (Exception ex)
        {
            Log($"扫描失败：{ex.Message}", isError: true);
        }
        finally
        {
            IsScanning = false;
        }
    }

    /// <summary>
    /// 启动完整配网流程（连 AP -> 下发凭据 -> 等设备重启 -> 发现新 IP）。
    /// 前置条件：PC 已连到设备的 VibeKey-xxxx 热点。
    /// </summary>
    public async Task StartProvisioningAsync()
    {
        if (IsProvisioning)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(WifiSsid))
        {
            Log("请先填写家庭 WiFi 的名称（SSID）", isError: true);
            return;
        }

        if (WifiPassword.Length < 8)
        {
            Log("密码至少 8 位", isError: true);
            return;
        }

        IsProvisioning = true;
        ProvisionStatus = "准备中…";
        Log("开始配网。请先确认 PC 已连到设备的 VibeKey-xxxx 热点。");

        try
        {
            using var discovery = new DeviceDiscovery();
            var flow = new ProvisioningFlow(discovery);
            using var apTransport = new WifiTcpTransport(WifiTcpTransport.ApModeIp);

            var device = await flow.RunAsync(
                apTransport,
                WifiSsid.Trim(),
                WifiPassword,
                p => UiDispatcher.Invoke(() =>
                {
                    ProvisionStatus = p.Message;
                    Log(p.Message, isError: p.Stage == ProvisionStage.Failed);
                }));

            if (device is not null)
            {
                Log($"设备已上线：{device.Value}");
            }
        }
        catch (OperationCanceledException)
        {
            Log("配网已取消");
        }
        catch (Exception ex)
        {
            ProvisionStatus = $"出错：{ex.Message}";
            Log($"配网失败：{ex.Message}", isError: true);
        }
        finally
        {
            IsProvisioning = false;
        }
    }

    /// <summary>选择要烧录的固件镜像（.bin）。</summary>
    public void SetFirmwarePath(string path)
    {
        FirmwarePath = path;
        if (File.Exists(path))
        {
            var size = new FileInfo(path).Length;
            Log($"已选择固件：{path}（{size} 字节）");
        }
        else
        {
            Log($"文件不存在：{path}", isError: true);
        }
    }

    /// <summary>
    /// 完整烧录：进入下载模式 -> 等端口跳变 -> 写入 -> 校验 -> 重启。
    /// 二次确认由 View 层负责（PRD 6.3）。
    /// </summary>
    public async Task StartFlashingAsync()
    {
        if (IsFlashing)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(FirmwarePath) || !File.Exists(FirmwarePath))
        {
            Log("请先选择一个有效的固件镜像（.bin）", isError: true);
            return;
        }

        if (_session is null || _transport is not UsbCdcTransport usb)
        {
            Log("烧录需要设备通过 USB 连接", isError: true);
            return;
        }

        IsFlashing = true;
        FlashStatus = "准备中…";
        FlashProgress = 0;

        try
        {
            // ---- 1. 下发进入下载模式指令 ----
            Log("第 1 步：请求设备进入下载模式…");
            FlashStatus = "进入下载模式…";
            RequestEnterDfu();

            // ---- 2. 等端口跳变 ----
            FlashStatus = "等待设备重启并重新枚举…";
            Log("第 2 步：等待 COM 口跳变…");

            // 等 _jumpWatcher 找到新口（RequestEnterDfu 内部已启动轮询）
            var foundPort = await WaitForDownloadPortAsync(usb, TimeSpan.FromSeconds(20));
            if (foundPort is null)
            {
                throw new InvalidOperationException("等待下载口超时，设备没有进入下载模式");
            }

            Log($"下载模式就绪：{foundPort}");

            // ---- 3. 读镜像信息 ----
            FlashStatus = "读取固件信息…";
            var size = new FileInfo(FirmwarePath).Length;
            var sha = ComputeSha256File(FirmwarePath);
            var image = new FirmwareImage
            {
                Path = FirmwarePath,
                Version = Path.GetFileNameWithoutExtension(FirmwarePath),
                SizeBytes = size,
                Sha256Hex = sha,
                Address = 0x10000,
            };

            // ---- 4. 烧录 ----
            Log("第 3 步：开始烧录…");
            using var flasher = new Flasher();
            await flasher.FlashAsync(foundPort, image, p => UiDispatcher.Invoke(() =>
            {
                FlashStatus = p.Message;
                FlashProgress = p.Fraction;
                if (p.Stage == FlashStage.Done || p.Stage == FlashStage.Failed)
                {
                    Log(p.Message, isError: p.Stage == FlashStage.Failed);
                }
            }));

            FlashProgress = 1.0;
        }
        catch (OperationCanceledException)
        {
            FlashStatus = "已取消";
            Log("烧录已取消");
        }
        catch (Exception ex)
        {
            FlashStatus = $"失败：{ex.Message}";
            Log($"烧录失败：{ex.Message}", isError: true);
        }
        finally
        {
            IsFlashing = false;
        }
    }

    /// <summary>等 PortJumpWatcher 找到下载口。</summary>
    private async Task<string?> WaitForDownloadPortAsync(UsbCdcTransport usb, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var devices = usb.EnumerateAllDevices();
            foreach (var d in devices)
            {
                if (d.Matches(PortJumpWatcher.DownloadVid, PortJumpWatcher.DownloadPid))
                {
                    return d.PortName;
                }
            }

            await Task.Delay(300);
        }

        return null;
    }

    private static string ComputeSha256File(string path)
    {
        using var sha = System.Security.Cryptography.SHA256.Create();
        using var fs = File.OpenRead(path);
        var hash = sha.ComputeHash(fs);
        return BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();
    }

    /// <summary>界面上的「开始录音」按钮。</summary>
    public void VoiceStart()
    {
        _session?.VoiceStart();
        Log("已请求开始录音");
    }

    /// <summary>界面上的「停止录音」按钮。</summary>
    public void VoiceStop()
    {
        _session?.VoiceStop();
        Log("已请求停止录音");
    }


    // ---------------- 映射编辑 ----------------

    public void SaveMappings()
    {
        _store.Save(Profile);
        Log($"映射已保存到 {_store.Path}");
    }

    public void ExportMappings(string destination)
    {
        _store.Export(Profile, destination);
        Log($"已导出到 {destination}");
    }

    public void ImportMappings(string source)
    {
        var (profile, error) = _store.Import(source);
        if (profile is null)
        {
            Log($"导入失败：{error}", isError: true);
            return;
        }

        Profile.Mappings.Clear();
        Profile.Mappings.AddRange(profile.Mappings);
        Profile.Name = profile.Name;
        RefreshMappings();
        Log($"已从 {source} 导入 {profile.Mappings.Count} 条映射");
    }

    private void RefreshMappings()
    {
        Mappings.Clear();
        foreach (var m in Profile.Mappings)
        {
            Mappings.Add(m);
        }

        Selected = Mappings.FirstOrDefault();
    }

    // ---------------- 烧录 ----------------

    /// <summary>
    /// 请求进入烧录模式。二次确认由 View 层负责（PRD 6.3）。
    ///
    /// 硬件已确认为设计文档 §7.1 情形 A：USB-C 的 D+/D- 接到 GPIO19/20，无 USB-TTL 桥。
    /// 因此下发 magic word 后设备会重启进下载模式，COM 口从 TinyUSB CDC（303A:4001）
    /// 跳变为 ROM 的 USB Serial/JTAG（303A:1001）——同一个 USB-C 口，线不用拔，
    /// 但 COM 口名会变。这里负责等新口出现。
    /// </summary>
    public void RequestEnterDfu()
    {
        if (_session is null)
        {
            Log("未连接设备", isError: true);
            return;
        }

        if (_transport is not UsbCdcTransport usb)
        {
            Log("烧录指令只接受来自 USB 通道（设计文档 §7.1）", isError: true);
            return;
        }

        Log("已下发进入烧录模式指令，等待设备重启并重新枚举…");

        // 记录本轮开始前就已存在的下载态口，避免误认上一轮遗留的口
        _jumpWatcher = new PortJumpWatcher();
        _jumpWatcher.Begin(usb.EnumerateAllDevices());
        _jumpStartedAt = DateTime.UtcNow;

        _session.RequestEnterDfu();

        // 每 300ms 轮询一次，最多等 15s
        _jumpTimer?.Dispose();
        _jumpTimer = new System.Threading.Timer(_ => OnJumpPoll(usb), null,
            TimeSpan.FromMilliseconds(300), TimeSpan.FromMilliseconds(300));
    }

    private void OnJumpPoll(UsbCdcTransport usb)
    {
        var watcher = _jumpWatcher;
        if (watcher is null)
        {
            return;
        }

        var r = watcher.Poll(usb.EnumerateAllDevices(), DateTime.UtcNow - _jumpStartedAt);

        switch (r.State)
        {
            case PortJumpState.Found:
                StopJumpPolling();
                Log($"下载模式就绪，串口已跳变为 {r.PortName}。可以开始写入固件。");
                Log($"（原串口 {usb.PortName ?? "?"} 已消失，这是正常现象，线不用拔）");
                break;

            case PortJumpState.TimedOut:
                StopJumpPolling();
                Log("等待下载口超时（15s）。请检查 USB 线，或重新插拔一次后重试。", isError: true);
                break;

            case PortJumpState.WaitingForDownloadPort:
            case PortJumpState.Idle:
            default:
                break;
        }
    }

    private void StopJumpPolling()
    {
        _jumpTimer?.Dispose();
        _jumpTimer = null;
        _jumpWatcher = null;
    }

    // ---------------- 日志 ----------------

    public void Log(string text, bool isError = false)
    {
        var dispatcher = System.Windows.Application.Current?.Dispatcher;
        if (dispatcher is null)
        {
            return;
        }

        dispatcher.Invoke(() =>
        {
            Logs.Insert(0, new LogLine(DateTime.Now, text, isError));
            while (Logs.Count > 200)
            {
                Logs.RemoveAt(Logs.Count - 1);
            }
        });
    }

    public void ClearLog() => Logs.Clear();
}
