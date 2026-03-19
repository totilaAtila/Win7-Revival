# Quick Standalone Publish - Uses existing build
param(
    [string]$Version = "1.0.0"
)

Write-Host "=== GlassBar Standalone Publish (Quick) ===" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "Using existing build..." -ForegroundColor Yellow

# Check if Core.dll exists
$coreSource = ".\Core\build\bin\Release\GlassBar.Core.dll"
if (-not (Test-Path $coreSource)) {
    Write-Host "`nError: Core.dll not found. Run build.ps1 first!" -ForegroundColor Red
    Write-Host "Run: .\build.ps1" -ForegroundColor Yellow
    exit 1
}

# Publish Dashboard as self-contained
Write-Host "`nPublishing Dashboard (self-contained)..." -ForegroundColor Yellow
Push-Location Dashboard

$publishPath = Join-Path ".." "publish\GlassBar-v$Version-Standalone"

dotnet publish `
    -c Release `
    -r win-x64 `
    --self-contained true `
    -p:PublishSingleFile=false `
    -p:PublishReadyToRun=false `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -o $publishPath

if ($LASTEXITCODE -ne 0) {
    Write-Host "Dashboard publish failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# Copy Core.dll
Write-Host "Copying Core.dll..." -ForegroundColor Yellow
Copy-Item $coreSource -Destination $publishPath -Force

# Create README
Write-Host "Creating documentation..." -ForegroundColor Yellow
@"
GlassBar v$Version (Standalone)
====================================

This is a STANDALONE package - no .NET installation required!

Installation:
1. Extract all files to a folder
2. Run GlassBar.Dashboard.exe
3. Click "Start Core" to activate

Requirements:
- Windows 10/11 (64-bit)
- NO other dependencies needed

Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

GitHub: https://github.com/totilaAtila/GlassBar
"@ | Out-File -FilePath (Join-Path $publishPath "README.txt") -Encoding UTF8

# Create ZIP
Write-Host "`nCreating ZIP archive..." -ForegroundColor Yellow
$zipPath = ".\publish\GlassBar-v$Version-Standalone.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path $publishPath -DestinationPath $zipPath -Force

Write-Host "`n=== Publish Complete ===" -ForegroundColor Green
Write-Host "ZIP archive: $zipPath" -ForegroundColor Cyan
Write-Host "Size: $((Get-Item $zipPath).Length / 1MB | ForEach-Object { '{0:N2}' -f $_ }) MB" -ForegroundColor Cyan

Write-Host "`nReady for distribution! 🚀" -ForegroundColor Green
