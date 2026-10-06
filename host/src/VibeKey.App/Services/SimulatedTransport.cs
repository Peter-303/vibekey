using System;
using System.Collections.Generic;
using System.Timers;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>
/// 模拟设备传输：按 PRD 5.1 的状态表产生 LED/状态变化，
/// 并对下行指令给出与真实固件一致的反应。
///
/// 用途：让上位机 UI、映射逻辑、烧录流程在**没有任何硬件**的情况下完整开发和自测。
/// </summary>
public sealed class SimulatedTransport : IDeviceTransport
{
    private readonly System.Timers.Timer _timer;
    private readonly Queue<Frame> _pending = new();
    private readonly object _gate = new();
    private LinkState _state = LinkState.Disconnected;
    private ushort _seq;
    private bool _inDfu;

    public SimulatedTransport()
    {
        _timer = new System.Timers.Timer(200) { AutoReset = true };
        _timer.Elapsed += OnTick;
    }

    public TransportKind Kind => TransportKind.Simulated;

    public LinkState State => _state;

    public string Description => "模拟设备";

    public event EventHandler<Frame>? FrameReceived;

    public event EventHandler<LinkState>? StateChanged;

    public void Open()
    {
        SetState(LinkState.Connecting);
        _timer.Start();
        // 500ms 后"连上"，先推一帧 BOOT
        _ = System.Threading.Tasks.Task.Delay(500).ContinueWith(_ =>
        {
            lock (_gate)
            {
                _pending.Enqueue(new Frame { Type = Proto.TStatus, Seq = NextSeq(), Payload = Text("BOOT,FW=0.1.0") });
                _pending.Enqueue(new Frame { Type = Proto.TStatus, Seq = NextSeq(), Payload = Text("AP_START,FW=0.1.0,SSID=VibeKey-SIM1") });
            }

            SetState(LinkState.Connected);
        });
    }

    public void Close()
    {
        _timer.Stop();
        lock (_gate)
        {
            _pending.Clear();
        }

        SetState(LinkState.Disconnected);
    }

    public void Send(byte type, ushort seq, ReadOnlySpan<byte> payload)
    {
        if (_state != LinkState.Connected)
        {
            return;
        }

        lock (_gate)
        {
            switch (type)
            {
                case Proto.TPing:
                    _pending.Enqueue(new Frame { Type = Proto.TPong, Seq = NextSeq() });
                    break;

                case Proto.TStatusReq:
                    _pending.Enqueue(new Frame
                    {
                        Type = Proto.TStatus,
                        Seq = NextSeq(),
                        Payload = Text(_inDfu ? "DOWNLOAD,FW=0.1.0" : "WORK_READY,FW=0.1.0,RSSI=-52,ASR=OK"),
                    });
                    break;

                case Proto.TWifiCfg:
                    // 与真实固件一致：先 ACK，然后"重启"
                    _pending.Enqueue(new Frame { Type = Proto.TAck, Seq = seq, Payload = new byte[] { Proto.AckOk } });
                    _inDfu = true;
                    _pending.Enqueue(new Frame { Type = Proto.TStatus, Seq = NextSeq(), Payload = Text("DOWNLOAD,FW=0.1.0") });
                    break;

                case Proto.TEnterDfu:
                    if (PayloadEquals(payload, Proto.DfuMagic))
                    {
                        _pending.Enqueue(new Frame { Type = Proto.TAck, Seq = seq, Payload = new byte[] { Proto.AckOk } });
                        _pending.Enqueue(new Frame { Type = Proto.TStatus, Seq = NextSeq(), Payload = Text("DOWNLOAD,FW=0.1.0") });
                        _inDfu = true;
                    }
                    else
                    {
                        _pending.Enqueue(new Frame { Type = Proto.TAck, Seq = seq, Payload = Nack("MAGIC") });
                    }

                    break;

                case Proto.TVoiceCtl:
                    _pending.Enqueue(new Frame { Type = Proto.TAck, Seq = seq, Payload = new byte[] { Proto.AckOk } });
                    break;

                default:
                    _pending.Enqueue(new Frame { Type = Proto.TAck, Seq = seq, Payload = Nack("TYPE") });
                    break;
            }
        }
    }

    /// <summary>模拟一次按键，便于测试映射链。</summary>
    public void SimulateKeyPress(char ch)
    {
        lock (_gate)
        {
            _pending.Enqueue(new Frame { Type = Proto.TKeyEvent, Seq = NextSeq(), Payload = new[] { (byte)ch } });
        }
    }

    /// <summary>模拟 ASR 返回一段文本。</summary>
    public void SimulateAsrText(string text, bool isFinal)
    {
        var bytes = System.Text.Encoding.UTF8.GetBytes(text);
        var payload = new byte[bytes.Length + 1];
        payload[0] = isFinal ? Proto.AsrFinal : Proto.AsrPartial;
        Array.Copy(bytes, 0, payload, 1, bytes.Length);
        lock (_gate)
        {
            _pending.Enqueue(new Frame { Type = Proto.TAsrText, Seq = NextSeq(), Payload = payload });
        }
    }

    public void Dispose()
    {
        _timer.Dispose();
        GC.SuppressFinalize(this);
    }

    private void OnTick(object? sender, ElapsedEventArgs e)
    {
        Frame? frame = null;
        lock (_gate)
        {
            if (_pending.Count > 0)
            {
                frame = _pending.Dequeue();
            }
        }

        if (frame is not null)
        {
            FrameReceived?.Invoke(this, frame);
        }
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

    private ushort NextSeq() => ++_seq;

    private static byte[] Text(string s) => System.Text.Encoding.ASCII.GetBytes(s);

    private static byte[] Nack(string reason)
    {
        var r = System.Text.Encoding.ASCII.GetBytes(reason);
        var buf = new byte[1 + r.Length];
        buf[0] = Proto.AckNack;
        Array.Copy(r, 0, buf, 1, r.Length);
        return buf;
    }

    private static bool PayloadEquals(ReadOnlySpan<byte> payload, byte[] magic)
        => payload.SequenceEqual(magic);
}
