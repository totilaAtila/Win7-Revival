# Testing Guide — GlassBar

Scenarii de testare manuală pentru validarea funcționalității.

---

## Pre-Test Checklist

- [ ] Windows 11 (22H2 or later)
- [ ] Core compiled (Release build) → `Core/build/bin/Release/GlassBar.Core.dll`
- [ ] Dashboard compiled (Release build) → `Dashboard/bin/Release/.../GlassBar.Dashboard.exe`
- [ ] `GlassBar.Dashboard.exe` pornit; Core toggle → ON (indicator verde)

---

## Milestone M1: Basic Taskbar Overlay

### TC-M1-01: Taskbar Detection
**Steps:**
1. Start `GlassBar.Dashboard.exe`, enable Core toggle
2. Check log: `%LOCALAPPDATA%\GlassBar\GlassBar.log`
3. Look for: `[INFO] Taskbar found: edge=bottom`

**Expected:** ✓ Taskbar detectat; overlay poziționat corect

### TC-M1-02: Opacity Control
**Steps:**
1. Enable Taskbar overlay toggle
2. Set Transparency slider to 0 → overlay invizibil
3. Set Transparency slider to 50 → overlay vizibil la 50%
4. Set Transparency slider to 100 → opacitate maximă

**Expected:** ✓ Opacitate se modifică în timp real, fără flicker

### TC-M1-03: RGB Color Control
**Steps:**
1. Set R=255, G=0, B=0 → overlay roșu
2. Set R=0, G=0, B=200, opacity=60 → overlay albastru semitransparent
3. Verifică preview square (live color preview lângă slidere)

**Expected:** ✓ Culoarea se actualizează instant; preview square reflectă valorile

### TC-M1-04: Click-Through
**Steps:**
1. Set Taskbar opacity to 60
2. Click Start button prin overlay
3. Click system tray icons
4. Right-click Taskbar

**Expected:** ✓ Toate click-urile funcționează ca și când overlay-ul nu există

---

## Milestone M2: Taskbar Hardening

### TC-M2-01: Edge Positions
**For each edge (Bottom, Top, Left, Right):**
1. Move taskbar to edge (Taskbar Settings → Taskbar behaviors → Taskbar alignment)
2. Verify overlay follows
3. Test click-through

**Expected:** ✓ Overlay urmărește Taskbar pe toate edge-urile

### TC-M2-02: Auto-Hide
**Steps:**
1. Enable auto-hide (Taskbar Settings)
2. Move mouse away → Taskbar se ascunde
3. Verifică că overlay-ul se ascunde și el
4. Move mouse to edge → Taskbar apare
5. Verifică că overlay-ul reapare
6. Repetă 10 ori rapid

**Expected:** ✓ Fără flicker; overlay sincronizat perfect cu Taskbar

### TC-M2-03: Explorer Restart
**Steps:**
1. Core + Dashboard running, overlay vizibil
2. Open Task Manager → Find "Windows Explorer" → Right-click → End task
3. Windows restartează Explorer automat
4. Așteaptă 2–3 secunde
5. Verifică că overlay-ul reapare

**Expected:** ✓ Recovery automat; overlay restaurat fără intervenție

### TC-M2-04: Multi-Monitor
**Pre-condition:** Două sau mai multe monitoare conectate

**Steps:**
1. Enable Taskbar overlay
2. Verifică că overlay-ul apare pe **toate** monitoarele unde există Taskbar
3. Modifică opacity → se aplică pe toate monitoarele simultan

**Expected:** ✓ Overlay pe toate monitoarele

---

## Milestone M3: Start Menu Replacement

### TC-M3-01: Start Menu Opens on Win Key
**Steps:**
1. Enable Start Menu toggle în Dashboard (panoul Start Menu)
2. Apasă tasta **Windows** (Win key)
3. Verifică că meniul **GlassBar custom** apare (two-column Win7 layout)
4. Apasă **Esc** sau click în afara meniului → meniul se închide
5. Click pe butonul Start din Taskbar → meniul reapare

**Expected:** ✓ Meniul custom GlassBar înlocuiește complet Start Menu-ul nativ;
latență < 250ms; se deschide/închide fără crash

### TC-M3-02: Win Key Combos Not Blocked
**Steps:**
1. Apasă Win+D → Desktop show/hide funcționează
2. Apasă Win+E → File Explorer se deschide
3. Apasă Win+R → Run dialog se deschide

**Expected:** ✓ Combinațiile Win+X nu sunt blocate; doar Win singur deschide meniul custom

### TC-M3-03: Spam Test
**Steps:**
1. Apasă Win key de 20 ori rapid
2. Verifică absența crash-urilor, lag-ului sau flicker-ului

