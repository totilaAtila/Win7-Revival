# Agents.md — CrystalFrame Engine (Windows 11)

## 1) Scopul proiectului
CrystalFrame este un utilitar pentru Windows 11 care aplică un **overlay extern** (fără injection / fără patching Explorer) peste:
- **Taskbar** (cu suport pentru **auto-hide** și poziționare pe **oricare edge**)
- **Start Menu** (overlay **doar când Start este deschis**, altfel ascuns)

Overlay-ul este **click-through** (nu interceptează input). Controlul principal este prin **slidere 0–100** pentru opacitate:
- `TaskbarOpacity` (0–100)
- `StartOpacity` (0–100)

**Single monitor**: proiectul este proiectat și testat pentru un singur monitor.

---

## 2) Principii non-negociabile
1. **Sustenabilitate**  
   - Niciun hook/injection în procesele Explorer/StartMenuExperienceHost.
   - Nicio modificare internă a UI-ului nativ; doar strat vizual extern.

2. **Fără impact pe funcționalitate**
   - Taskbar și Start rămân complet utilizabile (click, drag, context menus).
   - Overlay-ul nu blochează interacțiunea.

3. **Performanță**
   - Țintă: **CPU < 2% idle**.
   - Randare eficientă: update-uri doar la schimbări (show/hide, rect change, slider change).

4. **Fail-safe**
   - Dacă Start nu este detectat în mod sigur, componenta Start se dezactivează elegant.
   - Core nu se prăbușește; log + status în Dashboard.

---

## 3) Arhitectură (overview)
### Executabile
- **CrystalFrame.Core** (C++20)  
  Responsabil pentru: detectarea țintelor, gestionarea overlay-ului, randare, IPC, logging.
