# VSCode Task Helper - Publish with Version Prompt
param(
    [Parameter(Mandatory=$true)]
    [string]$ScriptPath,

    [switch]$IsRelease
)

# Clear screen for better UX
Clear-Host

Write-Host "=== CrystalFrame Publishing ===" -ForegroundColor Cyan
Write-Host ""

# Prompt for version
$version = Read-Host "Enter version (e.g., 1.0.0)"

if ([string]::IsNullOrWhiteSpace($version)) {
    Write-Host ""
    Write-Host "ERROR: Version is required!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Press any key to close..." -ForegroundColor Yellow
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}

# Validate version format (basic)
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    Write-Host ""
    Write-Host "WARNING: Version format should be X.Y.Z (e.g., 1.0.0)" -ForegroundColor Yellow
    $continue = Read-Host "Continue anyway? (y/n)"
    if ($continue -ne 'y' -and $continue -ne 'Y') {
        Write-Host "Cancelled." -ForegroundColor Yellow
        exit 0
    }
}

Write-Host ""
Write-Host "Publishing version: $version" -ForegroundColor Green
Write-Host ""

# Run the publish script
try {
    & $ScriptPath -Version $version

    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "=== Publish Complete! ===" -ForegroundColor Green

        if ($IsRelease) {
            Write-Host ""
            Write-Host "Next steps:" -ForegroundColor Yellow
            Write-Host "1. Go to: https://github.com/totilaAtila/Win7-Revival/releases/new" -ForegroundColor White
            Write-Host "2. Create tag: v$version" -ForegroundColor White
            Write-Host "3. Upload the ZIP files from the publish folder" -ForegroundColor White
            Write-Host ""
        }

        Write-Host "Opening publish folder..." -ForegroundColor Cyan
        Start-Sleep -Seconds 2

        $publishPath = Join-Path $PSScriptRoot "..\publish"
        if (Test-Path $publishPath) {
            explorer $publishPath
        }
    } else {
        Write-Host ""
        Write-Host "ERROR: Publish failed!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Press any key to close..." -ForegroundColor Yellow
        $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        exit 1
    }
} catch {
    Write-Host ""
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "Press any key to close..." -ForegroundColor Yellow
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    exit 1
}
