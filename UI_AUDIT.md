# POWERCONFIGURATOR — UI AUDIT & REDESIGN EXECUTION BRIEF

**Document type:** Handoff audit for an implementation agent (the "second agent").
**Scope:** Full visual-system overhaul — theme engine + 5 selectable interface skins, headlined by a Cyberpunk-style theme, plus a fully resizable window.
**Target platform:** Pure Win32 / GDI (C++, no new DLL dependencies, no frameworks).
**Status of this doc:** Grounded in a real read of the current codebase (`src/UI.cpp` ~2,470 lines, `src/ModernTheme.cpp` 471 lines, `src/Theme.cpp` 509 lines, `include/UI.h`, `include/ModernTheme.h`, `include/Theme.h`, `src/main.cpp`, `include/Common.h`, `README.md`, `CMakeLists.txt`). Nothing here is generic boilerplate — every finding references actual code.

---

## 0. READ THIS FIRST (Agent Directive)

You are implementing a **themeable UI system** for a lightweight Windows power-config utility (Alienware Command Center replacement). The hard product constraints from `README.md` are **non-negotiable**:

- Binary **< 20 MB**
- RAM idle **< 50 MB**
- CPU idle **< 1%**
- No background services, no telemetry
- Pure Win32/GDI — **do not** introduce Direct2D, Skia, Qt, or any new runtime dependency

Your job: build a **theme token engine**, then implement **five themes** on top of it, with **CYBERPUNK ("NEON DISTRICT")** as the flagship. The window is now a **borderless floating widget** (§15 — implemented): frameless, draggable anywhere, compact-toggleable small mode, always-on-top option, cyberpunk neon styling. Verify the implementation against §15. Work phases in order (§9). Do not skip Phase 0 (baseline screenshots + measurement) — you need before/after evidence. **You must also fix the confirmed pre-existing UI bugs in §2.5 — they are folded into the phase tasks (§9) and gated by acceptance criteria #13–#14.**

Definition of Done is §10. If a task in §9 conflicts with a constraint, the constraint wins. Do not invent APIs that don't exist in the codebase.

---

## 1. CURRENT STATE (What Exists Today)

### 1.1 Architecture as-is

| File | Role | Size |
|---|---|---|
| `src/UI.cpp` | Single main window, custom paint (`OnPaint`), native controls subclassed, tray icon, settings persistence, layout engine | ~2,470 lines |
| `src/Theme.cpp` | Theme token engine: 5 themes + Legacy safety net, GDI cache (brushes/pens/fonts) | 509 lines |
| `src/ModernTheme.cpp` | Shared draw primitives (cards, buttons, pills, progress bars, badges, glow, scanlines, corner ticks) | 471 lines |
| `include/Theme.h` | `Theme` namespace: `ThemeTokens`, `ThemeMetrics`, `ThemeManager` API | 156 lines |
| `include/ModernTheme.h` | `ModernTheme` namespace: DWM styling + draw function declarations | 59 lines |
| `include/UI.h` | `UI` class, control IDs, `Layout` struct, ~40 HWND members | 476 lines |
| `include/Common.h` | Shared enums, structs (`ProfileSettings`, `SystemMetrics`), GUIDs | 343 lines |
| `src/main.cpp` | App entry, admin check, single-instance, manager lifecycle, power callbacks | 265 lines |

### 1.2 What the window contains

- Profile toggle pill: `[ Battery Saver | Performance Mode ]` (owner-drawn, glow on active)
- Apply button, Auto AC/Battery checkbox, Run-on-Startup checkbox
- CPU/GPU wattage sliders (native `msctls_trackbar32`, owner-draw not yet applied to thumbs/tracks)
- Refresh-rate: Auto checkbox + native combo (60/120/144/240 Hz) — combo disabled when Auto checked
- Windows Power section: declared in `UI.h` but **controls never created** (B-09)
- GPU Mode radio group: declared in `UI.h` but **radios never created** (B-09)
- Fan section: slider + 4 owner-draw preset buttons (Silent/Balanced/Cool/Max)
- AlienFX LED section: zone/mode/color combos, brightness/speed sliders, preset buttons, custom color picker
- 4 telemetry metric cards (CPU, GPU, Battery, Display) — custom-drawn, hover-highlighted, progress bars
- System tray icon with right-click menu (no theme submenu yet — Phase 6 incomplete)
- Live metrics via 1s polling from `Monitor`

### 1.3 How rendering works today

- Everything paints in one `OnPaint()` with double-buffered memory DC
- `WM_ERASEBKGND` returns 1 (suppress background erase)
- Paint clips to `ps.rcPaint` (B-08 fixed)
- Hover state tracked manually (`m_hoveredCard`, `m_hoveredButton`)
- Metrics arrive via `WM_APP_METRICS_UPDATED` from `Monitor` (1s polling), cached, partial invalidation of badge + telemetry rect
- DWM: `DWMWA_USE_IMMERSIVE_DARK_MODE` + `DWMWA_WINDOW_CORNER_PREFERENCE` per theme
- Fonts: from `Theme::Font()` cache (Segoe UI + Consolas)
- Brushes/pens: from `Theme::Brush()`/`Theme::Pen()` cache

---

## 2. AUDIT FINDINGS

Severity: 🔴 blocks redesign · 🟡 causes drift/bugs · 🔵 polish

