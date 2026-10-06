using System;
using System.Collections.Generic;
using System.Management;
using System.Text.RegularExpressions;

namespace VibeKey.App.Services;

/// <summary>
/// 基于 WMI 的串口设备枚举器。
///
/// 为什么需要它：<see cref="System.IO.Ports.SerialPort.GetPortNames"/> 只能给出
/// "COM12" 这样的名字，拿不到 VID/PID。而 PRD 6.1 要求"上位机通过 VID/PID 自动识别设备"，
/// 烧录时的端口跳变也需要按 VID/PID 区分工作态与下载态（设计文档 §7.1 情形 A）。
/// </summary>
public sealed class WmiSerialDeviceEnumerator : ISerialDeviceEnumerator
{
    // 形如 "USB Serial Device (COM12)" 或 "Silicon Labs CP210x USB to UART Bridge (COM7)"
    private static readonly Regex PortNameRegex = new(@"\((?<port>COM\d+)\)",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    // 形如 "VID_303A&PID_4001"
    private static readonly Regex VidPidRegex = new(@"VID_(?<vid>[0-9A-Fa-f]{4}).*?PID_(?<pid>[0-9A-Fa-f]{4})",
        RegexOptions.Compiled | RegexOptions.IgnoreCase);

    public IReadOnlyList<SerialDeviceInfo> Enumerate()
    {
        var result = new List<SerialDeviceInfo>();

        try
        {
            using var searcher = new ManagementObjectSearcher(
                "SELECT Name, PNPDeviceID FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'");

            foreach (ManagementBaseObject item in searcher.Get())
            {
                using (item)
                {
                    var name = item["Name"]?.ToString();
                    var pnpId = item["PNPDeviceID"]?.ToString();
                    if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(pnpId))
                    {
                        continue;
                    }

                    var portMatch = PortNameRegex.Match(name);
                    if (!portMatch.Success)
                    {
                        continue;
                    }

                    var idMatch = VidPidRegex.Match(pnpId);
                    if (!idMatch.Success)
                    {
                        // 没有 USB VID/PID 的设备（如板载传统串口）跳过
                        continue;
                    }

                    result.Add(new SerialDeviceInfo(
                        portMatch.Groups["port"].Value.ToUpperInvariant(),
                        ushort.Parse(idMatch.Groups["vid"].Value, System.Globalization.NumberStyles.HexNumber),
                        ushort.Parse(idMatch.Groups["pid"].Value, System.Globalization.NumberStyles.HexNumber),
                        name));
                }
            }
        }
        catch (Exception)
        {
            // WMI 偶发不可用（服务未起/权限不足）：返回空列表，让上层显示"未发现设备"，
            // 不要让枚举失败把整个应用带走。
        }

        return result;
    }
}
