using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using VibeKey.App.Models;

namespace VibeKey.App.Services;

/// <summary>
/// 映射配置持久化。
/// PRD 6.4：配置存在用户 PC 上（%AppData%/VibeKey/mappings.json），不写入固件；
/// 支持导入/导出，方便多台 PC 同步。
/// </summary>
public sealed class MappingStore
{
    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.Never,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// <summary>配置文件路径。</summary>
    public static string DefaultPath
    {
        get
        {
            var dir = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "VibeKey");
            return System.IO.Path.Combine(dir, "mappings.json");
        }
    }

    private readonly string _path;

    public MappingStore(string? path = null)
    {
        _path = path ?? DefaultPath;
    }

    public string Path => _path;

    /// <summary>
    /// 加载配置。文件不存在或损坏时返回默认方案（PRD 6.1：不得让坏配置挡住用户）。
    /// </summary>
    public MappingProfile Load()
    {
        try
        {
            if (!File.Exists(_path))
            {
                return DefaultKeys.CreateDefault();
            }

            var json = File.ReadAllText(_path);
            var profile = JsonSerializer.Deserialize<MappingProfile>(json, Options);
            if (profile?.Mappings is null or { Count: 0 })
            {
                return DefaultKeys.CreateDefault();
            }

            // 前向兼容：补齐新增的按键行
            var known = new System.Collections.Generic.HashSet<char>(profile.Mappings.ConvertAll(m => m.Key));
            foreach (var (key, label) in DefaultKeys.Table)
            {
                if (!known.Contains(key))
                {
                    profile.Mappings.Add(new KeyMapping { Key = key, Label = label });
                }
            }

            return profile;
        }
        catch (Exception)
        {
            return DefaultKeys.CreateDefault();
        }
    }

    public void Save(MappingProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);

        var dir = System.IO.Path.GetDirectoryName(_path);
        if (!string.IsNullOrEmpty(dir))
        {
            Directory.CreateDirectory(dir);
        }

        // 先写临时文件再替换，避免写一半留下损坏配置
        var tmp = _path + ".tmp";
        File.WriteAllText(tmp, JsonSerializer.Serialize(profile, Options));
        File.Move(tmp, _path, overwrite: true);
    }

    /// <summary>导出到用户指定位置。</summary>
    public void Export(MappingProfile profile, string destination)
    {
        ArgumentNullException.ThrowIfNull(profile);
        File.WriteAllText(destination, JsonSerializer.Serialize(profile, Options));
    }

    /// <summary>从用户指定位置导入。失败时返回 null 并给出原因。</summary>
    public (MappingProfile? Profile, string? Error) Import(string source)
    {
        try
        {
            if (!File.Exists(source))
            {
                return (null, "文件不存在");
            }

            var profile = JsonSerializer.Deserialize<MappingProfile>(File.ReadAllText(source), Options);
            if (profile?.Mappings is null or { Count: 0 })
            {
                return (null, "文件里没有映射配置");
            }

            return (profile, null);
        }
        catch (JsonException ex)
        {
            return (null, $"不是有效的配置文件：{ex.Message}");
        }
        catch (Exception ex)
        {
            return (null, ex.Message);
        }
    }
}
