# Product

<!-- impeccable:product-schema 1 -->

## Platform

adaptive

## Stack

Win32 C++ (MSVC), pure GDI rendering, no external UI frameworks. Built with Visual Studio 2022. Binary target: ~2 MB, RAM ~15 MB idle, CPU ~0.1% idle.

## Users

Primary: Alienware laptop owners (m15/m17 R3-R7, x15/x17, Area-51m) who want granular hardware control without bloatware. Situation: gaming or creative workload on battery vs plugged in; needs instant profile switching. Secondary: enthusiasts who manually tune CPU PL1/GPU power limits, fan curves, and AlienFX LED zones per game. Tertiary: IT/technical users who need Windows power plan automation without Group Policy friction.

## Product Purpose

Replace Alienware Command Center (AWCC) — a 400 MB+ Electron/Node.js service suite with telemetry, background processes, and 10+ second cold starts — with a <20 MB native Win32 executable that launches in <300 ms, has zero background services, zero telemetry, and exposes every hardware knob AWCC hides: CPU PL1/GPU power limits (Intel XTU / NVML), per-zone AlienFX LED control (USB HID), fan curve presets + manual PWM, display refresh rate locking, Windows power plan auto-switch, and GPU mode (Optimus) toggling. Two profiles (Battery / Performance) with one-click toggle; auto-switch on AC/DC.

Success = a user who previously uninstalled AWCC in frustration can download this, run it once, set their two profiles, and never think about power management again — while seeing live telemetry (CPU/GPU temp, clocks, power, battery, fan RPM, display refresh) that updates every second with <1% CPU cost.

## Positioning

The only Alienware control tool that is: (1) native Win32 with zero dependencies beyond Windows SDK + NVML, (2) completely portable (no installer, no services, no registry pollution beyond user HKCU), (3) open-source auditable, (4) themable with 5 distinct visual identities (Cyberpunk flagship, Stealth, Spectrum, Phosphor, Solarized) including a light theme regression tripwire, (5) keyboard-navigable and screen-reader accessible (WCAG 2.2 AA), (6) per-monitor DPI aware v2 with fully resizable layout, (7) under 1% idle CPU with partial invalidation rendering.

No competing tool (AWCC, ThrottleStop, HWiNFO, G-Helper, FanControl) hits all seven.

## Operating Context

Workflows: 
- First run: admin elevation prompt → choose theme → set Battery profile (CPU PL1, GPU limit, fan, LED, refresh) → set Performance profile → enable startup → minimize to tray
- Daily: app sits in tray; user clicks tray icon → window opens at cursor → toggle profile button → window hides
- Gaming: plug AC → auto-switch to Performance → fan ramps, LEDs shift, refresh locks → unplug → auto-switch to Battery
- Tuning: open window → drag CPU/GPU sliders → see live wattage → click Apply → settings persist

Environment: Windows 10 19041+ / Windows 11, Alienware laptops with EC firmware supporting fan/LED control via USB HID (0x187C/0x0550) or WMI. Requires admin for power limit writes (Intel XTU / powrprof) and NVML for GPU limits.

Tools: Visual Studio 2022, Windows SDK, NVML headers (dynamic load at runtime), no vcpkg/Conan.

Rituals: Build → run as admin → verify tray icon persists across Explorer restart → verify DPI change on monitor drag → verify theme switch instant repaint.

## Capabilities and Constraints

Confirmed functionality:
- Profile toggle (Battery/Performance) with instant hardware apply
- CPU PL1 slider (15–65W) + GPU power limit slider (30–160W) via NVML
- Fan curve: 4 presets (Silent/Balanced/Cool/Max) + manual PWM slider (0–100%)
- AlienFX LED: per-zone color/mode/brightness/speed, color picker, rainbow/off/on
- Display: auto refresh (OS default) or manual lock (60/120/144/165/240/360 Hz)
- Windows power plan: dropdown (Balanced/High Performance/Power Saver/Ultimate) + CPU min/max % sliders + sleep/display timeouts + auto-switch on profile change
- GPU mode: Auto/Integrated/Hybrid/Discrete (Optimus) via WMI
- Live telemetry: CPU/GPU temp/clock/power, battery %/time, fan RPM, display refresh, power plan name, GPU mode string
- Settings persistence: HKCU\Software\PowerConfigurator (profiles, startup, auto-switch, presets)
- System tray: show/hide, profile switch, fan presets, LED off, exit
- Theme engine: 5 themes + Legacy safety net, token-driven (Theme::Tokens()), GDI cache manager, DWM dark mode + corner preference