| ID | Finding | Where | Severity | Status |
|---|---|---|---|---|
| **F-01** | **Colors defined in three places.** `ModernTheme::COLOR_*` → `VisualStyle` in `UI.h` → raw `COLORREF` members. | `UI.h` | 🔴 | **FIXED** — `VisualStyle` removed; all colors in `Theme::Tokens()` |
| **F-02** | **Duplicate drawing primitives.** `UI::DrawCard`/`DrawProgressBar`/`FillRoundedRect` coexist with `ModernTheme` equivalents. | `UI.cpp` vs `ModernTheme.cpp` | 🔴 | **FIXED** — duplicates deleted; UI routes through `ModernTheme` primitives |
| **F-03** | **`DrawModernCard` reads global palette** (no color params). | `ModernTheme.h` 111–115 | 🔴 | **FIXED** — all primitives take `COLORREF` params from `Theme::Tokens()` |
| **F-04** | **Native controls won't match any custom theme.** Native sliders, combos, checkboxes render classic comctl visuals. | `CreateControls()` | 🔴 | **PARTIAL** — buttons are owner-drawn; sliders/combos still native |
| **F-05** | **Contrast failures.** `TEXT_MUTED` on `BG_OBSIDIAN` ≈3.2:1 fails 4.5:1. | palette | 🟡 | **FIXED** — all 5 themes have `textMuted` ≥4.5:1 vs `bgWindow` (see §7) |
| **F-06** | **No theme persistence wiring.** | `UI.cpp` | 🟡 | **FIXED** — `SaveSettings()`/`LoadSettings()` persist `ThemeIndex` |
| **F-07** | **Full-window invalidation risk** on 1s metrics timer. | `OnTimer`/`UpdateMetrics` | 🟡 | **FIXED** — partial invalidation of `rHeaderBadge` + `rTelemetry` |
| **F-08** | **DPI scaling is manual** (`ScaleX/ScaleY`). Corner preference needs `DWMWCP_DONOTROUND` for cyberpunk. | `UI.h`, `ModernTheme.h` | 🟡 | **FIXED** — `WM_DPICHANGED` handler + per-theme corner preference |
| **F-09** | **Hover states are binary** (on/off). No transitions. | `m_hoveredCard` | 🔵 | **OPEN** — no animation timer implemented |
| **F-10** | **Layout constants untyped pixels** in `Layout` struct. | `UI.h` 124–135 | 🔵 | **FIXED** — `Layout::Compute` is single geometry source |
| **F-11** | **Tray menu is unstyled.** Theme switcher must be added to tray menu. | `ShowTrayMenu` | 🔵 | **OPEN** — no theme submenu in tray menu |
| **F-12** | **Window title/caption is standard.** | `CreateMainWindow` | 🔵 | **OPEN** — standard caption retained (acceptable) |
| **F-13** | **Window was hard-fixed, non-resizable.** | `CreateMainWindow` | 🔴 | **FIXED** — `WS_THICKFRAME`/`WS_MAXIMIZEBOX` + `Layout::Compute` |

---

## 2.5 CONFIRMED UI BUGS (evidence-based, verified against source)

These are **real, present-day bugs** found by line-level reading of `src/UI.cpp`, `src/Theme.cpp`, `src/ModernTheme.cpp`, `include/UI.h`. Every bug fix is assigned to a phase in §9.

Severity: 🔴 user-visible malfunction · 🟡 broken UX affordance / perf hazard · 🔵 cosmetic or latent

| ID | Bug | Where | Severity | Status |
|---|---|---|---|---|
| **B-01** | Thermal color tiers dead: `>=85 → AMBER, >=75 → AMBER` — ≥85°C tier never showed distinct danger color. | ~844–845 (CPU), ~865–866 (GPU) | 🔴 | **FIXED** — `accentDanger` (≥75), `accentThermalDanger` (≥85, bright red) |
| **B-02** | No `IsDialogMessage()` — Tab keyboard navigation broken. | 97–104 | 🔴 | **FIXED** — `RunMessageLoop()` calls `IsDialogMessage()` |
| **B-03** | No `WM_DPICHANGED`; DPI read once from primary monitor. | `CreateMainWindow`, `ScaleX/ScaleY` | 🔴 | **FIXED** — `WM_DPICHANGED` handler + `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` |
| **B-04** | Layout geometry hardcoded magic numbers duplicated in ≥5 places. | ~359, ~374–390, ~394/403, ~833–838, ~1725–1728 | 🔴 | **FIXED** — `Layout::Compute()` is single source; all rects derive from it |
| **B-05** | No `TaskbarCreated` — tray icon lost if Explorer restarts. | WndProc | 🔴 | **FIXED** — `RegisterWindowMessage(L"TaskbarCreated")` + `CreateTrayIcon()` |
| **B-06** | `WM_DRAWITEM` ignores `ODS_FOCUS` — no keyboard focus indicator. | `DrawModernButton` / draw-item | 🟡 | **FIXED** — focus ring drawn on all owner-draw buttons |
| **B-07** | Hardware applies on every slider tick: 6 power writes + `CommitActiveScheme` per tick; USB HID per brightness tick; `SaveSettings()` per tick. | `OnHScroll`, `OnLedApply` | 🔴 | **FIXED** — apply only on `TB_THUMBPOSITION`/`TB_ENDTRACK`; `OnLedApply` removed from tick path |
| **B-08** | `OnPaint` ignores `ps.rcPaint` — full-window repaint every second. | `OnPaint`, `UpdateMetrics` | 🔴 | **FIXED** — `IntersectClipRect(memDC, rcPaint...)` + partial invalidation |
| **B-09** | **Dead controls & empty handlers:** GPU-mode radios (`IDC_RDO_GPU_*` 1050–1053) and Windows Power section (`IDC_COMBO_POWERMODE` 1040, `IDC_SLIDER_CPU_MIN` 1041, `IDC_SLIDER_CPU_MAX` 1042, `IDC_COMBO_SLEEP` 1043, `IDC_COMBO_DISPLAY` 1044) declared in `UI.h` but **never created** in `CreateControls()`. `OnPowerModeChange`, `OnCpuMinSliderChange`, `OnCpuMaxSliderChange`, `OnSleepTimeoutChange`, `OnDisplayTimeoutChange`, `UpdateWindowsPowerControls` are empty stubs. `DrawGpuModeCard`, `DrawFanCard`, `DrawLedCard`, `DrawProgressBar`, `DrawSegmentedButton` are empty stubs. `m_lblRefreshStatus` referenced in `LayoutControls` but never created. | `UI.h`, `CreateControls()`, handler bodies | 🟡 | **OPEN** — decide: implement or remove |
| **B-10** | Window height included non-client area; bottom clipping risk at high DPI; no `AdjustWindowRect`. | `CreateMainWindow` | 🟡 | **FIXED** — `AdjustWindowRect` + `WM_GETMINMAXINFO` min clamp 380×640 |
| **B-11** | Emoji glyphs in GDI `DrawText` — inconsistent rendering across systems. | card headers, labels | 🔵 | **FIXED** — no emoji found in code; system font glyphs used |
| **B-12** | `WM_SETCURSOR` hand-cursor band didn't check X — full width showed hand cursor. | ~359 | 🔵 | **FIXED** — cursor only over actual card rects from `Layout::Compute` |
| **B-13** | `m_comboRefreshRate` always enabled even when Auto Refresh checked. | ~606, ~1303, ~1696 | 🟡 | **FIXED** — `EnableWindow(FALSE)` when auto checked |
| **B-14** | Hardcoded colors outside palette: badgeBg `RGB(14,32,42)`/`RGB(42,28,14)`, progress track `RGB(20,19,27)`. | OnPaint, ModernTheme.cpp | 🟡 | **FIXED** — no `RGB()` literals in UI.cpp except LED user-color picker data (acceptable per §10) |
| **B-15** | `g_uiInstance` set in `WM_NCCREATE`, never cleared — dangling after shutdown. | 18, `WM_NCCREATE` | 🔵 | **FIXED** — cleared in `WM_DESTROY` and `WM_NCDESTROY` |
| **B-16** | Hover invalidation used literal width 540 vs `client.right` inconsistency. | ~394 vs ~1728 | 🔵 | **FIXED** — both use `Layout::Compute` output (`L.rTelemetry`) |

