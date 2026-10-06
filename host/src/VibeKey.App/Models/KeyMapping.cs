using System;
using System.Collections.Generic;
using System.Text.Json.Serialization;

namespace VibeKey.App.Models;

/// <summary>动作类型。PRD 6.4 要求的五类。</summary>
public enum ActionType
{
    /// <summary>模拟输入一段文本（粘贴 / 逐键发送）。</summary>
    Text,

    /// <summary>触发快捷键，如 Ctrl+Shift+P。</summary>
    Hotkey,

    /// <summary>打开指定程序 / URL / 文件。</summary>
    OpenTarget,

    /// <summary>触发语音输入（开始/停止录音）。</summary>
    Voice,

    /// <summary>发送固定 Prompt 到剪贴板或当前光标。</summary>
    Clipboard,
}

/// <summary>
/// 一个按键字符到系统动作的绑定。
/// 下位机永远只发一个字符，全部翻译在上位机完成（PRD 6.4）。
/// </summary>
public sealed class KeyMapping
{
    /// <summary>按键字符，'1'-'7' / '+' / '-' / '0'。</summary>
    public char Key { get; set; }

    /// <summary>人类可读名称，如 "K1"、"旋钮右旋"。</summary>
    public string Label { get; set; } = string.Empty;

    public ActionType Action { get; set; } = ActionType.Text;

    /// <summary>Text 动作的文本内容，或 Clipboard 动作的 Prompt 内容。</summary>
    public string Text { get; set; } = string.Empty;

    /// <summary>Hotkey 动作的组合键，如 "Ctrl+Shift+P"。</summary>
    public string Hotkey { get; set; } = string.Empty;

    /// <summary>OpenTarget 动作的目标（路径 / URL）。</summary>
    public string Target { get; set; } = string.Empty;

    /// <summary>是否启用。未绑定的按键显示为"未设置"。</summary>
    public bool Enabled { get; set; } = true;

    [JsonIgnore]
    public bool IsBound => Action switch
    {
        ActionType.Text => !string.IsNullOrWhiteSpace(Text),
        ActionType.Hotkey => !string.IsNullOrWhiteSpace(Hotkey),
        ActionType.OpenTarget => !string.IsNullOrWhiteSpace(Target),
        ActionType.Voice => true,
        ActionType.Clipboard => !string.IsNullOrWhiteSpace(Text),
        _ => false,
    };

    /// <summary>一句话描述，用于 UI 列表显示。</summary>
    public string Describe()
    {
        if (!Enabled)
        {
            return "已禁用";
        }

        return Action switch
        {
            ActionType.Text => $"输入文本：{Truncate(Text)}",
            ActionType.Hotkey => $"快捷键：{Hotkey}",
            ActionType.OpenTarget => $"打开：{Truncate(Target)}",
            ActionType.Voice => "语音输入",
            ActionType.Clipboard => $"复制到剪贴板：{Truncate(Text)}",
            _ => "未设置",
        };
    }

    private static string Truncate(string s)
    {
        if (string.IsNullOrEmpty(s))
        {
            return "(空)";
        }

        return s.Length <= 24 ? s : s[..24] + "…";
    }
}

/// <summary>映射配置文件的根对象。</summary>
public sealed class MappingProfile
{
    public const int CurrentVersion = 1;

    public int Version { get; set; } = CurrentVersion;

    public string Name { get; set; } = "默认方案";

    public List<KeyMapping> Mappings { get; set; } = new();
}

/// <summary>
/// 出厂默认按键字符表（设计文档 §4 已冻结）。
/// </summary>
public static class DefaultKeys
{
    public static readonly (char Key, string Label)[] Table =
    {
        ('1', "K1"),
        ('2', "K2"),
        ('3', "K3"),
        ('4', "K4"),
        ('5', "K5"),
        ('6', "K6"),
        ('7', "K7（建议语音键）"),
        ('+', "旋钮 右旋"),
        ('-', "旋钮 左旋"),
        ('0', "旋钮 按下"),
    };

    public static string LabelFor(char key)
    {
        foreach (var (k, label) in Table)
        {
            if (k == key)
            {
                return label;
            }
        }

        return key.ToString();
    }

    public static MappingProfile CreateDefault()
    {
        var profile = new MappingProfile();
        foreach (var (key, label) in Table)
        {
            profile.Mappings.Add(new KeyMapping { Key = key, Label = label });
        }

        // 出厂默认只给语音键一个动作，其余留给用户自己配
        foreach (var m in profile.Mappings)
        {
            if (m.Key == '7')
            {
                m.Action = ActionType.Voice;
            }
        }

        return profile;
    }
}
