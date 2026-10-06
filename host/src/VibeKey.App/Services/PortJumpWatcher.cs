using System;
using System.Collections.Generic;
using System.Linq;

namespace VibeKey.App.Services;

/// <summary>端口跳变状态机的当前状态。</summary>
public enum PortJumpState
{
    /// <summary>未开始。</summary>
    Idle,

    /// <summary>已下发"进入烧录"指令，正在等下载口出现。</summary>
    WaitingForDownloadPort,

    /// <summary>找到了下载口。</summary>
    Found,

    /// <summary>超时仍未找到。</summary>
    TimedOut,
}

/// <summary>一次轮询的结果。</summary>
public readonly record struct PortJumpResult(PortJumpState State, string? PortName)
{
    public bool IsFound => State == PortJumpState.Found && !string.IsNullOrEmpty(PortName);
}

/// <summary>
/// 烧录时的"COM 口跳变"处理（设计文档 §7.1 情形 A，硬件已确认为该情形）。
///
/// 背景：工作态设备跑 TinyUSB CDC（VID 303A / PID 4001），呈现为 COMx；
/// 下发 magic word 后设备 esp_restart() 进下载模式，ROM 启用 USB Serial/JTAG
/// （VID 303A / PID 1001），呈现为一个**新的 COMy**。同一个 USB-C 口，线不用拔，
/// 但 COM 口名会变。上位机若不处理，用户 100% 看到"连接失败"。
///
/// 本类只做"何时算找到新口"的判定，不含任何串口操作，因此可完整单测。
/// </summary>
public sealed class PortJumpWatcher
{
    /// <summary>工作态（TinyUSB CDC）的 VID/PID。</summary>
    public const ushort WorkVid = 0x303A;

    public const ushort WorkPid = 0x4001;

    /// <summary>下载态（ROM USB Serial/JTAG）的 VID/PID。</summary>
    public const ushort DownloadVid = 0x303A;

    public const ushort DownloadPid = 0x1001;

    private readonly HashSet<string> _preExistingDownloadPorts = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>等待下载口出现的超时时间。</summary>
    public TimeSpan Timeout { get; init; } = TimeSpan.FromSeconds(15);

    public PortJumpState State { get; private set; } = PortJumpState.Idle;

    /// <summary>找到的下载口名；未找到时为 null。</summary>
    public string? DownloadPort { get; private set; }

    /// <summary>
    /// 开始等待。应在下发 magic word 之前或之后立即调用。
    /// </summary>
    /// <param name="currentDevices">
    /// 当前已枚举到的设备列表。用于记录"已经存在的下载口"，
    /// 避免把上一轮遗留的下载态口误当成本轮新出现的口。
    /// </param>
    public void Begin(IReadOnlyList<SerialDeviceInfo> currentDevices)
    {
        _preExistingDownloadPorts.Clear();
        if (currentDevices is not null)
        {
            foreach (var d in currentDevices.Where(x => x.Matches(DownloadVid, DownloadPid)))
            {
                _preExistingDownloadPorts.Add(d.PortName);
            }
        }

        State = PortJumpState.WaitingForDownloadPort;
        DownloadPort = null;
    }

    /// <summary>
    /// 轮询一次。调用方按固定间隔（建议 300ms）反复调用，直到返回非 Waiting。
    /// </summary>
    /// <param name="devices">本次枚举到的设备。</param>
    /// <param name="elapsed">距 <see cref="Begin"/> 以来经过的时间。</param>
    public PortJumpResult Poll(IReadOnlyList<SerialDeviceInfo> devices, TimeSpan elapsed)
    {
        if (State != PortJumpState.WaitingForDownloadPort)
        {
            return new PortJumpResult(State, DownloadPort);
        }

        if (devices is not null)
        {
            foreach (var d in devices)
            {
                if (!d.Matches(DownloadVid, DownloadPid))
                {
                    continue;
                }

                if (_preExistingDownloadPorts.Contains(d.PortName))
                {
                    continue;   // 本轮开始前就已存在，不是新出现的
                }

                State = PortJumpState.Found;
                DownloadPort = d.PortName;
                return new PortJumpResult(State, DownloadPort);
            }
        }

        if (elapsed >= Timeout)
        {
            State = PortJumpState.TimedOut;
            return new PortJumpResult(State, null);
        }

        return new PortJumpResult(PortJumpState.WaitingForDownloadPort, null);
    }

    /// <summary>复位，允许下一轮烧录重新开始。</summary>
    public void Reset()
    {
        _preExistingDownloadPorts.Clear();
        State = PortJumpState.Idle;
        DownloadPort = null;
    }
}