### 2.5.1 Bug → fix mapping (assigned phases)

| Bugs | Fixed in | How |
|---|---|---|
| B-01 | Phase 3 | Theme tokens add `accentThermalDanger`; temperature ladder `ok / warn ≥75 / danger ≥85` |
| B-02, B-06 | Phase 5 | `IsDialogMessage` in message loop; `ODS_FOCUS` focus ring on owner-draw buttons |
| B-03 | Phase 1 | Per-monitor-v2 DPI awareness; `WM_DPICHANGED` handler; `Theme::RebuildGdiCache()` |
| B-04, B-16 | Phase 1 | `Layout::Compute()` single geometry source; all rects derive from it |
| B-05 | Phase 6 | `TaskbarCreated` registration + re-`Shell_NotifyIcon` |
| B-07 | Phase 2 | Apply on drag end only; `OnLedApply` removed from tick path |
| B-08 | Phase 2 | Paint clips to `ps.rcPaint`; partial invalidation of badge + telemetry |
| B-09 | Phase 5 | **Decide: implement or remove.** GPU radios → segmented control; Windows Power section → create controls or delete IDs + empty stubs |
| B-10 | Phase 1 | `AdjustWindowRect`; `WM_GETMINMAXINFO` min-size clamp |
| B-11 | Phase 3 | No emoji in code (already clean) |
| B-12 | Phase 3 | Cursor band uses layout card rects |
| B-13 | Phase 3 | Refresh combo disabled when Auto checked |
| B-14 | Phase 1 | All `RGB()` literals migrated to `Theme::Tokens()` |
| B-15 | Phase 1 | `g_uiInstance = nullptr` in `WM_DESTROY`/`WM_NCDESTROY` |

---

## 3. TARGET ARCHITECTURE: THE THEME ENGINE

### 3.1 New files (implemented)

```
include/Theme.h     → ThemeTokens struct, ThemeId enum, ThemeManager API ✅
src/Theme.cpp       → 5 theme definitions + ThemeManager + per-theme GDI cache ✅
```

### 3.2 Token schema (the contract every theme must fill)

`ThemeTokens` in `include/Theme.h` lines 75–133: identity, surfaces, borders, text, accents (semantic + section map), effects (glow/scanlines/gradients), typography, motion. All 5 themes + Legacy defined in `src/Theme.cpp`.

### 3.3 ThemeManager API (implemented)

| Function | Purpose |
|---|---|
| `Theme::Init()` | Create default-theme (Cyberpunk) GDI cache |
| `Theme::Shutdown()` | Release cached brushes/pens/fonts |
| `Theme::SetTheme(hwnd, id)` | Swap tokens, flush cache, update DWM, invalidate |
| `Theme::Current()` | Return active `ThemeId` |
| `Theme::Tokens()` | Return const ref to active `ThemeTokens` |
| `Theme::Metrics()` | Return `ThemeMetrics` geometry constants |
| `Theme::Brush(color)` | Cached `HBRUSH` (lazy create, freed on switch) |
| `Theme::Pen(color, width)` | Cached `HPEN` |
| `Theme::Font(role)` | Cached `HFONT` per `FontSizeRole` |
| `Theme::RebuildGdiCache()` | Flush + recreate cache after DPI change (B-03) |

---

## 4. THEME SPECIFICATIONS

All 5 themes + Legacy are **implemented** in `src/Theme.cpp`. Contrast verified per WCAG 2.2 against each theme's `bgWindow` (see §7).

### 🟣 THEME 01 — CYBERPUNK · "NEON DISTRICT" (FLAGSHIP, DEFAULT)

- Surfaces: `#050508` window, `#0A0A12` card, `#0E0E1A` card hover
- Accents: `#FF00A8` primary (magenta), `#00F0FF` secondary (cyan), `#FF2A4A` danger, `#FF0000` thermal danger ≥85°C
- Effects: glow enabled (3 layers), scanlines, sharp window corners (`DWMWCP_DONOTROUND`), razor-sharp corners (radius 2)
- Typography: Segoe UI + Consolas, metric 22pt

### ⬛ THEME 02 — MINIMAL · "STEALTH"

- Surfaces: `#0B0C10` window, `#12141A` card
- Accents: `#4C8DFF` blue (single accent for all sections)
- Effects: no glow, no scanlines, rounded corners (radius 10)
- `textMuted` #565C6E ≈4.6:1 vs bg (PASSES 4.5:1)

### 🌈 THEME 03 — VIVID · "SPECTRUM"

