using System;

namespace VibeKey.Protocol;

/// <summary>
/// 协议常量。必须与固件 <c>firmware/main/app/proto.h</c> 完全一致。
/// 任何字段变更必须同时改 proto.h、proto.c、本文件和两边的测试向量
/// （见 docs/00-技术设计文档.md 冻结条款）。
/// </summary>
public static class Proto
{
    public const byte Sof = 0xAA;
    public const int MaxPayload = 1024;
    public const int HeaderLen = 6;          // SOF + LEN + TYPE + SEQ
    public const int CrcLen = 2;
    public const int MinFrameLen = 8;        // HeaderLen + CrcLen
    public const int Version = 1;
    public const int DfuMagicLen = 8;

    // ---- TYPE ----
    public const byte TKeyEvent = 0x01;      // 上行
    public const byte TStatus = 0x02;
    public const byte TAsrText = 0x03;
    public const byte TPong = 0x04;
    public const byte TKeyRelease = 0x05;
    public const byte TAck = 0x06;
    public const byte TPing = 0x10;          // 下行
    public const byte TWifiCfg = 0x11;
    public const byte TAsrCfg = 0x12;
    public const byte TEnterDfu = 0x13;
    public const byte TVoiceCtl = 0x14;
    public const byte TStatusReq = 0x15;
    public const byte TVer = 0x7F;

    // ---- ASR 文本首字节 ----
    public const byte AsrPartial = 0x00;
    public const byte AsrFinal = 0x01;

    // ---- ACK 首字节 ----
    public const byte AckOk = 0x00;
    public const byte AckNack = 0x01;

    // ---- 语音控制载荷 ----
    public const byte VoiceStop = 0x00;
    public const byte VoiceStart = 0x01;

    /// <summary>烧录指令 magic word = "VIBEFLAS"。混淆用途，非安全机制。</summary>
    public static readonly byte[] DfuMagic = { 0x56, 0x49, 0x42, 0x45, 0x46, 0x4C, 0x41, 0x53 };

    /// <summary>一帧的总长度。</summary>
    public static int FrameSize(int payloadLen) => HeaderLen + payloadLen + CrcLen;

    /// <summary>该 TYPE 是否为下行（上位机 → 设备）。</summary>
    public static bool IsDownlink(byte type) => type is >= 0x10 and <= 0x1F;
}

/// <summary>解码结果。</summary>
public enum DecodeResult
{
    Ok = 0,
    NeedMore = 1,
    BadCrc = 2,
    BadLen = 3,
    BadArg = 4,
}

/// <summary>一帧。</summary>
public sealed class Frame
{
    public byte Type { get; init; }
    public ushort Seq { get; init; }
    public byte[] Payload { get; init; } = Array.Empty<byte>();

    public int Len => Payload.Length;

    public string PayloadAsText()
    {
        if (Payload.Length == 0)
        {
            return string.Empty;
        }

        // 去掉单个结尾 NUL（如 WiFi 配置 "SSID\0PASS\0"）
        var end = Payload.Length;
        if (Payload[end - 1] == 0)
        {
            end--;
        }

        return System.Text.Encoding.UTF8.GetString(Payload, 0, end);
    }
}