**Expected:** ✓ Fără crash; handling stabil

---

## Milestone S: Start Menu Features

### TC-S-01: Two-Column Layout
**Steps:**
1. Deschide Start Menu (Win key)
2. Verifică coloana stângă: pinned programs + recent programs + "All Programs ›"
3. Verifică coloana dreaptă: Documents, Pictures, Music, Downloads, Control Panel etc.
4. Verifică user avatar (sus-stânga) și power button (jos-stânga)

**Expected:** ✓ Layout Win7 complet; toate elementele vizibile și cu text clar

### TC-S-02: All Programs Navigation
**Steps:**
1. Click pe "All Programs ›" în coloana stângă
2. Verifică că lista programelor instalate apare (structură folder)
3. Hover pe un folder → submeniu apare
4. Click pe un program → aplicația se lansează; meniul se închide

**Expected:** ✓ Tree complet; drill-down funcțional; aplicațiile se lansează

### TC-S-03: Keyboard Navigation
**Steps:**
1. Deschide Start Menu
2. Apasă ↑/↓ → selecția se mișcă între itemi
3. Apasă Enter pe un item → aplicația se lansează
4. Apasă Esc → meniul se închide
5. În All Programs: ↑/↓ navighează lista; ← se întoarce la lista principală

**Expected:** ✓ Navigare completă cu tastatura

### TC-S-04: Mouse Wheel Scroll
**Steps:**
1. Deschide All Programs cu mai multe intrări
2. Scroll cu mouse wheel pe lista All Programs

**Expected:** ✓ Lista scrollează smooth

### TC-S-05: Pinned Items — Right-Click
**Steps:**
1. Right-click pe un item din secțiunea Pinned
2. Verifică că apare context menu cu: "Unpin from Start", "Select custom icon"
3. Click "Unpin from Start" → itemul dispare din Pinned

**Expected:** ✓ Context menu funcțional; unpin persistent (supraviețuiește restart)

### TC-S-06: Recent Items — Right-Click
**Steps:**
1. Deschide câteva aplicații, re-deschide Start Menu
2. Verifică că Recent items s-au actualizat
3. Right-click pe un item Recent → "Remove from list"
4. Verifică că itemul dispare

**Expected:** ✓ Recent list se actualizează la fiecare deschidere; remove persistent

### TC-S-07: Right-Column Item Visibility
**Steps:**
1. În Dashboard (Start Menu tab), dezactivează "Documents" din lista de itemi
2. Deschide Start Menu → "Documents" nu mai apare în coloana dreaptă
3. Reactivează → reapare

**Expected:** ✓ Visibility toggles funcționale; modificările se aplică imediat

### TC-S-08: Power/Session Submenu
**Steps:**
1. Click pe butonul power (jos-stânga în Start Menu)
2. Verifică submeniu: Sleep, Shut down, Restart
3. Verifică că opțiunile funcționează (sau cel puțin nu cauzează crash la click)

**Expected:** ✓ Submeniu apare; opțiunile funcționează

---

## Milestone M4: Final Validation

### TC-M4-00: Autostart — Start hidden in System Tray
**Steps:**
1. Open Dashboard
2. Enable "Run at startup" toggle
3. Verifică registry key: `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run\GlassBar`
   - Valoarea trebuie să fie: `"<path>\GlassBar.Dashboard.exe" /autostart`
4. Sign out și sign back in (sau rulează comanda manual din CLI)
5. Verifică că **nicio fereastră Dashboard** nu apare pe ecran
6. Verifică că **iconița GlassBar este vizibilă în System Tray**
7. Double-click pe tray icon → fereastra Dashboard se deschide
8. Right-click pe tray icon → context menu: "Open GlassBar" și "Exit"

**Expected:** ✓ La startup fereastra rămâne ascunsă; tray icon prezent; double-click/right-click funcționale

### TC-M4-01: Config Persistence
**Steps:**
1. Set Taskbar opacity 63, R=100, G=50, B=20
2. Set Start Menu bg color R=20, G=40, B=80
3. Close Dashboard
4. Verifică `%LOCALAPPDATA%\GlassBar\config.json` conține valorile
5. Reopen Dashboard
6. Verifică că slidere reflectă valorile salvate

**Expected:** ✓ Setările persistă și se restaurează corect

### TC-M4-02: Performance
**Steps:**
1. Open Task Manager
2. GlassBar Dashboard running, Core ON, overlay activ
3. Let run 5 minutes idle
4. Check CPU usage

**Expected:** ✓ CPU < 2% average

### TC-M4-03: Memory Stability
**Steps:**
1. Notează memoria inițială (Task Manager)
2. Toggle Start Menu 50 de ori
3. Mută Taskbar între edge-uri de 10 ori
4. Ajustează slidere de 100 ori
5. Notează memoria finală

