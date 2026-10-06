using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using VibeKey.App.Services;
using VibeKey.Protocol;
using Xunit;

namespace VibeKey.App.Tests;

/// <summary>假传输：连接状态可控，记录发送的帧。</summary>
internal sealed class FakeProvisionTransport : IDeviceTransport
{
    private LinkState _state = LinkState.Disconnected;

    public bool ConnectSucceeds { get; set; } = true;

    public List<Frame> Sent { get; } = new();

    public TransportKind Kind => TransportKind.WifiTcp;

    public LinkState State => _state;

    public string Description => "fake";

    public event EventHandler<Frame>? FrameReceived
    {
        add { }
        remove { }
    }

    public event EventHandler<LinkState>? StateChanged;

    public void Open()
    {
        _state = ConnectSucceeds ? LinkState.Connected : LinkState.Disconnected;
        StateChanged?.Invoke(this, _state);
    }

    public void Close() => _state = LinkState.Disconnected;

    public void Send(byte type, ushort seq, ReadOnlySpan<byte> payload)
    {
        Sent.Add(new Frame { Type = type, Seq = seq, Payload = payload.ToArray() });
    }

    public void Dispose() { }
}

/// <summary>
/// 配网流程测试。编排逻辑用假传输验证，不依赖真实设备。
/// 真实设备的端到端联调属于上板测试，不在这里覆盖。
/// </summary>
public sealed class ProvisioningFlowTests
{
    [Fact]
    public async Task Fails_When_Pc_Not_On_Device_Ap()
    {
        using var discovery = new DeviceDiscovery();
        var flow = new ProvisioningFlow(discovery);
        var transport = new FakeProvisionTransport { ConnectSucceeds = false };

        var progress = new List<ProvisionProgress>();
        var device = await flow.RunAsync(transport, "MyHome", "password123", p => progress.Add(p));

        Assert.Null(device);
        Assert.Equal(ProvisionStage.Failed, flow.Stage);
        Assert.Contains(progress, p => p.Stage == ProvisionStage.WaitForDeviceAp);
        Assert.Contains(progress, p => p.Stage == ProvisionStage.Failed && p.Error == "AP 不可达");

        // AP 都连不上，不该下发任何凭据
        Assert.Empty(transport.Sent);
    }

    [Fact]
    public async Task Sends_Credentials_When_Ap_Reachable()
    {
        using var discovery = new DeviceDiscovery();
        var flow = new ProvisioningFlow(discovery);
        var transport = new FakeProvisionTransport();

        // 环境里没有真实设备：阶段 3 会发现超时，但我们只验证阶段 1-2 的行为
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(8));

        try
        {
            await flow.RunAsync(transport, "MyHome", "password123",
                _ => { }, cts.Token);
        }
        catch (OperationCanceledException)
        {
            // 预期：阶段 3 会等到超时或被取消
        }

        // 必须下发过一次配网帧，且载荷是 "SSID\0PASS\0"
        Assert.Single(transport.Sent);
        Assert.Equal(Proto.TWifiCfg, transport.Sent[0].Type);

        var (ok, ssid, pass) = DecodeWifiPayload(transport.Sent[0].Payload);
        Assert.Equal(0, ok);
        Assert.Equal("MyHome", ssid);
        Assert.Equal("password123", pass);

        // 阶段 3 进入了"等待重连"
        Assert.Equal(ProvisionStage.WaitingForReconnect, flow.Stage);
    }

    [Fact]
    public void Stage_Starts_At_Idle()
    {
        using var discovery = new DeviceDiscovery();
        var flow = new ProvisioningFlow(discovery);
        Assert.Equal(ProvisionStage.Idle, flow.Stage);
    }

    [Fact]
    public void Progress_IsFinished_Only_On_Terminal_States()
    {
        Assert.False(new ProvisionProgress(ProvisionStage.WaitForDeviceAp, "").IsFinished);
        Assert.False(new ProvisionProgress(ProvisionStage.SendingCredentials, "").IsFinished);
        Assert.False(new ProvisionProgress(ProvisionStage.WaitingForReconnect, "").IsFinished);
        Assert.True(new ProvisionProgress(ProvisionStage.Done, "").IsFinished);
        Assert.True(new ProvisionProgress(ProvisionStage.Failed, "", "err").IsFinished);
    }

    [Fact]
    public async Task Throws_When_Transport_Null()
    {
        using var discovery = new DeviceDiscovery();
        var flow = new ProvisioningFlow(discovery);
        await Assert.ThrowsAsync<ArgumentNullException>(
            () => flow.RunAsync(null!, "s", "password123", _ => { }));
    }

    private static (int ok, string ssid, string pass) DecodeWifiPayload(byte[] payload)
    {
        var nul = Array.IndexOf(payload, (byte)0);
        if (nul < 0 || nul + 1 >= payload.Length || payload[^1] != 0)
        {
            return (-1, "", "");
        }

        var ssid = System.Text.Encoding.ASCII.GetString(payload, 0, nul);
        var pass = System.Text.Encoding.ASCII.GetString(payload, nul + 1, payload.Length - nul - 2);
        return (0, ssid, pass);
    }
}
