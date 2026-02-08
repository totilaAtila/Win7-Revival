# CrystalFrame GitHub Release Creator
# Creates both packages and prepares for GitHub Release

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,

    [string]$ReleaseName = "",
    [string]$ReleaseNotes = ""
)

if ($ReleaseName -eq "") {
    $ReleaseName = "CrystalFrame v$Version"
}

Write-Host "=== Creating GitHub Release ===" -ForegroundColor Cyan
Write-Host "Version: $Version" -ForegroundColor Green
Write-Host "Release Name: $ReleaseName" -ForegroundColor Green

# Step 1: Create both packages
Write-Host "`n[1/3] Creating Framework-Dependent package..." -ForegroundColor Yellow
& .\publish.ps1 -Version $Version
if ($LASTEXITCODE -ne 0) {
    Write-Host "Framework-dependent publish failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n[2/3] Creating Standalone package..." -ForegroundColor Yellow
& .\publish-standalone.ps1 -Version $Version
if ($LASTEXITCODE -ne 0) {
    Write-Host "Standalone publish failed!" -ForegroundColor Red
    exit 1
}

# Step 3: Create release notes
Write-Host "`n[3/3] Preparing GitHub Release..." -ForegroundColor Yellow

$releaseNotesPath = ".\publish\RELEASE_NOTES_v$Version.md"

if ($ReleaseNotes -eq "") {
    # Auto-generate release notes from recent commits
    $recentCommits = git log --oneline -n 10 --pretty=format:"- %s"

    $ReleaseNotes = @"
# $ReleaseName

## 🎉 What's New

### Features
- Custom Windows 7-style Start Menu
- Transparent taskbar with blur effects
- Editable menu items and title (right-click to edit)
- Rounded corners (Windows 11 style)
- Precise positioning and visual improvements

### Recent Changes
$recentCommits

## 📦 Downloads

**Choose the right version for you:**

### CrystalFrame-v$Version.zip (Recommended - Smaller)
- **Size:** ~15-20 MB
- **Requires:** .NET 8 Runtime installed
- **Best for:** Users who already have .NET 8 or don't mind installing it
- [Download .NET 8 Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)

### CrystalFrame-v$Version-Standalone.zip (No Dependencies)
- **Size:** ~70-90 MB
- **Requires:** Nothing! Includes .NET runtime
- **Best for:** Users who want a simple drag-and-drop installation

## 🚀 Installation

1. Download your preferred ZIP file
2. Extract to a folder (e.g., ``C:\Program Files\CrystalFrame``)
3. Run ``CrystalFrame.Dashboard.exe``
4. Click "Start Core" to activate

## 💡 Usage

- **Open Start Menu:** Press Windows key
- **Close Menu:** Press ESC or click outside
- **Edit Menu Title:** Right-click on title
- **Rename Items:** Right-click on any menu item
- **Adjust Transparency:** Use Dashboard sliders

## 📋 Requirements

- Windows 10/11 (64-bit), version 22H2 or later
- .NET 8 Runtime (for regular version only)

## 🐛 Known Issues

None currently reported. Please report issues at: https://github.com/totilaAtila/Win7-Revival/issues

## 🔧 Configuration

Settings are stored in: ``%LocalAppData%\CrystalFrame\``
- ``config.json`` - Main configuration
- ``menu_names.json`` - Custom menu item names

## 📝 Changelog

See commit history for detailed changes.

---

**Full Changelog:** [View on GitHub](https://github.com/totilaAtila/Win7-Revival/commits/main)
"@
}

$ReleaseNotes | Out-File -FilePath $releaseNotesPath -Encoding UTF8

# Display summary
Write-Host "`n=== Release Ready ===" -ForegroundColor Green
Write-Host "Version: $Version" -ForegroundColor Cyan
Write-Host "`nPackages created:" -ForegroundColor Yellow
Get-ChildItem ".\publish\*.zip" | ForEach-Object {
    $sizeMB = $_.Length / 1MB
    Write-Host "  $($_.Name) - $([math]::Round($sizeMB, 2)) MB" -ForegroundColor Cyan
}

Write-Host "`nRelease notes saved to: $releaseNotesPath" -ForegroundColor Yellow

Write-Host "`n=== Next Steps ===" -ForegroundColor Yellow
Write-Host "1. Go to: https://github.com/totilaAtila/Win7-Revival/releases/new" -ForegroundColor White
Write-Host "2. Create a new tag: v$Version" -ForegroundColor White
Write-Host "3. Release title: $ReleaseName" -ForegroundColor White
Write-Host "4. Copy release notes from: $releaseNotesPath" -ForegroundColor White
Write-Host "5. Upload both ZIP files:" -ForegroundColor White
Write-Host "   - CrystalFrame-v$Version.zip" -ForegroundColor Cyan
Write-Host "   - CrystalFrame-v$Version-Standalone.zip" -ForegroundColor Cyan
Write-Host "6. Click 'Publish release'" -ForegroundColor White

Write-Host "`nOr use GitHub CLI (gh):" -ForegroundColor Yellow
Write-Host "gh release create v$Version .\publish\*.zip --title `"$ReleaseName`" --notes-file `"$releaseNotesPath`"" -ForegroundColor Cyan

Write-Host "`n🎉 Release preparation complete!" -ForegroundColor Green