**Expected:** ✓ Creștere memorie < 10 MB

---

## Milestone M7: Windows Compatibility + Theme Presets

### TC-M7-01: Windows Transparency Effects conflict (22H2+)
**Pre-condition:** Windows 11 22H2+; "Transparency effects" ON în Settings → Personalization → Colors

**Steps:**
1. Start GlassBar, enable Taskbar overlay (opacity 50, blur OFF)
2. Verifică că Taskbar este semitransparent
3. Toggle "Transparency effects" din Windows Settings
4. Verifică că Taskbar rămâne semitransparent (nu devine opac)

**Expected:** ✓ Overlay funcționează indiferent de setarea Windows Transparency Effects

### TC-M7-02: Windows Build Detection
**Steps:**
1. Start GlassBar
2. Check log: `%LOCALAPPDATA%\GlassBar\GlassBar.log`
3. Look for: `Windows build number: XXXXX`

**Expected:** ✓ Build number logat la startup cu clasificarea corectă (pre-24H2 / 24H2/25H2+)

### TC-M7-03: Win7 Aero Global Theme Preset
**Steps:**
1. Click butonul "Win7 Aero" din sidebar (NavigationView.PaneFooter)
2. Navighează la Taskbar tab → verifică: opacity=50, R=20, G=40, B=80
3. Navighează la Start Menu tab → verifică: bg R=20/G=40/B=80, text R=255/G=255/B=255, border R=60/G=100/B=160, opacity=17, blur=OFF
4. Verifică că ambele overlay-uri toggle sunt ON

**Expected:** ✓ Ambele panouri reflectă valorile corecte; efectele aplicate imediat

### TC-M7-04: Dark Global Theme Preset
**Steps:**
1. Click butonul "Dark" din sidebar
2. Verifică Taskbar: opacity=50, R=18, G=18, B=22
3. Verifică Start Menu: bg R=18/G=18/B=22, text R=200/G=200/B=200, border R=60/G=60/B=65, opacity=17, blur=OFF

**Expected:** ✓ Ambele panouri cu valorile Dark corecte; efect charcoal aplicat

### TC-M7-05: Theme Preset Slider Sync
**Steps:**
1. Click "Win7 Aero" din sidebar
2. Verifică că Taskbar color preview box se actualizează la albastru închis
3. Switch la Start Menu tab
4. Verifică că slidere Start Menu reflectă noile valori (nu valorile vechi)

**Expected:** ✓ Toate slider-ele și preview box-urile se actualizează imediat după click tema

---

## Stress Testing

### 8-Hour Idle Test
1. Start Core + Dashboard
2. Set opacity 50 pe ambele panouri
3. Lasă computerul idle (fără sleep)
4. After 8 hours: verifică CPU/memorie, testează funcțiile

**Expected:** ✓ Fără crash, fără memory leaks

### Rapid Operations (1 minut)
```
- Deschide/închide Start Menu la fiecare 2 secunde
- Ajustează slidere aleatoriu
- Toggle enable/disable
```

**Expected:** ✓ Fără crash, performanță stabilă

---

## Known Limitations (Expected Behavior)

### Search Box
- Caseta de căutare din Start Menu este **vizibilă dar nefuncțională** (placeholder).
- Aceasta este o limitare known — restul meniului nu este afectat.

### Renderer pe 24H2 / 25H2+ (build ≥ 26000)
- SWCA nu mai funcționează pe aceste build-uri; se folosește fallback LWA_ALPHA.
- RGB color tint și Blur/Acrylic nu au efect vizual.
- Transparența funcționează, dar iconițele Taskbar devin proporțional mai puțin vizibile
  odată cu creșterea transparenței. Aceasta este o limitare a platformei Windows.
- Pe 22H2/23H2, toate efectele funcționează complet.

### DPI Scaling
- Testat cu 100%, 125%, 150%.
- Pot apărea artefacte vizuale la 200%+ scaling.

---

## Bug Reporting

1. **Captează:**
   - Screenshot al problemei
   - `%LOCALAPPDATA%\GlassBar\GlassBar.log`
   - Pașii exacți de reproducere

2. **Raportează:**
   - Versiunea Windows (Settings → System → About → OS build)
   - Pași de reproducere
   - Expected vs actual behavior
   - Extras din log

---

## Performance Metrics

| Test | Expected | Pass/Fail |
|------|----------|-----------|
| CPU (idle) | < 2% | |
| Memory | < 50 MB | |
| Startup | < 2 sec | |
| Opacity change latency | < 50 ms | |
| Taskbar track latency | < 100 ms | |
| Start Menu open latency | < 250 ms | |

---

**After passing all tests, mark the project as validated!** ✅
