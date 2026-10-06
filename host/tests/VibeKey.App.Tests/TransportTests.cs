using System;
using System.Collections.Generic;
using System.Linq;
using VibeKey.App.Services;
using Xunit;

namespace VibeKey.App.Tests;

/// <summary>假的设备枚举器，按脚本返回设备列表。</summary>
public sealed class FakeEnumerator : ISerialDeviceEnumerator
{
    private readonly Queue<IReadOnlyList<SerialDeviceInfo>> _script = new();
    private IReadOnlyList<SerialDeviceInfo> _current = Array.Empty<SerialDeviceInfo>();

    public void Set(params SerialDeviceInfo[] devices) => _current = devices.ToList();

    public void Enqueue(params SerialDeviceInfo[] devices) => _script.Enqueue(devices.ToList());

    public IReadOnlyList<SerialDeviceInfo> Enumerate()
        => _script.Count > 0 ? _script.Dequeue() : _current;
}

/// <summary>假的串口，记录写入的字节，可手动触发 DataReceived。</summary>
public sealed class FakeSerialPort : ISerialPort
{
    public List<byte> Written { get; } = new();

    public bool IsOpen { get; private set; }

    public string PortName { get; }

    public bool ThrowOnOpen { get; set; }

    public FakeSerialPort(string portName) => PortName = portName;

    public void Open()
    {
        if (ThrowOnOpen)
        {
            throw new InvalidOperationException("端口被占用");
        }

        IsOpen = true;
    }

    public void Close() => IsOpen = false;

    public void Write(byte[] data) => Written.AddRange(data);

    public void RaiseData(byte[] data) => DataReceived?.Invoke(this, data);

    public event EventHandler<byte[]>? DataReceived;

    public void Dispose() => IsOpen = false;
}

/// <summary>
/// 端口跳变测试。对应设计文档 §7.1 情形 A（硬件已确认：USB-C 接 GPIO19/20，无 USB-TTL 桥）。
/// </summary>
public sealed class PortJumpWatcherTests
{
    private static SerialDeviceInfo Work(string port) => new(port, 0x303A, 0x4001, "VibeKey");
    private static SerialDeviceInfo Download(string port) => new(port, 0x303A, 0x1001, "USB JTAG/serial debug unit");
    private static SerialDeviceInfo Other(string port) => new(port, 0x10C4, 0xEA60, "CP2102");

    [Fact]
    public void Finds_New_Download_Port()
    {
        var w = new PortJumpWatcher();
        w.Begin(new[] { Work("COM3") });

        var r = w.Poll(new[] { Work("COM3"), Download("COM7") }, TimeSpan.FromSeconds(1));

        Assert.True(r.IsFound);
        Assert.Equal("COM7", r.PortName);
        Assert.Equal(PortJumpState.Found, w.State);
    }

    [Fact]
    public void Ignores_PreExisting_Download_Port()
    {
        var w = new PortJumpWatcher();
        // 上一轮遗留的下载态口已存在
        w.Begin(new[] { Work("COM3"), Download("COM7") });

        var r = w.Poll(new[] { Work("COM3"), Download("COM7") }, TimeSpan.FromSeconds(1));

        Assert.False(r.IsFound);
        Assert.Equal(PortJumpState.WaitingForDownloadPort, r.State);
    }

    [Fact]
    public void Times_Out()
    {
        var w = new PortJumpWatcher { Timeout = TimeSpan.FromSeconds(15) };
        w.Begin(new[] { Work("COM3") });

        var r = w.Poll(new[] { Work("COM3") }, TimeSpan.FromSeconds(16));

        Assert.Equal(PortJumpState.TimedOut, r.State);
        Assert.Null(r.PortName);
    }

    [Fact]
    public void Still_Waiting_Before_Timeout()
    {
        var w = new PortJumpWatcher { Timeout = TimeSpan.FromSeconds(15) };
        w.Begin(new[] { Work("COM3") });

        var r = w.Poll(new[] { Work("COM3") }, TimeSpan.FromSeconds(2));

        Assert.Equal(PortJumpState.WaitingForDownloadPort, r.State);
    }

    [Fact]
    public void Ignores_Non_Matching_Devices()
    {
        var w = new PortJumpWatcher();
        w.Begin(Array.Empty<SerialDeviceInfo>());

        var r = w.Poll(new[] { Other("COM5"), Work("COM6") }, TimeSpan.FromSeconds(1));

        Assert.False(r.IsFound);
    }

    [Fact]
    public void Reset_Allows_Next_Round()
    {
        var w = new PortJumpWatcher { Timeout = TimeSpan.FromSeconds(1) };
        w.Begin(Array.Empty<SerialDeviceInfo>());
        w.Poll(Array.Empty<SerialDeviceInfo>(), TimeSpan.FromSeconds(2));
        Assert.Equal(PortJumpState.TimedOut, w.State);

        w.Reset();
        Assert.Equal(PortJumpState.Idle, w.State);

        w.Begin(Array.Empty<SerialDeviceInfo>());
        var r = w.Poll(new[] { Download("COM9") }, TimeSpan.Zero);
        Assert.True(r.IsFound);
        Assert.Equal("COM9", r.PortName);
    }

    [Fact]
    public void Port_Name_Comparison_Is_Case_Insensitive()
    {
        var w = new PortJumpWatcher();
        w.Begin(new[] { Download("com7") });

        // 同一个口但大小写不同，仍应视为"已存在"
        var r = w.Poll(new[] { Download("COM7") }, TimeSpan.Zero);
        Assert.False(r.IsFound);
    }
}

