using System;
using VibeKey.Protocol;
using Xunit;

namespace VibeKey.Protocol.Tests;

/// <summary>
/// 帧编解码测试。
///
/// 这些向量与固件 <c>firmware/test/test_proto.c</c> 以及
/// <c>docs/00-技术设计文档.md</c> §3.3 完全一致，是"C 与 C# 实现逐字节一致"
/// 的唯一裁判。任何一边改了字段，这三处必须同时改。
/// </summary>
public sealed class FrameCodecTests
{
    private static byte[] Hex(params string[] bytes)
    {
        var arr = new byte[bytes.Length];
        for (var i = 0; i < bytes.Length; i++)
        {
            arr[i] = Convert.ToByte(bytes[i], 16);
        }

        return arr;
    }

    [Fact]
    public void Crc16_KnownValue()
    {
        // CRC16/CCITT-FALSE("123456789") == 0x29B1，标准校验值
        Assert.Equal((ushort)0x29B1, FrameCodec.Crc16("123456789"u8));
        // 空输入的 CRC 就是初始值 0xFFFF（C 实现同样如此）
        Assert.Equal((ushort)0xFFFF, FrameCodec.Crc16(ReadOnlySpan<byte>.Empty));
    }

    // ---------------- 文档向量 1：按键 K1 按下，SEQ=1 ----------------
    [Fact]
    public void DocVector1_KeyPress()
    {
        var buf = new byte[64];
        var n = FrameCodec.Encode(Proto.TKeyEvent, 1, "1"u8, buf);
        Assert.Equal(9, n);
        Assert.Equal(
            Hex("AA", "01", "00", "01", "01", "00", "31", "46", "2C"),
            buf.AsSpan(0, n).ToArray());
    }

    // ---------------- 文档向量 2：心跳 ping，SEQ=1000 ----------------
    [Fact]
    public void DocVector2_Ping()
    {
        var buf = new byte[64];
        var n = FrameCodec.Encode(Proto.TPing, 1000, ReadOnlySpan<byte>.Empty, buf);
        Assert.Equal(8, n);
        Assert.Equal(
            Hex("AA", "00", "00", "10", "E8", "03", "17", "FB"),
            buf.AsSpan(0, n).ToArray());
    }

    // ---------------- 文档向量 3：状态上报，SEQ=7 ----------------
    [Fact]
    public void DocVector3_Status()
    {
        const string payload = "WORK_READY,FW=1.0.0,RSSI=-58";
        var buf = new byte[128];
        var n = FrameCodec.Encode(Proto.TStatus, 7, System.Text.Encoding.ASCII.GetBytes(payload), buf);

        Assert.Equal(36, n);
        Assert.Equal(28, buf[1]);   // LEN 低字节
        Assert.Equal(0, buf[2]);    // LEN 高字节
        Assert.Equal(0x68, buf[n - 2]);   // CRC 低字节
        Assert.Equal(0xD0, buf[n - 1]);   // CRC 高字节
        Assert.Equal(payload, System.Text.Encoding.ASCII.GetString(buf, 6, 28));
    }

    [Theory]
    [InlineData(Proto.TAsrText, 65535, "hello world")]
    [InlineData(Proto.TStatus, 0, "BOOT")]
    [InlineData(Proto.TPong, 42, "")]
    public void RoundTrip(byte type, ushort seq, string text)
    {
        var payload = System.Text.Encoding.UTF8.GetBytes(text);
        var buf = new byte[Proto.FrameSize(payload.Length)];
        var n = FrameCodec.Encode(type, seq, payload, buf);

        var result = FrameCodec.Decode(buf.AsSpan(0, n), out var frame, out var consumed);
        Assert.Equal(DecodeResult.Ok, result);
        Assert.NotNull(frame);
        Assert.Equal(type, frame.Type);
        Assert.Equal(seq, frame.Seq);
        Assert.Equal(text, frame.PayloadAsText());
        Assert.Equal(n, consumed);
    }

