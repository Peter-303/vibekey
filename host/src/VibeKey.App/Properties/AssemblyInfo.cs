using System.Runtime.CompilerServices;

// 让测试项目能访问 internal 成员（例如 DeviceDiscovery.Parse）。
// 不必把这些成员改成 public——它们属于实现细节，不该进公开 API 面。
[assembly: InternalsVisibleTo("VibeKey.App.Tests")]
