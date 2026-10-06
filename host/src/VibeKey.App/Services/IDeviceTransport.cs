using System;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>传输通道类型。</summary>
public enum TransportKind
{
    /// <summary>模拟设备：无硬件也能开发 UI（PRD 9 章可测试性要求）。</summary>
    Simulated,

    /// <summary>USB CDC 虚拟串口（主通道）。</summary>
    UsbCdc,

    /// <summary>WiFi TCP（辅通道）。</summary>
    WifiTcp,
}

/// <summary>连接状态。</summary>
public enum LinkState
{
    Disconnected,
    Connecting,
    Connected,
}

/// <summary>设备通道抽象。USB / WiFi / 模拟设备走同一条上层代码路径。</summary>
public interface IDeviceTransport : IDisposable
{
    TransportKind Kind { get; }

    LinkState State { get; }

    /// <summary>人类可读的连接描述，如 "COM12" 或 "192.168.1.23:8266"。</summary>
    string Description { get; }

    /// <summary>打开通道。</summary>
    void Open();

    /// <summary>关闭通道。</summary>
    void Close();

    /// <summary>发送一个下行帧。同一帧只发一次，不涉及双通道去重。</summary>
    void Send(byte type, ushort seq, ReadOnlySpan<byte> payload);

    /// <summary>收到一个完整上行帧。</summary>
    event EventHandler<Frame>? FrameReceived;

    /// <summary>链路状态变化。</summary>
    event EventHandler<LinkState>? StateChanged;
}