    [Fact]
    public void Payload_WithEmbeddedNul()
    {
        // WiFi 配置 "MySSID\0pass\0" —— 按二进制载荷处理，不得按字符串截断
        byte[] payload =
        {
            (byte)'M', (byte)'y', (byte)'S', (byte)'S', (byte)'I', (byte)'D', 0x00,
            (byte)'p', (byte)'a', (byte)'s', (byte)'s', (byte)'w', (byte)'o', (byte)'r', (byte)'d', 0x00
        };
        var buf = new byte[Proto.FrameSize(payload.Length)];
        var n = FrameCodec.Encode(Proto.TWifiCfg, 42, payload, buf);

        Assert.Equal(DecodeResult.Ok, FrameCodec.Decode(buf.AsSpan(0, n), out var frame, out _));
        Assert.Equal(payload, frame!.Payload);

        Assert.Equal(0, FrameCodec.ParseWifiConfig(payload, out var ssid, out var pass));
        Assert.Equal("MySSID", ssid);
        Assert.Equal("password", pass);
    }

    [Fact]
    public void GarbagePrefix_IsSkipped()
    {
        var frame = new byte[9];
        FrameCodec.Encode(Proto.TKeyEvent, 5, "7"u8, frame);

        var noisy = new byte[2 + frame.Length];
        noisy[0] = 0x00;
        noisy[1] = 0x11;
        Array.Copy(frame, 0, noisy, 2, frame.Length);

        var result = FrameCodec.Decode(noisy, out _, out var consumed);
        Assert.Equal(DecodeResult.NeedMore, result);
        Assert.Equal(2, consumed);

        Assert.Equal(DecodeResult.Ok, FrameCodec.Decode(frame, out var f, out _));
        Assert.Equal(5, f!.Seq);
        Assert.Equal("7", f.PayloadAsText());

        // 整个缓冲区都没有帧头 -> 可全部丢弃
        Assert.Equal(DecodeResult.NeedMore, FrameCodec.Decode(new byte[8], out _, out var c2));
        Assert.Equal(8, c2);
    }

    [Fact]
    public void BadCrc_ResyncsWithOneByte()
    {
        var buf = new byte[9];
        FrameCodec.Encode(Proto.TKeyEvent, 1, "1"u8, buf);
        buf[6] ^= 0xFF;

        Assert.Equal(DecodeResult.BadCrc, FrameCodec.Decode(buf, out _, out var consumed));
        Assert.Equal(1, consumed);
    }

    [Fact]
    public void BadLen_ResyncsWithOneByte()
    {
        byte[] bad = { 0xAA, 0xFF, 0x0F, 0x01, 0x00, 0x00 };
        Assert.Equal(DecodeResult.BadLen, FrameCodec.Decode(bad, out _, out var consumed));
        Assert.Equal(1, consumed);
    }

    [Fact]
    public void PartialFrame_ReturnsNeedMore()
    {
        var buf = new byte[12];
        var n = FrameCodec.Encode(Proto.TStatus, 7, "BOOT"u8, buf);
        Assert.Equal(DecodeResult.NeedMore, FrameCodec.Decode(buf.AsSpan(0, 5), out _, out var consumed));
        Assert.Equal(0, consumed);
    }

    [Fact]
    public void Encode_RejectsOversizePayload()
    {
        var big = new byte[Proto.MaxPayload + 1];
        Assert.Throws<ArgumentException>(() => FrameCodec.Encode(Proto.TKeyEvent, 1, big, new byte[4096]));
    }

    [Fact]
    public void Encode_RejectsSmallBuffer()
    {
        Assert.Throws<ArgumentException>(() => FrameCodec.Encode(Proto.TKeyEvent, 1, "1"u8, new byte[4]));
    }

    [Fact]
    public void FrameSize_MatchesFirmware()
    {
        Assert.Equal(8, Proto.FrameSize(0));
        Assert.Equal(1032, Proto.FrameSize(1024));
    }

    [Fact]
    public void DfuMagic_MatchesFirmware()
    {
        Assert.Equal(8, Proto.DfuMagicLen);
        Assert.Equal("VIBEFLAS", System.Text.Encoding.ASCII.GetString(Proto.DfuMagic));
    }

    [Theory]
    [InlineData(Proto.TKeyEvent, false)]
    [InlineData(Proto.TStatus, false)]
    [InlineData(Proto.TAck, false)]
    [InlineData(Proto.TPing, true)]
    [InlineData(Proto.TEnterDfu, true)]
    public void IsDownlink(byte type, bool expected)
    {
        Assert.Equal(expected, Proto.IsDownlink(type));
    }
}

