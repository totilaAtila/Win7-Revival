using System.ComponentModel;
using Win7Revival.Core.Interfaces;
using Win7Revival.Core.Services;

namespace Win7Revival.Core.Tests;

public class CoreServiceTests : IDisposable
{
    private readonly string _testDir;
    private readonly SettingsService _settingsService;
    private readonly CoreService _coreService;

    public CoreServiceTests()
    {
        _testDir = Path.Combine(Path.GetTempPath(), $"Win7Revival_Tests_{Guid.NewGuid():N}");
        _settingsService = new SettingsService(_testDir);
        _coreService = new CoreService(_settingsService);
    }

    public void Dispose()
    {
        _coreService.Dispose();
        if (Directory.Exists(_testDir))
            Directory.Delete(_testDir, true);
    }

    [Fact]
    public void RegisterModule_AddsModule()
    {
        var module = new FakeModule("Test");
        _coreService.RegisterModule(module);

        Assert.Single(_coreService.Modules);
        Assert.Equal("Test", _coreService.Modules[0].Name);
    }

    [Fact]
    public void RegisterModule_IgnoresDuplicate()
    {
        var module1 = new FakeModule("Test");
        var module2 = new FakeModule("Test");
        _coreService.RegisterModule(module1);
        _coreService.RegisterModule(module2);

        Assert.Single(_coreService.Modules);
    }

    [Fact]
    public async Task EnableModuleAsync_EnablesDisabledModule()
    {
        var module = new FakeModule("Test");
        _coreService.RegisterModule(module);

        await _coreService.EnableModuleAsync("Test");

        Assert.True(module.IsEnabled);
        Assert.Equal(1, module.EnableCallCount);
    }

    [Fact]
    public async Task EnableModuleAsync_DoesNothing_WhenAlreadyEnabled()
    {
        var module = new FakeModule("Test");
        _coreService.RegisterModule(module);
        await _coreService.EnableModuleAsync("Test");

        await _coreService.EnableModuleAsync("Test");

        Assert.Equal(1, module.EnableCallCount);
    }

    [Fact]
    public async Task DisableModuleAsync_DisablesEnabledModule()
    {
        var module = new FakeModule("Test");
        _coreService.RegisterModule(module);
        await _coreService.EnableModuleAsync("Test");

        await _coreService.DisableModuleAsync("Test");

        Assert.False(module.IsEnabled);
    }

    [Fact]
    public async Task EnableModuleAsync_AttemptsDisable_OnFailure()
    {
        var module = new FakeModule("Test") { ShouldThrowOnEnable = true };
        _coreService.RegisterModule(module);

        await _coreService.EnableModuleAsync("Test");

        Assert.False(module.IsEnabled);
        Assert.Equal(1, module.DisableCallCount);
    }

    [Fact]
    public async Task InitializeModulesAsync_InitializesAllModules()
    {
        var m1 = new FakeModule("A");
        var m2 = new FakeModule("B");
        _coreService.RegisterModule(m1);
        _coreService.RegisterModule(m2);

        await _coreService.InitializeModulesAsync();

        Assert.True(m1.Initialized);
        Assert.True(m2.Initialized);
    }

    private class FakeModule : IModule
    {
        public event PropertyChangedEventHandler? PropertyChanged;
        public string Name { get; }
        public string Description => "Fake module for testing";
        public bool IsEnabled { get; private set; }
        public string Version => "1.0.0-test";
        public bool Initialized { get; private set; }
        public int EnableCallCount { get; private set; }
        public int DisableCallCount { get; private set; }
        public bool ShouldThrowOnEnable { get; set; }

        public FakeModule(string name) => Name = name;

        public Task InitializeAsync()
        {
            Initialized = true;
            return Task.CompletedTask;
        }

        public Task EnableAsync()
        {
            EnableCallCount++;
            if (ShouldThrowOnEnable)
                throw new InvalidOperationException("Simulated failure");
            IsEnabled = true;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsEnabled)));
            return Task.CompletedTask;
        }

        public Task DisableAsync()
        {
            DisableCallCount++;
            IsEnabled = false;
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(IsEnabled)));
            return Task.CompletedTask;
        }

        public Task SaveSettingsAsync() => Task.CompletedTask;
    }
}