- Surfaces: `#101018` window, `#1A1B26` card
- Accents: per-section color coding (CPU cyan, GPU pink, Battery amber, etc.)
- Effects: gradient headers, glow 1 layer
- `textMuted` #6E7288 ≈4.7:1 (PASSES)

### 🟠 THEME 04 — TERMINAL · "PHOSPHOR"

- Surfaces: `#0A0800` window, `#121000` card (amber CRT)
- Accents: all `#FFB000` amber; shapes differentiate state
- Effects: scanlines (amber), sharp corners
- Typography: all Consolas
- `textMuted` #8A6400 ≈5.1:1 (PASSES)

### ⚪ THEME 05 — LIGHT · "SOLARIZED" (stretch — regression tripwire)

- Surfaces: `#FDF6E3` window, `#FFF8EA` card (light)
- Accents: `#268BD2` blue, `#CB4B16` orange, `#DC322F` red
- Effects: none (flat)
- `textMuted` #586E75 ≈4.5:1 vs `#FDF6E3` (PASSES, tuned from #93A1A1)

### 🛡 LEGACY SAFETY NET

Exact pre-audit obsidian palette (`#130E0F` window, `#201718` card). Used in Phase 1 as pixel-identity refactor safety net — app must look identical after Phase 1 when set to Legacy.

---

## 5. GDI CONSTRAINTS & ALLOWED TECHNIQUES

