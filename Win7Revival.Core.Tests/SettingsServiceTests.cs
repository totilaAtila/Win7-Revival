using Win7Revival.Core.Models;
using Win7Revival.Core.Services;

namespace Win7Revival.Core.Tests;

public class SettingsServiceTests : IDisposable
{
    private readonly string _testDir;
    private readonly SettingsService _service;

    public SettingsServiceTests()
    {
        _testDir = Path.Combine(Path.GetTempPath(), $"Win7Revival_Tests_{Guid.NewGuid():N}");
        _service = new SettingsService(_testDir);
    }

    public void Dispose()
    {
        if (Directory.Exists(_testDir))
            Directory.Delete(_testDir, true);
    }

    [Fact]
    public async Task LoadSettingsAsync_ReturnsDefault_WhenFileDoesNotExist()
    {
        var result = await _service.LoadSettingsAsync<ModuleSettings>("NonExistent");

        Assert.NotNull(result);
        Assert.Equal(string.Empty, result.Name);
        Assert.False(result.IsEnabled);
    }

    [Fact]
    public async Task SaveAndLoad_RoundTrips_Correctly()
    {
        var settings = new ModuleSettings { Name = "TestModule", IsEnabled = true };

        await _service.SaveSettingsAsync("TestModule", settings);
        var loaded = await _service.LoadSettingsAsync<ModuleSettings>("TestModule");

        Assert.Equal("TestModule", loaded.Name);
        Assert.True(loaded.IsEnabled);
    }

    [Fact]
    public async Task LoadSettingsAsync_ReturnsDefault_WhenFileIsCorrupted()
    {
        string filePath = Path.Combine(_testDir, "Corrupt.json");
        await File.WriteAllTextAsync(filePath, "{{not valid json!!!");

        var result = await _service.LoadSettingsAsync<ModuleSettings>("Corrupt");

        Assert.NotNull(result);
        Assert.False(result.IsEnabled);
    }

    [Fact]
    public void GetFilePath_Throws_WhenModuleNameIsEmpty()
    {
        Assert.ThrowsAsync<ArgumentException>(() => _service.LoadSettingsAsync<ModuleSettings>(""));
    }

    [Fact]
    public void GetFilePath_Throws_WhenModuleNameIsNull()
    {
        Assert.ThrowsAsync<ArgumentException>(() => _service.LoadSettingsAsync<ModuleSettings>(null!));
    }

    [Fact]
    public async Task GetFilePath_SanitizesPathTraversal()
    {
        var settings = new ModuleSettings { Name = "../../evil", IsEnabled = true };
        await _service.SaveSettingsAsync("../../evil", settings);

        // Fișierul trebuie creat DOAR în directorul de test, nu în altă parte
        var files = Directory.GetFiles(_testDir, "*.json");
        Assert.Single(files);
        Assert.DoesNotContain("..", Path.GetFileName(files[0]));
    }
}
