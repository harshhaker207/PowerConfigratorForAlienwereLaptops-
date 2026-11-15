#include "ModernTheme.h"
#include "Theme.h"
#include <cwchar>

namespace ModernTheme {

[[maybe_unused]] static HFONT CreateThemeFont(int pointSize, int weight, const wchar_t* faceName) {
    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    int height = -MulDiv(pointSize, logPixelsY, 72);
    return CreateFontW(
        height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        faceName
    );
}

bool EnableWindowDarkMode(HWND hwnd, bool enable) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    BOOL darkMode = enable ? TRUE : FALSE;

    // Windows 10 19041+ and Windows 11
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    if (FAILED(hr)) {
        // Fallback for Windows 10 18362/18363
        DWORD attr19 = 19;
        DwmSetWindowAttribute(hwnd, attr19, &darkMode, sizeof(darkMode));
    }

    // Modern rounded window corners (Windows 11) - use theme preference
    const auto& tokens = Theme::Tokens();
    DWORD corner = tokens.sharpWindowCorners ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return true;
}

bool SetWindowCornerPreference(HWND hwnd, DWORD cornerPreference) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
    return SUCCEEDED(hr);
}

bool ApplyModernWindowStyle(HWND hwnd) {
    bool ok1 = EnableWindowDarkMode(hwnd, true);
    bool ok2 = SetWindowCornerPreference(hwnd, DWMWCP_ROUND);
    return ok1 || ok2;
}

