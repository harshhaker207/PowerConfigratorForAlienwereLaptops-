#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include "Theme.h"

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_ROUND
#define DWMWCP_DEFAULT    0
#define DWMWCP_DONOTROUND 1
#define DWMWCP_ROUND      2
#define DWMWCP_ROUNDSMALL 3
#endif

namespace ModernTheme {

// ============================================================================
// Window & DWM Styling
// ============================================================================
bool EnableWindowDarkMode(HWND hwnd, bool enable = true);
bool SetWindowCornerPreference(HWND hwnd, DWORD cornerPreference = DWMWCP_ROUND);
bool ApplyModernWindowStyle(HWND hwnd);

// ============================================================================
// Helper Drawing Functions (Specified in Toolkit Specification)
// All colors now come from Theme::Tokens() — no hardcoded palette here.
// ============================================================================
void FillRoundedRect(HDC hdc, RECT rect, int radius, COLORREF fillColor, COLORREF borderColor, int borderWidth = 1);

void DrawModernButton(HDC hdc, RECT rect, const wchar_t* text, bool isHovered, bool isPressed, bool isAccent, COLORREF accentColor);

void DrawModernPillToggle(HDC hdc, RECT rect, const wchar_t* leftText, const wchar_t* rightText, bool isRightSelected, COLORREF accentColor);

void DrawProgressBar(HDC hdc, RECT rect, double percent, COLORREF barColor, COLORREF bgColor);

void DrawStatusBadge(HDC hdc, RECT rect, const wchar_t* text, COLORREF dotColor, COLORREF badgeBg, COLORREF textColor);

void DrawModernCard(HDC hdc, RECT rect, const wchar_t* title = nullptr, bool isHovered = false);

void DrawTelemetryMetric(HDC hdc, RECT rect, const wchar_t* label, const wchar_t* value, const wchar_t* unit, COLORREF accentColor);

void DrawModernSectionHeader(HDC hdc, RECT rect, const wchar_t* title, const wchar_t* icon = nullptr, COLORREF accentColor = 0);

// Cyberpunk-specific effects
void DrawScanlines(HDC hdc, RECT rect, const Theme::ThemeTokens& tokens);
void DrawGlow(HDC hdc, RECT rect, int baseRadius, COLORREF glowColor, int layers, int step);
void DrawCornerTicks(HDC hdc, RECT rect, COLORREF color, bool topLeft, bool bottomRight);

} // namespace ModernTheme