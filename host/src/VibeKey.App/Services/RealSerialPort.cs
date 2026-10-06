using System;
using System.IO.Ports;
using System.Threading;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>
/// 对 <see cref="System.IO.Ports.SerialPort"/> 的薄封装，实现 <see cref="ISerialPort"/>。
/// </summary>
public sealed class RealSerialPort : ISerialPort
{
    private readonly SerialPort _inner;

    public RealSerialPort(string portName, int baudRate = 115200)
    {
        PortName = portName ?? throw new ArgumentNullException(nameof(portName));
        _inner = new SerialPort(portName, baudRate)
        {
            // 键盘固件用 TinyUSB CDC，不需要流控，DTR/RTS 也不参与复位逻辑
            DtrEnable = false,
            RtsEnable = false,
            ReadTimeout = 500,
            WriteTimeout = 500,
        };
        _inner.DataReceived += OnDataReceived;
    }

    public string PortName { get; }

    public bool IsOpen => _inner.IsOpen;

    public event EventHandler<byte[]>? DataReceived;

    public void Open() => _inner.Open();

    public void Close()
    {
        if (_inner.IsOpen)
        {
            _inner.Close();
        }
    }

    public void Write(byte[] data) => _inner.Write(data, 0, data.Length);

    private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            var n = _inner.BytesToRead;
            if (n <= 0)
            {
                return;
            }

            var buf = new byte[n];
            var read = _inner.Read(buf, 0, n);
            if (read > 0)
            {
                if (read == n)
                {
                    DataReceived?.Invoke(this, buf);
                }
                else
                {
                    var trimmed = new byte[read];
                    Array.Copy(buf, trimmed, read);
                    DataReceived?.Invoke(this, trimmed);
                }
            }
        }
        catch (Exception)
        {
            // 设备可能在读取瞬间被拔掉：忽略本次，断线由轮询逻辑发现
        }
    }

    public void Dispose()
    {
        _inner.DataReceived -= OnDataReceived;
        try
        {
            if (_inner.IsOpen)
            {
                _inner.Close();
            }
        }
        catch (Exception)
        {
            // 忽略关闭时的异常
        }

        _inner.Dispose();
        GC.SuppressFinalize(this);
    }
}
