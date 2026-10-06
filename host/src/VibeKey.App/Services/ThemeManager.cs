using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;

namespace VibeKey.App.Services;

/// <summary>
/// 主题切换器：浅色 / 深色。
///
/// 实现方式：每次切换都**重建** Application.Resources。
///
/// <code>
/// Application.Resources
/// ├── (直接 Resources)  色板：Accent / Card / Border / ...   ← C# 创建的 SolidColorBrush
/// └── MergedDictionaries
///     └── Themes/BaseStyles.xaml（样式，用 DynamicResource 引用色板键）
/// </code>
///
/// 为什么用 DynamicResource 而不是 StaticResource：
/// StaticResource 在 ResourceDictionary 内部**只查它自己所在的那个字典**
/// （加载期一次性求值，那时还不知道宿主是谁），跨字典根本查不到。
/// DynamicResource 是运行期求值，从控件沿逻辑树向上查到 Application.Resources，
/// Application.Resources 的直接 Resources 里就有色板（C# 创建的 SolidColorBrush），
/// 所以能找到。
///
/// 色值直接定义在 C# 里而不从 XAML 文件加载，是为了避开
/// "延迟加载字典的 DeferredResourceReference 失去宿主" 的坑：
/// C# new 出来的 SolidColorBrush 是真实实例，放进 Resources 就能直接用。
///
/// 切换主题时整个重建，DynamicResource 重新求值成新色板的颜色。
/// </summary>
public static class ThemeManager
{
    public const string Light = "light";
    public const string Dark = "dark";

    private static readonly Uri BaseUri = new("Themes/BaseStyles.xaml", UriKind.Relative);

    /// <summary>当前主题名。</summary>
    public static string Current { get; private set; } = Light;

    // ---- 色板：Neo-Brutalism（新粗野主义）----
    private static readonly Dictionary<string, string> LightPalette = new()
    {
        ["Bg"] = "#F3E5CE", ["Card"] = "#FFFFFF", ["CardHover"] = "#FFF9EF",
        ["CardAccent"] = "#F7CE4B", ["Overlay"] = "#FFFFFF",
        ["Border"] = "#1A1A1A", ["BorderStrong"] = "#1A1A1A",
        ["Text"] = "#1A1A1A", ["TextMuted"] = "#6B6257", ["TextFaint"] = "#A3988A",
        ["Accent"] = "#E8437C", ["AccentHover"] = "#D1336B", ["AccentPressed"] = "#B82658",
        ["AccentSubtle"] = "#FBE3EE", ["AccentSubtleHover"] = "#F7D0E2",
        ["Ok"] = "#1A9E6F", ["OkSubtle"] = "#DDF2E9",
        ["Warn"] = "#C77F00", ["WarnSubtle"] = "#FCEEC8",
        ["Bad"] = "#D93A3A", ["BadSubtle"] = "#FBE0E0",
        ["Divider"] = "#D8C9B2", ["FocusRing"] = "#E8437C",
        ["ShadowColor"] = "#1A1A1A",
    };

    private static readonly Dictionary<string, string> DarkPalette = new()
    {
        ["Bg"] = "#1B1611", ["Card"] = "#2A231A", ["CardHover"] = "#332A1F",
        ["CardAccent"] = "#5C4A12", ["Overlay"] = "#2A231A",
        ["Border"] = "#F3E5CE", ["BorderStrong"] = "#F3E5CE",
        ["Text"] = "#F0E9DD", ["TextMuted"] = "#B0A693", ["TextFaint"] = "#7E7462",
        ["Accent"] = "#F0558C", ["AccentHover"] = "#E8437C", ["AccentPressed"] = "#D1336B",
        ["AccentSubtle"] = "#3D1F2A", ["AccentSubtleHover"] = "#4E2A38",
        ["Ok"] = "#3DBB8A", ["OkSubtle"] = "#1A2E24",
        ["Warn"] = "#E0A42E", ["WarnSubtle"] = "#33270F",
        ["Bad"] = "#E05555", ["BadSubtle"] = "#351C1C",
        ["Divider"] = "#3D3428", ["FocusRing"] = "#F0558C",
        ["ShadowColor"] = "#F3E5CE",
    };

    /// <summary>
    /// 切换主题。name 只能是 <see cref="Light"/> 或 <see cref="Dark"/>。
    /// </summary>
    public static void SetTheme(string name)
    {
        var palette = name.Equals(Dark, StringComparison.OrdinalIgnoreCase)
            ? DarkPalette
            : LightPalette;

        var dict = new ResourceDictionary();
        foreach (var kv in palette)
        {
            var color = (System.Windows.Media.Color)System.Windows.Media.ColorConverter.ConvertFromString(kv.Value);
            dict[kv.Key] = kv.Key == "ShadowColor"
                ? (object)color
                : new SolidColorBrush(color);
        }
        dict.MergedDictionaries.Add(new ResourceDictionary { Source = BaseUri });

        System.Windows.Application.Current.Resources = dict;
        Current = name.Equals(Dark, StringComparison.OrdinalIgnoreCase) ? Dark : Light;
    }

    /// <summary>在浅色与深色之间来回切换。</summary>
    public static void Toggle()
        => SetTheme(Current == Dark ? Light : Dark);
}
