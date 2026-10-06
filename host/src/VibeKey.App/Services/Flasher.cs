using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace VibeKey.App.Services;

/// <summary>烧录阶段。</summary>
public enum FlashStage
{
    Idle,

    /// <summary>与芯片握手同步。</summary>
    Syncing,

    /// <summary>读取芯片信息（型号 / flash 大小）。</summary>
    Detecting,

    /// <summary>擦除目标区域。</summary>
    Erasing,

    /// <summary>写入固件数据。</summary>
    Writing,

    /// <summary>校验写入的数据。</summary>
    Verifying,

    /// <summary>重启设备回到工作态。</summary>
    Rebooting,

    /// <summary>完成。</summary>
    Done,

    /// <summary>失败，原因见 <see cref="FlashProgress.Error"/>。</summary>
    Failed,
}

/// <summary>一次进度上报。</summary>
public readonly record struct FlashProgress(
    FlashStage Stage,
    string Message,
    double Fraction = 0.0,
    string? Error = null)
{
    public bool IsFinished => Stage is FlashStage.Done or FlashStage.Failed;
}

/// <summary>一份要烧录的固件镜像。</summary>
public sealed class FirmwareImage
{
    public required string Path { get; init; }

    public required string Version { get; init; }

    public required long SizeBytes { get; init; }

    public required string Sha256Hex { get; init; }

    /// <summary>写入 flash 的起始地址（应用分区通常 0x10000）。</summary>
    public uint Address { get; init; } = 0x10000;
}

/// <summary>
/// ESP32 ROM 串行烧录协议（SLIP 封装的 esptool 协议）。
///
/// 为什么自己实现而不打包 Python esptool：
/// PRD 6.6 要求安装包"一键安装、不装第三方驱动"，而打包 esptool 意味着
/// 拖一个 Python 运行时（几百 MB + 许可链），违背"干净系统即插即用"的目标。
/// 协议本身是文档化的简单 SLIP 封装，本实现只覆盖本期需要的最小子集：
/// 同步 / 读芯片信息 / 擦除 / 写入 / 校验 / 重启。
///
/// 接收用事件驱动：SerialPort 的 DataReceived 事件把字节推入内部缓冲，
/// 协议层从缓冲里按 SLIP 帧解析，不轮询、不占用线程。
/// </summary>
public sealed class Flasher : IDisposable
{
    // ---- SLIP ----
    private const byte SlipEnd = 0xC0;
    private const byte SlipEsc = 0xDB;
    private const byte SlipEscEnd = 0xDC;
    private const byte SlipEscEsc = 0xDD;

    // ---- ROM 命令字 ----
    private const byte CmdFlashBegin = 0x02;
    private const byte CmdFlashData = 0x03;
    private const byte CmdFlashEnd = 0x04;
    private const byte CmdSync = 0x08;
    private const byte CmdReadReg = 0x0A;
    private const byte CmdSpiFlashMd5 = 0x13;

    private const byte RequestDir = 0x00;
    private const byte ResponseDir = 0x01;

    private readonly Func<string, ISerialPort> _portFactory;
    private ISerialPort? _port;

    /// <summary>接收缓冲。SerialPort 的 DataReceived 事件往这里追加。</summary>
    private readonly List<byte> _rx = new();

    private readonly object _rxGate = new();
    private readonly SemaphoreSlim _rxSignal = new(0);

    public Flasher(Func<string, ISerialPort>? portFactory = null)
    {
        _portFactory = portFactory ?? (name => new RealSerialPort(name, baudRate: 460800));
    }

    /// <summary>是否正在烧录。</summary>
    public bool IsBusy { get; private set; }

    /// <summary>
    /// 完整烧录一份固件镜像。
    ///
    /// <para>调用方必须先把设备切到下载模式（见 <see cref="PortJumpWatcher"/>），
    /// 再把找到的新 COM 口名传进来。</para>
    /// </summary>
    /// <param name="portName">下载模式设备的 COM 口（VID 303A / PID 1001）。</param>
    /// <param name="image">要写入的镜像。</param>
    /// <param name="onProgress">进度回调。</param>
    /// <param name="ct">取消令牌。</param>
    public async Task FlashAsync(
        string portName,
        FirmwareImage image,
        Action<FlashProgress> onProgress,
        CancellationToken ct = default)
    {
        if (IsBusy)
        {
            throw new InvalidOperationException("正在烧录中");
        }

        IsBusy = true;
        try
        {
            await RunFlashAsync(portName, image, onProgress, ct);
        }
        finally
        {
            ClosePort();
            IsBusy = false;
        }
    }

