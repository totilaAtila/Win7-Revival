# GlassBar Build Script
# Compiles Core (C++) and Dashboard (C#) into a single distributable package

param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",

    [switch]$Clean,
    [switch]$SkipCore,
    [switch]$SkipDashboard,
    [switch]$Publish
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$CoreDir = Join-Path $ScriptDir "Core"
$DashboardDir = Join-Path $ScriptDir "Dashboard"
$CoreBuildDir = Join-Path $CoreDir "build"
$CoreOutputDir = Join-Path $CoreBuildDir "bin\$Configuration"

Write-Host "=== GlassBar Build ===" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration"
Write-Host "Platform: $Platform"
Write-Host ""

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning build directories..." -ForegroundColor Yellow

    if (Test-Path $CoreBuildDir) {
        Remove-Item -Recurse -Force $CoreBuildDir
    }

    $DashboardBinDir = Join-Path $DashboardDir "bin"
    $DashboardObjDir = Join-Path $DashboardDir "obj"
    if (Test-Path $DashboardBinDir) {
        Remove-Item -Recurse -Force $DashboardBinDir
    }
    if (Test-Path $DashboardObjDir) {
        Remove-Item -Recurse -Force $DashboardObjDir
    }

    Write-Host "Clean complete." -ForegroundColor Green
    Write-Host ""
}

# Step 1: Build Core (C++)
if (-not $SkipCore) {
    Write-Host "=== Building Core (C++) ===" -ForegroundColor Cyan

    # Create build directory
    if (-not (Test-Path $CoreBuildDir)) {
        New-Item -ItemType Directory -Path $CoreBuildDir | Out-Null
    }

    Push-Location $CoreBuildDir
    try {
        # Configure with CMake
        Write-Host "Configuring CMake..." -ForegroundColor Yellow
        $cmakeArgs = @(
            "..",
            "-G", "Visual Studio 18 2026",
            "-A", $Platform,
            "-DCMAKE_BUILD_TYPE=$Configuration"
        )
        cmake @cmakeArgs
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed"
        }

        # Build
        Write-Host "Building..." -ForegroundColor Yellow
        cmake --build . --config $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "CMake build failed"
        }

        Write-Host "Core build complete." -ForegroundColor Green
    }
    finally {
        Pop-Location
    }

    # Verify Core.dll exists
    $CoreDll = Join-Path $CoreOutputDir "GlassBar.Core.dll"
    if (-not (Test-Path $CoreDll)) {
        throw "Core.dll not found at: $CoreDll"
    }
    Write-Host "Core.dll: $CoreDll" -ForegroundColor Gray
    Write-Host ""
}

# Step 2: Build Dashboard (C#)
if (-not $SkipDashboard) {
    Write-Host "=== Building Dashboard (C#) ===" -ForegroundColor Cyan

    Push-Location $DashboardDir
    try {
        # Restore packages
        Write-Host "Restoring NuGet packages..." -ForegroundColor Yellow
        dotnet restore
        if ($LASTEXITCODE -ne 0) {
            throw "NuGet restore failed"
        }

        # Build Dashboard (includes Core.exe as embedded resource)
        Write-Host "Building..." -ForegroundColor Yellow
        $buildArgs = @(
            "build",
            "--configuration", $Configuration,
            "-p:Platform=$Platform"
        )
        dotnet @buildArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Dashboard build failed"
        }

        Write-Host "Dashboard build complete." -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
    Write-Host ""
}

# Step 3: Verify output
Write-Host "=== Build Verification ===" -ForegroundColor Cyan

$DashboardOutputDir = Join-Path $DashboardDir "bin\$Platform\$Configuration\net8.0-windows10.0.22621.0"
$DashboardExe = Join-Path $DashboardOutputDir "GlassBar.Dashboard.exe"

if (Test-Path $DashboardExe) {
    Write-Host "Dashboard.exe: $DashboardExe" -ForegroundColor Green
} else {
    Write-Host "Dashboard.exe not found at: $DashboardExe" -ForegroundColor Red
}

# Step 4: Publish (copy to publish folder)
if ($Publish) {
    Write-Host ""
    Write-Host "=== Publishing ===" -ForegroundColor Cyan

    $PublishDir = Join-Path $ScriptDir "publish"

    # Clean publish directory
    if (Test-Path $PublishDir) {
        Remove-Item -Recurse -Force $PublishDir
    }
    New-Item -ItemType Directory -Path $PublishDir | Out-Null

    # Copy Dashboard and all dependencies
    Copy-Item -Path "$DashboardOutputDir\*" -Destination $PublishDir -Recurse

    Write-Host "Published to: $PublishDir" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Build Complete ===" -ForegroundColor Cyan
Write-Host "To run: $DashboardExe"
