using System;

namespace VibeKey.App.Services;

/// <summary>一个枚举到的串口设备。</summary>
/// <param name="PortName">COM 口名，如 "COM12"。</param>
/// <param name="Vid">USB 厂商 ID。</param>
/// <param name="Pid">USB 产品 ID。</param>
/// <param name="Description">设备描述，用于界面显示。</param>
public readonly record struct SerialDeviceInfo(string PortName, ushort Vid, ushort Pid, string Description)
{
    /// <summary>是否匹配指定的 VID/PID。</summary>
    public bool Matches(ushort vid, ushort pid) => Vid == vid && Pid == pid;

    public override string ToString() => $"{PortName} (VID {Vid:X4} PID {Pid:X4})";
}

/// <summary>串口设备枚举器抽象。Windows 实现用 WMI，测试用假实现。</summary>
public interface ISerialDeviceEnumerator
{
    /// <summary>枚举当前所有以 COM 口形式出现的设备。</summary>
    IReadOnlyList<SerialDeviceInfo> Enumerate();
}

/// <summary>串口抽象，便于测试（真实实现包一层 System.IO.Ports.SerialPort）。</summary>
public interface ISerialPort : IDisposable
{
    string PortName { get; }

    bool IsOpen { get; }

    /// <summary>打开串口。失败应抛出异常，由调用方转成用户可读的错误。</summary>
    void Open();

    void Close();

    /// <summary>写入字节。</summary>
    void Write(byte[] data);

    /// <summary>收到数据。参数为本次读到的字节。</summary>
    event EventHandler<byte[]>? DataReceived;
}
