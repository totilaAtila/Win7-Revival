# Plan: Optimizare Start Menu — Calitate, Viteză, UX

## Analiza completă a fișierelor critice

Am analizat în profunzime: `StartMenuWindow.cpp` (3500+ linii), `StartMenuWindow.h`,
`AllProgramsEnumerator.cpp/.h`, `StartMenuHook.cpp/.h`, `Renderer.cpp/.h`, `ConfigManager.cpp/.h`.

---

## Modificări propuse (ordonate după prioritate)

### P1. BUG CRITIC — Operator Precedence în Hover Animation
**Fișier:** `StartMenuWindow.cpp:2461-2464`
**Problema:** Lipsesc parantezele → `&&` se evaluează înaintea `||`:
```cpp
// ACTUAL (buggy):
if (anyNewHover && nProg  != m_hoveredProgIndex ||
                   nApRow != m_hoveredApRow     || ...)
// Se evaluează ca: (anyNewHover && nProg!=...) || (nApRow!=...) || ...
```
Animația de hover se repornește chiar și când `anyNewHover` e false → timer-ul de 10ms rulează continuu → **CPU usage crescut inutil**.

**Fix:** Adăugare paranteze:
```cpp
if (anyNewHover && (nProg  != m_hoveredProgIndex ||
                    nApRow != m_hoveredApRow     ||
                    nAp    != m_hoveredApIndex   ||
                    nrc    != m_hoveredRightIndex))
```

---

### P2. PERFORMANȚĂ HIGH — 17× CreateFontW la fiecare Paint()
**Fișier:** `StartMenuWindow.cpp` (liniile 1244, 1369, 1372, 1477, 1528, 1572, 1578, 1590, 1687, 1693, 1731, 1757, 2168, 2179, 2182)
**Problema:** La fiecare ciclu de paint (10-100ms în timpul animațiilor hover), se creează și distrug ~15 fonturi GDI. CreateFontW alocă resurse GDI kernel (limitate la ~10000/proces). Cu timer-ul hover de 10ms, asta înseamnă ~1500 fonturi/secundă create+distruse.

**Fix:** Cache fonts ca member variables, create o singură dată în Initialize():
```cpp
// Header — noi membrii:
HFONT m_fontNormal14 = nullptr;   // 14pt normal
HFONT m_fontBold14   = nullptr;   // 14pt bold/semibold
HFONT m_fontNormal15 = nullptr;   // 15pt normal
HFONT m_fontBold15   = nullptr;   // 15pt semibold
HFONT m_fontNormal13 = nullptr;   // 13pt normal
HFONT m_fontBold12   = nullptr;   // 12pt bold
HFONT m_fontNormal12 = nullptr;   // 12pt normal
HFONT m_fontSmall10  = nullptr;   // 10pt normal (arrow)
HFONT m_fontBold16   = nullptr;   // 16pt bold (right column header)
```
Create în Initialize(), DestroyFont în Shutdown(). Toate Paint* folosesc fonturile cached.

---

### P3. PERFORMANȚĂ HIGH — Network Shortcut Freeze
**Fișier:** `AllProgramsEnumerator.cpp:75`
**Problema:** `psl->Resolve(nullptr, SLR_NO_UI | SLR_UPDATE)` — flag-ul `SLR_UPDATE` forțează acces la rețea pentru shortcut-uri pe network shares. Dacă un shortcut pointează spre un share offline, **întregul tree build se blochează** (fără timeout).

**Fix:** Înlocuire `SLR_UPDATE` cu `SLR_NOSEARCH | SLR_NOTRACK`:
```cpp
psl->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH | SLR_NOTRACK);
```
Asta previne căutarea pe rețea și tracking-ul — shortcut-ul se rezolvă instant din cache-ul local.

---

### P4. PERFORMANȚĂ MEDIUM — Hover animation timer prea agresiv (10ms = 100 FPS)
**Fișier:** `StartMenuWindow.cpp:2467`
**Problema:** Timer-ul hover animation rulează la 10ms (100 FPS). Monitoarele standard sunt 60Hz. Fiecare tick triggerează Paint() complet → overhead inutile.

**Fix:** Mărire interval la 16ms (~60 FPS) — se potrivește cu refresh rate-ul standard:
```cpp
m_hoverAnimTimer = SetTimer(m_hwnd, HOVER_ANIM_TIMER_ID, 16, NULL);
```

---

