using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>
/// USB CDC 虚拟串口通道（主通道）。
///
/// PRD 5.5：TinyUSB CDC，Windows 10 1809+ / 11 自带 usbser.sys 驱动，
/// 安装包不携带任何第三方驱动。设备按 VID/PID 自动识别（PRD 6.1）。
///
/// 设计文档 §6.1：断线重连——设备移除即标记离线，重新出现即自动重连，
/// 不要求用户拔插。
/// </summary>
public sealed class UsbCdcTransport : IDeviceTransport
{
    /// <summary>工作态设备的 VID/PID（TinyUSB CDC，Espressif 默认值）。</summary>
    public const ushort DeviceVid = PortJumpWatcher.WorkVid;   // 0x303A

    public const ushort DevicePid = PortJumpWatcher.WorkPid;   // 0x4001

    private readonly ISerialDeviceEnumerator _enumerator;
    private readonly Func<string, ISerialPort> _portFactory;
    private readonly System.Threading.Timer _reconnectTimer;
    private readonly FrameStream _stream = new();
    private readonly object _gate = new();
    private ISerialPort? _port;
    private LinkState _state = LinkState.Disconnected;
    private bool _disposed;

    public UsbCdcTransport(
        ISerialDeviceEnumerator? enumerator = null,
        Func<string, ISerialPort>? portFactory = null)
    {
        _enumerator = enumerator ?? new WmiSerialDeviceEnumerator();
        _portFactory = portFactory ?? (name => new RealSerialPort(name));

        // 每 1.5s 尝试一次自动重连；已连接时不做任何事
        _reconnectTimer = new System.Threading.Timer(_ => TryReconnect(), null, Timeout.Infinite, Timeout.Infinite);
    }

    public TransportKind Kind => TransportKind.UsbCdc;

    public LinkState State => _state;

    /// <summary>人类可读的连接描述，如 "COM12"。</summary>
    public string Description { get; private set; } = "未连接";

    /// <summary>当前打开的 COM 口名；未连接时为 null。</summary>
    public string? PortName { get; private set; }

    public event EventHandler<Frame>? FrameReceived;

    public event EventHandler<LinkState>? StateChanged;

    /// <summary>
    /// 枚举当前所有 VibeKey 工作态设备。
    /// </summary>
    public IReadOnlyList<SerialDeviceInfo> EnumerateDevices()
        => _enumerator.Enumerate()
            .Where(d => d.Matches(DeviceVid, DevicePid))
            .ToList();

    /// <summary>
    /// 枚举所有串口设备（不按 VID/PID 过滤）。
    /// 端口跳变侦测需要它：要记录"本轮开始前就已存在的下载态口"，
    /// 才能把本轮真正新出现的下载态口认出来（设计文档 §7.1）。
    /// </summary>
    public IReadOnlyList<SerialDeviceInfo> EnumerateAllDevices()
        => _enumerator.Enumerate();

    public void Open()
    {
        var devices = EnumerateDevices();
        var target = devices.FirstOrDefault();

        if (target.Equals(default(SerialDeviceInfo)))
        {
            // 没有设备：进入"等待设备出现"模式，由定时器负责重连
            SetState(LinkState.Disconnected);
            Description = "未发现设备";
            _reconnectTimer.Change(TimeSpan.FromMilliseconds(500), TimeSpan.FromMilliseconds(1500));
            return;
        }

        OpenPort(target.PortName);
    }

    /// <summary>打开指定的 COM 口（供界面手动选择）。</summary>
    public void OpenPort(string portName)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            throw new ArgumentException("COM 口名不能为空", nameof(portName));
        }

        lock (_gate)
        {
            ClosePortLocked();
        }

        SetState(LinkState.Connecting);
        Description = $"正在打开 {portName}…";

        try
        {
            var port = _portFactory(portName);
            port.Open();
            port.DataReceived += OnPortData;

            lock (_gate)
            {
                _port = port;
            }

            PortName = portName;
            Description = portName;
            SetState(LinkState.Connected);
            _reconnectTimer.Change(Timeout.Infinite, Timeout.Infinite);
        }
        catch (Exception ex)
        {
            SetState(LinkState.Disconnected);
            Description = $"打开 {portName} 失败：{ex.Message}";
            _reconnectTimer.Change(TimeSpan.FromMilliseconds(500), TimeSpan.FromMilliseconds(1500));
        }
    }

    public void Close()
    {
        _reconnectTimer.Change(Timeout.Infinite, Timeout.Infinite);
        lock (_gate)
        {
            ClosePortLocked();
        }

        PortName = null;
        Description = "未连接";
        SetState(LinkState.Disconnected);
    }

    public void Send(byte type, ushort seq, ReadOnlySpan<byte> payload)
    {
        ISerialPort? port;
        lock (_gate)
        {
            port = _port;
        }

        if (port is null || !port.IsOpen)
        {
            return;
        }

        var buf = new byte[Proto.FrameSize(payload.Length)];
        var n = FrameCodec.Encode(type, seq, payload, buf);
        try
        {
            port.Write(n == buf.Length ? buf : buf.AsSpan(0, n).ToArray());
        }
        catch (Exception)
        {
            // 写入失败多半是设备刚被拔掉：交给重连逻辑处理
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _reconnectTimer.Change(Timeout.Infinite, Timeout.Infinite);
        _reconnectTimer.Dispose();
        lock (_gate)
        {
            ClosePortLocked();
        }

        GC.SuppressFinalize(this);
    }

    private void OnPortData(object? sender, byte[] data)
    {
        _stream.Feed(data, OnFrame);
    }

    private void OnFrame(Frame frame)
    {
        FrameReceived?.Invoke(this, frame);
    }

    private void TryReconnect()
    {
        if (_disposed)
        {
            return;
        }

        // 已连接但串口被拔掉：SerialPort.IsOpen 可能仍为 true，用能否枚举到来判断更可靠
        var devices = EnumerateDevices();

        if (_state == LinkState.Connected)
        {
            var stillThere = devices.Any(d => string.Equals(d.PortName, PortName, StringComparison.OrdinalIgnoreCase));
            if (stillThere)
            {
                return;
            }

            // 设备被拔掉：断开并进入等待
            lock (_gate)
            {
                ClosePortLocked();
            }

            PortName = null;
            Description = "设备已断开，等待重新连接…";
            SetState(LinkState.Disconnected);
            return;
        }

        var target = devices.FirstOrDefault();
        if (!target.Equals(default(SerialDeviceInfo)))
        {
            OpenPort(target.PortName);
        }
    }

    private void ClosePortLocked()
    {
        if (_port is null)
        {
            return;
        }

        try
        {
            _port.DataReceived -= OnPortData;
            _port.Dispose();
        }
        catch (Exception)
        {
            // 忽略
        }

        _port = null;
    }

    private void SetState(LinkState s)
    {
        if (_state == s)
        {
            return;
        }

        _state = s;
        StateChanged?.Invoke(this, s);
    }
}
