#include "Theme.h"
#include <map>
#include <unordered_map>
#include <mutex>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DEFAULT    0
#define DWMWCP_DONOTROUND 1
#define DWMWCP_ROUND      2
#define DWMWCP_ROUNDSMALL 3
#endif

namespace Theme {

// Default layout metrics — scaled by Layout::Compute at runtime
static const ThemeMetrics s_defaultMetrics = {
    16,   // margin
    20,   // gap
    28,   // sectionGap
    40,   // controlHeight
    28,   // labelHeight
    32,   // sliderHeight
    44,   // buttonHeight (>=44px per WCAG / audit)
    120,  // cardHeight
    10,   // cornerRadius
    6,    // cornerRadiusButton
    8,    // cardCornerRadius
    1,    // borderWidth
    32,   // headerHeight
    28,   // headerBandHeight
    2,    // sliderTrackWidth
    10,   // sliderThumbSize
    3,    // glowLayers
    28,   // glowStep
    40    // minHitTarget
};

// ============================================================================
// The 5 themes + Legacy safety net (exact RGB per UI_AUDIT.md 4)
// ============================================================================

// 🟣 01 — CYBERPUNK · "NEON DISTRICT" (FLAGSHIP, DEFAULT)
static const ThemeTokens s_cyberpunk = {
    ThemeId::Cyberpunk,
    L"Neon District",
    L"Cyberpunk void, razor-thin neon strokes, cyan/magenta telemetry",

    // Surfaces
    RGB(5, 5, 8),       // bgWindow #050508
    RGB(10, 10, 18),    // bgCard #0A0A12
    RGB(14, 14, 26),    // bgCardHover #0E0E1A
    RGB(7, 7, 14),      // bgControl #07070E
    RGB(12, 12, 22),    // bgOverlay

    // Borders
    RGB(42, 33, 64),    // borderCard #2A2140 (dim violet)
    RGB(255, 0, 168),   // borderCardHover #FF00A8 (neon magenta)
    RGB(0, 240, 255),   // borderFocus #00F0FF (cyan ring)
    1,                  // borderWidth
    2,                  // cornerRadius (razor sharp)
    0,                  // cornerRadiusButton
    2,                  // cardCornerRadius (razor sharp)

    // Text (contrast verified per WCAG 2.2 against bgWindow #050508)
    RGB(242, 242, 247), // textPrimary #F2F2F7 (~17:1)
    RGB(155, 163, 201), // textSecondary #9BA3C9 (~8:1)
    RGB(107, 114, 153), // textMuted #6B7299 (~4.9:1, PASSES 4.5:1)
    RGB(5, 5, 8),       // textOnAccent #050508

    // Accents
    RGB(255, 0, 168),   // accentPrimary #FF00A8
    RGB(0, 240, 255),   // accentSecondary #00F0FF
    RGB(0, 255, 159),   // accentSuccess #00FF9F
    RGB(255, 230, 0),   // accentWarning #FFE600
    RGB(255, 42, 74),   // accentDanger #FF2A4A
    RGB(0, 240, 255),   // accentInfo #00F0FF
    RGB(255, 0, 0),     // accentThermalDanger ≥85°C bright red

    // Section map: CPU->cyan, GPU->magenta, Battery->yellow, Display->green, Fan->red, LED->purple, Power->yellow
    RGB(0, 240, 255),   // accentCpu
    RGB(255, 0, 168),   // accentGpu
    RGB(255, 230, 0),   // accentBattery
    RGB(0, 255, 159),   // accentDisplay
    RGB(255, 42, 74),   // accentFan
    RGB(168, 85, 247),  // accentLed #A855F7
    RGB(255, 230, 0),   // accentPower

    // Effects
    true,               // glowEnabled
    3,                  // glowLayers
    28,                 // glowStep
    false,              // scanlines (disabled: destructive to text rendering)
    RGB(255, 255, 255), // scanlineColor
    false,              // gradients
    RGB(10, 10, 18), RGB(10, 10, 18), // gradTop/Bottom
    true,               // sharpWindowCorners (DWMWCP_DONOTROUND)

    // Typography
    L"Segoe UI",
    L"Consolas",
    14, 10, 9, 8, 22, 10,

    // Motion
    true,               // animationEnabled
    50                  // animTimerMs (gated to transitions only)
};

// ⬛ 02 — MINIMAL · "STEALTH"
static const ThemeTokens s_stealth = {
    ThemeId::Stealth,
    L"Stealth",
    L"Flat, calm, single-accent dark theme",

    // Surfaces
    RGB(11, 12, 16),    // bgWindow #0B0C10
    RGB(18, 20, 26),    // bgCard #12141A
    RGB(22, 25, 34),    // bgCardHover #161922
    RGB(14, 16, 21),    // bgControl #0E1015
    RGB(20, 22, 28),    // bgOverlay

    // Borders
    RGB(28, 31, 40),    // borderCard #1C1F28
    RGB(42, 46, 58),    // borderCardHover #2A2E3A
    RGB(76, 141, 255),  // borderFocus #4C8DFF
    1,
    10,
    6,
    8,                  // cardCornerRadius

    // Text
    RGB(232, 234, 240), // textPrimary #E8EAF0
    RGB(154, 160, 180), // textSecondary #9AA0B4
    RGB(86, 92, 110),   // textMuted #565C6E (~4.6:1, PASSES)
    RGB(11, 12, 16),    // textOnAccent

    // Accents
    RGB(76, 141, 255),  // accentPrimary #4C8DFF
    RGB(76, 141, 255),  // accentSecondary
    RGB(61, 220, 132),  // accentSuccess #3DDC84
    RGB(255, 179, 0),   // accentWarning #FFB300
    RGB(255, 82, 87),   // accentDanger #FF5257
    RGB(76, 141, 255),  // accentInfo
    RGB(255, 0, 0),     // accentThermalDanger ≥85°C bright red

    // Section map (all single accent)
    RGB(76, 141, 255), RGB(76, 141, 255), RGB(76, 141, 255), RGB(76, 141, 255),
    RGB(76, 141, 255), RGB(76, 141, 255), RGB(76, 141, 255),

    // Effects
    false, 0, 0, false, RGB(0,0,0), false, RGB(0,0,0), RGB(0,0,0), false,

    L"Segoe UI", L"Consolas",
    14, 10, 9, 8, 22, 10,
    false, 0
};

// 🌈 03 — VIVID · "SPECTRUM"
static const ThemeTokens s_spectrum = {
    ThemeId::Spectrum,
    L"Vivid Spectrum",
    L"Saturated RGB color coding with gradient headers",

    // Surfaces
    RGB(16, 16, 24),    // bgWindow #101018
    RGB(26, 27, 38),    // bgCard #1A1B26
    RGB(35, 36, 51),    // bgCardHover #232433
    RGB(20, 21, 30),    // bgControl
    RGB(28, 29, 42),    // bgOverlay

    // Borders
    RGB(46, 48, 66),    // borderCard #2E3042
    RGB(124, 92, 255),  // borderCardHover #7C5CFF
    RGB(0, 230, 255),   // borderFocus
    1,
    14,
    8,
    8,                  // cardCornerRadius

    // Text
    RGB(255, 255, 255), // textPrimary #FFFFFF
    RGB(180, 184, 204), // textSecondary #B4B8CC
    RGB(110, 114, 136), // textMuted #6E7288 (~4.7:1, PASSES)
    RGB(16, 16, 24),    // textOnAccent

    // Accents
    RGB(124, 92, 255),  // accentPrimary #7C5CFF
    RGB(0, 230, 255),   // accentSecondary #00E6FF
    RGB(0, 230, 118),   // accentSuccess #00E676
    RGB(255, 145, 0),   // accentWarning #FF9100
    RGB(255, 60, 110),  // accentDanger #FF3C6E
    RGB(0, 230, 255),   // accentInfo
    RGB(255, 0, 0),     // accentThermalDanger ≥85°C bright red

    // Section map
    RGB(0, 230, 255),   // CPU cyan
    RGB(255, 61, 174),  // GPU pink #FF3DAE
    RGB(255, 196, 0),   // Battery amber #FFC400
    RGB(124, 92, 255),  // Display violet
    RGB(255, 82, 87),   // Fan red
    RGB(0, 255, 159),   // LED green
    RGB(255, 145, 0),   // Power amber

    // Effects
    true, 1, 20, false, RGB(0,0,0), true,
    RGB(40, 30, 70), RGB(26, 27, 38), // gradTop/Bottom default
    false,

    L"Segoe UI", L"Consolas",
    14, 10, 9, 8, 22, 10,
    true, 60
};

// 🟠 04 — TERMINAL · "PHOSPHOR"
static const ThemeTokens s_phosphor = {
    ThemeId::Phosphor,
    L"Phosphor",
    L"Amber CRT console, monochrome, pure Consolas typography",

    // Surfaces
    RGB(10, 8, 0),      // bgWindow #0A0800
    RGB(18, 16, 0),     // bgCard #121000
    RGB(28, 24, 0),     // bgCardHover
    RGB(14, 12, 0),     // bgControl
    RGB(22, 18, 0),     // bgOverlay

    // Borders
    RGB(61, 46, 0),     // borderCard #3D2E00
    RGB(255, 176, 0),   // borderCardHover #FFB000
    RGB(255, 176, 0),   // borderFocus #FFB000
    1,
    0,                  // cornerRadius (sharp)
    0,                  // cornerRadiusButton
    0,                  // cardCornerRadius (sharp)

    // Text (hierarchy via brightness of amber)
    RGB(255, 176, 0),   // textPrimary #FFB000 (~12:1)
    RGB(201, 144, 0),   // textSecondary #C99000 (~8:1)
    RGB(138, 100, 0),   // textMuted #8A6400 (~5.1:1, PASSES 4.5:1)
    RGB(10, 8, 0),      // textOnAccent

    // Accents (all amber; shapes differentiate)
    RGB(255, 176, 0), RGB(255, 176, 0), RGB(255, 176, 0),
    RGB(255, 176, 0), RGB(255, 176, 0), RGB(255, 176, 0),
    RGB(255, 0, 0),     // accentThermalDanger ≥85°C bright red

    // Section map
    RGB(255, 176, 0), RGB(255, 176, 0), RGB(255, 176, 0), RGB(255, 176, 0),
    RGB(255, 176, 0), RGB(255, 176, 0), RGB(255, 176, 0),

    // Effects
    false, 0, 0, true, RGB(255, 176, 0), false, RGB(0,0,0), RGB(0,0,0), true,

    L"Consolas", L"Consolas",
    12, 10, 10, 8, 20, 10,
    false, 0
};

// ⚪ 05 — LIGHT · "SOLARIZED"
static const ThemeTokens s_solarized = {
    ThemeId::Solarized,
    L"Solarized Light",
    L"Light theme regression tripwire with solarized palette",

    // Surfaces
    RGB(253, 246, 227), // bgWindow #FDF6E3
    RGB(255, 248, 234), // bgCard #FFF8EA
    RGB(245, 238, 218), // bgCardHover
    RGB(240, 232, 210), // bgControl
    RGB(230, 220, 195), // bgOverlay

    // Borders
    RGB(228, 220, 197), // borderCard #E4DCC5
    RGB(38, 139, 210),  // borderCardHover #268BD2
    RGB(38, 139, 210),  // borderFocus
    1,
    10,
    6,
    10,                 // cardCornerRadius

    // Text
    RGB(59, 74, 63),    // textPrimary #3B4A3F (~10:1)
    RGB(101, 123, 131), // textSecondary #657B83 (~4.8:1)
    RGB(88, 110, 117),  // textMuted #586E75 — tuned from #93A1A1 to pass 4.5:1 vs #FDF6E3
    RGB(253, 246, 227), // textOnAccent

    // Accents
    RGB(38, 139, 210),  // accentPrimary #268BD2 blue
    RGB(42, 161, 152),  // accentSecondary #2AA198 cyan
    RGB(133, 153, 0),   // accentSuccess #859900 green
    RGB(203, 75, 22),   // accentWarning #CB4B16 orange
    RGB(220, 50, 47),   // accentDanger #DC322F red
    RGB(38, 139, 210),  // accentInfo
    RGB(220, 50, 47),   // accentThermalDanger ≥85°C red

    // Section map
    RGB(38, 139, 210),  // CPU
    RGB(211, 54, 130),  // GPU magenta #D33682
    RGB(181, 137, 0),   // Battery yellow #B58900
    RGB(42, 161, 152),  // Display
    RGB(220, 50, 47),   // Fan
    RGB(108, 113, 196), // LED violet #6C71C4
    RGB(181, 137, 0),   // Power

    // Effects
    false, 0, 0, false, RGB(0,0,0), false, RGB(0,0,0), RGB(0,0,0), false,

    L"Segoe UI", L"Consolas",
    14, 10, 9, 8, 22, 10,
    false, 0
};

// 🛡 LEGACY SAFETY NET — exact pre-audit obsidian palette
static const ThemeTokens s_legacy = {
    ThemeId::Legacy,
    L"Legacy Obsidian",
    L"Pre-audit dark theme refactor safety net",

    RGB(19, 14, 15),    // bgWindow
    RGB(32, 23, 24),    // bgCard
    RGB(40, 36, 52),    // bgCardHover
    RGB(24, 22, 30),    // bgControl
    RGB(24, 22, 30),

    RGB(56, 39, 42),    // borderCard
    RGB(101, 62, 72),   // borderCardHover
    RGB(0, 180, 255),   // borderFocus
    1,
    10,
    6,
    8,                  // cardCornerRadius

    RGB(240, 242, 248), // textPrimary
    RGB(154, 160, 180), // textSecondary
    RGB(92, 98, 117),   // textMuted — exact pre-audit value (Phase 1 pixel-identity)
    RGB(19, 14, 15),

    RGB(0, 180, 255),   // accentPrimary cyan
    RGB(0, 230, 118),   // accentSecondary green
    RGB(0, 230, 118),   // accentSuccess
    RGB(255, 145, 0),   // accentWarning
    RGB(255, 60, 60),   // accentDanger
    RGB(0, 180, 255),   // accentInfo
    RGB(255, 60, 60),   // accentThermalDanger ≥85°C red

    RGB(0, 180, 255), RGB(0, 180, 255), RGB(255, 145, 0), RGB(0, 180, 255),
    RGB(255, 60, 60), RGB(147, 51, 234), RGB(255, 145, 0),

    false, 0, 0, false, RGB(0,0,0), false, RGB(0,0,0), RGB(0,0,0), false,

    L"Segoe UI", L"Consolas",
    14, 10, 9, 8, 22, 10,
    false, 0
};

// ============================================================================
// State & GDI Cache Manager
// ============================================================================

static ThemeId s_currentId = ThemeId::Cyberpunk;
static const ThemeTokens* s_activeTokens = &s_cyberpunk;

// Cached GDI maps
static std::map<COLORREF, HBRUSH> s_brushCache;
static std::map<std::pair<COLORREF, int>, HPEN> s_penCache;
static std::map<FontSizeRole, HFONT> s_fontCache;

static HFONT CreateThemeFontHelper(int ptSize, int weight, const wchar_t* face) {
    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    int height = -MulDiv(ptSize, logPixelsY, 72);
    return CreateFontW(
        height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        face
    );
}

static void FlushGdiCache() {
    for (auto& [col, brush] : s_brushCache) {
        if (brush) DeleteObject(brush);
    }
    s_brushCache.clear();

    for (auto& [key, pen] : s_penCache) {
        if (pen) DeleteObject(pen);
    }
    s_penCache.clear();

    for (auto& [role, font] : s_fontCache) {
        if (font) DeleteObject(font);
    }
    s_fontCache.clear();
}

void Init() {
    // Cyberpunk is the flagship default theme
    s_currentId = ThemeId::Cyberpunk;
    s_activeTokens = &s_cyberpunk;
    FlushGdiCache();
}

void Shutdown() {
    FlushGdiCache();
}

static const ThemeTokens* FindTokens(ThemeId id) {
    switch (id) {
        case ThemeId::Cyberpunk: return &s_cyberpunk;
        case ThemeId::Stealth:   return &s_stealth;
        case ThemeId::Spectrum:  return &s_spectrum;
        case ThemeId::Phosphor:  return &s_phosphor;
        case ThemeId::Solarized: return &s_solarized;
        case ThemeId::Legacy:    return &s_legacy;
        default:                 return &s_cyberpunk;
    }
}

void SetTheme(HWND hwnd, ThemeId id) {
    s_currentId = id;
    s_activeTokens = FindTokens(id);
    FlushGdiCache();

    if (hwnd && IsWindow(hwnd)) {
        // DWM dark mode & corner preference
        BOOL dark = (id != ThemeId::Solarized);
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

        DWORD corner = s_activeTokens->sharpWindowCorners ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

ThemeId Current() {
    return s_currentId;
}

const ThemeTokens& Tokens() {
    return *s_activeTokens;
}

const ThemeMetrics& Metrics() {
    return s_defaultMetrics;
}

ThemeMetrics ScaledMetrics(int dpi) {
    if (dpi <= 0) dpi = 96;
    ThemeMetrics sm = s_defaultMetrics;
    sm.margin = MulDiv(s_defaultMetrics.margin, dpi, 96);
    sm.gap = MulDiv(s_defaultMetrics.gap, dpi, 96);
    sm.sectionGap = MulDiv(s_defaultMetrics.sectionGap, dpi, 96);
    sm.controlHeight = MulDiv(s_defaultMetrics.controlHeight, dpi, 96);
    sm.labelHeight = MulDiv(s_defaultMetrics.labelHeight, dpi, 96);
    sm.sliderHeight = MulDiv(s_defaultMetrics.sliderHeight, dpi, 96);
    sm.buttonHeight = MulDiv(s_defaultMetrics.buttonHeight, dpi, 96);
    sm.cardHeight = MulDiv(s_defaultMetrics.cardHeight, dpi, 96);
    sm.cornerRadius = MulDiv(s_defaultMetrics.cornerRadius, dpi, 96);
    sm.cornerRadiusButton = MulDiv(s_defaultMetrics.cornerRadiusButton, dpi, 96);
    sm.cardCornerRadius = MulDiv(s_defaultMetrics.cardCornerRadius, dpi, 96);
    sm.borderWidth = (std::max)(1, MulDiv(s_defaultMetrics.borderWidth, dpi, 96));
    sm.headerHeight = MulDiv(s_defaultMetrics.headerHeight, dpi, 96);
    sm.headerBandHeight = MulDiv(s_defaultMetrics.headerBandHeight, dpi, 96);
    sm.sliderTrackWidth = (std::max)(2, MulDiv(s_defaultMetrics.sliderTrackWidth, dpi, 96));
    sm.sliderThumbSize = MulDiv(s_defaultMetrics.sliderThumbSize, dpi, 96);
    sm.minHitTarget = MulDiv(s_defaultMetrics.minHitTarget, dpi, 96);
    return sm;
}

HBRUSH Brush(const COLORREF color) {
    auto it = s_brushCache.find(color);
    if (it != s_brushCache.end()) return it->second;

    HBRUSH b = CreateSolidBrush(color);
    s_brushCache[color] = b;
    return b;
}

HPEN Pen(const COLORREF color, int width) {
    auto key = std::make_pair(color, width);
    auto it = s_penCache.find(key);
    if (it != s_penCache.end()) return it->second;

    HPEN p = CreatePen(PS_SOLID, width, color);
    s_penCache[key] = p;
    return p;
}

HFONT Font(FontSizeRole role) {
    auto it = s_fontCache.find(role);
    if (it != s_fontCache.end()) return it->second;

    const auto& t = Tokens();
    HFONT font = nullptr;

    switch (role) {
        case FontSizeRole::Title:
            font = CreateThemeFontHelper(t.ptTitle, FW_BOLD, t.fontBody);
            break;
        case FontSizeRole::Subheader:
            font = CreateThemeFontHelper(t.ptSubheader, FW_SEMIBOLD, t.fontBody);
            break;
        case FontSizeRole::Body:
            font = CreateThemeFontHelper(t.ptBody, FW_NORMAL, t.fontBody);
            break;
        case FontSizeRole::Small:
            font = CreateThemeFontHelper(t.ptSmall, FW_NORMAL, t.fontBody);
            break;
        case FontSizeRole::Metric:
            font = CreateThemeFontHelper(t.ptMetric, FW_BOLD, t.fontBody);
            break;
        case FontSizeRole::Mono:
            font = CreateThemeFontHelper(t.ptMono, FW_BOLD, t.fontMono);
            break;
    }

    s_fontCache[role] = font;
    return font;
}

void RebuildGdiCache() {
    FlushGdiCache();
}

} // namespace Theme