    private async Task RunFlashAsync(
        string portName,
        FirmwareImage image,
        Action<FlashProgress> onProgress,
        CancellationToken ct)
    {
        // ---- 1. 打开端口 ----
        Report(onProgress, FlashStage.Syncing, $"打开 {portName}…");
        OpenPort(portName);

        // ---- 2. 同步握手 ----
        Report(onProgress, FlashStage.Syncing, "与芯片握手同步…");
        await SyncAsync(ct);
        Report(onProgress, FlashStage.Syncing, "同步成功，芯片已进入下载模式。", 0.05);

        // ---- 3. 读芯片信息 ----
        Report(onProgress, FlashStage.Detecting, "读取芯片信息…", 0.08);
        var chipInfo = await ReadChipInfoAsync(ct);
        Report(onProgress, FlashStage.Detecting,
            $"芯片：{chipInfo.Model}，flash {chipInfo.FlashSizeMb} MB", 0.12);

        // ---- 4. 读镜像并校验 ----
        var data = await File.ReadAllBytesAsync(image.Path, ct);
        if (data.Length != image.SizeBytes)
        {
            throw new InvalidDataException(
                $"镜像大小不符：文件 {data.Length} 字节，声明 {image.SizeBytes} 字节");
        }

        var actualSha = ComputeSha256(data);
        if (!string.Equals(actualSha, image.Sha256Hex, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException(
                $"镜像校验失败：SHA256 应为 {image.Sha256Hex[..12]}…，实际 {actualSha[..12]}…");
        }

        // ---- 5. 擦除 ----
        Report(onProgress, FlashStage.Erasing, "擦除目标区域…", 0.15);
        await EraseAsync(ct);
        Report(onProgress, FlashStage.Erasing, "擦除完成。", 0.25);

        // ---- 6. 写入 ----
        Report(onProgress, FlashStage.Writing, $"写入 {FormatBytes(data.Length)}…", 0.3);
        await WriteAsync(image.Address, data, ct,
            frac => Report(onProgress, FlashStage.Writing,
                $"写入中… {frac:P0}", 0.3 + frac * 0.55));
        Report(onProgress, FlashStage.Writing, "写入完成。", 0.85);

        // ---- 7. 校验 ----
        Report(onProgress, FlashStage.Verifying, "校验写入的数据…", 0.9);
        var ok = await VerifyAsync(image.Address, data, ct);
        if (!ok)
        {
            throw new InvalidDataException("写入的数据与源镜像不一致（校验失败）");
        }

        Report(onProgress, FlashStage.Verifying, "校验通过。", 0.95);

        // ---- 8. 重启 ----
        Report(onProgress, FlashStage.Rebooting, "重启设备…", 0.98);
        await RebootAsync(ct);

        Report(onProgress, FlashStage.Done,
            $"烧录完成：{image.Version}（{FormatBytes(data.Length)}）。"
            + "设备将重启回到工作态，COM 口会跳回 303A:4001。", 1.0);
    }

    // ----------------------------------------------------------------
    //  协议实现
    // ----------------------------------------------------------------

    private async Task SyncAsync(CancellationToken ct)
    {
        var payload = new byte[36];
        payload[0] = 0x07;
        payload[1] = 0x07;
        payload[2] = 0x12;
        payload[3] = 0x20;
        for (var i = 4; i < 36; i++)
        {
            payload[i] = 0x55;
        }

        // esptool 惯例：SYNC 发两次，第二次的响应里有 ROM 的启动信息
        await CommandAsync(CmdSync, payload, ct);
        await CommandAsync(CmdSync, payload, ct);
    }

    private sealed record ChipInfo(string Model, int FlashSizeMb);

    private async Task<ChipInfo> ReadChipInfoAsync(CancellationToken ct)
    {
        var magic = await ReadRegisterAsync(0x40001000, ct);
        var flash = await ReadRegisterAsync(0x40002000, ct);

        var model = (magic & 0xFFFFF000) switch
        {
            0x00000000 => "ESP32",
            0x006CC000 => "ESP32-S2",
            0x00C00000 => "ESP32-S3",
            0x1B000000 => "ESP32-C3",
            _ => $"未知 (0x{magic:X8})",
        };

        var flashMb = 1 << ((int)((flash >> 16) & 0xFF) - 2);
        return new ChipInfo(model, flashMb);
    }

    private async Task<uint> ReadRegisterAsync(uint address, CancellationToken ct)
    {
        var payload = BitConverter.GetBytes(address);
        var resp = await CommandAsync(CmdReadReg, payload, ct);
        if (resp.Length < 4)
        {
            throw new InvalidDataException($"读寄存器响应过短：{resp.Length} 字节");
        }

        return BitConverter.ToUInt32(resp, 0);
    }

    private async Task EraseAsync(CancellationToken ct)
    {
        // 全片擦除：FLASH_BEGIN 的 size=0 / num_blocks=0 会让 ROM 擦除整片 flash
        var payload = new byte[16];
        // size=0, num_blocks=0, block_size=0, offset=0
        await CommandAsync(CmdFlashBegin, payload, ct, timeoutMs: 60000);
    }

    private async Task WriteAsync(uint address, byte[] data, CancellationToken ct,
        Action<double> onProgress)
    {
        const int blockSize = 0x400;

        var blocks = (uint)Math.Ceiling((double)data.Length / blockSize);

        var beginPayload = new byte[16];
        BitConverter.GetBytes((uint)data.Length).CopyTo(beginPayload, 0);
        BitConverter.GetBytes(blocks).CopyTo(beginPayload, 4);
        BitConverter.GetBytes((uint)blockSize).CopyTo(beginPayload, 8);
        BitConverter.GetBytes(address).CopyTo(beginPayload, 12);

        await CommandAsync(CmdFlashBegin, beginPayload, ct, timeoutMs: 30000);

        for (uint i = 0; i < blocks; i++)
        {
            ct.ThrowIfCancellationRequested();

            var offset = (int)(i * blockSize);
            var chunkLen = Math.Min(blockSize, data.Length - offset);

            var payload = new byte[16 + chunkLen];
            BitConverter.GetBytes((uint)chunkLen).CopyTo(payload, 0);
            BitConverter.GetBytes(i).CopyTo(payload, 4);
            Array.Copy(data, offset, payload, 16, chunkLen);

            await CommandAsync(CmdFlashData, payload, ct, timeoutMs: 30000);

            onProgress((double)(i + 1) / blocks);
        }

        var endPayload = BitConverter.GetBytes(0x00000000u);
        await CommandAsync(CmdFlashEnd, endPayload, ct);
    }

    private async Task<bool> VerifyAsync(uint address, byte[] data, CancellationToken ct)
    {
        var payload = new byte[16];
        BitConverter.GetBytes(address).CopyTo(payload, 0);
        BitConverter.GetBytes((uint)data.Length).CopyTo(payload, 4);

        var resp = await CommandAsync(CmdSpiFlashMd5, payload, ct, timeoutMs: 60000);

        using var md5 = System.Security.Cryptography.MD5.Create();
        var local = md5.ComputeHash(data);
        return resp.Length == 16 && resp.AsSpan().SequenceEqual(local);
    }

    private async Task RebootAsync(CancellationToken ct)
    {
        try
        {
            var endPayload = BitConverter.GetBytes(0x00000001u);
            await CommandAsync(CmdFlashEnd, endPayload, ct, timeoutMs: 2000);
        }
        catch (Exception)
        {
            // 串口在复位瞬间消失是预期行为，不算失败
        }

        await Task.Delay(500, ct);
    }

    // ----------------------------------------------------------------
    //  SLIP 收发
    // ----------------------------------------------------------------

    private async Task<byte[]> CommandAsync(
        byte cmd, byte[] payload, CancellationToken ct, int timeoutMs = 5000)
    {
        if (_port is null || !_port.IsOpen)
        {
            throw new InvalidOperationException("串口未打开");
        }

        // 清空上一轮残留
        lock (_rxGate)
        {
            _rx.Clear();
        }

        _port.Write(BuildFrame(cmd, payload));

        var deadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            ct.ThrowIfCancellationRequested();

            byte[]? resp;
            lock (_rxGate)
            {
                resp = TryParseResponse();
            }

            if (resp is not null)
            {
                return resp;
            }

            // 等有新数据或超时
            var remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                break;
            }

            try
            {
                await _rxSignal.WaitAsync(
                    remaining < TimeSpan.FromMilliseconds(100) ? remaining : TimeSpan.FromMilliseconds(100), ct);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
        }

        throw new TimeoutException($"等待芯片响应超时（命令 0x{cmd:X2}）");
    }

