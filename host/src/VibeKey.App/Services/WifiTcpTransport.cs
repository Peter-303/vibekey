using System;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>
/// WiFi TCP 通道（辅通道）。
///
/// 用途：配网。PC 连到设备的 AP 热点后，连 192.168.4.1:8266 下发家庭 WiFi 凭据；
/// 设备重启后用 STA 模式连上路由器，PC 再靠 UDP 发现拿到新 IP 继续连。
///
/// 设计文档 §6.2：
///   - 与 USB CDC 同一套帧格式，两边都能收业务数据；
///   - 下行指令（配网/语音/烧录）只走 USB——这条通道主要用来**收**状态与按键字符，
///     配网下发是个例外（设备处于 AP 态、没有 USB 连接时，只能走这条）。
/// </summary>
public sealed class WifiTcpTransport : IDeviceTransport
{
    /// <summary>AP 模式下设备的固定 IP。</summary>
    public const string ApModeIp = "192.168.4.1";

    /// <summary>TCP 服务端口，必须与固件 CONFIG_VIBEKEY_TCP_PORT 一致。</summary>
    public const int TcpPort = 8266;

    private readonly FrameStream _stream = new();
    private readonly object _gate = new();
    private TcpClient? _client;
    private CancellationTokenSource? _cts;
    private LinkState _state = LinkState.Disconnected;
    private bool _disposed;

    public WifiTcpTransport(string host)
    {
        Host = host ?? throw new ArgumentNullException(nameof(host));
    }

    /// <summary>目标 IP。AP 模式下用 <see cref="ApModeIp"/>。</summary>
    public string Host { get; }

    public TransportKind Kind => TransportKind.WifiTcp;

    public LinkState State => _state;

    /// <summary>人类可读的连接描述，如 "192.168.4.1:8266"。</summary>
    public string Description => $"{Host}:{TcpPort}";

    public event EventHandler<Frame>? FrameReceived;

    public event EventHandler<LinkState>? StateChanged;

    public void Open()
    {
        if (_state == LinkState.Connected)
        {
            return;
        }

        SetState(LinkState.Connecting);
        _cts = new CancellationTokenSource();
        _ = Task.Run(() => RunAsync(_cts.Token), _cts.Token);
    }

    public void Close()
    {
        _cts?.Cancel();
        lock (_gate)
        {
            CloseClientLocked();
        }

        _cts?.Dispose();
        _cts = null;
        SetState(LinkState.Disconnected);
    }

    public void Send(byte type, ushort seq, ReadOnlySpan<byte> payload)
    {
        TcpClient? client;
        NetworkStream? stream;
        lock (_gate)
        {
            client = _client;
            stream = client?.Connected == true ? client.GetStream() : null;
        }

        if (stream is null)
        {
            return;
        }

        var buf = new byte[Proto.FrameSize(payload.Length)];
        var n = FrameCodec.Encode(type, seq, payload, buf);
        try
        {
            stream.Write(buf, 0, n);
        }
        catch (Exception)
        {
            // 写入失败多半是设备重启了：断开让上层重连
            Close();
        }
    }

    private async Task RunAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                using var client = new TcpClient();
                await client.ConnectAsync(Host, TcpPort, ct);

                lock (_gate)
                {
                    _client = client;
                }

                SetState(LinkState.Connected);

                var stream = client.GetStream();
                var buf = new byte[512];
                while (!ct.IsCancellationRequested)
                {
                    int n;
                    try
                    {
                        n = await stream.ReadAsync(buf, ct);
                    }
                    catch (OperationCanceledException)
                    {
                        break;
                    }

                    if (n <= 0)
                    {
                        break;   // 对方关闭
                    }

                    _stream.Feed(buf.AsSpan(0, n), OnFrame);
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception)
            {
                // 连不上（设备重启中 / IP 变了）：等一会儿重试
            }
            finally
            {
                lock (_gate)
                {
                    CloseClientLocked();
                }

                if (_state == LinkState.Connected)
                {
                    SetState(LinkState.Disconnected);
                }
            }

            if (!ct.IsCancellationRequested)
            {
                try
                {
                    await Task.Delay(1500, ct);
                }
                catch (OperationCanceledException)
                {
                    return;
                }
            }
        }
    }

    private void OnFrame(Frame frame) => FrameReceived?.Invoke(this, frame);

    private void CloseClientLocked()
    {
        try
        {
            _client?.Close();
        }
        catch (Exception)
        {
            // 忽略
        }

        _client = null;
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

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Close();
        GC.SuppressFinalize(this);
    }
}
