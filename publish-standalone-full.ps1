# GlassBar Complete Standalone Publisher
# Creates a fully self-contained package with ZERO dependencies
# User doesn't need to install anything - just extract and run!

param(
    [string]$Version = "1.0.1"
)

Write-Host "=== GlassBar COMPLETE Standalone Package ===" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "This package includes EVERYTHING - no dependencies needed!" -ForegroundColor Yellow
Write-Host ""

$ErrorActionPreference = "Stop"

# Step 1: Clean build (ensure fresh compile)
Write-Host "[1/6] Cleaning previous builds..." -ForegroundColor Yellow
if (Test-Path ".\Core\build") {
    Remove-Item ".\Core\build" -Recurse -Force
    Write-Host "  [OK] Core build cleaned" -ForegroundColor Green
}

# Step 2: Build Core (C++) with heap-friendly settings
Write-Host "`n[2/6] Building Core (C++) - Single-threaded to avoid heap errors..." -ForegroundColor Yellow
Push-Location Core

New-Item -ItemType Directory -Path "build" -Force | Out-Null
Push-Location build

# Configure with single-threaded build
cmake .. -DCMAKE_BUILD_TYPE=Release -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    Pop-Location
    Pop-Location
    exit 1
}

# Build with reduced parallelism
cmake --build . --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Core build failed!" -ForegroundColor Red
    Write-Host "Try closing other applications and run again." -ForegroundColor Yellow
    Pop-Location
    Pop-Location
    exit 1
}

Pop-Location
Pop-Location

Write-Host "  [OK] Core.dll built successfully" -ForegroundColor Green

# Step 3: Publish Dashboard as self-contained with EVERYTHING
Write-Host "`n[3/6] Publishing Dashboard (self-contained with .NET runtime)..." -ForegroundColor Yellow

# Set publish path (absolute)
$publishPath = Join-Path (Get-Location) "publish\GlassBar-v$Version-Standalone"

# Remove old publish if exists
if (Test-Path $publishPath) {
    Remove-Item $publishPath -Recurse -Force
}

Push-Location Dashboard

dotnet publish `
    -c Release `
    -r win-x64 `
    --self-contained true `
    -p:PublishSingleFile=false `
    -p:PublishTrimmed=false `
    -p:PublishReadyToRun=false `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:IncludeAllContentForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true `
    -o $publishPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "Dashboard publish failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

Write-Host "  [OK] Dashboard published with .NET runtime" -ForegroundColor Green

# Step 4: Copy native DLLs explicitly from fresh Core build
Write-Host "`n[4/6] Copying native DLLs..." -ForegroundColor Yellow
$coreSource = ".\Core\build\bin\Release\GlassBar.Core.dll"
$xamlBridgeSource = ".\Core\build\bin\Release\GlassBar.XamlBridge.dll"

if (!(Test-Path $coreSource)) {
    Write-Host "GlassBar.Core.dll not found after build!" -ForegroundColor Red
    exit 1
}

if (!(Test-Path $xamlBridgeSource)) {
    Write-Host "GlassBar.XamlBridge.dll not found after build!" -ForegroundColor Red
    exit 1
}

Copy-Item $coreSource -Destination $publishPath -Force
Copy-Item $xamlBridgeSource -Destination $publishPath -Force

Write-Host "  [OK] GlassBar.Core.dll copied" -ForegroundColor Green
Write-Host "  [OK] GlassBar.XamlBridge.dll copied" -ForegroundColor Green

# Ensure config.json exists in %LOCALAPPDATA%\GlassBar\ (prevents "Config not found" on first run)
$configDir = "$env:LOCALAPPDATA\GlassBar"
$configDst = "$configDir\config.json"
$configSrc = "$PSScriptRoot\config.json"
if (Test-Path $configDst) {
    Write-Host "  [OK] config.json already present at AppData" -ForegroundColor Green
} elseif (Test-Path $configSrc) {
    if (-not (Test-Path $configDir)) { New-Item -ItemType Directory -Path $configDir | Out-Null }
    Copy-Item $configSrc $configDst -Force
    Write-Host "  [OK] Default config.json deployed to AppData" -ForegroundColor Green
} else {
    Write-Host "  [WARN] config.json not found in script dir - skipping" -ForegroundColor Yellow
}

# Step 5: Create comprehensive documentation
Write-Host "`n[5/6] Creating user documentation..." -ForegroundColor Yellow

@"
=================================================================
    GlassBar v$Version - STANDALONE COMPLETE PACKAGE
=================================================================

🎉 ZERO DEPENDENCIES - Ready to Run!
====================================

This package includes EVERYTHING you need:
✅ .NET 8 Runtime (embedded)
✅ All Windows libraries
✅ GlassBar Core engine
✅ Complete Dashboard application

No installation required!


📦 INSTALLATION (Super Simple)
===============================

1. Extract ALL files to a folder
   Example: C:\Program Files\GlassBar

2. Run: GlassBar.Dashboard.exe

3. Click "Start Core" button

4. Done! Your taskbar is now customized!


💻 SYSTEM REQUIREMENTS
======================

