using System;
using System.Threading;
using System.Threading.Tasks;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>配网流程的当前阶段。</summary>
public enum ProvisionStage
{
    Idle,

    /// <summary>等待 PC 连到设备的 AP 热点。</summary>
    WaitForDeviceAp,

    /// <summary>已连上 AP，正在下发家庭 WiFi 凭据。</summary>
    SendingCredentials,

    /// <summary>已下发，等设备重启后连上家庭 WiFi 并自报新 IP。</summary>
    WaitingForReconnect,

    /// <summary>设备已连上家庭 WiFi，配网完成。</summary>
    Done,

    /// <summary>失败，原因见 <see cref="ProvisionProgress.Error"/>。</summary>
    Failed,
}

/// <summary>一次阶段推进的结果。</summary>
public readonly record struct ProvisionProgress(
    ProvisionStage Stage,
    string Message,
    string? Error = null,
    DiscoveredDevice? Device = null)
{
    public bool IsFinished => Stage is ProvisionStage.Done or ProvisionStage.Failed;
}

/// <summary>
/// 配网编排：把"等待 PC 连热点 → 下发凭据 → 等设备重启 → 发现新 IP"
/// 串成一个可驱动的流程。
///
/// 编排逻辑与传输分离：本类只做状态推进，真正的收发由传入的
/// <see cref="IDeviceTransport"/> 与 <see cref="DeviceDiscovery"/> 完成，
/// 因此可以用模拟传输完整单测。
/// </summary>
public sealed class ProvisioningFlow
{
    private readonly DeviceDiscovery _discovery;

    public ProvisioningFlow(DeviceDiscovery discovery)
    {
        _discovery = discovery ?? throw new ArgumentNullException(nameof(discovery));
    }

    public ProvisionStage Stage { get; private set; } = ProvisionStage.Idle;

    /// <summary>
    /// 完整跑一遍配网。
    /// </summary>
    /// <param name="apTransport">连到设备 AP 的通道（192.168.4.1:8266）。</param>
    /// <param name="ssid">家庭 WiFi SSID。</param>
    /// <param name="password">家庭 WiFi 密码。</param>
    /// <param name="onProgress">每进入一个阶段回调一次，用于界面更新。</param>
    /// <param name="ct">取消令牌。</param>
    public async Task<DiscoveredDevice?> RunAsync(
        IDeviceTransport apTransport,
        string ssid,
        string password,
        Action<ProvisionProgress> onProgress,
        CancellationToken ct = default)
    {
        ArgumentNullException.ThrowIfNull(apTransport);

        // ---- 阶段 1：确认 PC 已连到设备热点 ----
        Stage = ProvisionStage.WaitForDeviceAp;
        onProgress(new ProvisionProgress(Stage,
            "请先用 PC 连接到设备的 WiFi 热点（形如 VibeKey-xxxx），然后继续。"));

        apTransport.Open();
        if (apTransport.State != LinkState.Connected)
        {
            Stage = ProvisionStage.Failed;
            onProgress(new ProvisionProgress(Stage,
                "没有连到设备热点。请确认 PC 已连到 VibeKey-xxxx，然后重试。",
                Error: "AP 不可达"));
            return null;
        }

        // ---- 阶段 2：下发凭据 ----
        Stage = ProvisionStage.SendingCredentials;
        onProgress(new ProvisionProgress(Stage,
            $"正在把家庭 WiFi（{ssid}）的凭据发给设备…"));
        apTransport.Send(Proto.TWifiCfg, 1, FrameCodec.BuildWifiConfig(ssid, password));

        // 等设备回 ACK（也可能不等——设备收到后会立刻重启，连接随时会断）
        await Task.Delay(1500, ct);

        // ---- 阶段 3：等设备重启并连上家庭 WiFi ----
        Stage = ProvisionStage.WaitingForReconnect;
        onProgress(new ProvisionProgress(Stage,
            $"设备已收到凭据并重启。等待它连上「{ssid}」并自报新的 IP…"));

        apTransport.Close();
        _discovery.Start();

        // 轮询最多 60s：设备重启 + 连路由器通常 10-30s
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(60);
        DiscoveredDevice? found = null;
        while (DateTime.UtcNow < deadline)
        {
            ct.ThrowIfCancellationRequested();

            var devices = _discovery.Devices;
            foreach (var d in devices)
            {
                // AP 模式 IP 是 192.168.4.1；配网成功后应该是家庭网段的地址
                if (!d.Ip.StartsWith("192.168.4.", StringComparison.Ordinal))
                {
                    found = d;
                    break;
                }
            }

            if (found is not null)
            {
                break;
            }

            await Task.Delay(500, ct);
        }

        _discovery.Stop();

        if (found is null)
        {
            Stage = ProvisionStage.Failed;
            onProgress(new ProvisionProgress(Stage,
                $"等了 60 秒，设备没有连上「{ssid}」。\n"
                + "可能原因：密码错误 / 路由器信号弱 / 该网络不允许新设备接入。\n"
                + "设备会回退到 AP 模式（红蓝交替闪），可以重试配网。",
                Error: "STA 连接超时"));
            return null;
        }

        // ---- 阶段 4：完成 ----
        Stage = ProvisionStage.Done;
        onProgress(new ProvisionProgress(Stage,
            $"配网完成。设备已连上「{ssid}」，IP 是 {found.Value.Ip}。\n"
            + "蓝灯应该已经常亮。",
            Device: found));
        return found;
    }
}