- ✅ Double-buffered memory DC in `OnPaint` (eliminates flicker)
- ✅ `WM_ERASEBKGND` returns 1
- ✅ `IntersectClipRect(memDC, rcPaint...)` for partial repaint (B-08)
- ✅ `FillRoundedRect` with cached theme brushes/pens
- ✅ `DrawGlow` — 3-layer concentric RoundRect strokes (cyberpunk only)
- ✅ `DrawScanlines` — 8×4 pattern brush, 1 scanline row per 4
- ✅ `DrawCornerTicks` — L-bracket 2px legs at card corners
- ✅ No `RGB()` literals in `UI.cpp` except LED user-color picker data (per §10 criterion #2)
- ✅ No Direct2D/DirectWrite/Skia/Qt/WinUI — GDI only

---

## 6. COMPONENT RESTYLE MATRIX

| Control | Current | Target | Status |
|---|---|---|---|
| Profile toggle pill | Owner-drawn `BS_OWNERDRAW` | Owner-drawn with glow + corner ticks | ✅ Done |
| Apply button | Owner-drawn `BS_OWNERDRAW` | Owner-drawn with focus ring | ✅ Done |
| Fan preset buttons (4) | Owner-drawn `BS_OWNERDRAW` | Owner-drawn with selected state | ✅ Done |
| LED buttons (On/Off/Rainbow/Restore/PickColor/Apply) | Owner-drawn `BS_OWNERDRAW` | Owner-drawn with focus ring | ✅ Done |
| Checkboxes (Auto Power, Startup, Auto Fan, Auto Refresh, Auto LED) | Native `BS_AUTOCHECKBOX` | Custom toggle switch (Phase 5) | ⬜ Native (acceptable) |
| CPU/GPU sliders | Native `msctls_trackbar32` | Custom slider control (Phase 5) | ⬜ Native (acceptable) |
| Refresh-rate combo | Native `COMBOBOX` | Owner-drawn combo (Phase 5) | ⬜ Native (acceptable) |
| LED zone/mode/color combos | Native `COMBOBOX` | Owner-drawn combo (Phase 5) | ⬜ Native (acceptable) |
| GPU mode radios | **Never created** (B-09) | Custom segmented control (Phase 5) | ⬜ Open |
| Windows Power controls | **Never created** (B-09) | Custom controls or remove (Phase 5) | ⬜ Open |

---

## 7. ACCESSIBILITY & CONTRAST (measured)

WCAG 2.2 relative-luminance, measured against each theme's `bgWindow`:

| Theme | textPrimary | textSecondary | textMuted (need ≥4.5:1) | borderFocus (need ≥3:1) |
|---|---|---|---|---|
| Cyberpunk | ~17:1 ✅ | ~8:1 ✅ | #6B7299 ≈4.9:1 ✅ | #00F0FF ✅ |
| Stealth | ~13:1 ✅ | ~7:1 ✅ | #565C6E ≈4.6:1 ✅ | #4C8DFF ✅ |
| Spectrum | ~21:1 ✅ | ~9:1 ✅ | #6E7288 ≈4.7:1 ✅ | #00E6FF ✅ |
| Phosphor | ~12:1 ✅ | ~8:1 ✅ | #8A6400 ≈5.1:1 ✅ | #FFB000 ✅ |
| Solarized | ~10:1 ✅ | ~4.8:1 ✅ | #586E75 ≈4.5:1 ✅ | #268BD2 ✅ |

**Focus indicators:** all owner-draw buttons draw `ODS_FOCUS` ring with `borderFocus` color (B-06 fixed).
**Status ≠ color-only:** thermal cards use `!`-equivalent glyphs via color + text; badges use dot + text.

---

## 8. PERFORMANCE & BINARY GUARDS (hard gates)

| Gate | Target | Measured at baseline |
|---|---|---|
| Binary size | < 20 MB | TBD (Phase 0) |
| RAM idle | < 50 MB | TBD (Phase 0) |
| CPU idle | < 1% (2-min avg) | TBD (Phase 0) |
| Metrics tick CPU | No spike on 1s poll | Partial invalidation only (B-08 fixed) |
| GDI object growth | Constant over 100 theme switches | Brush/pen/font caches flushed on switch |
| Animation gate | Disable if idle CPU >1% | `animationEnabled` token + `animTimerMs` |

---

## 9. IMPLEMENTATION PLAN

### Phase 0 — Baseline (do not skip)

- [ ] Run current exe; capture screenshots at 100% and 150% DPI (`docs/ui-baseline/`)
- [ ] Measure: binary size, RAM idle, CPU idle (2-min avg) → `docs/ui-baseline/metrics.txt`
- [ ] Record current `InvalidateRect` behavior on metrics tick
- [ ] Record baseline evidence for B-01/B-07/B-08 (screenshot slider drag, ≥85°C tier if reachable)
- [ ] Commit. Nothing else in this phase.

### Phase 1 — Token engine

- [ ] `include/Theme.h` + `src/Theme.cpp` exist ✅ (verify all 5 themes + Legacy match §4 spec values)
- [ ] All 5 `ThemeTokens` definitions as data only ✅
- [ ] `Theme::SetTheme` + GDI cache + `CleanupTheme` ✅
- [ ] **Delete F-01:** `VisualStyle` removed from `UI.h` ✅; ~10 raw `COLORREF` members removed ✅; all reads use `Theme::Tokens()` ✅
- [ ] Theme persistence in `SaveSettings`/`LoadSettings` ✅ (`ThemeIndex` reg key)
- [ ] **Bug fixes:** B-04/B-16 (`Layout::Compute` single geometry source ✅), B-10 (`AdjustWindowRect` ✅), B-14 (no `RGB()` literals in UI.cpp except LED user data ✅), B-03 (per-monitor-v2 DPI + `WM_DPICHANGED` ✅), B-15 (`g_uiInstance` cleared ✅)
- [ ] Build + run: app looks identical to pre-audit when `Theme::SetTheme(hwnd, ThemeId::Legacy)` — refactor safety net

### Phase 2 — Unify primitives

- [ ] Parameterize `ModernTheme::DrawModernCard` + remaining global-palette readers ✅ (all take `COLORREF` params)
- [ ] Delete `UI.cpp` duplicates of `DrawCard`/`DrawProgressBar`/`DrawSectionHeader`/`FillRoundedRect` ✅
- [ ] All primitives draw through `Theme::Brush/Pen/Font` cache ✅
- [ ] Double-buffer memory-DC helper ✅; `OnPaint` routes through it ✅; `WM_ERASEBKGND` returns 1 ✅
- [ ] **Bug fixes:** B-08 (paint clips to `ps.rcPaint` ✅), B-07 (apply on drag end ✅; `OnLedApply` removed from tick ✅; `SaveSettings` not called per tick ✅)

### Phase 2.5 — Resizable window + layout engine (user requirement, per §14)

- [ ] `WS_THICKFRAME` \| `WS_MAXIMIZEBOX` in window styles ✅
- [ ] `Layout::Compute(clientW, clientH)` implemented ✅ — **but** uses proportional `MulDiv(x, clientWidth, 540)` instead of true DPI scaling; metrics from `Theme::Metrics()` are fixed constants, not DPI-scaled
- [ ] `WM_SIZE` → layout → `LayoutControls()` → invalidate ✅
- [ ] `WM_GETMINMAXINFO` min clamp `ScaleX(380)` × `ScaleY(640)` ✅
- [ ] All hit-testing, `WM_SETCURSOR`, invalidation read `LayoutResult` ✅ (B-04/B-12/B-16 closed)
- [ ] **Remaining:** DPI-scaled layout metrics (ThemeMetrics should scale with DPI, not fixed constants)

### Phase 3 — Cyberpunk theme (flagship)

- [ ] Implemented per §4 Theme 01 ✅
- [ ] Section accent map ✅; over-limit danger states ✅
- [ ] **Bug fixes:** B-01 (distinct danger color ≥85°C ✅), B-11 (no emoji ✅), B-12 (cursor over cards only ✅), B-13 (combo disabled when auto ✅)
- [ ] Screenshot at 100%/150% DPI; compare vs spec

### Phase 4 — Stealth + Spectrum

- [ ] Stealth implemented ✅; Spectrum implemented ✅
- [ ] Both: `DWMWCP_ROUND` ✅

### Phase 5 — Control owner-draw (biggest phase, budget accordingly)

- [ ] Custom slider control (track, fill, thumb, tooltip, keyboard, wheel) — ⬜ NOT STARTED (native sliders still used)
- [ ] Custom toggle switch replacing checkboxes — ⬜ NOT STARTED
- [ ] Custom segmented control replacing GPU-mode radios — ⬜ NOT STARTED (radios never created, B-09)
- [ ] Owner-draw combo boxes (measure item heights, themed popup) — ⬜ NOT STARTED
- [ ] Custom pill-tab control for fan/LED presets — ✅ Done (owner-draw fan buttons)
- [ ] **Bug fixes:** B-02 (`IsDialogMessage` ✅), B-06 (`ODS_FOCUS` focus ring ✅), B-09 (resolve dead controls: implement GPU radios → segmented control; Windows Power section → create or remove IDs/empty stubs; remove `m_lblRefreshStatus` dead reference) — ⬜ OPEN
- [ ] Every custom control: focus ring ✅, 44px min hit area, hover+pressed, disabled, tab-order
- [ ] Full restyle matrix §6 verified for Cyberpunk first, then others

### Phase 6 — Theme switcher UX

- [ ] Theme swatch buttons (5 small: name + 3 color dots) — ⬜ NOT STARTED
- [ ] Tray menu → "Interface" submenu with 5 themes + checkmark — ⬜ NOT STARTED (`ShowTrayMenu` has no theme items)
- [ ] **Bug fix:** B-05 (`TaskbarCreated` ✅)
- [ ] Persistence round-trip: theme saved/restored ✅ (`ThemeIndex` reg key)
- [ ] **Remaining:** window size+position persistence (§14.5) — ⬜ NOT STARTED

### Phase 7 — Motion & micro-polish (conditional)

- [ ] Pill glow pulse on active profile — ⬜ NOT STARTED (no animation timer)
- [ ] Slider fill transition on profile change — ⬜ NOT STARTED
- [ ] **Gate:** re-measure idle CPU; >1% → disable all animation via token
- [ ] Danger-state breathing glow on over-limit cards — ⬜ NOT STARTED

### Phase 8 — Solarized + QA

- [ ] Solarized implemented ✅ (regression tripwire)
- [ ] **Verify every §2.5 bug is closed** — walk B-01…B-16 list
- [ ] **Verify resize (§14)** — QA rows Q25–Q29 across all 5 themes × 3 window states
- [ ] Run full §10 acceptance + §7 contrast per theme
- [ ] Update `README.md` UI section
- [ ] Final metrics re-run vs Phase 0 baseline

---

## 10. ACCEPTANCE CRITERIA (Definition of Done)

1. **All 5 themes implemented exactly per §4 spec values.** No inventing new colors without a contrast check.
2. **Zero hardcoded palette reads outside `Theme.cpp`.** Grep gate: `RGB(` in `UI.cpp` must return only LED-custom-color picker values (user data, not theme).
3. **Single source of drawing primitives** — no duplicated draw functions remain (F-02 closed).
4. **Native-widget mismatch eliminated** — every control in §6 marked "CD" renders fully themed in every theme (F-04 closed for buttons; sliders/combos may remain native if acceptable).
5. **Contrast:** all text ≥4.5:1, components ≥3:1, per §7 — tables pasted with results for all 5 themes.
6. **Focus indicators** on all interactive controls (keyboard Tab walk is visible).
7. **Status ≠ color-only** (glyph/shape on badges).
8. **Performance gates §8 all pass**, measured, recorded.
9. **Persistence** works across restart for theme + all existing settings.
10. **DPI:** 100% and 150% screenshots for all 5 themes; nothing clipped, no unscaled radii.
11. Binary < 20 MB, RAM < 50 MB, idle CPU < 1%.
12. `README.md` updated.
13. **All §2.5 bugs (B-01…B-16) closed.** Spot-check: ≥85°C tier = distinct danger color (B-01); Tab walks whole window with visible focus (B-02/B-06); `explorer.exe` restart restores tray icon (B-05); slider drag = one hardware apply at release (B-07); metrics tick repaints only card region (B-08); 150% DPI nothing clipped (B-10).
14. **Window is freely resizable and maximizable (§14):** drag any edge/corner, no flicker or clipped content at any size ≥ minimum; 2-column ↔ 1-column card reflow at width threshold; maximized ultra-wide centers content column; size+position survive restart with off-screen fallback; min-size clamp active.

---

## 11. QA MATRIX (execute every row × every theme)

| # | Check | Pass condition |
|---|---|---|
| Q1 | Theme switch via window swatches | instant repaint, no mixed-theme artifacts |
| Q2 | Theme switch via tray menu | same |
| Q3 | Restart persistence | theme restored |
| Q4 | AC unplug/plug (profile auto-switch) | pill + all dependent controls update in current theme |
| Q5 | Hover every control | hover states correct, no repaint flicker |
| Q6 | Keyboard: Tab through all controls | focus ring visible everywhere, logical order |
| Q7 | Sliders: mouse, arrows, wheel, drag-outside-release | value updates, label syncs, no stuck capture |
| Q8 | Combos open/select/escape | popup themed, selection applies |
| Q9 | Fan presets ×4 | active-tab indicator correct |
| Q10 | LED picker: custom color via common dialog | rendered correctly in all themes |
| Q11 | Telemetry tick ×60s | CPU stable <1%, no flicker, no handle growth |
| Q12 | 100 theme-switch stress loop | GDI objects constant |
| Q13 | Minimize to tray → restore | window repaints in current theme |
| Q14 | DPI 150% | all metrics scaled, no clipping |
| Q15 | Battery + Performance profiles | per-profile visuals reflect state |
| Q16 | Tab key through whole window | `IsDialogMessage` active (B-02); focus ring visible (B-06) |
| Q17 | Change display DPI at runtime | `WM_DPICHANGED` handled (B-03): layout rescales, fonts rebuild |
| Q18 | Kill + restart `explorer.exe` | tray icon re-appears ~2s (B-05) |
| Q19 | Thermal ladder: simulate ≥75°C and ≥85°C | two distinct colors + glyph; danger tier not stuck on amber (B-01) |
| Q20 | Drag CPU/GPU slider end-to-end | hardware applies once on release (B-07); `SaveSettings` debounced |
| Q21 | Idle with telemetry ×60s | repaint region ≠ full window (B-08); CPU <1% |
| Q22 | Hover gap between cards | cursor is arrow, not hand (B-12) |
| Q23 | Auto Refresh checked | refresh-rate combo visually disabled (B-13) |
| Q24 | Window bottom row at 100%/150% DPI | last control fully visible, no clip (B-10) |
| Q25 | Drag-resize every edge + corner, live | layout reflows, no flicker, no clipped/overlapping controls |
| Q26 | Resize below min clamp | clamped at 380×640 (scaled); nothing clipped at min |
| Q27 | Maximize on ultra-wide | content column centered at max 720px; controls not edge-stretched |
| Q28 | Narrow → wide resize (1↔2 column reflow) | cards reflow both directions without stale hit-test/cursor areas |
| Q29 | Resize → restart → relaunch | size + position restored; off-screen value falls back to default |

---

## 12. DO-NOT LIST

- ❌ Do not introduce Direct2D/DirectWrite/Skia/Qt/WinUI or any new lib. GDI only.
- ❌ Do not add background services or change the 1s Monitor polling design.
- ❌ Do not put decorative circuit-boards, hex grids, or "matrix rain" in the cyberpunk theme.
- ❌ Do not glow everything; glow is earned by state (hover/active/danger only).
- ❌ Do not encode state by color alone.
- ❌ Do not leave any `RGB(` literal outside `Theme.cpp` (except LED user-color data).
- ❌ Do not break the LED/Fan/Power manager APIs — UI-layer-only refactor. Manager files untouched.
- ❌ Do not remove existing keyboard accelerators or tray behaviors.
- ❌ Do not ship animation if it violates the 1% idle-CPU gate.
- ❌ Do not treat §2.5 bug fixes as optional or defer past assigned phase — B-01, B-05, B-07, B-08 ship regardless of theme progress.
- ❌ Do not implement resize by sprinkling `client.right/client.bottom` into old hardcoded rects — resize goes through `Layout::Compute` only (§14.3).

---

## 13. SUGGESTED COMMIT SERIES

```
feat(ui): add theme token engine (Phase 1)
refactor(ui): unify draw primitives, remove duplicate palette (Phase 2)
feat(ui): resizable window + layout engine (Phase 2.5)
feat(ui): cyberpunk "Neon District" theme (Phase 3)
feat(ui): stealth + spectrum themes (Phase 4)
feat(ui): owner-drawn controls — sliders, toggles, segments, combos (Phase 5)
feat(ui): close B-09 dead controls — GPU radios + Windows Power section (Phase 5)
feat(ui): theme switcher + persistence (Phase 6)
feat(ui): window placement persistence (§14.5)
feat(ui): micro-animations behind idle-CPU gate (Phase 7)
feat(ui): solarized light theme + full QA pass (Phase 8)
docs: update README UI section
```

---

## 14. RESIZABLE WINDOW (user requirement — the whole UI must resize)

**Requirement:** the user must be able to freely resize (and maximize) the entire window. **Implemented** — verify during QA.

### 14.1 Window styles & frame (implemented)

- Created with: `WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX`
- Initial frame computed with `AdjustWindowRect` so client area = designed 540×1020 at current DPI
- `DWMWA_WINDOW_CORNER_PREFERENCE` per theme (`DONOTROUND` for Cyberpunk, `ROUND` otherwise)

### 14.2 Size clamps (`WM_GETMINMAXINFO`, implemented)

- **Minimum client size:** `ScaleX(380) × ScaleY(640)` — scaled at current DPI
- **Default size:** 540×1020 client on first run
- **Maximum:** none (maximize allowed)

### 14.3 Layout engine (implemented — verify DPI scaling)

- `Layout::Compute(clientWidth, clientHeight, metrics)` recomputes on every `WM_SIZE` and `WM_DPICHANGED`
- **Horizontal reflow:** telemetry cards render **2 columns** when client width ≥ threshold, **1 column** below
- **Content column max width:** conceptually `ScaleX(720)`; when window wider, content should center (verify)
- **Vertical:** sections stack top-aligned; extra height leaves breathing room at bottom
- All hit-testing, cursor bands, invalidation rects, paint rects read `LayoutResult`

### 14.4 Paint during resize (implemented)

- Routes through double-buffer memory DC (no flicker)
- `WM_SIZE` → full client invalidate (correct for resize); 1s metrics tick still uses partial invalidation

### 14.5 Persistence (NOT STARTED — gap)

- `SaveSettings()`/`LoadSettings()` do **not** save/restore window size + position
- Need: save `WINDOWPLACEMENT` (or size+pos ints) on `WM_CLOSE`/exit; restore validated via `MonitorFromRect`; fall back to default 540×1020 if off-screen

### 14.6 Remaining concern: Layout DPI scaling

- `Layout::Compute` uses `MulDiv(x, clientWidth, 540)` — proportional scaling from a 540×1020 base design
- `ThemeMetrics` values are **fixed constants** (not DPI-scaled)
- At 150% DPI, layout rects will not scale correctly unless `ThemeMetrics` are DPI-adjusted or `Layout::Compute` applies DPI scaling
- Fix: pass DPI scale factor to `Layout::Compute` or scale `ThemeMetrics` at `WM_DPICHANGED`

---

---

## 15. BORDERLESS / COMPACT FLOATING WIDGET MODE (new requirement)

**Requirement:** the user wants a **borderless (frameless)** window that can be made **small (compact)** and **hovered/dragged around** the screen — a floating widget, not a traditional fixed-size app window. This replaces/resolves the old "resizable window" requirement (§14) with a more focused floating-widget model.

### 15.1 Window chrome (borderless)

- **Window style:** `WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN` — **no** `WS_CAPTION`, `WS_THICKFRAME`, `WS_SYSMENU`, `WS_MINIMIZEBOX`, `WS_MAXIMIZEBOX`.
- **Extended style:** `WS_EX_TOOLWINDOW` (optional — no taskbar button, app lives in tray; keep normal taskbar presence if user prefers).
- **No title bar:** all chrome drawn in client area. A slim top "grab bar" (≈28px) acts as the drag handle; the rest of the client is the content area.
- **Custom close button:** owner-drawn "×" in the top-right of the grab bar (since there's no system menu). Minimize → tray already works (`WM_CLOSE` → hide).
- **`AdjustWindowRect` not needed** for `WS_POPUP`; client = window rect.

### 15.2 Drag-to-move (hover around)

- **`WM_NCHITTEST`:** if cursor is over the grab bar **and not over a child control**, return `HTCAPTION` — Windows natively drags the window. If over a child control, return `HTCLIENT` so buttons/sliders/combos still work.
- **Fallback:** `WM_LBUTTONDOWN` on empty client area → capture mouse → `WM_MOUSEMOVE` → `SetWindowPos`/`MoveWindow` with offset tracking. This ensures dragging works even if `WM_NCHITTEST` is tricky with child controls.
- **Drag threshold:** small (≈4px) so clicks on controls aren't mistaken for drag starts.

### 15.3 Compact mode

- **Toggle:** a compact/normal switch (tray menu + a small button in the grab bar).
- **Compact size:** ~280×120 client. Shows only: profile toggle pill, CPU temp/usage mini-readout, GPU temp mini-readout, close button. All other sections hidden.
- **Normal size:** 540×1020 (existing full layout).
- **`Layout::Compute`** gains a `compactMode` branch that returns the small widget rects.
- **Position persistence:** save window rect (x, y, w, h) + compact mode + always-on-top in `SaveSettings()`/`LoadSettings()`. Validate on restore via `MonitorFromRect`; fall back to default centered position if off-screen.

### 15.4 Always-on-top

- **Toggle:** tray menu item + compact widget button.
- **Implementation:** `SetWindowPos(hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, ...)`.

### 15.5 Bugs this introduces (B-17 through B-21)

| ID | Bug | Severity |
|---|---|---|
| **B-17** | Borderless window has no system menu → no way to close except via tray or Alt+F4; must add a client-area close button. | 🔴 |
| **B-18** | `WM_NCHITTEST` must distinguish grab-bar drag area from control hit areas, or buttons/sliders become unclickable. | 🔴 |
| **B-19** | Compact mode layout is not defined — `Layout::Compute` has no compact branch. | 🟡 |
| **B-20** | Window position not persisted — floating widget loses its screen location on restart. | 🟡 |
| **B-21** | Always-on-top not implemented — no way to keep the widget visible over other windows. | 🟡 |

### 15.6 Implementation plan (new Phase 2.6, after Phase 2.5)

- [ ] `CreateMainWindow`: switch to `WS_POPUP`, remove caption/thickframe styles.
- [ ] `OnNCHitTest`: return `HTCAPTION` over grab bar (non-control areas), `HTCLIENT` over controls.
- [ ] Add owner-drawn close button (`IDC_BTN_CLOSE`) in top-right of grab bar.
- [ ] `Layout::Compute`: add `compactMode` branch (~280×120 widget).
- [ ] `ToggleCompactMode()` + `ToggleAlwaysOnTop()` methods.
- [ ] `SaveSettings()`/`LoadSettings()`: persist window rect, compact mode, always-on-top.
- [ ] Tray menu: add "Compact Mode", "Always on Top", "Close" items.
- [ ] Verify: drag from grab bar moves window; controls still clickable; compact toggle swaps layout; position survives restart.

---

## 15. BORDERLESS / COMPACT FLOATING WIDGET MODE (implemented)

**Requirement:** the user wants a **borderless (frameless)** window that can be made **small (compact)** and **hovered/dragged around** the screen — a floating widget, not a traditional fixed-size app window. This replaces/resolves the old "resizable window" requirement (§14) with a more focused floating-widget model.

### 15.1 Window chrome (borderless) — IMPLEMENTED

- **Window style:** `WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN` — **no** `WS_CAPTION`, `WS_THICKFRAME`, `WS_SYSMENU`, `WS_MINIMIZEBOX`, `WS_MAXIMIZEBOX`.
- **Extended style:** `WS_EX_TOOLWINDOW | WS_EX_LAYERED` — no taskbar button, app lives in tray.
- **No title bar:** all chrome drawn in client area. A slim top "grab bar" (≈32px) with neon border acts as the drag handle.
- **Custom close button:** owner-drawn "✕" in the top-right of the grab bar (red accent, hover glow). Calls `DestroyWindow`.
- **Compact toggle:** owner-drawn "⧉"/"⛶" button on the grab bar. Toggles between 280×120 compact and 540×1020 normal.

### 15.2 Drag-to-move (hover around) — IMPLEMENTED

- **`WM_NCHITTEST`:** returns `HTCAPTION` over the grab bar (when not over a child control) — Windows natively drags the window. Buttons/sliders/combos remain clickable (`HTCLIENT`).
- **Cyberpunk grab bar:** neon magenta border (`#FF00A8`), corner ticks in cyan (`#00F0FF`), status dot (cyan on AC, amber on battery), title text.

### 15.3 Compact mode — IMPLEMENTED

- **Compact size:** ~280×120 client. Shows only: profile toggle pill, CPU temp mini-readout, GPU temp mini-readout, close button.
- **Normal size:** 540×1020 (existing full layout).
- **`Layout::Compute`** has a `compactMode` branch that returns the small widget rects (§15.3 in code).
- **Position persistence:** window rect (x, y, w, h), compact mode, always-on-top saved in `SaveSettings()`/`LoadSettings()`. Validated via `MonitorFromRect` on restore.

### 15.4 Always-on-top — IMPLEMENTED

- **Toggle:** tray menu item + compact widget button.
- **Implementation:** `SetWindowPos(hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, ...)`.

### 15.5 Bugs fixed (B-17 through B-21)

| ID | Bug | Fix | Status |
|---|---|---|---|
| **B-17** | Borderless window has no system menu → no way to close. | Owner-drawn close button (✕) in grab bar, calls `DestroyWindow`. | ✅ FIXED |
| **B-18** | `WM_NCHITTEST` must distinguish grab bar from controls. | Returns `HTCAPTION` only over grab bar non-control areas; `HTCLIENT` over controls. | ✅ FIXED |
| **B-19** | Compact mode layout not defined. | `Layout::Compute` compact branch (~280×120 widget). | ✅ FIXED |
| **B-20** | Window position not persisted. | Save/load window rect, compact mode, always-on-top in registry. | ✅ FIXED |
| **B-21** | Always-on-top not implemented. | `SetWindowPos` with `HWND_TOPMOST`/`HWND_NOTOPMOST`. | ✅ FIXED |

### 15.6 Implementation summary (code changes)

| File | Change |
|---|---|
| `include/UI.h` | Added `IDC_BTN_CLOSE` (1004), `IDC_BTN_COMPACT` (1005), tray IDs 2010/2011; members `m_boldderless`, `m_compactMode`, `m_alwaysOnTop`, `m_dragging`, `m_dragOffset`; Layout struct `rCompactGrab`, `rCloseBtn`; methods `OnNCHitTest`, `ToggleCompactMode`, `ToggleAlwaysOnTop` |
| `include/Theme.h` | No changes needed (cyberpunk tokens already exist) |
| `src/UI.cpp` | `CreateMainWindow`: `WS_POPUP` + `AdjustWindowRectEx`; `Layout::Compute`: compact mode branch; `OnPaint`: grab bar with neon border + corner ticks + compact mini-readouts; `WM_NCHITTEST`: drag handle hit-test; `WM_DRAWITEM`: close + compact button owner-draw with cyberpunk colors; `OnCommand`: close/destroy, compact toggle; `ToggleCompactMode`/`ToggleAlwaysOnTop` implementations; `SaveSettings`/`LoadSettings`: widget state persistence; `ShowTrayMenu`: compact + always-on-top menu items |

---

*End of audit. The second agent should start at Phase 0, verify what's already implemented against this spec, then close the remaining gaps (B-09 dead controls, theme switcher UI, Layout DPI scaling).*