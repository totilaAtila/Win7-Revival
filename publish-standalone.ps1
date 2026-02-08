# CrystalFrame Standalone Publish Script
# Creates a self-contained package that includes .NET runtime (no dependencies needed)

param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = ".\publish"
)

Write-Host "=== CrystalFrame Standalone Publish ===" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "This will create a larger package that includes .NET runtime" -ForegroundColor Yellow

# Step 1: Build Core (C++)
Write-Host "`n[1/4] Building Core (C++)..." -ForegroundColor Yellow
Push-Location Core

# Clean and configure
if (Test-Path "build") {
    Remove-Item "build" -Recurse -Force
}
New-Item -ItemType Directory -Path "build" | Out-Null
Push-Location build

cmake .. -DCMAKE_BUILD_TYPE=Release -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    Pop-Location
    Pop-Location
    exit 1
}

cmake --build . --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Core build failed!" -ForegroundColor Red
    Pop-Location
    Pop-Location
    exit 1
}

Pop-Location
Pop-Location

# Step 2: Publish Dashboard as self-contained
Write-Host "`n[2/4] Publishing Dashboard (self-contained)..." -ForegroundColor Yellow
Push-Location Dashboard

$publishPath = Join-Path ".." (Join-Path $OutputDir "CrystalFrame-v$Version-Standalone")

dotnet publish `
    -c Release `
    -r win-x64 `
    --self-contained true `
    -p:PublishSingleFile=false `
    -p:PublishReadyToRun=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $publishPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "Dashboard publish failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# Step 3: Copy Core.dll
Write-Host "`n[3/4] Copying Core.dll..." -ForegroundColor Yellow
$coreSource = ".\Core\build\bin\Release\CrystalFrame.Core.dll"
Copy-Item $coreSource -Destination $publishPath -Force

# Step 4: Create documentation
Write-Host "`n[4/4] Creating documentation..." -ForegroundColor Yellow

@"
CrystalFrame v$Version (Standalone)
====================================

This is a STANDALONE package - no .NET installation required!

Installation:
1. Extract all files to a folder (e.g., C:\Program Files\CrystalFrame)
2. Run CrystalFrame.Dashboard.exe
3. Click "Start Core" to activate

Requirements:
- Windows 10/11 (64-bit) - 22H2 or later
- NO other dependencies needed (includes .NET runtime)

Package Size: ~70-90 MB (includes runtime)

For a smaller package that requires .NET 8 installed separately,
use the regular publish script (publish.ps1)

Features:
- Custom transparent taskbar
- Custom Start Menu with editable items
- Right-click to rename menu items and title
- Adjustable transparency and colors

Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Platform: Windows x64 (Self-Contained)

GitHub: https://github.com/totilaAtila/Win7-Revival
"@ | Out-File -FilePath (Join-Path $publishPath "README.txt") -Encoding UTF8

# Create ZIP
Write-Host "`nCreating ZIP archive..." -ForegroundColor Yellow
$zipPath = Join-Path $OutputDir "CrystalFrame-v$Version-Standalone.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path $publishPath -DestinationPath $zipPath -Force

Write-Host "`n=== Standalone Publish Complete ===" -ForegroundColor Green
Write-Host "Published to: $publishPath" -ForegroundColor Cyan
Write-Host "ZIP archive: $zipPath" -ForegroundColor Cyan
Write-Host "Size: $((Get-Item $zipPath).Length / 1MB | ForEach-Object { '{0:N2}' -f $_ }) MB" -ForegroundColor Cyan

Write-Host "`nThis package includes .NET runtime and requires no dependencies! 🎉" -ForegroundColor Green
