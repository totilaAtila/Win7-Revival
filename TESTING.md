# Testing Guide - GlassBar Engine

Quick testing scenarios to validate all functionality.

---

## Pre-Test Checklist

- [ ] Windows 11 (22H2 or later)
- [ ] Core compiled (Release build)
- [ ] Dashboard compiled (Release build)
- [ ] Both executables running

---

## Milestone M1: Basic Taskbar Overlay

### TC-M1-01: Taskbar Detection
**Steps:**
1. Start `GlassBar.Dashboard.exe`
2. Check log: `%LOCALAPPDATA%\GlassBar\GlassBar.log`
3. Look for: `[INFO] Taskbar found: edge=bottom`

**Expected:** ✓ Taskbar detected, overlay positioned correctly

### TC-M1-02: Opacity Control
**Steps:**
1. Start Dashboard
2. Set Taskbar slider to 0 → Taskbar appears normal
3. Set Taskbar slider to 50 → Overlay visible at 50%
4. Set Taskbar slider to 100 → Maximum overlay opacity

**Expected:** ✓ Opacity changes in real-time, no flicker

### TC-M1-03: Click-Through
**Steps:**
1. Set Taskbar opacity to 50
2. Click Start button through overlay
3. Click system tray icons
4. Right-click taskbar

**Expected:** ✓ All clicks work as if overlay doesn't exist

---

## Milestone M2: Taskbar Hardening

### TC-M2-01: Edge Positions
**For each edge (Bottom, Top, Left, Right):**
1. Move taskbar to edge (Taskbar Settings → Taskbar behaviors → Taskbar alignment)
2. Verify overlay follows
3. Test click-through

**Expected:** ✓ Overlay tracks taskbar on all edges

### TC-M2-02: Auto-Hide
**Steps:**
1. Enable auto-hide (Taskbar Settings)
2. Move mouse away → Taskbar hides
3. Verify overlay hides too
4. Move mouse to edge → Taskbar shows
5. Verify overlay shows too
6. Repeat 10 times rapidly

**Expected:** ✓ No flicker, perfect tracking

### TC-M2-03: Explorer Restart
**Steps:**
1. Core + Dashboard running, overlay visible
2. Open Task Manager
3. Find "Windows Explorer" process
4. Right-click → End task
5. Windows will restart Explorer automatically
6. Wait 2 seconds
7. Check if overlay reappears

**Expected:** ✓ Automatic recovery, overlay restored

---

## Milestone M3: Start Menu Overlay

### TC-M3-01: Start Detection
**Steps:**
1. Enable Start overlay in Dashboard
2. Press Windows key
3. Dashboard should show "Start: Open"
4. Overlay appears over Start Menu
5. Press Esc
6. Dashboard shows "Start: Closed"
7. Overlay disappears

**Expected:** ✓ Detection < 250ms, smooth tracking

### TC-M3-02: Start Opacity
**Steps:**
1. Open Start Menu
2. Set Start slider to 0 → No overlay visible
3. Set Start slider to 50 → 50% overlay
4. Set Start slider to 100 → Maximum opacity

**Expected:** ✓ Opacity changes work

### TC-M3-03: Spam Test
**Steps:**
1. Set Start opacity to 75
2. Rapidly press Windows key 20 times
3. Check for crashes, lag, or flicker

**Expected:** ✓ No crashes, smooth handling

---

## Milestone M4: Final Validation

### TC-M4-00: Autostart → Start hidden in System Tray
**Steps:**
1. Open Dashboard
2. Enable "Run at startup" toggle
3. Verify registry key: `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Run\GlassBar`
   - Value must be: `"<path>\GlassBar.Dashboard.exe" /autostart`
4. Sign out and sign back in (or run the command manually from CLI)
5. Check that **no Dashboard window appears** on screen
6. Check that a **GlassBar icon is visible in the System Tray**
7. Double-click the tray icon → Dashboard window opens
8. Right-click the tray icon → context menu shows "Open GlassBar" and "Exit"

**Expected:** ✓ Window stays hidden at startup; tray icon present; double-click/right-click work

### TC-M4-01: Config Persistence
**Steps:**
1. Set Taskbar opacity to 63, Start opacity to 42
2. Close Dashboard
3. Verify `%LOCALAPPDATA%\GlassBar\config.json` contains values
4. Reopen Dashboard
5. Verify sliders show 63 and 42

**Expected:** ✓ Settings persisted and restored

### TC-M4-02: Performance
**Steps:**
1. Open Task Manager
2. Find `GlassBar.Dashboard.exe`
3. Let run for 5 minutes idle
4. Check CPU usage

**Expected:** ✓ CPU < 2% average

### TC-M4-03: Memory Stability
**Steps:**
1. Note initial memory (Task Manager)
2. Toggle Start 50 times
3. Move taskbar between edges 10 times
4. Adjust sliders 100 times
5. Note final memory

**Expected:** ✓ Memory increase < 10 MB

---

## Stress Testing