/// <summary>流式解码器测试，与固件 test_proto.c 的 proto_stream 套件对应。</summary>
public sealed class FrameStreamTests
{
    [Fact]
    public void Feed_SplitAcrossChunks()
    {
        var stream = new FrameStream();
        var f1 = new byte[9];
        var f2 = new byte[9];
        FrameCodec.Encode(Proto.TKeyEvent, 1, "1"u8, f1);
        FrameCodec.Encode(Proto.TKeyEvent, 2, "2"u8, f2);

        var frames = new System.Collections.Generic.List<Frame>();

        // 第一帧的前 4 字节（不足一整帧）
        stream.Feed(f1.AsSpan(0, 4), f => frames.Add(f));
        Assert.Empty(frames);

        // 第一帧剩余 + 第二帧前 3 字节
        var mixed = new byte[(f1.Length - 4) + 3];
        Array.Copy(f1, 4, mixed, 0, f1.Length - 4);
        Array.Copy(f2, 0, mixed, f1.Length - 4, 3);
        stream.Feed(mixed, f => frames.Add(f));
        Assert.Single(frames);
        Assert.Equal((ushort)1, frames[0].Seq);
        Assert.Equal("1", frames[0].PayloadAsText());

        // 第二帧剩余
        stream.Feed(f2.AsSpan(3), f => frames.Add(f));
        Assert.Equal(2, frames.Count);
        Assert.Equal((ushort)2, frames[1].Seq);
        Assert.Equal("2", frames[1].PayloadAsText());
    }

    [Fact]
    public void Feed_RecoversFromGarbage()
    {
        var stream = new FrameStream();
        var frames = new System.Collections.Generic.List<Frame>();

        var f = new byte[9];
        FrameCodec.Encode(Proto.TKeyEvent, 9, "5"u8, f);

        var noisy = new byte[3 + f.Length];
        noisy[0] = 0x00;
        noisy[1] = 0xAA;   // 假帧头，后面不足 -> 应被跳过
        noisy[2] = 0x01;
        Array.Copy(f, 0, noisy, 3, f.Length);

        stream.Feed(noisy, fr => frames.Add(fr));
        Assert.Single(frames);
        Assert.Equal((ushort)9, frames[0].Seq);
    }

    [Fact]
    public void Reset_ClearsBuffer()
    {
        var stream = new FrameStream();
        var frames = new System.Collections.Generic.List<Frame>();
        stream.Feed(new byte[] { 0xAA, 0x01, 0x00 }, f => frames.Add(f));
        Assert.Empty(frames);

        stream.Reset();
        var f = new byte[9];
        FrameCodec.Encode(Proto.TKeyEvent, 1, "1"u8, f);
        stream.Feed(f, fr => frames.Add(fr));
        Assert.Single(frames);
    }
}

/// <summary>WiFi 配置载荷测试。</summary>
public sealed class WifiConfigTests
{
    [Fact]
    public void BuildAndParse_RoundTrip()
    {
        var payload = FrameCodec.BuildWifiConfig("MyHome", "password123");
        Assert.Equal(0, FrameCodec.ParseWifiConfig(payload, out var ssid, out var pass));
        Assert.Equal("MyHome", ssid);
        Assert.Equal("password123", pass);
    }

    [Theory]
    [InlineData("MyHome", "short")]      // 密码太短
    [InlineData("", "password123")]      // 空 SSID
    public void RejectsInvalid(string ssid, string pass)
    {
        var payload = FrameCodec.BuildWifiConfig(ssid, pass);
        Assert.NotEqual(0, FrameCodec.ParseWifiConfig(payload, out _, out _));
    }

    [Theory]
    [InlineData("My,Home", "password123")]   // 逗号会破坏状态字符串
    [InlineData("My=Home", "password123")]    // 等号同理
    public void RejectsBadChars(string ssid, string pass)
    {
        var payload = FrameCodec.BuildWifiConfig(ssid, pass);
        Assert.Equal(-3, FrameCodec.ParseWifiConfig(payload, out _, out _));
    }

    [Fact]
    public void Build_ProducesExpectedLayout()
    {
        var payload = FrameCodec.BuildWifiConfig("AB", "12345678");
        Assert.Equal(new byte[]
        {
            (byte)'A', (byte)'B', 0x00,
            (byte)'1', (byte)'2', (byte)'3', (byte)'4', (byte)'5', (byte)'6', (byte)'7', (byte)'8',
            0x00
        }, payload);
    }
}
