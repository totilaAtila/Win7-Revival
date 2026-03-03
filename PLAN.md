# Plan: Blur fix + Text shadow/quality

## Problemă 1 — Switch-ul Blur nu funcționează

### Diagnostic complet

**Start Menu blur — bug de cod confirmat (2 defecte):**

**Defect A — `StartMenuWindow::ApplyTransparency()` ignoră blur:**
```cpp
// StartMenuWindow.cpp:471
accent.AccentState = ACCENT_ENABLE_TRANSPARENTGRADIENT; // mereu, indiferent
```
Nu există `m_blur` în clasă, nu există `SetBlur()`. Funcția setează
mereu `TRANSPARENTGRADIENT`, niciodată `ACRYLICBLURBEHIND`.

**Defect B — `Core::SetStartBlur()` nu ajunge la StartMenuWindow:**
```cpp
// Core.cpp:297-305
void Core::SetStartBlur(bool enabled) {
    m_startBlur = enabled;
    m_renderer->SetStartBlur(enabled);   // ← aplică pe HWND-ul Win native Start
    m_config->SetStartBlur(enabled);
    // LIPSĂ: m_startMenuWindow->SetBlur(enabled)
}
```
Renderer-ul aplică efectul pe HWND-ul native Windows Start Menu (care nu
mai e vizibil — e înlocuit de StartMenuWindow noastră).

**Taskbar blur — codul Renderer.cpp este corect:**
`Renderer::SetTaskbarBlur()` deja setează `ACCENT_ENABLE_ACRYLICBLURBEHIND`
pe HWND-ul taskbar-ului real. Dacă nu e vizibil, cauza probabilă:
- Opacity implicită 30 → alpha = 178/255 → tinta e ~70% opacă → blur-ul
  e acoperit de culoarea de fond. La opacity mai mare (ex. 70+), blur-ul
  e vizibil.
- Posibil Windows 11 taskbar specificity (de verificat la runtime).

### Fix

**Fișier: `Core/StartMenuWindow.h`**
- Adaugă `bool m_blur = false;` la starea privată
- Adaugă declarație publică `void SetBlur(bool useBlur);`

**Fișier: `Core/StartMenuWindow.cpp`**
- Implementează `SetBlur()`:
  ```cpp
  void StartMenuWindow::SetBlur(bool useBlur) {
      m_blur = useBlur;
      if (m_visible) ApplyTransparency();
  }
  ```
- Modifică `ApplyTransparency()` — linia 471:
  ```cpp
  accent.AccentState = m_blur ? ACCENT_ENABLE_ACRYLICBLURBEHIND
                               : ACCENT_ENABLE_TRANSPARENTGRADIENT;
  ```

**Fișier: `Core/Core.cpp`**
- În `Core::SetStartBlur()`, adaugă apelul lipsă la StartMenuWindow:
  ```cpp
  if (m_startMenuWindow) m_startMenuWindow->SetBlur(enabled);
  ```
- La inițializare, aplică starea inițială de blur pe StartMenuWindow.

---

## Problemă 2 — Text pixelat în Start Menu

### Diagnostic

**Cauza — ClearType pe fundal transparent:**
`CLEARTYPE_QUALITY` folosește subpixel rendering LCD: îmbină componentele
R/G/B cu fundalul asumat. Când fundalul real (blur/transparență DWM) diferă,
apar halouri de culoare și aspectul „pixelat"/zimțat.

### Soluție — ANTIALIASED_QUALITY + Shadow text

**Pasul 1 — Schimbă calitatea fontului:**
`CLEARTYPE_QUALITY` → `ANTIALIASED_QUALITY` în TOATE apelurile `CreateFontW()`
din `StartMenuWindow.cpp` (liniile ~543, 1062, 1115, 1125, 1260, 1283, 1674
și fontul default al Window-ului dacă există).

`ANTIALIASED_QUALITY` = antialiasing griuri, independent de fundal →
text neted pe orice fundal (transparent sau blur).

**Pasul 2 — Shadow text (efect stil Windows 11 desktop):**
Adaugă funcție statică `DrawShadowText()`:

```cpp
static COLORREF ShadowColor(COLORREF fg) {
    int lum = (GetRValue(fg)*299 + GetGValue(fg)*587 + GetBValue(fg)*114) / 1000;
    return lum > 128 ? RGB(0, 0, 0) : RGB(220, 220, 220);
}

static void DrawShadowText(HDC hdc, const wchar_t* text, int len,
                           RECT* rect, UINT fmt, COLORREF fg) {
    RECT sr = { rect->left + 1, rect->top + 1,
                rect->right + 1, rect->bottom + 1 };
    ::SetTextColor(hdc, ShadowColor(fg));
    DrawTextW(hdc, text, len, &sr, fmt | DT_NOCLIP);
    ::SetTextColor(hdc, fg);
    DrawTextW(hdc, text, len, rect, fmt);
}
```

Logica umbrei: text deschis → umbră neagră; text închis → umbră gri deschis.
Deplasare 1px jos-dreapta — identic cu etichete icoane Windows 11.

**Pasul 3 — Înlocuiește DrawTextW → DrawShadowText** pentru texte vizibile:
- Programe (pinned + recent): liniile ~820, ~873
- All Programs (foldere/apps): ~957
- Right column items: ~1172
- Username: ~1131
- Submenu title + items: ~1682, ~1724
- Scroll hints (▲▼): ~966, ~973, ~1741

Excepții (nu se modifică): butoane Shut down și arrow (text închis pe
fundal solid - umbra nu ajută).

---

## Fișiere de modificat

| Fișier | Modificări |
|--------|-----------|
| `Core/StartMenuWindow.h` | + `bool m_blur = false`, + `void SetBlur(bool)` |
| `Core/StartMenuWindow.cpp` | `SetBlur()`, `ApplyTransparency()`, toate `CreateFontW` CLEARTYPE→ANTIALIASED, + `DrawShadowText` helper, DrawTextW→DrawShadowText |
| `Core/Core.cpp` | `SetStartBlur()` + `m_startMenuWindow->SetBlur()` + init |

**Nu se modifică:** Dashboard (C#), Renderer.cpp, CoreApi, IpcBridge.
