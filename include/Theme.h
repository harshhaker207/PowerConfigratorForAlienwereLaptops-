#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

// ============================================================================
// PowerConfigurator — Theme Token Engine
//
// Every color, radius, border width, glow layer, font size and animation
// parameter used by the UI lives here, owned by exactly one translation unit
// (src/Theme.cpp). The UI layer reads only through Theme::Tokens() /
// Theme::Brush()/Pen()/Font(). This closes audit findings F-01 (colors defined
// in three places), F-03 (DrawModernCard reads a global palette) and B-14
// (raw RGB() literals scattered through UI.cpp / ModernTheme.cpp).
//
// The token schema is the contract every theme must fill; see UI_AUDIT.md 3.2.
// ============================================================================

namespace Theme {

// ----------------------------------------------------------------------------
// Identity
// ----------------------------------------------------------------------------
enum class ThemeId : int {
    Cyberpunk   = 0,   // "NEON DISTRICT"  — flagship, default
    Stealth     = 1,   // "STEALTH"
    Spectrum    = 2,   // "VIVID SPECTRUM"
    Phosphor    = 3,   // "PHOSPHOR"
    Solarized   = 4,   // "SOLARIZED" (light)
    Legacy      = 5,   // exact match of the pre-audit obsidian palette —
                        // used as the refactor safety net in Phase 1
};

// ----------------------------------------------------------------------------
// Geometry / layout metrics (theme-owned, but shared by the layout engine)
// ----------------------------------------------------------------------------
struct ThemeMetrics {
    int margin;               // outer content margin
    int gap;                  // gap between sibling controls
    int sectionGap;           // gap between sections
    int controlHeight;        // height of a control row
    int labelHeight;          // height of a label
    int sliderHeight;         // height of a slider row
    int buttonHeight;         // min hit target / button height (>=44px)
    int cardHeight;           // telemetry card height
    int cornerRadius;         // card corner radius (0 = sharp)
    int cornerRadiusButton;   // button corner radius
    int cardCornerRadius;     // telemetry card corner radius
    int borderWidth;         // card border width at 96 DPI
    int headerHeight;         // section header band height
    int headerBandHeight;     // gradient header band height (spectrum)
    int sliderTrackWidth;     // custom slider track thickness
    int sliderThumbSize;      // custom slider thumb size
    int glowLayers;           // 0..3 concentric glow strokes
    int glowStep;             // RGB darkening per glow layer
    int minHitTarget;         // minimum accessible hit target (>=40px)
};

// ----------------------------------------------------------------------------
// Font roles — maps to one cached HFONT per theme
// ----------------------------------------------------------------------------
enum class FontSizeRole : int {
    Title,
    Subheader,
    Body,
    Small,
    Metric,   // large telemetry value font
    Mono,     // Consolas telemetry
};

// ----------------------------------------------------------------------------
// Token schema — the contract every theme fills
// ----------------------------------------------------------------------------
struct ThemeTokens {
    ThemeId       id;
    const wchar_t* displayName;
    const wchar_t* tagline;

    // Surfaces
    COLORREF bgWindow;
    COLORREF bgCard;
    COLORREF bgCardHover;
    COLORREF bgControl;
    COLORREF bgOverlay;

    // Borders & strokes
    COLORREF borderCard;
    COLORREF borderCardHover;
    COLORREF borderFocus;
    int      borderWidth;
    int      cornerRadius;
    int      cornerRadiusButton;
    int      cardCornerRadius;

    // Text
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF textMuted;
    COLORREF textOnAccent;

    // Accents (semantic)
    COLORREF accentPrimary;
    COLORREF accentSecondary;
    COLORREF accentSuccess;
    COLORREF accentWarning;
    COLORREF accentDanger;
    COLORREF accentInfo;
    COLORREF accentThermalDanger;  // ≥85°C thermal danger tier

    // Section accent mapping (per-card color coding)
    COLORREF accentCpu, accentGpu, accentBattery, accentDisplay,
             accentFan, accentLed, accentPower;

    // Effects (GDI-emulated, see UI_AUDIT.md 5)
    bool     glowEnabled;
    int      glowLayers;
    int      glowStep;
    bool     scanlines;
    COLORREF scanlineColor;
    bool     gradients;
    COLORREF gradTop, gradBottom;
    bool     sharpWindowCorners;

    // Typography
    const wchar_t* fontBody;
    const wchar_t* fontMono;
    int      ptTitle, ptSubheader, ptBody, ptSmall, ptMetric, ptMono;

    // Motion
    bool     animationEnabled;
    UINT     animTimerMs;   // 0 = none (idle-CPU guard)
};

// ----------------------------------------------------------------------------
// ThemeManager — public API
// ----------------------------------------------------------------------------
void              Init();                       // create default-theme GDI cache
void              Shutdown();                   // release cached brushes/pens/fonts
void              SetTheme(HWND hwnd, ThemeId id);      // swap tokens, flush cache, InvalidateRect
ThemeId           Current();
const ThemeTokens& Tokens();
const ThemeMetrics& Metrics();
ThemeMetrics      ScaledMetrics(int dpi = 96);

// Cached GDI resources per theme (created lazily, freed on switch):
HBRUSH  Brush(const COLORREF);
HPEN    Pen(const COLORREF, int width = 1);
HFONT   Font(FontSizeRole role);

// Rebuild the per-theme font/brush cache after a DPI change (B-03).
void              RebuildGdiCache();

// Persistence key used by UI::SaveSettings / LoadSettings.
inline const wchar_t* ThemeRegKey() { return L"ThemeIndex"; }

} // namespace Theme