    private static byte[] BuildFrame(byte cmd, byte[] payload)
    {
        var inner = new byte[8 + payload.Length];
        inner[0] = RequestDir;
        inner[1] = cmd;
        BitConverter.GetBytes((ushort)payload.Length).CopyTo(inner, 2);
        BitConverter.GetBytes(ComputeChecksum(payload)).CopyTo(inner, 4);
        Array.Copy(payload, 0, inner, 8, payload.Length);

        var outp = new List<byte> { SlipEnd };
        foreach (var b in inner)
        {
            switch (b)
            {
                case SlipEnd:
                    outp.Add(SlipEsc);
                    outp.Add(SlipEscEnd);
                    break;
                case SlipEsc:
                    outp.Add(SlipEsc);
                    outp.Add(SlipEscEsc);
                    break;
                default:
                    outp.Add(b);
                    break;
            }
        }

        outp.Add(SlipEnd);
        return outp.ToArray();
    }

    /// <summary>从接收缓冲里找一帧完整的 SLIP 响应。找到即返回并消费，否则返回 null。</summary>
    private byte[]? TryParseResponse()
    {
        // 从后往前找 SLIP_END，作为帧结束
        var end = -1;
        for (var i = _rx.Count - 1; i >= 0; i--)
        {
            if (_rx[i] == SlipEnd)
            {
                end = i;
                break;
            }
        }

        if (end < 0)
        {
            return null;
        }

        // 再往前找帧起始
        var start = -1;
        for (var i = end - 1; i >= 0; i--)
        {
            if (_rx[i] == SlipEnd)
            {
                start = i;
                break;
            }
        }

        if (start < 0 || start == end)
        {
            return null;
        }

        // 解转义
        var inner = new List<byte>();
        for (var i = start + 1; i < end; i++)
        {
            if (_rx[i] == SlipEsc && i + 1 < end)
            {
                i++;
                inner.Add(_rx[i] switch
                {
                    SlipEscEnd => SlipEnd,
                    SlipEscEsc => SlipEsc,
                    _ => _rx[i],
                });
            }
            else
            {
                inner.Add(_rx[i]);
            }
        }

        // 响应头至少 8 字节
        if (inner.Count < 8 || inner[0] != ResponseDir)
        {
            // 不是响应帧：丢弃这一段，继续等下一帧
            _rx.RemoveRange(0, end + 1);
            return null;
        }

        var size = BitConverter.ToUInt16(inner.ToArray(), 2);
        if (inner.Count < 8 + size)
        {
            return null;   // 数据没到齐
        }

        var payload = inner.Skip(8).Take(size).ToArray();

        // 消费掉这一帧
        _rx.RemoveRange(0, end + 1);

        // 状态字节在 payload 末尾（esptool 的 ROM 响应格式）
        if (payload.Length > 0 && payload[^1] != 0x00)
        {
            throw new InvalidOperationException(
                $"芯片拒绝命令，状态码 0x{payload[^1]:X2}");
        }

        return payload;
    }

