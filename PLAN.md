# Plan: Start Menu Fixes

## Cerințe

1. **Poziționare corectă** – Start Menu să apară ancorat la butonul Windows, indiferent de poziția taskbar-ului (jos, sus, stânga, dreapta).
2. **Meniuri funcționale** – Search Box și zona User/Avatar să răspundă la click.
3. **Autostart → tray** – Dacă aplicația pornește cu Windows, să se ascundă în System Tray (fără fereastră).

---

## Task 1 – Poziționare Start Menu ancorat la butonul Start ✅ DONE

### Problema rezolvată
`Show(int x, int y)` – meniu-ul se centra mereu orizontal.
Acum se ancorează la butonul Start indiferent de orientarea taskbar-ului.

**Fix suplimentar (review P2):** Taskbar-urile verticale (stânga/dreapta) erau clasificate greșit
ca „bottom-docked" deoarece `tbRect.bottom ≈ screenH`. Rezolvat prin compararea `tbW` vs `tbH`
**înainte** de verificarea poziției pe ecran.

### Fișier modificat
`Core/StartMenuWindow.cpp` – funcția `Show()`

### Logică implementată

**Pas 1 – Obține RECT taskbar**
```cpp
HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
RECT tbRect = {};
GetWindowRect(taskbar, &tbRect);
```

**Pas 2 – Detectează orientarea prin dimensiuni (tbW vs tbH)**
```cpp
int tbW = tbRect.right  - tbRect.left;
int tbH = tbRect.bottom - tbRect.top;

bool tbLeft  = tbW < tbH && tbRect.left <= screenW / 2;  // vertical, stânga
bool tbRight = tbW < tbH && tbRect.left >  screenW / 2;  // vertical, dreapta
bool tbTop   = tbW >= tbH && tbRect.top <= screenH / 2;  // orizontal, sus
// default = jos
```

**Pas 3 – Găsește RECT Start button**
```cpp
HWND sb = FindWindowExW(taskbar, nullptr, L"Start", nullptr);
if (!sb) sb = FindWindowExW(taskbar, nullptr, L"TrayButton", nullptr);
```

**Pas 4 – Calculează poziția în funcție de orientare**
| Orientare       | menuX                    | menuY                    |
|-----------------|--------------------------|--------------------------|
| Taskbar stânga  | `tbRect.right + 1`       | `sbTop`                  |
| Taskbar dreapta | `tbRect.left - WIDTH - 1`| `sbTop`                  |
| Taskbar sus     | `sbLeft`                 | `tbRect.bottom + 1`      |
| Taskbar jos     | `sbLeft`                 | `tbRect.top - HEIGHT - 1`|
| Fallback        | `0`                      | `screenH - HEIGHT - 48`  |

**Pas 5 – Clamp final** – asigură că meniu-ul nu iese din ecran pe niciun ax.

---

## Task 2 – Search Box funcțional (click → Windows Search) ✅ DONE

### Problema rezolvată
Click pe search box deschide Windows Search (`ms-search:`).

### Implementare
- Hit test: `[MARGIN, SEARCH_Y, cr.right-MARGIN, SEARCH_Y+SEARCH_H]`
- `WM_LBUTTONDOWN` → `ShellExecuteW(NULL, L"open", L"ms-search:", NULL, NULL, SW_SHOW)`
- Hover state cu redesenare la `WM_MOUSEMOVE`

---

## Task 3 – Zona User/Avatar funcțională (click → Setări cont) ✅ DONE

### Problema rezolvată
Click pe avatar/user din bottom bar deschide `ms-settings:accounts`.

### Implementare
- Hit test pe zona avatar + text (stânga bottom bar)
- `WM_LBUTTONDOWN` → `ShellExecuteW(NULL, L"open", L"ms-settings:accounts", NULL, NULL, SW_SHOW)`
- Hover highlight subtil, același pattern ca pinned items

---

## Task 4 – Autostart → pornește hidden în System Tray ✅ DONE

### Problema rezolvată
La pornirea Windows (autostart prin registry), aplicația deschidea fereastra principală
în loc să se ascundă silențios în System Tray.

### Fișiere modificate

**`Dashboard/StartupManager.cs`**
```csharp
key.SetValue(AppName, $"\"{path}\" /autostart");
```

**`Dashboard/App.xaml.cs`** – `OnLaunched()`
```csharp
bool startHidden = args.Arguments.Contains("/autostart", StringComparison.OrdinalIgnoreCase)
                || Environment.GetCommandLineArgs().Any(
                       a => a.Equals("/autostart", StringComparison.OrdinalIgnoreCase));
_window = new MainWindow(startHidden);
_window.Activate();
```

**`Dashboard/MainWindow.xaml.cs`** – constructor
```csharp
public MainWindow(bool startHidden = false)
{
    // ...
    if (startHidden)
        DispatcherQueue.TryEnqueue(() => _appWindow.Hide());
}
```

### Comportament rezultat
| Mod lansare                  | Comportament                             |
|------------------------------|------------------------------------------|
| Normal (dublu-click pe .exe) | Deschide fereastra principală            |
| Autostart (registry Run key) | Pornește silențios, icoană în Tray       |
| Al doilea launch când hidden | `EnumWindows` găsește fereastra ascunsă, o afișează |
| Dublu-click icoană tray      | Deschide fereastra principală            |
| Click dreapta → Exit         | Oprește Core + iese complet              |

**Fix suplimentar (review P2):** `BringExistingInstanceToForeground()` nu gestiona ferestre
ascunse (`IsWindowVisible=false`). `Process.MainWindowHandle` returnează `IntPtr.Zero` pentru
ferestre hidden. Rezolvat prin `EnumWindows` + `GetWindowThreadProcessId` ca fallback,
plus `SW_SHOW` (nu `SW_RESTORE`) pentru dezascundere.

---

## Fix-uri de calitate ✅ DONE

- **JSON trailing comma** (`SaveCustomNames()`) — virgulă în plus înainte de `}` când
  `m_customTitle` era gol. Rezolvat prin colectarea intrărilor într-un `vector` și join cu
  separator condiționat.

---

## Ordine de implementare

1. ✅ Task 1 (poziționare) — izolată în `Show()`; fix vertical taskbar în review
2. ✅ Task 2 (search box) — hover + click
3. ✅ Task 3 (user area) — hover + click
4. ✅ Task 4 (autostart tray) — `/autostart` flag + `_appWindow.Hide()`; fix hidden re-launch în review

## Următori pași posibili

- **Multi-monitor** — overlay pe display-uri non-primare
- **Global hotkey** — toggle overlay-uri fără Dashboard
- **Color presets** — teme predefinite (Aero Glass, Dark, etc.)
- **Auto-update check** — notificare la nouă versiune GitHub

## Ce NU facem (pentru a evita bug-uri)
- Nu modificăm hook-urile (`StartMenuHook.cpp`) – funcționează corect
- Nu modificăm sistemul de rendering/transparență
- Nu schimbăm dimensiunile ferestrei (`WIDTH`/`HEIGHT`)
- Nu adăugăm input text real în search box (risc ridicat, complex)