Technical constraints:
- Pure Win32/GDI — no Direct2D, Skia, Qt, Electron, WPF, WinUI, MAUI
- Double-buffered rendering (memory DC + BitBlt)
- Per-monitor DPI awareness v2 (SetProcessDpiAwarenessContext)
- No background threads/services — single UI thread, 1s timer for metrics
- Binary <20 MB, RAM <50 MB, CPU <1% idle
- Admin required for power writes; non-admin runs read-only telemetry
- NVML loaded dynamically (nvml.dll) — gracefully degrades if absent

Undecided / open:
- Whether Windows Power section (power plan dropdown, CPU min/max, sleep timeouts) ships in MVP or Phase 5 — currently has control IDs but zero implementation (B-09)
- Whether GPU mode radios ship in MVP or Phase 5 — control IDs reserved, handlers empty (B-09)
- Exact fan curve editor UI (Phase 5: custom points vs 4 presets only)
- Whether to support non-Alienware laptops (fan/LED EC protocols vary)

## Brand Commitments

Name: Power Configurator (working title; may become "Alienware Power Tools" or similar)
Voice: Technical, precise, no marketing fluff. Copy is imperative ("Set CPU PL1", not "Manage your CPU power").
Assets: None yet — no logo, no icon, no brand imagery. Uses Alienware cyan/magenta as de facto accent in Cyberpunk theme.
Identity constraints: Must feel like first-party Alienware software — sharp, high-contrast, gaming-adjacent without "gamer aesthetic" clichés (no RGB vomit, no fake carbon fiber). The Cyberpunk theme is the flagship; others are deliberate alternatives.

## Evidence on Hand

Real content:
- UI_AUDIT.md: 670-line comprehensive redesign spec with 8 phases, 16 confirmed bugs (B-01–B-16), token schema, theme specs, layout engine for resizable windows, 14 acceptance criteria, 29 QA checks
- Theme.cpp: 5 complete theme definitions (Cyberpunk, Stealth, Spectrum, Phosphor, Solarized) + Legacy safety net, all with WCAG-verified contrast ratios, token schema per UI_AUDIT.md §4
- ModernTheme.cpp: Drawing primitives (cards, buttons, pills, progress bars, badges, section headers) consuming Theme::Tokens()
- UI.cpp: 2176 lines — window proc, custom painting, control creation, metrics display, hardware handlers
- README.md: Product spec, features, two profiles, build instructions, architecture

Absences future work must not fabricate:
- No user testimonials, case studies, or press
- No benchmark comparisons vs AWCC (though ~20 MB vs ~400 MB is factual)
- No accessibility audit report (audit found 3.2:1 contrast failures, broken keyboard nav)
- No DPI testing evidence (audit confirmed B-03: no WM_DPICHANGED handler)

## Product Principles

1. **Native first, always** — If it can't be done in pure Win32/GDI with <1% idle CPU, it doesn't ship. No frameworks, no runtimes, no background services.
2. **Hardware honesty** — Every slider, toggle, and number maps to a real EC/XTU/NVML/WMI call. No placebo controls. If hardware doesn't support it, the control is disabled with explanation.
3. **Two profiles, zero friction** — Battery and Performance. One click/tap/tray action to switch. Auto on AC/DC. No third profile, no "balanced" compromise.
4. **Theme as contract** — The 5 themes are not skins; they are complete visual worlds with distinct typography, spacing, motion, and color philosophy. Switching themes must be instant and total (window chrome, controls, tray menu).
5. **Accessibility is not optional** — WCAG 2.2 AA is the floor. Keyboard navigation, focus visibility, contrast, screen reader names, high contrast mode support — all ship in Phase 5, not as afterthought.

## Accessibility & Inclusion

Required standard: WCAG 2.2 AA (4.5:1 normal text, 3:1 large text/UI components, focus visible, keyboard operable, screen reader compatible).
Known gaps from audit:
- Tab navigation completely broken (no IsDialogMessage in message loop) — B-02
- No focus indicators on any custom-draw control — B-06
- Contrast failures: TEXT_MUTED 3.2:1, ACCENT_PURPLE 3.6:1 on BG — F-05
- No high contrast mode detection/response
- No screen reader accessible names on owner-draw controls
- No Dynamic Type equivalent (Win32: respect system font scaling via DPI awareness v2)
Phase 5 (Owner-Draw Controls & Accessibility) addresses all of the above.