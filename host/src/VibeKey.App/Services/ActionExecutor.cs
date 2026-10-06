using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using VibeKey.App.Models;

namespace VibeKey.App.Services;

/// <summary>动作执行结果。</summary>
public enum ExecuteResult
{
    Ok,
    NotConfigured,
    InvalidConfig,
    Failed,
}

/// <summary>
/// 把按键字符翻译成系统动作（PRD 6.4）。
///
/// 校验与执行分离：<see cref="Validate"/> 可在单元测试里跑，
/// <see cref="Execute"/> 才真正触碰系统（按键模拟、剪贴板、进程启动）。
/// </summary>
public sealed class ActionExecutor
{
    /// <summary>需要触发语音输入时抛出，由上层转给 DeviceSession。</summary>
    public event EventHandler<bool>? VoiceRequested;

    /// <summary>
    /// 校验一条映射是否可执行。
    /// </summary>
    public static (ExecuteResult Result, string? Message) Validate(KeyMapping mapping)
    {
        if (!mapping.Enabled)
        {
            return (ExecuteResult.NotConfigured, "该按键已禁用");
        }

        switch (mapping.Action)
        {
            case ActionType.Text:
                if (string.IsNullOrEmpty(mapping.Text))
                {
                    return (ExecuteResult.InvalidConfig, "要输入的文本是空的");
                }

                return (ExecuteResult.Ok, null);

            case ActionType.Hotkey:
                if (string.IsNullOrWhiteSpace(mapping.Hotkey))
                {
                    return (ExecuteResult.InvalidConfig, "快捷键是空的");
                }

                var parts = mapping.Hotkey.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
                if (parts.Length == 0)
                {
                    return (ExecuteResult.InvalidConfig, "快捷键格式不对");
                }

                var known = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
                {
                    "Ctrl", "Control", "Alt", "Shift", "Win", "Windows",
                };
                // 最后一段是主键，前面必须是修饰键
                foreach (var mod in parts.Take(parts.Length - 1))
                {
                    if (!known.Contains(mod))
                    {
                        return (ExecuteResult.InvalidConfig, $"“{mod}”不是受支持的修饰键（Ctrl/Alt/Shift/Win）");
                    }
                }

                if (known.Contains(parts[^1]) && parts.Length > 1)
                {
                    // 形如 "Ctrl+Shift" 没有主键
                    return (ExecuteResult.InvalidConfig, "快捷键缺少主键");
                }

                return (ExecuteResult.Ok, null);

            case ActionType.OpenTarget:
                if (string.IsNullOrWhiteSpace(mapping.Target))
                {
                    return (ExecuteResult.InvalidConfig, "打开目标是空的");
                }

                return (ExecuteResult.Ok, null);

            case ActionType.Clipboard:
                if (string.IsNullOrEmpty(mapping.Text))
                {
                    return (ExecuteResult.InvalidConfig, "要复制的文本是空的");
                }

                return (ExecuteResult.Ok, null);

            case ActionType.Voice:
                return (ExecuteResult.Ok, null);

            default:
                return (ExecuteResult.NotConfigured, "未设置的动作");
        }
    }

    /// <summary>
    /// 执行一条映射。必须在 UI 线程（STA）调用。
    /// </summary>
    public ExecuteResult Execute(KeyMapping mapping)
    {
        var (result, message) = Validate(mapping);
        if (result != ExecuteResult.Ok)
        {
            return result;
        }

        try
        {
            switch (mapping.Action)
            {
                case ActionType.Text:
                    TypeText(mapping.Text!);
                    return ExecuteResult.Ok;

                case ActionType.Hotkey:
                    SendHotkey(mapping.Hotkey!);
                    return ExecuteResult.Ok;

                case ActionType.OpenTarget:
                    OpenTarget(mapping.Target!);
                    return ExecuteResult.Ok;

                case ActionType.Clipboard:
                    System.Windows.Clipboard.SetText(mapping.Text ?? string.Empty);
                    return ExecuteResult.Ok;

                case ActionType.Voice:
                    VoiceRequested?.Invoke(this, true);
                    return ExecuteResult.Ok;

                default:
                    return ExecuteResult.NotConfigured;
            }
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"执行动作失败：{ex}");
            return ExecuteResult.Failed;
        }
    }

    /// <summary>
    /// 把 "Ctrl+Shift+P" 转成 WPF SendKeys 语法 "^+P"。
    /// </summary>
    public static string ToSendKeysSyntax(string hotkey)
    {
        var parts = hotkey.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        if (parts.Length == 0)
        {
            return string.Empty;
        }

        var sb = new StringBuilder();
        foreach (var mod in parts.Take(parts.Length - 1))
        {
            sb.Append(mod.ToLowerInvariant() switch
            {
                "ctrl" or "control" => "^",
                "alt" => "%",
                "shift" => "+",
                "win" or "windows" => "^%",   // WPF 无 Win 键记号，退化为 Ctrl+Alt
                _ => string.Empty,
            });
        }

        var main = parts[^1];
        sb.Append(main.Length == 1 ? main.ToUpperInvariant() : $"({main})");
        return sb.ToString();
    }

    private static void TypeText(string text)
    {
        // SendKeys 把 + ^ % ~ ( ) { } 视为特殊字符，需要转义
        var escaped = new StringBuilder(text.Length);
        foreach (var c in text)
        {
            if (c is '+' or '^' or '%' or '~' or '(' or ')' or '{' or '}' or '[' or ']')
            {
                escaped.Append('{');
                escaped.Append(c);
                escaped.Append('}');
            }
            else
            {
                escaped.Append(c);
            }
        }

        System.Windows.Forms.SendKeys.SendWait(escaped.ToString());
    }

    private static void SendHotkey(string hotkey)
    {
        var syntax = ToSendKeysSyntax(hotkey);
        if (!string.IsNullOrEmpty(syntax))
        {
            System.Windows.Forms.SendKeys.SendWait(syntax);
        }
    }

    private static void OpenTarget(string target)
    {
        var psi = new ProcessStartInfo
        {
            FileName = target,
            UseShellExecute = true,
        };
        Process.Start(psi);
    }
}