/// <summary>USB CDC 传输测试（用假枚举器与假串口，不碰真实硬件）。</summary>
public sealed class UsbCdcTransportTests
{
    private static SerialDeviceInfo VibeKeyDevice(string port) => new(port, 0x303A, 0x4001, "VibeKey");
    private static SerialDeviceInfo Other(string port) => new(port, 0x10C4, 0xEA60, "CP2102");

    [Fact]
    public void EnumerateDevices_Filters_By_VidPid()
    {
        var enum_ = new FakeEnumerator();
        enum_.Set(VibeKeyDevice("COM3"), Other("COM4"), VibeKeyDevice("COM5"));
        using var t = new UsbCdcTransport(enum_, _ => new FakeSerialPort("COM3"));

        var found = t.EnumerateDevices();

        Assert.Equal(2, found.Count);
        Assert.Contains(found, d => d.PortName == "COM3");
        Assert.Contains(found, d => d.PortName == "COM5");
    }

    [Fact]
    public void Open_Connects_When_Device_Present()
    {
        var enum_ = new FakeEnumerator();
        enum_.Set(VibeKeyDevice("COM3"));
        var port = new FakeSerialPort("COM3");
        using var t = new UsbCdcTransport(enum_, _ => port);

        t.Open();

        Assert.Equal(LinkState.Connected, t.State);
        Assert.Equal("COM3", t.Description);
        Assert.Equal("COM3", t.PortName);
        Assert.True(port.IsOpen);
    }

    [Fact]
    public void Open_No_Device_Stays_Disconnected()
    {
        var enum_ = new FakeEnumerator();
        using var t = new UsbCdcTransport(enum_, _ => new FakeSerialPort("COM3"));

        t.Open();

        Assert.Equal(LinkState.Disconnected, t.State);
        Assert.Equal("未发现设备", t.Description);
    }

    [Fact]
    public void Open_Port_Failure_Reports_Error()
    {
        var enum_ = new FakeEnumerator();
        var bad = new FakeSerialPort("COM3") { ThrowOnOpen = true };
        using var t = new UsbCdcTransport(enum_, _ => bad);

        t.OpenPort("COM3");

        Assert.Equal(LinkState.Disconnected, t.State);
        Assert.Contains("失败", t.Description);
    }

    [Fact]
    public void Received_Bytes_Are_Decoded_Into_Frames()
    {
        var enum_ = new FakeEnumerator();
        enum_.Set(VibeKeyDevice("COM3"));
        var port = new FakeSerialPort("COM3");
        using var t = new UsbCdcTransport(enum_, _ => port);

        var frames = new List<VibeKey.Protocol.Frame>();
        t.FrameReceived += (_, f) => frames.Add(f);

        t.Open();

        // 构造一个"按键 K1"帧喂进去
        var buf = new byte[VibeKey.Protocol.Proto.FrameSize(1)];
        var n = VibeKey.Protocol.FrameCodec.Encode(VibeKey.Protocol.Proto.TKeyEvent, 1, "1"u8, buf);
        port.RaiseData(buf.AsSpan(0, n).ToArray());

        Assert.Single(frames);
        Assert.Equal(VibeKey.Protocol.Proto.TKeyEvent, frames[0].Type);
        Assert.Equal((ushort)1, frames[0].Seq);
        Assert.Equal("1", frames[0].PayloadAsText());
    }

    [Fact]
    public void Send_Encodes_Frame()
    {
        var enum_ = new FakeEnumerator();
        enum_.Set(VibeKeyDevice("COM3"));
        var port = new FakeSerialPort("COM3");
        using var t = new UsbCdcTransport(enum_, _ => port);

        t.Open();
        t.Send(VibeKey.Protocol.Proto.TPing, 42, ReadOnlySpan<byte>.Empty);

        // SEQ=42 的 ping 帧，CRC 已用 CRC16/CCITT-FALSE 实算核对
        Assert.Equal(
            new byte[] { 0xAA, 0x00, 0x00, 0x10, 0x2A, 0x00, 0x42, 0xBB },
            port.Written.ToArray());
    }

    [Fact]
    public void Send_When_Not_Connected_Is_NoOp()
    {
        var enum_ = new FakeEnumerator();
        var port = new FakeSerialPort("COM3");
        using var t = new UsbCdcTransport(enum_, _ => port);

        t.Send(VibeKey.Protocol.Proto.TPing, 1, ReadOnlySpan<byte>.Empty);

        Assert.Empty(port.Written);
    }

    [Fact]
    public void Close_Clears_State()
    {
        var enum_ = new FakeEnumerator();
        enum_.Set(VibeKeyDevice("COM3"));
        var port = new FakeSerialPort("COM3");
        using var t = new UsbCdcTransport(enum_, _ => port);

        t.Open();
        t.Close();

        Assert.Equal(LinkState.Disconnected, t.State);
        Assert.False(port.IsOpen);
        Assert.Null(t.PortName);
    }
}

/// <summary>
/// 真实串口封装的测试（不打开真实端口，只验证不变量）。
/// </summary>
public sealed class RealSerialPortTests
{
    [Fact]
    public void Ctor_Rejects_Null_PortName()
    {
        Assert.Throws<ArgumentNullException>(() => new RealSerialPort(null!));
    }

    [Fact]
    public void Not_Open_Before_Open_Called()
    {
        using var p = new RealSerialPort("COM999");
        Assert.False(p.IsOpen);
        Assert.Equal("COM999", p.PortName);
    }
}