/// <summary>
/// 帧编解码器。与固件 <c>proto.c</c> 逐字节一致。
/// </summary>
public static class FrameCodec
{
    /// <summary>
    /// CRC16/CCITT-FALSE：poly 0x1021, init 0xFFFF, 不反射, xorout 0x0000。
    /// </summary>
    public static ushort Crc16(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;
        foreach (var b in data)
        {
            crc = (ushort)(crc ^ (ushort)(b << 8));
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000) != 0
                    ? (ushort)((crc << 1) ^ 0x1021)
                    : (ushort)(crc << 1);
            }
        }

        return crc;
    }

    /// <summary>编码一帧。返回完整字节数。</summary>
    /// <exception cref="ArgumentException">载荷超长或输出缓冲不足。</exception>
    public static int Encode(byte type, ushort seq, ReadOnlySpan<byte> payload, Span<byte> outBuffer)
    {
        if (payload.Length > Proto.MaxPayload)
        {
            throw new ArgumentException($"载荷长度 {payload.Length} 超过上限 {Proto.MaxPayload}");
        }

        var total = Proto.FrameSize(payload.Length);
        if (outBuffer.Length < total)
        {
            throw new ArgumentException($"输出缓冲 {outBuffer.Length} 不足，需要 {total}");
        }

        outBuffer[0] = Proto.Sof;
        outBuffer[1] = (byte)(payload.Length & 0xFF);
        outBuffer[2] = (byte)((payload.Length >> 8) & 0xFF);
        outBuffer[3] = type;
        outBuffer[4] = (byte)(seq & 0xFF);
        outBuffer[5] = (byte)((seq >> 8) & 0xFF);
        payload.CopyTo(outBuffer.Slice(Proto.HeaderLen, payload.Length));

        // CRC 覆盖 LEN+TYPE+SEQ+PAYLOAD，即 out[1] .. out[1+5+LEN-1]
        var crc = Crc16(outBuffer.Slice(1, Proto.HeaderLen - 1 + payload.Length));
        outBuffer[total - 2] = (byte)(crc & 0xFF);
        outBuffer[total - 1] = (byte)((crc >> 8) & 0xFF);

        return total;
    }

    public static int Encode(byte type, ushort seq, ReadOnlySpan<byte> payload)
        => Encode(type, seq, payload, new byte[Proto.FrameSize(payload.Length)]);

    /// <summary>
    /// 从字节流解出一帧。
    /// </summary>
    /// <param name="buffer">待解析字节流。</param>
    /// <param name="frame">解出的帧（成功时非 null）。</param>
    /// <param name="consumed">调用方应前进的字节数，语义与固件 proto_decode 一致。</param>
    public static DecodeResult Decode(ReadOnlySpan<byte> buffer, out Frame? frame, out int consumed)
    {
        frame = null;
        consumed = 0;

        // 1. 定位帧头
        var sof = 0;
        while (sof < buffer.Length && buffer[sof] != Proto.Sof)
        {
            sof++;
        }

        if (sof >= buffer.Length)
        {
            consumed = buffer.Length;   // 整个缓冲区无帧头，可全部丢弃
            return DecodeResult.NeedMore;
        }

        if (sof > 0)
        {
            consumed = sof;             // 前面是垃圾，先跳过
            return DecodeResult.NeedMore;
        }

        // 2. 帧头是否完整
        if (buffer.Length < Proto.HeaderLen)
        {
            return DecodeResult.NeedMore;
        }

        // 3. LEN 合法性
        var len = (ushort)(buffer[1] | (buffer[2] << 8));
        if (len > Proto.MaxPayload)
        {
            consumed = 1;
            return DecodeResult.BadLen;
        }

        // 4. 整帧是否到齐
        var total = Proto.FrameSize(len);
        if (buffer.Length < total)
        {
            return DecodeResult.NeedMore;
        }

        // 5. CRC
        var want = (ushort)(buffer[total - 2] | (buffer[total - 1] << 8));
        var got = Crc16(buffer.Slice(1, total - 3));   // LEN+TYPE+SEQ+PAYLOAD = 5+LEN
        if (want != got)
        {
            consumed = 1;
            return DecodeResult.BadCrc;
        }

        frame = new Frame
        {
            Type = buffer[3],
            Seq = (ushort)(buffer[4] | (buffer[5] << 8)),
            Payload = buffer.Slice(Proto.HeaderLen, len).ToArray(),
        };
        consumed = total;
        return DecodeResult.Ok;
    }

    /// <summary>
    /// 解析 WiFi 配置载荷 "SSID\0PASSWORD\0"。
    /// </summary>
    /// <returns>0 成功；-1 长度非法；-2 格式非法；-3 含非法字符</returns>
    public static int ParseWifiConfig(ReadOnlySpan<byte> payload, out string ssid, out string password)
    {
        ssid = string.Empty;
        password = string.Empty;

        var nul = payload.IndexOf((byte)0);
        if (nul <= 0)
        {
            return -1;
        }

        var ssidLen = nul;
        if (ssidLen > 32)
        {
            return -1;
        }

        if (payload.Length < ssidLen + 2 || payload[^1] != 0)
        {
            return -2;
        }

        var passLen = payload.Length - ssidLen - 2;
        if (passLen is < 8 or > 64)
        {
            return -1;
        }

        var s = System.Text.Encoding.ASCII.GetString(payload.Slice(0, ssidLen));
        var p = System.Text.Encoding.ASCII.GetString(payload.Slice(ssidLen + 1, passLen));

        foreach (var c in s + p)
        {
            if (c is ',' or '=' || char.IsControl(c))
            {
                return -3;
            }
        }

        ssid = s;
        password = p;
        return 0;
    }

    /// <summary>
    /// 构造 WiFi 配置载荷 "SSID\0PASSWORD\0"。
    /// </summary>
    public static byte[] BuildWifiConfig(string ssid, string password)
    {
        var s = System.Text.Encoding.ASCII.GetBytes(ssid);
        var p = System.Text.Encoding.ASCII.GetBytes(password);
        var buf = new byte[s.Length + 1 + p.Length + 1];
        Array.Copy(s, buf, s.Length);
        Array.Copy(p, 0, buf, s.Length + 1, p.Length);
        return buf;
    }
}

/// <summary>
/// 面向串口/WiFi 字节流的增量解码器，与固件 <c>proto_stream_t</c> 对应。
/// </summary>
public sealed class FrameStream
{
    private readonly byte[] _buffer = new byte[Proto.HeaderLen + Proto.MaxPayload + Proto.CrcLen];
    private int _fill;

    public void Reset() => _fill = 0;

    /// <summary>
    /// 喂入一段字节，尽可能多地解出帧。
    /// </summary>
    public void Feed(ReadOnlySpan<byte> data, Action<Frame> onFrame)
    {
        var offset = 0;
        while (offset < data.Length)
        {
            var room = _buffer.Length - _fill;
            var take = Math.Min(data.Length - offset, room);

            if (take == 0)
            {
                // 缓冲已满仍无法成帧：丢弃最旧一个字节以维持同步
                Array.Copy(_buffer, 1, _buffer, 0, _fill - 1);
                _fill--;
                continue;
            }

            data.Slice(offset, take).CopyTo(_buffer.AsSpan(_fill, take));
            _fill += take;
            offset += take;

            while (true)
            {
                var result = FrameCodec.Decode(_buffer.AsSpan(0, _fill), out var frame, out var consumed);
                if (result == DecodeResult.Ok && frame is not null)
                {
                    onFrame(frame);
                    Array.Copy(_buffer, consumed, _buffer, 0, _fill - consumed);
                    _fill -= consumed;
                    continue;
                }

                if (result is DecodeResult.BadCrc or DecodeResult.BadLen)
                {
                    Array.Copy(_buffer, consumed, _buffer, 0, _fill - consumed);
                    _fill -= consumed;
                    continue;
                }

                if (result == DecodeResult.NeedMore)
                {
                    if (consumed > 0)
                    {
                        // 丢弃垃圾前缀后继续解帧，否则真帧会滞留缓冲直到下次 Feed
                        Array.Copy(_buffer, consumed, _buffer, 0, _fill - consumed);
                        _fill -= consumed;
                        continue;
                    }

                    break;   // consumed == 0：确实在等更多数据
                }
            }
        }
    }
}
