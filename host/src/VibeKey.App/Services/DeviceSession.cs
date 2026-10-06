using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using VibeKey.Protocol;

namespace VibeKey.App.Services;

/// <summary>设备状态快照（对应固件 vk_status_t）。</summary>
public sealed class DeviceStatus : INotifyPropertyChanged
{
    private string _state = "BOOT";
    private string _firmware = "";
    private string _ip = "";
    private string _ssid = "";
    private int _rssi;
    private string _error = "NONE";
    private string _warning = "NONE";
    private string _asr = "";

    public string State
    {
        get => _state;
        set => Set(ref _state, value);
    }

    public string Firmware
    {
        get => _firmware;
        set => Set(ref _firmware, value);
    }

    public string Ip
    {
        get => _ip;
        set => Set(ref _ip, value);
    }

    public string Ssid
    {
        get => _ssid;
        set => Set(ref _ssid, value);
    }

    public int Rssi
    {
        get => _rssi;
        set => Set(ref _rssi, value);
    }

    public string Error
    {
        get => _error;
        set => Set(ref _error, value);
    }

    public string Warning
    {
        get => _warning;
        set => Set(ref _warning, value);
    }

    public string Asr
    {
        get => _asr;
        set => Set(ref _asr, value);
    }

    /// <summary>是否处于故障态。</summary>
    public bool IsError => string.Equals(State, "ERROR", StringComparison.OrdinalIgnoreCase);

    /// <summary>是否配网中。</summary>
    public bool IsProvisioning => string.Equals(State, "AP_START", StringComparison.OrdinalIgnoreCase);

    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// 解析状态帧载荷 "STATE[,K=V]*"（设计文档 §3.5）。
    /// </summary>
    public void ApplyPayload(byte[] payload)
    {
        var text = Encoding.UTF8.GetString(payload);
        var fields = text.Split(',', StringSplitOptions.RemoveEmptyEntries);
        if (fields.Length == 0)
        {
            return;
        }

        State = fields[0];

        foreach (var f in fields.Skip(1))
        {
            var eq = f.IndexOf('=');
            if (eq <= 0)
            {
                continue;
            }

            var key = f[..eq];
            var val = f[(eq + 1)..];
            switch (key)
            {
                case "FW":
                    Firmware = val;
                    break;
                case "IP":
                    Ip = val;
                    break;
                case "SSID":
                    Ssid = val;
                    break;
                case "RSSI":
                    _ = int.TryParse(val, out var r);
                    Rssi = r;
                    break;
                case "ERR":
                    Error = val;
                    break;
                case "WARN":
                    Warning = val;
                    break;
                case "ASR":
                    Asr = val;
                    break;
            }
        }

        OnChanged(nameof(IsError));
        OnChanged(nameof(IsProvisioning));
    }

    private void OnChanged(string name) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    private void Set<T>(ref T field, T value, [System.Runtime.CompilerServices.CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }

        field = value;
        OnChanged(name!);
    }
}

/// <summary>
/// 设备会话：心跳、SEQ 跨通道去重、状态缓存。
///
/// 对应 PRD 6.2：上位机每 1s 发一次 ping，500ms 内回 pong，
/// 连续 3 次超时判定设备离线。
/// </summary>
public sealed class DeviceSession : IDisposable
{
    private readonly IDeviceTransport _transport;
    private readonly System.Timers.Timer _heartbeat;
    private readonly Dictionary<byte, FrameStream> _streams = new();
    private ushort _downSeq;
    private ushort _lastUpSeq;
    private bool _hasUpSeq;
    private int _missedPongs;

    public DeviceSession(IDeviceTransport transport)
    {
        _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        _heartbeat = new System.Timers.Timer(1000) { AutoReset = true };
        _heartbeat.Elapsed += (_, _) => OnHeartbeat();

        _transport.FrameReceived += OnFrameReceived;
        _transport.StateChanged += OnStateChanged;
    }

    /// <summary>当前设备状态。</summary>
    public DeviceStatus Status { get; } = new();

    /// <summary>是否在线（按 PRD：连续 3 次 ping 无 pong 判离线）。</summary>
    public bool IsOnline { get; private set; }

    /// <summary>收到按键/旋钮事件。</summary>
    public event EventHandler<char>? KeyEventReceived;

    /// <summary>收到 ASR 文本。</summary>
    public event EventHandler<AsrText>? AsrTextReceived;

    /// <summary>收到 ACK/NACK。</summary>
    public event EventHandler<Ack>? AckReceived;

