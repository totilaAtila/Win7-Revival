# 📦 CrystalFrame Publishing Guide

Ghid complet pentru publicarea și distribuirea aplicației CrystalFrame.

## 📚 Cuprins

1. [Quick Start](#quick-start)
2. [Publishing Options](#publishing-options)
3. [Detailed Workflows](#detailed-workflows)
4. [GitHub Release](#github-release)
5. [Troubleshooting](#troubleshooting)

---

## 🚀 Quick Start

### Opțiunea 1: Publish Simplu (Framework-Dependent)
```powershell
.\publish.ps1 -Version "1.0.0"
```
**Rezultat:** Package de ~15-20 MB în `.\publish\CrystalFrame-v1.0.0.zip`
**Necesită:** .NET 8 Runtime instalat pe calculatorul utilizatorului

### Opțiunea 2: Publish Standalone (Self-Contained)
```powershell
.\publish-standalone.ps1 -Version "1.0.0"
```
**Rezultat:** Package de ~70-90 MB în `.\publish\CrystalFrame-v1.0.0-Standalone.zip`
**Necesită:** NIMIC! Include .NET runtime

### Opțiunea 3: GitHub Release (Ambele Packages)
```powershell
.\create-release.ps1 -Version "1.0.0"
```
**Rezultat:** Ambele package-uri + release notes pregătite pentru GitHub

---

## 📋 Publishing Options

### 1️⃣ Framework-Dependent (Recomandat pentru distribuție generală)

**Avantaje:**
- ✅ Package mic (~15-20 MB)
- ✅ Build rapid
- ✅ Updates mai ușoare
- ✅ Utilizatorii pot folosi .NET shared runtime

**Dezavantaje:**
- ⚠️ Necesită .NET 8 Runtime instalat
- ⚠️ Utilizatorii trebuie să descarce .NET separat

**Când să folosești:**
- Pentru distribuție publică pe GitHub
- Când utilizatorii au deja .NET instalat
- Când vrei package-uri mici

### 2️⃣ Self-Contained (Recomandat pentru non-tehnici)

**Avantaje:**
- ✅ Zero dependencies
- ✅ Simplu drag-and-drop
- ✅ Funcționează out-of-the-box
- ✅ Nu necesită .NET instalat

**Dezavantaje:**
- ⚠️ Package mare (~70-90 MB)
- ⚠️ Build mai lent
- ⚠️ Include duplicate runtime files

**Când să folosești:**
- Pentru utilizatori non-tehnici
- Când vrei instalare simplă
- Pentru demo-uri și teste rapide

---

## 🔧 Detailed Workflows

### Workflow 1: Development Build (Local Testing)
```powershell
# Build simplu pentru testing
.\build.ps1

# Run direct din bin
.\Dashboard\bin\x64\Release\net8.0-windows10.0.22621.0\CrystalFrame.Dashboard.exe
```

### Workflow 2: Create Release Package
```powershell
# 1. Asigură-te că ai ultimele changes committed
git status

# 2. Tag versiunea în git
git tag v1.0.0
git push origin v1.0.0

# 3. Create packages
.\create-release.ps1 -Version "1.0.0"

# 4. Verifică package-urile
explorer .\publish
```

### Workflow 3: Update Existing Release
```powershell
# 1. Increment versiunea
$newVersion = "1.0.1"

# 2. Create new packages
.\create-release.ps1 -Version $newVersion

# 3. Upload la GitHub Release
# (manual sau cu gh CLI)
```

---

## 🌟 GitHub Release

### Metoda 1: Manual (Web Interface)

1. **Mergi pe GitHub:**
   ```
   https://github.com/totilaAtila/GlassBar/releases/new
   ```

2. **Create tag:**
   - Tag version: `v1.0.0`
   - Target: `main` branch

3. **Release details:**
   - Release title: `GlassBar v1.0.0`
   - Description: Copy din `.\publish\RELEASE_NOTES_v1.0.0.md`

4. **Upload files:**
   - Drag & drop ambele ZIP files
   - `CrystalFrame-v1.0.0.zip`
   - `CrystalFrame-v1.0.0-Standalone.zip`

5. **Publish:**
   - ✅ Set as latest release
   - Click "Publish release"

### Metoda 2: GitHub CLI (Automat)

**Instalare GitHub CLI:**
```powershell
winget install GitHub.cli
# sau download de pe: https://cli.github.com/
```

**Login:**
```powershell
gh auth login
```

**Create release:**
```powershell
# După ce ai rulat create-release.ps1
gh release create v1.0.0 `
    .\publish\CrystalFrame-v1.0.0.zip `
    .\publish\CrystalFrame-v1.0.0-Standalone.zip `
    --title "GlassBar v1.0.0" `
    --notes-file .\publish\RELEASE_NOTES_v1.0.0.md
```

### Metoda 3: Script Automatizat (Complet)

```powershell
# Create și publish într-un singur pas
$version = "1.0.0"

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

## 🎯 Best Practices

### Versioning (Semantic Versioning)
```
v1.0.0 - Initial release
v1.0.1 - Bug fix
v1.1.0 - New feature (minor)
v2.0.0 - Breaking change (major)
```

### Pre-release Testing Checklist
- [ ] Build succeed fără erori
- [ ] Toate feature-urile funcționează
- [ ] Edit dialog funcționează (nu face crash)
- [ ] Start Menu toggle funcționează
- [ ] Transparency funcționează
- [ ] Custom names se salvează și persistă
- [ ] Testat pe Windows 10 și 11

### Release Checklist
- [ ] Version number actualizat
- [ ] Git tag creat
- [ ] Changelog actualizat
- [ ] Screenshots actualizate (dacă e cazul)
- [ ] README actualizat
- [ ] Tested on clean Windows install
- [ ] Ambele packages create și testate
- [ ] Release notes scrise

---

## 🐛 Troubleshooting

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
# Install .NET 8 SDK
winget install Microsoft.DotNet.SDK.8

# Sau download de pe:
# https://dotnet.microsoft.com/download/dotnet/8.0
```

### Error: "CMake not found"
```powershell
# Install CMake
winget install Kitware.CMake

# Add to PATH sau restart terminal
```

### Package prea mare
```powershell
# Folosește Framework-Dependent în loc de Standalone
.\publish.ps1 -Version "1.0.0"

# Sau optimizează:
dotnet publish -c Release -r win-x64 `
    --self-contained true `
    -p:PublishTrimmed=true `
    -p:TrimMode=link
```

### ZIP file corrupt
```powershell
# Verifică integritatea
$hash = Get-FileHash .\publish\CrystalFrame-v1.0.0.zip
Write-Host $hash.Hash

# Re-create ZIP
$version = "1.0.0"
Compress-Archive -Path ".\publish\CrystalFrame-v$version" `
    -DestinationPath ".\publish\CrystalFrame-v$version.zip" `
    -Force
```

---

## 📖 Additional Resources

- [.NET Publishing Guide](https://docs.microsoft.com/en-us/dotnet/core/deploying/)
- [GitHub Releases Guide](https://docs.github.com/en/repositories/releasing-projects-on-github)
- [Semantic Versioning](https://semver.org/)

---

## 🎉 Example Complete Workflow

```powershell
# 1. Finish development
git add .
git commit -m "Release v1.0.0 preparation"
git push

# 2. Create packages
.\create-release.ps1 -Version "1.0.0"

# 3. Create git tag
git tag v1.0.0
git push origin v1.0.0

# 4. Create GitHub release (manual sau CLI)
gh release create v1.0.0 .\publish\*.zip `
    --title "GlassBar v1.0.0" `
    --notes-file .\publish\RELEASE_NOTES_v1.0.0.md `
    --latest

# 5. Anunță release
# - Update README.md cu link la latest release
# - Post pe social media / forum
# - Notify users
```

**Gata! 🚀**
