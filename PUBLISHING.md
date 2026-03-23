# GlassBar Publishing Guide

Ghid complet pentru publicarea și distribuirea aplicației GlassBar.

## Cuprins

1. [Quick Start](#quick-start)
2. [Publishing Options](#publishing-options)
3. [Detailed Workflows](#detailed-workflows)
4. [GitHub Release](#github-release)
5. [Troubleshooting](#troubleshooting)

---

## Quick Start

### Opțiunea 1: Publish Simplu (Framework-Dependent)
```powershell
.\publish.ps1 -Version "2.2"
```
**Rezultat:** Package de ~15-20 MB în `.\publish\GlassBar-v2.2.zip`
**Necesită:** .NET 8 Runtime instalat pe calculatorul utilizatorului

### Opțiunea 2: Publish Standalone (Self-Contained)
```powershell
.\publish-standalone.ps1 -Version "2.2"
```
**Rezultat:** Package de ~70-90 MB în `.\publish\GlassBar-v2.2-Standalone.zip`
**Necesită:** NIMIC! Include .NET runtime

### Opțiunea 3: GitHub Release (Ambele Packages)
```powershell
.\create-release.ps1 -Version "2.2"
```
**Rezultat:** Ambele package-uri + release notes pregătite pentru GitHub

---

## Publishing Options

### 1) Framework-Dependent (Recomandat pentru distribuție generală)

**Avantaje:**
- Package mic (~15-20 MB)
- Build rapid
- Updates mai ușoare
- Utilizatorii pot folosi .NET shared runtime

**Dezavantaje:**
- Necesită .NET 8 Runtime instalat
- Utilizatorii trebuie să descarce .NET separat dacă nu îl au

**Când să folosești:**
- Pentru distribuție publică pe GitHub
- Când vrei package-uri mici

### 2) Self-Contained (Recomandat pentru utilizatori non-tehnici)

**Avantaje:**
- Zero dependencies
- Simplu drag-and-drop
- Funcționează out-of-the-box

**Dezavantaje:**
- Package mare (~70-90 MB)
- Build mai lent
- Include duplicate runtime files

**Când să folosești:**
- Pentru utilizatori non-tehnici
- Pentru demo-uri și teste rapide

---

## Detailed Workflows

### Workflow 1: Development Build (Local Testing)
```powershell
# Build simplu pentru testing
.\build.ps1

# Run direct din bin
.\Dashboard\bin\Release\net8.0-windows10.0.22621.0\win-x64\GlassBar.Dashboard.exe
```

### Workflow 2: Create Release Package
```powershell
# 1. Asigură-te că ai ultimele changes committed
git status

# 2. Tag versiunea în git
git tag v2.2
git push origin v2.2

# 3. Create packages
.\create-release.ps1 -Version "2.2"

# 4. Verifică package-urile
explorer .\publish
```

### Workflow 3: Update Existing Release
```powershell
# 1. Increment versiunea
$newVersion = "2.3"

# 2. Create new packages
.\create-release.ps1 -Version $newVersion

# 3. Upload la GitHub Release
# (manual sau cu gh CLI)
```

---

## GitHub Release

### Metoda 1: Manual (Web Interface)

1. **Mergi pe GitHub:**
   ```
   https://github.com/totilaAtila/GlassBar/releases/new
   ```

2. **Create tag:**
   - Tag version: `v2.2`
   - Target: `main` branch

3. **Release details:**
   - Release title: `GlassBar v2.2`
   - Description: Copy din `.\publish\RELEASE_NOTES_v2.2.md`

4. **Upload files:**
   - Drag & drop ambele ZIP files:
   - `GlassBar-v2.2.zip`
   - `GlassBar-v2.2-Standalone.zip`

5. **Publish:**
   - Set as latest release
   - Click "Publish release"

### Metoda 2: GitHub CLI (Automat)

**Instalare GitHub CLI:**
```powershell
winget install GitHub.cli
```

**Login:**
```powershell
gh auth login
```

**Create release:**
```powershell
gh release create v2.2 `
    .\publish\GlassBar-v2.2.zip `
    .\publish\GlassBar-v2.2-Standalone.zip `
    --title "GlassBar v2.2" `
    --notes-file .\publish\RELEASE_NOTES_v2.2.md
```

### Metoda 3: Script Automatizat (Complet)

```powershell
$version = "2.2"

# 1. Create packages
.\create-release.ps1 -Version $version

# 2. Create git tag
git tag "v$version"
git push origin "v$version"

# 3. Create GitHub release
gh release create "v$version" `
    .\publish\*.zip `
    --title "GlassBar v$version" `
    --notes-file ".\publish\RELEASE_NOTES_v$version.md" `
    --latest
```

---

## Best Practices

### Versioning (Semantic Versioning)
```
v2.2   — current release
v2.2.1 — bug fix
v2.3   — new feature (minor)
v3.0   — breaking change (major)
```

### Pre-release Testing Checklist
- [ ] Build reușit fără erori
- [ ] Taskbar overlay apare pe Windows 11
- [ ] Start Menu se deschide la Win key (meniul custom GlassBar)
- [ ] All Programs funcționează (drill-down, keyboard nav)
- [ ] Pinned items: pin/unpin via right-click funcționează
- [ ] Recent items: se actualizează la fiecare deschidere
- [ ] Theme presets (Win7 Aero / Dark) aplicate pe ambele panouri
- [ ] Config persistă după restart
- [ ] System Tray icon funcționează
- [ ] Autostart: Dashboard pornește ascuns în tray

### Release Checklist
- [ ] Version number actualizat
- [ ] Git tag creat
- [ ] README actualizat (dacă e cazul)
- [ ] Testat pe Windows 11 22H2+
- [ ] Ambele packages create și testate
- [ ] Release notes scrise

---

## Troubleshooting

### Error: "Build failed"
```powershell
# Clean rebuild
Remove-Item .\Core\build -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\Dashboard\bin -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item .\Dashboard\obj -Recurse -Force -ErrorAction SilentlyContinue
.\build.ps1
```

### Error: "dotnet command not found"
```powershell
winget install Microsoft.DotNet.SDK.8
```

### Error: "CMake not found"
```powershell
winget install Kitware.CMake
# Restart terminal după instalare
```

### Package prea mare
```powershell
# Folosește Framework-Dependent în loc de Standalone
.\publish.ps1 -Version "2.2"

# Sau optimizează cu trimming:
dotnet publish -c Release -r win-x64 `
    --self-contained true `
    -p:PublishTrimmed=true `
    -p:TrimMode=link
```

### ZIP file corrupt
```powershell
# Verifică integritatea
$hash = Get-FileHash .\publish\GlassBar-v2.2.zip
Write-Host $hash.Hash

# Re-create ZIP
$version = "2.2"
Compress-Archive -Path ".\publish\GlassBar-v$version" `
    -DestinationPath ".\publish\GlassBar-v$version.zip" `
    -Force
```

---

## Additional Resources

- [.NET Publishing Guide](https://docs.microsoft.com/en-us/dotnet/core/deploying/)
- [GitHub Releases Guide](https://docs.github.com/en/repositories/releasing-projects-on-github)
- [Semantic Versioning](https://semver.org/)

---

## Example Complete Workflow

```powershell
# 1. Finish development
git add .
git commit -m "Release v2.2 preparation"
git push

# 2. Create packages
.\create-release.ps1 -Version "2.2"

# 3. Create git tag
git tag v2.2
git push origin v2.2

# 4. Create GitHub release
gh release create v2.2 .\publish\*.zip `
    --title "GlassBar v2.2" `
    --notes-file .\publish\RELEASE_NOTES_v2.2.md `
    --latest
```