    private void OpenPort(string portName)
    {
        _port = _portFactory(portName);
        _port.Open();
        _port.DataReceived += OnPortData;
    }

    private void OnPortData(object? sender, byte[] data)
    {
        lock (_rxGate)
        {
            _rx.AddRange(data);
        }

        _rxSignal.Release();
    }

    private void ClosePort()
    {
        if (_port is not null)
        {
            _port.DataReceived -= OnPortData;
            _port.Dispose();
            _port = null;
        }
    }

    private static uint ComputeChecksum(byte[] data)
    {
        uint crc = 0xEF;
        foreach (var b in data)
        {
            crc ^= b;
        }

        return crc;
    }

    private static string ComputeSha256(byte[] data)
    {
        using var sha = System.Security.Cryptography.SHA256.Create();
        var hash = sha.ComputeHash(data);
        var sb = new StringBuilder(hash.Length * 2);
        foreach (var b in hash)
        {
            sb.Append(b.ToString("x2"));
        }

        return sb.ToString();
    }

    private static string FormatBytes(long n)
        => n switch
        {
            >= 1_048_576 => $"{n / 1_048_576.0:F1} MB",
            >= 1024 => $"{n / 1024.0:F1} KB",
            _ => $"{n} B",
        };

    private static void Report(Action<FlashProgress> onProgress, FlashStage stage,
        string message, double fraction = 0.0, string? error = null)
        => onProgress(new FlashProgress(stage, message, fraction, error));

    public void Dispose()
    {
        ClosePort();
        GC.SuppressFinalize(this);
    }
}