- **CrystalFrame.Dashboard** (C# .NET 8, WinUI 3)  
  Responsabil pentru: UI setări (slidere/toggles), persistență config, status, comenzi IPC.

### Module Core (obligatorii)
- `ShellTargetLocator` — localizează Taskbar și detectează Start (open/close + rect)
- `OverlayHost` — gestionează ferestrele overlay (Taskbar/Start), click-through, poziționare
- `Renderer` — pipeline performant (DirectComposition recomandat) + opacitate
- `IpcBridge` — canal de comunicare Dashboard ↔ Core (Named Pipes recomandat)
- `ConfigManager` — citește/scrie `config.json`
- `Diagnostics/Logging` — log ring-buffer + fișier

---

## 4) Agenți / roluri (responsabilități clare)

### Agent A — Core Rendering Agent (C++20)
**Obiectiv:** overlay stabil, performant, click-through.

**Responsabilități:**
- Inițializare Core (lifecycle, message loop, DPI awareness pe single monitor)
- Creare `Overlay_Taskbar` și `Overlay_Start`
- Integrare Renderer (DComp tree, SetOpacity, commit scheduling)
- Gestionare reacții: taskbar rect changes, auto-hide show/hide, Explorer restart
- Asigură non-interferența cu input-ul

**Deliverables:**
- Overlay over Taskbar cu opacitate setabilă 0–100
- Overlay Start doar când e deschis, cu opacitate 0–100
- Loguri clare + status hooks

---

### Agent B — Shell Target Locator Agent (C++20)
**Obiectiv:** detectare robustă pentru Taskbar și Start pe un singur monitor.

**Responsabilități:**
- Taskbar:
  - găsire handle principal
  - determinare `RECT` corect indiferent de edge
  - reacție la auto-hide și schimbări ale shell-ului
- Start:
  - detectare open/close
  - calcul `RECT` pentru zona meniului Start
  - fail-safe dacă detecția e incertă

**Metodă:**
- Evenimente / polling minim (coalescing)
- Heuristici pentru Start (fără injection) + „confidence score” intern (opțional)

**Deliverables:**
- API intern: `GetTaskbarRect()`, `IsStartOpen()`, `GetStartRect()`
- Semnale: `OnTaskbarChanged`, `OnStartShown`, `OnStartHidden`

---

### Agent C — Dashboard & UX Agent (C# WinUI 3)
**Obiectiv:** UI de setări simplu, fără lag, cu persistență.

**Responsabilități:**
- UI:
  - Toggle Enable Taskbar/Start (recomandat)
  - Slider 0–100 pentru fiecare
  - Status: Taskbar Found/Not found; Start Detected/Undetected
- IPC:
  - trimitere update live la schimbarea sliderului (cu debounce optional)
  - conectare/reconectare la Core
- Config:
  - scriere `config.json` (debounce 250ms recomandat)
  - încărcare și populare UI la start

**Deliverables:**
- Dashboard funcțional, cu control live asupra opacității

---

### Agent D — QA & Reliability Agent
**Obiectiv:** stabilitate pe termen lung și criterii verificabile.

**Responsabilități:**
- Plan de test: funcțional + regresii
- Scenarii obligatorii:
  - Taskbar jos/sus/stânga/dreapta
  - Auto-hide ON: show/hide
  - Start open/close repetat (spam)
  - Explorer restart recovery
  - Sleep/Wake (optional, dar recomandat)
- Validare performanță:
  - CPU idle
  - memorie stabilă (24h smoke)

**Deliverables:**
- Checklist de acceptanță semnat
- Raport bug-uri cu pași de reproducere + loguri anexate

---

## 5) Instrumente & tehnologii acceptate
### Core (C++)
- C++20, MSVC
- API-uri: `user32`, `dwmapi`, `dcomp`, `d2d1` (după nevoie)
- Smart pointers: `Microsoft::WRL::ComPtr`
- Logging: macro `CF_LOG(level, message)` + thread id + timestamp

### Dashboard (C#)
- .NET 8
- WinUI 3
- JSON: `System.Text.Json`

### IPC
- Named Pipes (recommended) sau alternativ: Local sockets
- Mesaje minimale:
  - `SetTaskbarOpacity(0..100)`
  - `SetStartOpacity(0..100)`
  - `SetTaskbarEnabled(bool)`
  - `SetStartEnabled(bool)`
  - `GetStatus()`

---

## 6) Metode de lucru (workflow)
### Milestones
- **M1**: Taskbar overlay + slider 0–100 (click-through)
- **M2**: Taskbar hardening: auto-hide, edge positions, Explorer restart recovery
- **M3**: Start overlay + slider 0–100 (doar când Start e deschis)
- **M4**: Start hardening + fallback + diag/status complet

### Reguli de commit
- Commits mici, tematice (1 feature / fix per commit)
- Mesaje: `Core: ...`, `Dashboard: ...`, `Locator: ...`, `QA: ...`

---

## 7) Standard de calitate (Definition of Done)
Un milestone este „Done” doar dacă:
- Taskbar/Start sunt complet utilizabile (overlay click-through)
- Slider 0–100 aplică opacitate în timp real
- Fără flicker vizibil la:
  - auto-hide show/hide
  - Start open/close
- CPU idle < 2%
- Recovery după Explorer restart
- Logurile confirmă evenimentele cheie (found/lost, shown/hidden)

---

## 8) Observabilitate (logging & diagnostic)
### Log obligatoriu (CrystalFrame.log)
- Startup summary (versiune, init ok, config loaded)
- Taskbar found/lost + rect + edge
- Start shown/hidden + rect
- Explorer restart detectat + reacție
- Erori `HRESULT` + context (file/line)

### Diagnostic mode (opțional)
- Activat din Dashboard:
  - contur discret al rect-urilor overlay
  - text mic cu status (Taskbar/Start)

---

## 9) Riscuri cunoscute (acceptate)
- Start Menu poate varia între build-uri Windows 11; detecția fără injection este inerent mai fragilă.
- În caz de detecție nesigură, Start overlay se dezactivează (fail-safe), iar proiectul rămâne valid (Taskbar continuă să funcționeze).

---

## 10) Roadmap ulterior (după scope-ul actual)
- Material effects (blur) cu degradare la animare
- Profilare GPU/CPU + optimizări
- Opțiuni UX: preseturi (0/20/40/60/80), hotkey toggle