    public void Start()
        {
            _transport.Open();
            _heartbeat.Start();
            SendPing();
        }

    public void Stop()
    {
        _heartbeat.Stop();
        _transport.Close();
        IsOnline = false;
    }

    /// <summary>喂入从通道读到的原始字节。</summary>
    public void FeedBytes(TransportKind kind, ReadOnlySpan<byte> data)
    {
            if (!_streams.TryGetValue((byte)kind, out var stream))
            {
                stream = new FrameStream();
                _streams[(byte)kind] = stream;
            }

            stream.Feed(data, OnFrame);
        }

        /// <summary>
        /// 上行 SEQ 去重（设计文档 §3.4 规则 3）。
        /// 用 RFC 1982 式序列号比较以正确处理 16 位回绕——简单的大小比较会在
        /// 回绕点附近把真实事件误判成重复帧而丢弃。
        /// </summary>
        private bool IsDuplicateUpSeq(ushort seq)
        {
            var diff = (short)(seq - _lastUpSeq);
            if (_hasUpSeq && diff <= 0)
            {
                return true;
            }

            _lastUpSeq = seq;
            _hasUpSeq = true;
            return false;
        }

        private void OnFrame(Frame frame)
        {
            if (IsDuplicateUpSeq(frame.Seq))
            {
                return;   // USB 与 WiFi 双通道的重复帧，丢弃
            }
            switch (frame.Type)
            {
                case Proto.TKeyEvent when frame.Payload.Length == 1:
                    KeyEventReceived?.Invoke(this, (char)frame.Payload[0]);
                    break;

                case Proto.TStatus:
                    Status.ApplyPayload(frame.Payload);
                    break;

                case Proto.TAsrText:
                    var isFinal = frame.Payload.Length > 0 && frame.Payload[0] == Proto.AsrFinal;
                    var text = Encoding.UTF8.GetString(frame.Payload, 1, Math.Max(0, frame.Payload.Length - 1));
                    AsrTextReceived?.Invoke(this, new AsrText(text, isFinal));
                    break;

                case Proto.TPong:
                    _missedPongs = 0;
                    IsOnline = true;
                    break;

                case Proto.TAck:
                    var ok = frame.Payload.Length > 0 && frame.Payload[0] == Proto.AckOk;
                    var reason = frame.Payload.Length > 1
                        ? Encoding.ASCII.GetString(frame.Payload, 1, frame.Payload.Length - 1)
                        : string.Empty;
                    AckReceived?.Invoke(this, new Ack(ok, reason, frame.Seq));
                    break;
            }
        }

        private void OnFrameReceived(object? sender, Frame frame) => OnFrame(frame);

        private void OnStateChanged(object? sender, LinkState state)
        {
            if (state != LinkState.Connected)
            {
                IsOnline = false;
                _missedPongs = 0;
            }
        }

        private void OnHeartbeat()
        {
            SendPing();

            _missedPongs++;
            if (_missedPongs >= 3 && IsOnline)
            {
                IsOnline = false;
            }
        }

        private void SendPing()
        {
            if (_transport.State != LinkState.Connected)
            {
                return;
            }

            _transport.Send(Proto.TPing, ++_downSeq, ReadOnlySpan<byte>.Empty);
        }

        // ---- 下行指令 ----

        public void RequestStatus() => _transport.Send(Proto.TStatusReq, ++_downSeq, ReadOnlySpan<byte>.Empty);

        public void SendWifiConfig(string ssid, string password)
            => _transport.Send(Proto.TWifiCfg, ++_downSeq, FrameCodec.BuildWifiConfig(ssid, password));

        public void VoiceStart() => _transport.Send(Proto.TVoiceCtl, ++_downSeq, new[] { Proto.VoiceStart });

        public void VoiceStop() => _transport.Send(Proto.TVoiceCtl, ++_downSeq, new[] { Proto.VoiceStop });

        /// <summary>请求进入烧录模式。仅 USB 通道会被固件接受（设计文档 §7.1）。</summary>
        public void RequestEnterDfu()
            => _transport.Send(Proto.TEnterDfu, ++_downSeq, Proto.DfuMagic);

        public void Dispose()
        {
            _heartbeat.Dispose();
            _transport.FrameReceived -= OnFrameReceived;
            _transport.StateChanged -= OnStateChanged;
            GC.SuppressFinalize(this);
        }
    }

/// <summary>ASR 文本。</summary>
public readonly record struct AsrText(string Text, bool IsFinal);

/// <summary>指令应答。</summary>
public readonly record struct Ack(bool Ok, string Reason, ushort Seq);
