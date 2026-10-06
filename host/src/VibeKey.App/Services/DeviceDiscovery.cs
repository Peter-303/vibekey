using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace VibeKey.App.Services;

/// <summary>发现到的设备。</summary>
/// <param name="Ip">设备 IP，AP 模式下为 192.168.4.1。</param>
/// <param name="Mac">设备 MAC，如 "A1B2C3D4E5F6"。</param>
/// <param name="Firmware">固件版本。</param>
public readonly record struct DiscoveredDevice(string Ip, string Mac, string Firmware)
{
    public override string ToString() => $"{Ip} (FW {Firmware})";
}

/// <summary>
/// UDP 广播发现 VibeKey 设备。
///
/// 为什么需要它：STA 模式下 PC 连的是路由器，设备 IP 由 DHCP 分配，PC 无从得知。
/// 让设备自报家门（设计文档 §6.2）——这是 PRD 缺失的一环，
/// 没有它用户每次都得去查路由器后台。
///
/// AP 模式下不需要发现：设备 IP 固定 192.168.4.1，直接连即可。
/// </summary>
public sealed class DeviceDiscovery : IDisposable
{
    /// <summary>设备发现端口，必须与固件 CONFIG_VIBEKEY_DISCOVERY_PORT 一致。</summary>
    public const int DiscoveryPort = 8267;

    private const string Query = "VIBEKEY_DISCOVER";
    private const string ReplyPrefix = "VIBEKEY_HERE";

    private readonly object _gate = new();
    private readonly Dictionary<string, DiscoveredDevice> _found = new(StringComparer.OrdinalIgnoreCase);
    private CancellationTokenSource? _cts;

    /// <summary>发现到新设备时触发（参数为设备 IP）。</summary>
    public event EventHandler<DiscoveredDevice>? DeviceFound;

    /// <summary>当前已发现的设备，按 IP 去重。</summary>
    public IReadOnlyList<DiscoveredDevice> Devices
    {
        get
        {
            lock (_gate)
            {
                return new List<DiscoveredDevice>(_found.Values);
            }
        }
    }

    /// <summary>
    /// 启动后台发现。会一直广播直到调用 <see cref="Stop"/>。
    /// </summary>
    /// <param name="intervalMs">广播间隔，建议 500ms。</param>
    public void Start(int intervalMs = 500)
    {
        Stop();
        _cts = new CancellationTokenSource();
        _ = Task.Run(() => LoopAsync(_cts.Token, intervalMs), _cts.Token);
    }

    public void Stop()
    {
        _cts?.Cancel();
        _cts?.Dispose();
        _cts = null;
    }

    /// <summary>
    /// 同步探测一轮：发一次广播，最多等 <paramref name="timeoutMs"/>。
    /// 适合"点了配网按钮，等 2 秒看有没有设备"这种一次性场景。
    /// </summary>
    public async Task<IReadOnlyList<DiscoveredDevice>> ProbeOnceAsync(int timeoutMs = 2000)
    {
        var result = new List<DiscoveredDevice>();
        using var client = new UdpClient();
        client.EnableBroadcast = true;
        client.Client.ReceiveTimeout = timeoutMs;

        var query = Encoding.ASCII.GetBytes(Query);
        var endpoint = new IPEndPoint(IPAddress.Broadcast, DiscoveryPort);

        // 直接连到 AP 时设备 IP 固定 192.168.4.1，也往这个地址发一份，
        // 因为某些网卡不转发 255.255.255.255 广播。
        var targets = new List<IPEndPoint> { endpoint };
        try
        {
            targets.Add(new IPEndPoint(IPAddress.Parse("192.168.4.1"), DiscoveryPort));
        }
        catch (FormatException)
        {
            // 理论上不会发生
        }

        var deadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMs);

        while (DateTime.UtcNow < deadline)
        {
            foreach (var t in targets)
            {
                try
                {
                    await client.SendAsync(query, query.Length, t);
                }
                catch (SocketException)
                {
                    // 某个目标不可达就跳过，继续试下一个
                }
            }

            var remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                break;
            }

            try
            {
                client.Client.ReceiveTimeout = (int)remaining.TotalMilliseconds;
                var remote = new IPEndPoint(IPAddress.Any, 0);
                var replyTask = client.ReceiveAsync();
                var completed = await Task.WhenAny(replyTask,
                    Task.Delay((int)remaining.TotalMilliseconds + 50));
                if (completed != replyTask)
                {
                    break;
                }

                var reply = await replyTask;
                var parsed = Parse(Encoding.ASCII.GetString(reply.Buffer));
                if (parsed is { } d && result.All(r => r.Ip != d.Ip))
                {
                    result.Add(d);
                }
            }
            catch (SocketException)
            {
                // 超时或不可达：本轮结束
                break;
            }
            catch (AggregateException)
            {
                break;
            }
        }

        foreach (var d in result)
        {
            Remember(d);
        }

        return result;
    }

    private async Task LoopAsync(CancellationToken ct, int intervalMs)
    {
        using var client = new UdpClient();
        client.EnableBroadcast = true;
        client.Client.ReceiveTimeout = intervalMs;

        var query = Encoding.ASCII.GetBytes(Query);
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await client.SendAsync(query, query.Length, new IPEndPoint(IPAddress.Broadcast, DiscoveryPort));
            }
            catch (Exception)
            {
                // 网络没就绪（比如没连 WiFi）：等下一轮
            }

            try
            {
                var reply = await client.ReceiveAsync(ct);
                var parsed = Parse(Encoding.ASCII.GetString(reply.Buffer));
                if (parsed is { } d && Remember(d))
                {
                    DeviceFound?.Invoke(this, d);
                }
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception)
            {
                // 超时：正常，继续下一轮
            }

            try
            {
                await Task.Delay(intervalMs, ct);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    /// <summary>记录设备，返回是否为首次发现。</summary>
    private bool Remember(DiscoveredDevice d)
    {
        lock (_gate)
        {
            if (_found.ContainsKey(d.Ip))
            {
                return false;
            }

            _found[d.Ip] = d;
            return true;
        }
    }

    /// <summary>解析 "VIBEKEY_HERE,IP=192.168.4.1,MAC=A1B2C3D4E5F6,FW=0.1.0"。</summary>
    internal static DiscoveredDevice? Parse(string text)
    {
        if (string.IsNullOrWhiteSpace(text) || !text.TrimStart().StartsWith(ReplyPrefix, StringComparison.Ordinal))
        {
            return null;
        }

        var fields = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var seg in text.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var eq = seg.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }

            fields[seg[..eq]] = seg[(eq + 1)..];
        }

        if (!fields.TryGetValue("IP", out var ip) || string.IsNullOrWhiteSpace(ip))
        {
            return null;
        }

        fields.TryGetValue("MAC", out var mac);
        fields.TryGetValue("FW", out var fw);
        return new DiscoveredDevice(ip, mac ?? string.Empty, fw ?? string.Empty);
    }

    public void Dispose()
    {
        Stop();
        GC.SuppressFinalize(this);
    }
}
