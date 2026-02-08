# CrystalFrame Publish Script
# Creates a distributable package ready for deployment

param(
    [string]$Version = "1.0.0",
    [string]$OutputDir = ".\publish"
)

Write-Host "=== CrystalFrame Publish ===" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Green

# Step 1: Clean and build Release
Write-Host "`n[1/5] Building Release version..." -ForegroundColor Yellow
& .\build.ps1
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

# Step 2: Create publish directory
Write-Host "`n[2/5] Creating publish directory..." -ForegroundColor Yellow
$publishPath = Join-Path $OutputDir "CrystalFrame-v$Version"
if (Test-Path $publishPath) {
    Remove-Item $publishPath -Recurse -Force
}
New-Item -ItemType Directory -Path $publishPath -Force | Out-Null

# Step 3: Copy Dashboard (main executable)
Write-Host "`n[3/5] Copying Dashboard files..." -ForegroundColor Yellow
$dashboardSource = ".\Dashboard\bin\x64\Release\net8.0-windows10.0.22621.0"
if (-not (Test-Path $dashboardSource)) {
    Write-Host "Error: Dashboard not built. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

# Copy all Dashboard files
Copy-Item "$dashboardSource\*" -Destination $publishPath -Recurse -Force

# Step 4: Copy Core.dll (C++ component)
Write-Host "`n[4/5] Copying Core.dll..." -ForegroundColor Yellow
$coreSource = ".\Core\build\bin\Release\CrystalFrame.Core.dll"
if (-not (Test-Path $coreSource)) {
    Write-Host "Error: Core.dll not built." -ForegroundColor Red
    exit 1
}
Copy-Item $coreSource -Destination $publishPath -Force

# Step 5: Create documentation files
Write-Host "`n[5/5] Creating documentation..." -ForegroundColor Yellow

# Create README.txt
@"
CrystalFrame v$Version
=====================

Installation:
1. Extract all files to a folder (e.g., C:\Program Files\CrystalFrame)
2. Run CrystalFrame.Dashboard.exe
3. Click "Start Core" to activate the custom taskbar

Requirements:
- Windows 10/11 (64-bit)
- .NET 8 Runtime (https://dotnet.microsoft.com/download/dotnet/8.0)
- Visual C++ Redistributable 2015-2022 (usually pre-installed)

Features:
- Custom transparent taskbar (Windows 7 style)
- Custom Start Menu with editable items
- Right-click menu items to rename them
- Adjustable transparency and colors

Controls:
- Win key: Open Start Menu
- ESC: Close Start Menu
- Right-click Start Menu title: Edit menu title
- Right-click menu items: Rename items

Configuration:
- Settings stored in: %LocalAppData%\CrystalFrame\
- Menu names: %LocalAppData%\CrystalFrame\menu_names.json

Troubleshooting:
- If taskbar doesn't change: Restart Explorer (Ctrl+Shift+Esc > Restart Windows Explorer)
- If Core won't start: Run Dashboard as Administrator
- Logs located in: %LocalAppData%\CrystalFrame\logs\

Support:
- GitHub: https://github.com/totilaAtila/Win7-Revival
- Issues: https://github.com/totilaAtila/Win7-Revival/issues

"@ | Out-File -FilePath (Join-Path $publishPath "README.txt") -Encoding UTF8

# Create version info
@"
CrystalFrame v$Version
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Platform: Windows x64
Framework: .NET 8.0
"@ | Out-File -FilePath (Join-Path $publishPath "VERSION.txt") -Encoding UTF8

# Step 6: Create ZIP archive
Write-Host "`nCreating ZIP archive..." -ForegroundColor Yellow
$zipPath = Join-Path $OutputDir "CrystalFrame-v$Version.zip"
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Compress-Archive -Path $publishPath -DestinationPath $zipPath -Force

Write-Host "`n=== Publish Complete ===" -ForegroundColor Green
Write-Host "Published to: $publishPath" -ForegroundColor Cyan
Write-Host "ZIP archive: $zipPath" -ForegroundColor Cyan
Write-Host "Size: $((Get-Item $zipPath).Length / 1MB | ForEach-Object { '{0:N2}' -f $_ }) MB" -ForegroundColor Cyan

# Step 7: Show files
Write-Host "`nPackage contents:" -ForegroundColor Yellow
Get-ChildItem $publishPath | Format-Table Name, Length, LastWriteTime -AutoSize

Write-Host "`nReady for distribution! 🚀" -ForegroundColor Green
