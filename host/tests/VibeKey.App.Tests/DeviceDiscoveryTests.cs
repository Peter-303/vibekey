using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using VibeKey.App.Services;
using Xunit;

namespace VibeKey.App.Tests;

/// <summary>
/// 设备发现协议测试。
/// 回复报文格式由固件 port_esp32.c 的 vk_udp_task 生成，
/// 两端必须一致——这也是 WiFi 侧除了帧协议之外的第二个契约。
/// </summary>
public sealed class DeviceDiscoveryTests
{
    [Fact]
    public void Parse_NormalReply()
    {
        var d = DeviceDiscovery.Parse("VIBEKEY_HERE,IP=192.168.4.1,MAC=A1B2C3D4E5F6,FW=0.1.0");

        Assert.NotNull(d);
        Assert.Equal("192.168.4.1", d.Value.Ip);
        Assert.Equal("A1B2C3D4E5F6", d.Value.Mac);
        Assert.Equal("0.1.0", d.Value.Firmware);
    }

    [Fact]
    public void Parse_ToleratesWhitespaceAndTrailingNewline()
    {
        // 固件用 snprintf 拼字符串，没有加 \r\n；但防御性起见也允许带
        var d = DeviceDiscovery.Parse("  VIBEKEY_HERE,IP=10.0.0.5, FW=1.2.3 \r\n");

        Assert.NotNull(d);
        Assert.Equal("10.0.0.5", d.Value.Ip);
        Assert.Equal("1.2.3", d.Value.Firmware);
    }

    [Fact]
    public void Parse_RejectsWrongPrefix()
    {
        Assert.Null(DeviceDiscovery.Parse("HELLO,IP=1.2.3.4"));
        Assert.Null(DeviceDiscovery.Parse(""));
        Assert.Null(DeviceDiscovery.Parse("   "));
    }

    [Fact]
    public void Parse_RequiresIp()
    {
        // 没有 IP 的设备没法连，必须当作无效
        Assert.Null(DeviceDiscovery.Parse("VIBEKEY_HERE,MAC=A1B2C3D4E5F6,FW=0.1.0"));
        Assert.Null(DeviceDiscovery.Parse("VIBEKEY_HERE,IP=,MAC=A1"));
    }

    [Fact]
    public void Parse_MacAndFirmwareOptional()
    {
        var d = DeviceDiscovery.Parse("VIBEKEY_HERE,IP=192.168.1.23");

        Assert.NotNull(d);
        Assert.Equal("192.168.1.23", d.Value.Ip);
        Assert.Equal(string.Empty, d.Value.Mac);
        Assert.Equal(string.Empty, d.Value.Firmware);
    }

    [Fact]
    public void Parse_FieldOrderDoesNotMatter()
    {
        var d = DeviceDiscovery.Parse("VIBEKEY_HERE,FW=9.9.9,MAC=DEADBEEF0001,IP=172.16.0.9");

        Assert.NotNull(d);
        Assert.Equal("172.16.0.9", d.Value.Ip);
        Assert.Equal("DEADBEEF0001", d.Value.Mac);
        Assert.Equal("9.9.9", d.Value.Firmware);
    }

    [Fact]
    public void Parse_IgnoresMalformedSegments()
    {
        // 缺 '=' 的段、空值都应被跳过而不是抛异常
        var d = DeviceDiscovery.Parse("VIBEKEY_HERE,GARBAGE,IP=1.1.1.1,=nokey,EXTRA=");

        Assert.NotNull(d);
        Assert.Equal("1.1.1.1", d.Value.Ip);
    }

    [Fact]
    public void Devices_StartsEmpty()
    {
        using var d = new DeviceDiscovery();
        Assert.Empty(d.Devices);
    }

    [Fact]
    public async Task ProbeOnce_WhenNoDevice_ReturnsEmpty()
    {
        using var d = new DeviceDiscovery();
        // 环境里没有 VibeKey 设备：必须优雅返回空，不能抛异常或挂住
        var result = await d.ProbeOnceAsync(timeoutMs: 300);

        Assert.NotNull(result);
        Assert.Empty(result);
    }

    [Fact]
    public void DiscoveryPort_MatchesFirmware()
    {
        // 固件侧：CONFIG_VIBEKEY_DISCOVERY_PORT 默认 8267
        Assert.Equal(8267, DeviceDiscovery.DiscoveryPort);
    }
}
