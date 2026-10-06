using System;
using System.Windows;

namespace VibeKey.App.Services;

/// <summary>
/// 主题切换器：浅色 / 深色。
///
/// 原理：Application.Resources 的 MergedDictionaries 里，第 0 项是主题色板
/// （Light.xaml 或 Dark.xaml），第 1 项是共享样式（Base.xaml）。
/// 切换主题时只替换第 0 项的来源，样式层用 DynamicResource 引用语义键，
/// 不用改任何 Style。
///
/// 键的语义（两套主题共用）：
///   Bg / Card / CardHover / Overlay
///   Border / BorderStrong
///   Text / TextMuted / TextFaint
///   Accent / AccentHover / AccentPressed / AccentSubtle / AccentSubtleHover
///   Ok / OkSubtle / Warn / WarnSubtle / Bad / BadSubtle
///   Divider / FocusRing
/// </summary>
public static class ThemeManager
{
    public const string Light = "light";
    public const string Dark = "dark";

    private static readonly Uri LightUri = new("Themes/Light.xaml", UriKind.Relative);
    private static readonly Uri DarkUri = new("Themes/Dark.xaml", UriKind.Relative);

    /// <summary>当前主题名。</summary>
    public static string Current { get; private set; } = Light;

    /// <summary>
    /// 切换主题。name 只能是 <see cref="Light"/> 或 <see cref="Dark"/>。
    /// </summary>
    public static void SetTheme(string name)
    {
        if (string.Equals(name, Current, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }

        var uri = name.Equals(Dark, StringComparison.OrdinalIgnoreCase) ? DarkUri : LightUri;
        var dict = new ResourceDictionary { Source = uri };

        var merged = System.Windows.Application.Current.Resources.MergedDictionaries;
        if (merged.Count == 0)
        {
            merged.Add(dict);
        }
        else
        {
            merged[0] = dict;
        }

        Current = name.Equals(Dark, StringComparison.OrdinalIgnoreCase) ? Dark : Light;
    }

    /// <summary>在浅色与深色之间来回切换。</summary>
    public static void Toggle()
        => SetTheme(Current == Dark ? Light : Dark);
}