### 8-Hour Idle Test
**Steps:**
1. Start Core + Dashboard
2. Set opacity to 50/50
3. Leave computer idle (don't sleep)
4. Return after 8 hours
5. Check Task Manager for CPU/memory
6. Test all functions still work

**Expected:** ✓ No crashes, no memory leaks

### Rapid Operations
**Script (manual):**
```
For 1 minute:
  - Open/close Start every 2 seconds
  - Adjust sliders randomly
  - Toggle enable/disable
```

**Expected:** ✓ No crashes, stable performance

---

## Known Limitations (Expected Behavior)

### Start Menu Detection
- May not work on all Windows 11 builds
- If confidence < 0.6, Start overlay disables automatically
- **This is normal** - Taskbar continues to work

### Multi-Monitor
- Currently only supports primary monitor
- Taskbar on secondary monitors not detected

### DPI Scaling
- Tested with 100%, 125%, 150% scaling
- May have issues with 200%+ scaling

---

---

## Milestone M7: Windows Compatibility + Global Theme Presets (Session 22)

### TC-M7-01: Windows Transparency Effects conflict (22H2+)
**Pre-condition:** Windows 11 22H2+, "Transparency effects" ON in Settings → Personalization → Colors

**Steps:**
1. Start GlassBar, enable Taskbar transparency (opacity 50, blur OFF)
2. Verify taskbar is transparent (semi-transparent overlay visible)
3. Go to Windows Settings → Personalization → Colors → toggle "Transparency effects" ON
4. Verify taskbar remains transparent (NOT opaque)

**Expected:** ✓ Taskbar stays transparent regardless of Windows Transparency Effects setting
**Log verification:** Look for `DwmSetWindowAttribute DWMWA_SYSTEMBACKDROP_TYPE=NONE attempted`

### TC-M7-02: Windows build detection
**Steps:**
1. Start GlassBar
2. Check log: `%LOCALAPPDATA%\GlassBar\GlassBar.log`
3. Look for: `Windows build number: XXXXX`

**Expected:** ✓ Build number logged at startup with correct classification (pre-24H2 / 24H2 / 25H2+)

### TC-M7-03: 25H2+ universal fallback (no-blur)
**Pre-condition:** Windows build ≥ 27000 (25H2+)

**Steps:**
1. Enable Taskbar transparency, blur OFF, opacity 50
2. Verify taskbar shows basic alpha transparency (semi-transparent)

**Expected:** ✓ Basic transparency via `SetLayeredWindowAttributes` — taskbar is NOT opaque
**Log verification:** Look for `Win25H2+ layered fallback: alpha=`

### TC-M7-04: Win7 Aero global theme preset
**Steps:**
1. Navigate to Taskbar panel in Dashboard
2. Click "Win7 Aero" button (THEME PRESETS card)
3. Verify Taskbar sliders: opacity=50, R=20, G=40, B=80
4. Navigate to Start Menu panel
5. Verify Start Menu sliders: bg R=20/G=40/B=80, text R=255/G=255/B=255, border R=60/G=100/B=160, opacity=50, blur=OFF

**Expected:** ✓ Both panels show correct values; effects applied immediately on taskbar and Start Menu

### TC-M7-05: Dark global theme preset
**Steps:**
1. Click "Dark" button in Taskbar panel → THEME PRESETS card
2. Verify Taskbar: opacity=50, R=18, G=18, B=22
3. Navigate to Start Menu panel
4. Verify Start Menu: bg R=18/G=18/B=22, text R=200/G=200/B=200, border R=60/G=60/B=65, opacity=50, blur=OFF

**Expected:** ✓ Both panels show correct values; dark charcoal effect applied

### TC-M7-06: Theme preset slider sync
**Steps:**
1. Click "Win7 Aero" while on Taskbar panel
2. Verify Taskbar color preview box updates to dark blue
3. Switch to Start Menu panel
4. Verify all Start Menu sliders reflect the new values (not stale)

**Expected:** ✓ All sliders and preview boxes update immediately after theme button click

## Bug Reporting

If you find an issue:

1. **Capture:**
   - Screenshot of issue
   - Copy `%LOCALAPPDATA%\GlassBar\GlassBar.log`
   - Note exact steps to reproduce

2. **Report:**
   - Windows 11 version (Settings → System → About)
   - Steps to reproduce
   - Expected vs actual behavior
   - Log excerpt

---

## Performance Metrics

| Test | Expected | Pass/Fail |
|------|----------|-----------|
| CPU (idle) | < 2% | |
| Memory | < 50 MB | |
| Startup | < 2 sec | |
| Opacity change | < 50 ms | |
| Taskbar track | < 100 ms | |
| Start detection | < 250 ms | |

---

## Automated Testing (Future)

Currently manual testing only. Future versions may include:
- Unit tests (C++ Core)
- Integration tests (IPC)
- UI automation tests (Dashboard)
- Performance regression tests

---

**After passing all tests, mark the project as validated!** ✅