✅ Windows 10 (64-bit) - Version 22H2 or later
✅ Windows 11 (64-bit) - Any version
❌ NO .NET installation needed (included!)
❌ NO Visual C++ Redistributable needed (included!)
❌ NO other dependencies needed!


🎯 FEATURES
===========

• Custom Transparent Taskbar (Windows 7 Aero style)
• Customizable Start Menu
• Editable menu items (right-click to rename)
• Adjustable transparency and colors
• Rounded corners (Windows 11 style)
• Windows key support


⌨️ KEYBOARD SHORTCUTS
=====================

Windows Key       Open Start Menu
ESC              Close Start Menu
Right-Click      Edit menu items/title


🎨 CUSTOMIZATION
================

All settings stored in:
%LocalAppData%\GlassBar\

Files:
• config.json        - Main settings
• menu_names.json    - Custom menu names


🔧 TROUBLESHOOTING
==================

Problem: Taskbar doesn't change after starting Core
Solution: Restart Windows Explorer
         (Task Manager → Windows Explorer → Restart)

Problem: Start Menu doesn't open
Solution: Check if Core is running in Dashboard

Problem: Application won't start
Solution: Run as Administrator (right-click → Run as administrator)


📁 PACKAGE CONTENTS
===================

Total Size: ~80-90 MB (includes .NET runtime)

Main Files:
• GlassBar.Dashboard.exe  - Main application
• GlassBar.Core.dll        - Native engine
• *.dll (many)                 - .NET runtime & Windows SDK

All files are required - do not delete any!


🌐 SUPPORT & UPDATES
====================

GitHub: https://github.com/totilaAtila/GlassBar
Report Issues: https://github.com/totilaAtila/GlassBar/issues

Check for updates: https://github.com/totilaAtila/GlassBar/releases


📄 LICENSE
==========

See LICENSE file in repository


🏗️ BUILD INFO
==============

Version: $Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Platform: Windows x64 (Self-Contained)
.NET Version: 8.0 (embedded)
Packaging: Complete Standalone


=================================================================
          Enjoy your customized Windows experience!
=================================================================
"@ | Out-File -FilePath (Join-Path $publishPath "README.txt") -Encoding UTF8

# Create quick start guide
@"
QUICK START GUIDE
=================

1. Extract all files to: C:\GlassBar
   (or any folder you prefer)

2. Double-click: GlassBar.Dashboard.exe

3. Click the "Start Core" button

4. Press Windows key to open the new Start Menu!

5. Right-click menu items to customize them

That's it! 🎉

For detailed help, read README.txt
"@ | Out-File -FilePath (Join-Path $publishPath "QUICK_START.txt") -Encoding UTF8

# Create version file
@"
GlassBar v$Version (Standalone Complete)
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Platform: Windows x64
Package Type: Self-Contained (includes .NET runtime)
Dependencies: NONE
"@ | Out-File -FilePath (Join-Path $publishPath "VERSION.txt") -Encoding UTF8

Write-Host "  [OK] Documentation created" -ForegroundColor Green

# Step 6: Create ZIP archive
Write-Host "`n[6/6] Creating ZIP archive..." -ForegroundColor Yellow
$zipPath = ".\publish\GlassBar-v$Version-Standalone-COMPLETE.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path $publishPath -DestinationPath $zipPath -CompressionLevel Optimal -Force

$zipSize = (Get-Item $zipPath).Length / 1MB

Write-Host ""
Write-Host "==================================================================" -ForegroundColor Green
Write-Host "  COMPLETE STANDALONE PACKAGE CREATED SUCCESSFULLY!" -ForegroundColor Green
Write-Host "==================================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Package Details:" -ForegroundColor Cyan
Write-Host "  Location: $zipPath" -ForegroundColor White
Write-Host "  Size: $([math]::Round($zipSize, 2)) MB" -ForegroundColor White
Write-Host "  Type: Self-Contained (ZERO dependencies)" -ForegroundColor White
Write-Host ""
Write-Host "What's Included:" -ForegroundColor Cyan
Write-Host "  [OK] .NET 8 Runtime (embedded)" -ForegroundColor Green
Write-Host "  [OK] GlassBar Core Engine" -ForegroundColor Green
Write-Host "  [OK] Complete Dashboard Application" -ForegroundColor Green
Write-Host "  [OK] All Required Libraries" -ForegroundColor Green
Write-Host "  [OK] User Documentation" -ForegroundColor Green
Write-Host ""
Write-Host "User Experience:" -ForegroundColor Cyan
Write-Host "  - Extract ZIP" -ForegroundColor White
Write-Host "  - Run GlassBar.Dashboard.exe" -ForegroundColor White
Write-Host "  - Click Start Core" -ForegroundColor White
Write-Host "  - Done! No installation needed!" -ForegroundColor White
Write-Host ""
Write-Host "Ready for Distribution!" -ForegroundColor Yellow
Write-Host "  Users can run this on ANY Windows 10/11 PC without installing anything!" -ForegroundColor Yellow
Write-Host ""
Write-Host "Opening publish folder..." -ForegroundColor Cyan

Start-Sleep -Seconds 2
explorer ".\publish"

Write-Host ""
Write-Host "Package ready for GitHub Release or direct distribution!" -ForegroundColor Green
Write-Host ""