// Cyberpunk: Draw 3-layer concentric glow (outer->inner: full, 2/3, 1/3 radius+offset)
void DrawGlow(HDC hdc, RECT rect, int baseRadius, COLORREF glowColor, int layers, int step) {
    if (!layers || glowColor == CLR_INVALID) return;
    for (int i = layers; i >= 1; --i) {
        int r = baseRadius + i * 2;
        int darken = step * (layers - i);
        COLORREF layerColor = RGB(
            (std::max)(0, GetRValue(glowColor) - darken),
            (std::max)(0, GetGValue(glowColor) - darken),
            (std::max)(0, GetBValue(glowColor) - darken)
        );
        HPEN pen = CreatePen(PS_SOLID, 1, layerColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        if (r > 0) {
            RoundRect(hdc, rect.left - i * 2, rect.top - i * 2, rect.right + i * 2, rect.bottom + i * 2, r * 2, r * 2);
        } else {
            Rectangle(hdc, rect.left - i * 2, rect.top - i * 2, rect.right + i * 2, rect.bottom + i * 2);
        }
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

void FillRoundedRect(HDC hdc, RECT rect, int radius, COLORREF fillColor, COLORREF borderColor, int borderWidth) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    if (radius < 0) radius = 0;

    HBRUSH brush = CreateSolidBrush(fillColor);
    HPEN pen = (borderWidth > 0 && borderColor != CLR_INVALID) ?
               CreatePen(PS_SOLID, borderWidth, borderColor) :
               (HPEN)GetStockObject(NULL_PEN);

    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    if (radius > 0) {
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    } else {
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);

    if (pen != GetStockObject(NULL_PEN)) DeleteObject(pen);
    DeleteObject(brush);
}

// Cyberpunk: Scanline pattern overlay disabled to preserve crisp text and typography
void DrawScanlines(HDC /*hdc*/, RECT /*rect*/, const Theme::ThemeTokens& /*tokens*/) {
    return;
}

// Cyberpunk: Draw L-bracket corner ticks (2px legs) at rect corners
void DrawCornerTicks(HDC hdc, RECT rect, COLORREF color, bool topLeft, bool bottomRight) {
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    int leg = 8;

    if (topLeft) {
        // Top-left: L shape
        MoveToEx(hdc, rect.left, rect.top + leg, nullptr);
        LineTo(hdc, rect.left, rect.top);
        LineTo(hdc, rect.left + leg, rect.top);
    }
    if (bottomRight) {
        // Bottom-right: reverse L
        MoveToEx(hdc, rect.right, rect.bottom - leg, nullptr);
        LineTo(hdc, rect.right, rect.bottom);
        LineTo(hdc, rect.right - leg, rect.bottom);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void DrawModernButton(HDC hdc, RECT rect, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, COLORREF accentColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    COLORREF bg, border, textColor;

    if (isAccent) {
        if (isPressed) {
            // Darken accent by 20% for pressed state
            bg = RGB(
                GetRValue(accentColor) * 4 / 5,
                GetGValue(accentColor) * 4 / 5,
                GetBValue(accentColor) * 4 / 5
            );
            border = accentColor;
            textColor = tokens.textOnAccent;
        } else if (isHovered) {
            // Lighten accent by +30 for hover state (clamped to 255)
            bg = RGB(
                (std::min)(255, (int)GetRValue(accentColor) + 30),
                (std::min)(255, (int)GetGValue(accentColor) + 30),
                (std::min)(255, (int)GetBValue(accentColor) + 30)
            );
            border = tokens.textPrimary;
            textColor = tokens.textOnAccent;
        } else {
            bg = accentColor;
            border = accentColor;
            textColor = tokens.textOnAccent;
        }
    } else {
        if (isPressed) {
            bg = tokens.bgControl;
            border = accentColor;
            textColor = tokens.textPrimary;
        } else if (isHovered) {
            bg = tokens.bgCardHover;
            border = tokens.borderCardHover;
            textColor = tokens.textPrimary;
        } else {
            bg = tokens.bgCard;
            border = tokens.borderCard;
            textColor = tokens.textPrimary;
        }
    }

    FillRoundedRect(hdc, rect, tokens.cornerRadiusButton, bg, border, 1);

    // Cyberpunk: glow on hover for accent buttons
    if (tokens.glowEnabled && tokens.id == Theme::ThemeId::Cyberpunk && isAccent && isHovered && !isPressed) {
        DrawGlow(hdc, rect, tokens.cornerRadiusButton, accentColor, tokens.glowLayers, tokens.glowStep);
    }

    if (text && *text) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        int btnW = rect.right - rect.left;
        HFONT font = (btnW < 80) ? Theme::Font(Theme::FontSizeRole::Small) : Theme::Font(Theme::FontSizeRole::Body);
        HGDIOBJ oldFont = SelectObject(hdc, font);

        RECT textRect = rect;
        if (isPressed) {
            OffsetRect(&textRect, 1, 1);
        }
        DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, oldFont);
    }
}

void DrawModernPillToggle(HDC hdc, RECT rect, const wchar_t* leftText, const wchar_t* rightText, bool isRightSelected, COLORREF accentColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    int radius = (rect.bottom - rect.top) / 2;
    if (radius > 8) radius = 8;

    // Background container
    FillRoundedRect(hdc, rect, radius, tokens.bgWindow, tokens.borderCard, 1);

    int midX = rect.left + (rect.right - rect.left) / 2;
    int pad = 2;
    RECT leftRect = { rect.left + pad, rect.top + pad, midX - 1, rect.bottom - pad };
    RECT rightRect = { midX + 1, rect.top + pad, rect.right - pad, rect.bottom - pad };

    int pillRadius = radius > 2 ? radius - 2 : 2;

    SetBkMode(hdc, TRANSPARENT);
    int halfW = midX - rect.left;
    HFONT font = (halfW < 130) ? Theme::Font(Theme::FontSizeRole::Small) : Theme::Font(Theme::FontSizeRole::Body);
    HGDIOBJ oldFont = SelectObject(hdc, font);

    if (!isRightSelected) {
        // Selected left option
        FillRoundedRect(hdc, leftRect, pillRadius, accentColor, accentColor, 0);
        // Cyberpunk: glow on active profile
        if (tokens.glowEnabled && tokens.id == Theme::ThemeId::Cyberpunk) {
            DrawGlow(hdc, leftRect, pillRadius, accentColor, tokens.glowLayers, tokens.glowStep);
        }
        SetTextColor(hdc, tokens.textOnAccent);
        DrawTextW(hdc, leftText, -1, &leftRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Inactive right option
        SetTextColor(hdc, tokens.textSecondary);
        DrawTextW(hdc, rightText, -1, &rightRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Cyberpunk: corner ticks on active half
        if (tokens.id == Theme::ThemeId::Cyberpunk) {
            DrawCornerTicks(hdc, leftRect, tokens.accentSecondary, true, true);
        }
    } else {
        // Inactive left option
        SetTextColor(hdc, tokens.textSecondary);
        DrawTextW(hdc, leftText, -1, &leftRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Selected right option
        FillRoundedRect(hdc, rightRect, pillRadius, accentColor, accentColor, 0);
        // Cyberpunk: glow on active profile
        if (tokens.glowEnabled && tokens.id == Theme::ThemeId::Cyberpunk) {
            DrawGlow(hdc, rightRect, pillRadius, accentColor, tokens.glowLayers, tokens.glowStep);
        }
        SetTextColor(hdc, tokens.textOnAccent);
        DrawTextW(hdc, rightText, -1, &rightRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Cyberpunk: corner ticks on active half
        if (tokens.id == Theme::ThemeId::Cyberpunk) {
            DrawCornerTicks(hdc, rightRect, tokens.accentSecondary, true, true);
        }
    }

    SelectObject(hdc, oldFont);
}

void DrawProgressBar(HDC hdc, RECT rect, double percent, COLORREF barColor, COLORREF bgColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    if (percent > 1.0) percent /= 100.0;
    if (percent < 0.0) percent = 0.0;
    if (percent > 1.0) percent = 1.0;

    int height = rect.bottom - rect.top;
    int radius = height / 2;
    if (radius > 4) radius = 4;

    // Track
    FillRoundedRect(hdc, rect, radius, bgColor, tokens.borderCard, 1);

    int totalWidth = (rect.right - rect.left) - 2;
    int fillWidth = static_cast<int>(totalWidth * percent);
    if (fillWidth > 2) {
        RECT fillRect = { rect.left + 1, rect.top + 1, rect.left + 1 + fillWidth, rect.bottom - 1 };
        int fillRadius = radius > 1 ? radius - 1 : 1;
        FillRoundedRect(hdc, fillRect, fillRadius, barColor, barColor, 0);
    }
}

void DrawStatusBadge(HDC hdc, RECT rect, const wchar_t* text, COLORREF dotColor, COLORREF badgeBg, COLORREF textColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    int height = rect.bottom - rect.top;
    int radius = height / 2;

    // Pill badge background
    FillRoundedRect(hdc, rect, radius, badgeBg, tokens.borderCard, 1);

    // Glowing status dot
    int dotSize = (height >= 22) ? 8 : 6;
    int dotX = rect.left + 8;
    int dotY = rect.top + (height - dotSize) / 2;

    HBRUSH dotBrush = CreateSolidBrush(dotColor);
    HPEN dotPen = CreatePen(PS_SOLID, 1, dotColor);
    HGDIOBJ oldBrush = SelectObject(hdc, dotBrush);
    HGDIOBJ oldPen = SelectObject(hdc, dotPen);

    Ellipse(hdc, dotX, dotY, dotX + dotSize, dotY + dotSize);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(dotPen);
    DeleteObject(dotBrush);

    // Badge label
    if (text && *text) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, textColor);
        HGDIOBJ oldFont = SelectObject(hdc, Theme::Font(Theme::FontSizeRole::Small));

        RECT textRect = { dotX + dotSize + 6, rect.top, rect.right - 8, rect.bottom };
        DrawTextW(hdc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, oldFont);
    }
}

void DrawModernCard(HDC hdc, RECT rect, const wchar_t* title, bool isHovered) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    COLORREF border = isHovered ? tokens.borderCardHover : tokens.borderCard;
    COLORREF bg = isHovered ? tokens.bgCardHover : tokens.bgCard;
    int borderWidth = isHovered ? 2 : 1;

    FillRoundedRect(hdc, rect, tokens.cornerRadius, bg, border, borderWidth);

    // Cyberpunk: glow on hover
    if (tokens.glowEnabled && tokens.id == Theme::ThemeId::Cyberpunk && isHovered) {
        DrawGlow(hdc, rect, tokens.cornerRadius, tokens.borderCardHover, tokens.glowLayers, tokens.glowStep);
    }

    if (title && *title) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, tokens.textPrimary);
        HGDIOBJ oldFont = SelectObject(hdc, Theme::Font(Theme::FontSizeRole::Subheader));

        RECT titleRect = { rect.left + 14, rect.top + 8, rect.right - 14, rect.top + 28 };
        DrawTextW(hdc, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, oldFont);
    }
}

void DrawTelemetryMetric(HDC hdc, RECT rect, const wchar_t* label, const wchar_t* value, const wchar_t* unit, COLORREF accentColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    // Small rounded card for metric
    FillRoundedRect(hdc, rect, tokens.cardCornerRadius, tokens.bgCard, tokens.borderCard, 1);

    SetBkMode(hdc, TRANSPARENT);

    // Left accent vertical bar indicator
    RECT barRect = { rect.left + 3, rect.top + 6, rect.left + 6, rect.bottom - 6 };
    FillRoundedRect(hdc, barRect, 1, accentColor, accentColor, 0);

    // Label at top
    if (label && *label) {
        SetTextColor(hdc, tokens.textMuted);
        HGDIOBJ oldFont = SelectObject(hdc, Theme::Font(Theme::FontSizeRole::Small));
        RECT lblRect = { rect.left + 12, rect.top + 3, rect.right - 8, rect.top + 16 };
        DrawTextW(hdc, label, -1, &lblRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }

    // Value and Unit
    if (value && *value) {
        SetTextColor(hdc, tokens.textPrimary);
        HGDIOBJ oldFont = SelectObject(hdc, Theme::Font(Theme::FontSizeRole::Mono));
        RECT valRect = { rect.left + 12, rect.top + 18, rect.right - 8, rect.bottom - 4 };

        wchar_t fullText[64];
        if (unit && *unit) {
            swprintf_s(fullText, L"%s %s", value, unit);
        } else {
            swprintf_s(fullText, L"%s", value);
        }
        DrawTextW(hdc, fullText, -1, &valRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);
    }
}

void DrawModernSectionHeader(HDC hdc, RECT rect, const wchar_t* title, const wchar_t* icon, COLORREF accentColor) {
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;

    const auto& tokens = Theme::Tokens();
    SetBkMode(hdc, TRANSPARENT);

    // Use theme accent if no explicit color provided
    if (accentColor == 0) accentColor = tokens.accentPrimary;

    // Accent pill on left
    int h = rect.bottom - rect.top;
    int dotH = (std::max)(12, h * 65 / 100);
    int dotY = rect.top + (h - dotH) / 2;
    RECT indicatorRect = { rect.left, dotY, rect.left + 4, dotY + dotH };
    FillRoundedRect(hdc, indicatorRect, 2, accentColor, accentColor, 0);

    // Title text with "// " prefix for Cyberpunk theme
    wchar_t headerText[128];
    if (tokens.id == Theme::ThemeId::Cyberpunk) {
        swprintf_s(headerText, L"// %s", title);
    } else if (icon && *icon) {
        swprintf_s(headerText, L"%s  %s", icon, title);
    } else {
        swprintf_s(headerText, L"%s", title);
    }

    SetTextColor(hdc, tokens.textPrimary);
    HGDIOBJ oldFont = SelectObject(hdc, Theme::Font(Theme::FontSizeRole::Subheader));
    RECT titleRect = { rect.left + 12, rect.top, rect.right, rect.bottom };
    DrawTextW(hdc, headerText, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldFont);

    if (tokens.id == Theme::ThemeId::Cyberpunk) {
        // Underline: 1px, fades accent -> transparent (draw gradient strip)
        HPEN pen = CreatePen(PS_SOLID, 1, accentColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, rect.left, rect.bottom - 1, nullptr);
        LineTo(hdc, rect.right, rect.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
}

} // namespace ModernTheme