### P5. PERFORMANȚĂ MEDIUM — ConfigManager: mutex ținut pe durata I/O fișier
**Fișier:** `ConfigManager.cpp:39, 93`
**Problema:** `std::lock_guard` e achiziționat la începutul Load()/Save() și ținut pe toată durata citirii/scrierii fișierului. Orice apel `GetConfig()` de pe alt thread se blochează așteptând I/O disk.

**Fix:** Citire fișier fără lock → parsare → lock doar pentru update config:
```cpp
void ConfigManager::Load() {
    // Read file WITHOUT lock
    std::ifstream f(path);
    // ... parse into local ConfigData tempConfig ...

    // Lock only for the swap
    { std::lock_guard<std::mutex> lock(m_mutex);
      m_config = tempConfig; }
}
```

---

### P6. PERFORMANȚĂ MEDIUM — InvalidateRect cu NULL rect la fiecare mișcare de mouse
**Fișier:** `StartMenuWindow.cpp:2485`
**Problema:** `InvalidateRect(m_hwnd, NULL, FALSE)` invalidează **întreaga fereastră** la fiecare schimbare de hover state. Cu mouse în mișcare, asta cauzează repaint complet de ~30-100 ori/secundă.

**Fix:** Calculare dirty rect specific pentru zona hover-ului:
```cpp
RECT dirtyRect;
// Calculare rect doar pentru itemul anterior + itemul nou hover
InvalidateRect(m_hwnd, &dirtyRect, FALSE);
```
Asta reduce zona de repaint de la ~400×535px la ~400×36px (un singur rând).

---

### P7. CODE QUALITY MEDIUM — Hook callbacks fără sincronizare
**Fișier:** `StartMenuHook.cpp:69-73`
**Problema:** Callback-urile sunt `std::function` fără protecție atomică. Dacă un callback e setat/șters în timp ce hook-ul rulează pe alt thread, apare un race condition.

**Fix:** Protejare cu `std::atomic` sau `std::mutex` simplu pe callback assignment + citire.

---

### P8. UX MEDIUM — Hook thread apelează GetWindowRect la fiecare mouse event
**Fișier:** `StartMenuHook.cpp:123`
**Problema:** `IsClickOnStartButton()` apelează `GetWindowRect()` la **fiecare eveniment de mouse** din sistem (global hook). Asta e un apel cross-process sincron — adaugă latență la fiecare input event.

**Fix:** Cache-uire `GetWindowRect()` la Initialize() + refresh la `WM_SETTINGCHANGE`:
```cpp
RECT m_cachedStartBtnRect = {};  // refreshed on WM_SETTINGCHANGE
bool IsClickOnStartButton(POINT pt) {
    return PtInRect(&m_cachedStartBtnRect, pt);
}
```

---

### P9. CODE QUALITY LOW — Renderer: SetWindowLongW fără verificare stare
**Fișier:** `Renderer.cpp:205-215`
**Problema:** `SetWindowLongW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED)` se apelează la fiecare schimbare de transparență, chiar dacă flag-ul e deja setat.

**Fix:** Verificare înainte de apel:
```cpp
LONG cur = GetWindowLongW(hwnd, GWL_EXSTYLE);
if (!(cur & WS_EX_LAYERED))
    SetWindowLongW(hwnd, GWL_EXSTYLE, cur | WS_EX_LAYERED);
```

---

## Sumar impact estimat

| # | Tip | Impactul |
|---|-----|----------|
| P1 | Bug fix | Oprește timer-ul de animație care rulează inutil → reduce CPU |
| P2 | Perf | Elimină ~1500 alocări GDI/sec în timpul hover → reduce GDI pressure |
| P3 | Perf | Previne blocarea meniului pe network shares → elimină freeze-uri |
| P4 | Perf | Reduce frecvența repaint de la 100 la 60 FPS → reduce CPU ~40% |
| P5 | Perf | Deblochează GetConfig() pe alte thread-uri → reduce latență config |
| P6 | Perf | Reduce zona de repaint la un rând → reduce workload GPU/CPU |
| P7 | Safety | Elimină race condition pe callback-uri → previne crash-uri rare |
| P8 | Perf | Elimină apel cross-process la fiecare mouse event → reduce input lag |
| P9 | Quality | Reduce apeluri inutile SetWindowLongW → micro-optimizare |

## Fișiere afectate
- `Core/StartMenuWindow.cpp` — P1, P2, P4, P6
- `Core/StartMenuWindow.h` — P2 (noi membrii font cache)
- `Core/AllProgramsEnumerator.cpp` — P3
- `Core/StartMenuHook.cpp` + `.h` — P7, P8
- `Core/Renderer.cpp` — P9
- `Core/ConfigManager.cpp` — P5
