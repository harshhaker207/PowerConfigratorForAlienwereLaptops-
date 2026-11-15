#include "UI.h"
#include "Theme.h"
#include "ModernTheme.h"
#include "PowerManager.h"
#include "GpuManager.h"
#include "CpuManager.h"
#include "DisplayManager.h"
#include "WindowsPowerManager.h"
#include "OptimusManager.h"
#include "LedManager.h"
#include "FanManager.h"
#include "Monitor.h"
#include "AudioVisualizer.h"
#include "ScreenAmbient.h"
#include <shellapi.h>
#include <strsafe.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

static UI* g_uiInstance = nullptr;

// Static message ID for TaskbarCreated (re-register tray icon after Explorer restart)
static UINT s_msgTaskbarCreated = 0;

UI::UI() = default;

// Forward declaration for DPI change handling
static void OnDpiChanged(HWND hwnd, UINT dpi);

UI::~UI() {
    Shutdown();
}

bool UI::Initialize(HINSTANCE hInstance, int nCmdShow,
                    PowerManager* powerMgr, GpuManager* gpuMgr,
                    CpuManager* cpuMgr, DisplayManager* displayMgr,
                    WindowsPowerManager* winPowerMgr, OptimusManager* optimusMgr,
                    LedManager* ledMgr, FanManager* fanMgr, Monitor* monitor) {
    m_hInstance = hInstance;
    m_powerMgr = powerMgr;
    m_gpuMgr = gpuMgr;
    m_cpuMgr = cpuMgr;
    m_displayMgr = displayMgr;
    m_winPowerMgr = winPowerMgr;
    m_optimusMgr = optimusMgr;
    m_ledMgr = ledMgr;
    m_fanMgr = fanMgr;
    m_monitor = monitor;

    // B-03: Per-monitor v2 DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HDC hdc = GetDC(nullptr);
    m_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    m_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);

    // Initialize Theme engine (Legacy default for Phase 1 pixel-identity, will be overridden by LoadSettings)
    Theme::Init();

    // Initialize custom LED colors from theme tokens (replaces hardcoded RGB)
    const auto& tokens = Theme::Tokens();
    m_customLedColor = tokens.accentLed;
    m_customSecColor = tokens.accentInfo;

    // B-05: Register TaskbarCreated message for tray icon resilience
    s_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_HOTKEY_CLASS | ICC_UPDOWN_CLASS | ICC_LINK_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    // Cached GDI resources now come from Theme::Brush()/Pen() - legacy brushes removed.
    // Kept only for one-off drawing that hasn't been fully migrated yet.
    m_hBgBrush = nullptr;
    m_hCardBrush = nullptr;
    m_hCardHoverBrush = nullptr;
    m_hCardBorderPen = nullptr;

    if (!RegisterWindowClass(hInstance)) return false;
    if (!CreateMainWindow(hInstance, nCmdShow)) return false;

    CreateControls();
    CreateTrayIcon();

    m_isPerformanceMode = (m_powerMgr && m_powerMgr->GetCurrentProfileType() == ProfileType::Performance);
    UpdateProfileToggle(m_powerMgr ? m_powerMgr->GetCurrentProfileType() : ProfileType::Battery);

    LoadSettings();

    // §15: Apply loaded borderless widget state (compact mode, always-on-top)
    if (m_compactMode) {
        if (m_btnCompact) {
            SendMessage(m_btnCompact, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"EXPAND"));
        }
        RECT client;
        GetClientRect(m_hwnd, &client);
        int newWidth = ScaleX(340);
        int newHeight = ScaleY(138);
        SetWindowPos(m_hwnd, nullptr, 0, 0, newWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        const auto& metrics = Theme::ScaledMetrics(m_dpiX);
        m_layout = Layout::Compute(newWidth, newHeight, metrics, true, m_dpiX);
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
    if (m_alwaysOnTop) {
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    if (m_powerMgr) {
        const ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfile() : m_powerMgr->GetBatteryProfile();
        UpdateSlidersFromProfile(profile);
        UpdateRefreshRateControls(profile);
        UpdateWindowsPowerControls(profile);
    }

    if (m_monitor) {
        m_monitor->Start();
    }

    return true;
}

void UI::Shutdown() {
    DestroyTrayIcon();
    if (m_monitor) {
        m_monitor->Stop();
    }

    // Clean up legacy GDI resources
    if (m_fontLarge) { DeleteObject(m_fontLarge); m_fontLarge = nullptr; }
    if (m_hCardHoverBrush) { DeleteObject(m_hCardHoverBrush); m_hCardHoverBrush = nullptr; }
    if (m_hCardBorderPen) { DeleteObject(m_hCardBorderPen); m_hCardBorderPen = nullptr; }

    // Clean up Theme engine GDI cache
    Theme::Shutdown();

    SaveSettings();
}

int UI::RunMessageLoop() {
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // B-02: IsDialogMessage for keyboard navigation (Tab, Enter, Escape, arrow keys)
        if (!IsDialogMessage(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

bool UI::RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = StaticWndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof(long);
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (!wcex.hIcon) wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Background brush comes from Theme engine at runtime; null here to avoid
    // double-erase since we double-buffer in WM_PAINT and return 1 from WM_ERASEBKGND.
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"PowerConfiguratorWindow";
    wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    if (!wcex.hIconSm) wcex.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    return RegisterClassExW(&wcex) != 0;
}

LRESULT CALLBACK UI::ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    UI* pUi = reinterpret_cast<UI*>(dwRefData);
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (pUi && pUi->m_hoveredButton != hwnd) {
                HWND oldHover = pUi->m_hoveredButton;
                pUi->m_hoveredButton = hwnd;
                if (oldHover && IsWindow(oldHover)) InvalidateRect(oldHover, nullptr, FALSE);
                InvalidateRect(hwnd, nullptr, FALSE);

                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (pUi && pUi->m_hoveredButton == hwnd) {
                pUi->m_hoveredButton = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK UI::CheckboxSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    UI* pUi = reinterpret_cast<UI*>(dwRefData);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            const auto& tokens = Theme::Tokens();

            // Background of checkbox matches container
            COLORREF bg = (pUi && (hwnd == pUi->m_chkAutoPower || hwnd == pUi->m_chkStartup)) ?
                          tokens.bgWindow : tokens.bgCard;
            FillRect(memDC, &rc, Theme::Brush(bg));

            bool isChecked = (SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
            bool isHovered = (pUi && pUi->m_hoveredButton == hwnd);

            int boxSize = 16;
            int boxY = (rc.bottom - rc.top - boxSize) / 2;
            RECT rBox = { 0, boxY, boxSize, boxY + boxSize };

            // Draw check box square with smooth corners
            if (isChecked) {
                ModernTheme::FillRoundedRect(memDC, rBox, 3, tokens.accentPrimary, tokens.accentPrimary, 1);
                // Center accent indicator tick
                RECT rTick = { rBox.left + 4, rBox.top + 4, rBox.right - 4, rBox.bottom - 4 };
                ModernTheme::FillRoundedRect(memDC, rTick, 1, tokens.textOnAccent, tokens.textOnAccent, 0);
            } else {
                COLORREF border = isHovered ? tokens.borderCardHover : tokens.borderCard;
                ModernTheme::FillRoundedRect(memDC, rBox, 3, tokens.bgControl, border, 1);
            }

            // Draw checkbox label text in bright readable primary color
            wchar_t text[128] = { 0 };
            GetWindowTextW(hwnd, text, 128);
            if (text[0]) {
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, isHovered ? tokens.textPrimary : tokens.textSecondary);
                HGDIOBJ oldFont = SelectObject(memDC, Theme::Font(Theme::FontSizeRole::Body));
                RECT rText = { boxSize + 8, 0, rc.right, rc.bottom };
                DrawTextW(memDC, text, -1, &rText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(memDC, oldFont);
            }

            BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (pUi && pUi->m_hoveredButton != hwnd) {
                HWND oldHover = pUi->m_hoveredButton;
                pUi->m_hoveredButton = hwnd;
                if (oldHover && IsWindow(oldHover)) InvalidateRect(oldHover, nullptr, FALSE);
                InvalidateRect(hwnd, nullptr, FALSE);

                TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
            }
            break;
        }

        case WM_MOUSELEAVE: {
            if (pUi && pUi->m_hoveredButton == hwnd) {
                pUi->m_hoveredButton = nullptr;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            LRESULT res = DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return res;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT UI::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pcs = reinterpret_cast<CREATESTRUCT*>(lParam);
        UI* pUi = static_cast<UI*>(pcs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pUi));
        pUi->m_hwnd = hwnd;
        g_uiInstance = pUi;
    }
    UI* pUi = reinterpret_cast<UI*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    return pUi ? pUi->WndProc(hwnd, msg, wParam, lParam) : DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT UI::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // B-05: TaskbarCreated - re-register tray icon after Explorer restart
    if (msg == s_msgTaskbarCreated || (s_msgTaskbarCreated == 0 && msg == RegisterWindowMessageW(L"TaskbarCreated"))) {
        if (s_msgTaskbarCreated == 0) s_msgTaskbarCreated = msg;
        CreateTrayIcon();
        return 0;
    }

    switch (msg) {
        case WM_POWERBROADCAST:
            if (m_powerMgr) {
                m_powerMgr->OnPowerBroadcast(wParam, lParam);
            }
            if (m_monitor) {
                m_monitor->UpdateOnce();
            }
            if (m_autoLedEnabled && m_ledMgr && m_powerMgr) {
                if (m_powerMgr->IsOnAcPower()) {
                    m_ledMgr->ApplyPerformanceMode();
                } else {
                    m_ledMgr->ApplyBatteryMode();
                }
            }
            return TRUE;

        case WM_APP_METRICS_UPDATED:
            if (m_monitor) {
                UpdateMetrics(m_monitor->GetLatestMetrics());
            }
            return 0;

        case WM_ERASEBKGND:
            // Double-buffered rendering in WM_PAINT covers full client area
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            OnPaint(hdc, ps.rcPaint);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DPICHANGED: {
            // B-03: Handle per-monitor DPI change
            const RECT* suggestedRect = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggestedRect->left, suggestedRect->top,
                         suggestedRect->right - suggestedRect->left,
                         suggestedRect->bottom - suggestedRect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            m_dpiX = LOWORD(wParam);
            m_dpiY = HIWORD(wParam);
            Theme::RebuildGdiCache();
            // Recompute layout and reposition controls at new DPI
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            const auto& metrics = Theme::ScaledMetrics(m_dpiX);
            m_layout = Layout::Compute(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, metrics, m_compactMode, m_dpiX);
            LayoutControls();
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_SIZE:
            OnSize(static_cast<UINT>(wParam), LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_TIMER:
            OnTimer(wParam);
            return 0;

        case WM_COMMAND:
            OnCommand(wParam, lParam);
            return 0;

        case WM_NOTIFY: {
            LRESULT lRes = 0;
            if (OnNotify(wParam, lParam, &lRes)) {
                return lRes;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_HSCROLL:
            OnHScroll(wParam, lParam);
            return 0;

        case WM_VSCROLL:
            OnVScroll(wParam, lParam);
            return 0;

        case WM_MOUSEWHEEL:
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;

        case WM_CTLCOLORDLG:
            return reinterpret_cast<INT_PTR>(Theme::Brush(Theme::Tokens().bgWindow));

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = reinterpret_cast<HDC>(wParam);
            HWND hCtl = reinterpret_cast<HWND>(lParam);
            RECT rc;
            GetWindowRect(hCtl, &rc);
            POINT pt = { rc.left, rc.top };
            ScreenToClient(hwnd, &pt);

            const auto& tokens = Theme::Tokens();
            SetTextColor(hdcStatic, tokens.textPrimary);
            // B-04: Replace y<92 heuristic with a single geometry source (header band height)
            int headerHeight = ScaleY(Theme::Metrics().headerHeight);
            if (pt.y < headerHeight) {
                SetBkColor(hdcStatic, tokens.bgWindow);
                return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgWindow));
            } else {
                SetBkColor(hdcStatic, tokens.bgCard);
                return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgCard));
            }
        }

        case WM_CTLCOLORBTN: {
            HDC hdcBtn = reinterpret_cast<HDC>(wParam);
            HWND hCtl = reinterpret_cast<HWND>(lParam);
            RECT rc;
            GetWindowRect(hCtl, &rc);
            POINT pt = { rc.left, rc.top };
            ScreenToClient(hwnd, &pt);

            const auto& tokens = Theme::Tokens();
            SetTextColor(hdcBtn, tokens.textPrimary);
            int headerHeight = ScaleY(Theme::Metrics().headerHeight);
            if (pt.y < headerHeight) {
                SetBkColor(hdcBtn, tokens.bgWindow);
                return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgWindow));
            } else {
                SetBkColor(hdcBtn, tokens.bgCard);
                return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgCard));
            }
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdcList = reinterpret_cast<HDC>(wParam);
            const auto& tokens = Theme::Tokens();
            SetTextColor(hdcList, tokens.textPrimary);
            SetBkColor(hdcList, tokens.bgCard);
            return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgCard));
        }

        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = reinterpret_cast<HDC>(wParam);
            const auto& tokens = Theme::Tokens();
            SetTextColor(hdcEdit, tokens.textPrimary);
            SetBkColor(hdcEdit, tokens.bgCard);
            return reinterpret_cast<INT_PTR>(Theme::Brush(tokens.bgCard));
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (pDIS && pDIS->CtlType == ODT_BUTTON) {
                bool isPressed = (pDIS->itemState & ODS_SELECTED) != 0;
                bool isHovered = (pDIS->hwndItem == m_hoveredButton);
                // B-06: Focus ring support
                bool isFocused = (pDIS->itemState & ODS_FOCUS) != 0;

                const auto& tokens = Theme::Tokens();

                if (pDIS->CtlID == IDC_PROFILE_TOGGLE) {
                    COLORREF accentColor = m_isPerformanceMode ? tokens.accentCpu : tokens.accentWarning;
                    ModernTheme::DrawModernPillToggle(pDIS->hDC, pDIS->rcItem,
                        L"Battery Saver", L"Performance Mode",
                        m_isPerformanceMode, accentColor);
                    // B-06: Draw focus ring if focused
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_APPLY) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, true, tokens.accentCpu);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_FAN_SILENT || pDIS->CtlID == IDC_BTN_FAN_BALANCED ||
                    pDIS->CtlID == IDC_BTN_FAN_COOL || pDIS->CtlID == IDC_BTN_FAN_MAX) {
                    wchar_t text[32];
                    GetWindowTextW(pDIS->hwndItem, text, 32);

                    bool isSelected = false;
                    bool isAuto = (m_chkAutoFan && SendMessage(m_chkAutoFan, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    if (!isAuto) {
                        if (pDIS->CtlID == IDC_BTN_FAN_SILENT && m_activeFanPreset == FanPreset::Silent) isSelected = true;
                        else if (pDIS->CtlID == IDC_BTN_FAN_BALANCED && m_activeFanPreset == FanPreset::Balanced) isSelected = true;
                        else if (pDIS->CtlID == IDC_BTN_FAN_COOL && m_activeFanPreset == FanPreset::Cool) isSelected = true;
                        else if (pDIS->CtlID == IDC_BTN_FAN_MAX && m_activeFanPreset == FanPreset::Max) isSelected = true;
                    }

                    COLORREF fanAccent = (pDIS->CtlID == IDC_BTN_FAN_MAX) ? tokens.accentWarning : tokens.accentCpu;
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, isSelected, fanAccent);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_APPLY) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, true, tokens.accentLed);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_PICK_COLOR) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, m_customLedColor);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_ON) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentCpu);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_OFF) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentWarning);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_RAINBOW) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentInfo);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_LED_RESTORE) {
                    wchar_t text[64];
                    GetWindowTextW(pDIS->hwndItem, text, 64);
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentCpu);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_CLOSE) {
                    const wchar_t* text = L"\u2715";
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentDanger);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_COMPACT) {
                    const wchar_t* text = m_compactMode ? L"EXPAND" : L"MINI";
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, false, tokens.accentSecondary);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID == IDC_BTN_THEME_CYCLE) {
                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, L"THEME", isHovered, isPressed, false, tokens.accentPrimary);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID >= IDC_RDO_GPU_AUTO && pDIS->CtlID <= IDC_RDO_GPU_DISCRETE) {
                    wchar_t text[32];
                    GetWindowTextW(pDIS->hwndItem, text, 32);
                    bool isSelected = false;
                    if (pDIS->CtlID == IDC_RDO_GPU_AUTO && m_activeGpuMode == GpuMode::Auto) isSelected = true;
                    else if (pDIS->CtlID == IDC_RDO_GPU_INTEGRATED && m_activeGpuMode == GpuMode::IntegratedOnly) isSelected = true;
                    else if (pDIS->CtlID == IDC_RDO_GPU_HYBRID && m_activeGpuMode == GpuMode::Hybrid) isSelected = true;
                    else if (pDIS->CtlID == IDC_RDO_GPU_DISCRETE && m_activeGpuMode == GpuMode::DiscreteOnly) isSelected = true;

                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, isSelected, tokens.accentGpu);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }

                if (pDIS->CtlID >= IDC_BTN_THEME_0 && pDIS->CtlID <= IDC_BTN_THEME_4) {
                    wchar_t text[32];
                    GetWindowTextW(pDIS->hwndItem, text, 32);
                    int themeIdx = pDIS->CtlID - IDC_BTN_THEME_0;
                    bool isSelected = (static_cast<int>(Theme::Current()) == themeIdx);

                    COLORREF themeColor = tokens.accentPrimary;
                    if (themeIdx == 0) themeColor = RGB(255, 0, 168);       // Cyberpunk Magenta
                    else if (themeIdx == 1) themeColor = RGB(76, 141, 255); // Stealth Blue
                    else if (themeIdx == 2) themeColor = RGB(0, 240, 255);  // Spectrum Cyan
                    else if (themeIdx == 3) themeColor = RGB(0, 255, 102);  // Phosphor Green
                    else if (themeIdx == 4) themeColor = RGB(181, 137, 0);  // Solarized Gold

                    ModernTheme::DrawModernButton(pDIS->hDC, pDIS->rcItem, text, isHovered, isPressed, isSelected, themeColor);
                    if (isFocused) {
                        RECT focusRect = pDIS->rcItem;
                        InflateRect(&focusRect, -2, -2);
                        HPEN focusPen = Theme::Pen(tokens.borderFocus, 2);
                        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(pDIS->hDC, GetStockObject(NULL_BRUSH)));
                        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(pDIS->hDC, focusPen));
                        RoundRect(pDIS->hDC, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom,
                                  ScaleX(Theme::Metrics().cornerRadiusButton), ScaleY(Theme::Metrics().cornerRadiusButton));
                        SelectObject(pDIS->hDC, oldPen);
                        SelectObject(pDIS->hDC, oldBrush);
                    }
                    return TRUE;
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                // Minimize directly to hidden icons tray instead of taskbar
                ShowWindow(m_hwnd, SW_HIDE);
                SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);

        case WM_SETCURSOR: {
            HWND hTarget = reinterpret_cast<HWND>(wParam);
            if (hTarget != hwnd && hTarget != nullptr) {
                wchar_t className[32] = { 0 };
                GetClassNameW(hTarget, className, 32);
                if (_wcsicmp(className, L"BUTTON") == 0 || _wcsicmp(className, TRACKBAR_CLASSW) == 0 || _wcsicmp(className, L"COMBOBOX") == 0) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            if (LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                POINT ptLayout = { pt.x, pt.y + (m_compactMode ? 0 : m_scrollY) };
                // Use layout engine rects for telemetry card hover detection
                const auto& L = m_layout;
                if (L.clientWidth > 0) {
                    bool overCard = PtInRect(&L.rCardCpu, ptLayout) || PtInRect(&L.rCardGpu, ptLayout) ||
                                    PtInRect(&L.rCardPower, ptLayout) || PtInRect(&L.rCardFan, ptLayout);
                    if (overCard) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_NCHITTEST: {
            // §15: Borderless drag — return HTCAPTION over grab bar so Windows moves the window,
            // but only if not over a child control (buttons/sliders/combos stay clickable).
            if (m_borderless) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);

                // Determine grab bar rect
                RECT grabRect = { 0, 0, 0, 0 };
                if (m_compactMode && m_layout.clientWidth > 0) {
                    grabRect = m_layout.rCompactGrab;
                } else if (!m_compactMode) {
                    // Normal borderless: top 32px is the grab bar
                    RECT client;
                    GetClientRect(hwnd, &client);
                    grabRect = { 0, 0, client.right, 32 };
                }

                if (PtInRect(&grabRect, pt)) {
                    // Check if cursor is over a child control within the grab bar
                    HWND hChild = ChildWindowFromPoint(hwnd, pt);
                    if (hChild == nullptr || hChild == hwnd) {
                        return HTCAPTION;
                    }
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);

            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            POINT pt = { x, y + (m_compactMode ? 0 : m_scrollY) };

            int newHovered = -1;
            const auto& L = m_layout;
            if (L.clientWidth > 0) {
                if (PtInRect(&L.rCardCpu, pt)) newHovered = 0;      // CPU
                else if (PtInRect(&L.rCardGpu, pt)) newHovered = 1; // GPU
                else if (PtInRect(&L.rCardPower, pt)) newHovered = 2; // Power
                else if (PtInRect(&L.rCardFan, pt)) newHovered = 3;  // Cooling
            }

            if (newHovered != m_hoveredCard) {
                m_hoveredCard = newHovered;
                // Invalidate only telemetry section using layout rect
                if (L.clientWidth > 0) {
                    RECT telemetryRect = L.rTelemetry;
                    InvalidateRect(m_hwnd, &telemetryRect, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (m_hoveredCard != -1) {
                m_hoveredCard = -1;
                const auto& L = m_layout;
                if (L.clientWidth > 0) {
                    RECT telemetryRect = L.rTelemetry;
                    InvalidateRect(m_hwnd, &telemetryRect, FALSE);
                }
            }
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(m_hwnd, SW_HIDE);
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
            return 0;

        case WM_DESTROY:
            // B-15: Clear the global instance pointer to prevent dangling pointer
            g_uiInstance = nullptr;
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            // B-15: Ensure global instance pointer is cleared even if WM_DESTROY was bypassed
            g_uiInstance = nullptr;
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            if (m_compactMode) {
                mmi->ptMinTrackSize.x = ScaleX(200);
                mmi->ptMinTrackSize.y = ScaleY(80);
            } else {
                mmi->ptMinTrackSize.x = ScaleX(460);
                mmi->ptMinTrackSize.y = ScaleY(380);
            }
            return 0;
        }

        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowTrayMenu(pt);
            } else if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                ToggleWindowVisibility();
            }
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool UI::CreateMainWindow(HINSTANCE hInstance, int nCmdShow) {
    m_borderless = false;

    // Get monitor work area so window fits comfortably without overflowing the taskbar
    RECT workArea{};
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenW = workArea.right - workArea.left;
    int screenH = workArea.bottom - workArea.top;

    const int clientWidth = m_compactMode ? ScaleX(280) : ((screenW >= 1200) ? (std::min)(ScaleX(1020), screenW - 40) : (std::min)(ScaleX(560), screenW - 40));
    const int clientHeight = m_compactMode ? ScaleY(120) : (std::min)(ScaleY(780), screenH - 80);

    DWORD style = m_compactMode ?
        (WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN) :
        (WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL);

    DWORD exStyle = m_compactMode ?
        (WS_EX_TOOLWINDOW | (m_alwaysOnTop ? WS_EX_TOPMOST : 0)) :
        (WS_EX_APPWINDOW);

    RECT rect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    int winW = rect.right - rect.left;
    int winH = rect.bottom - rect.top;
    int posX = workArea.left + (std::max)(0, (screenW - winW) / 2);
    int posY = workArea.top + (std::max)(0, (screenH - winH) / 2);

    m_hwnd = CreateWindowExW(
        exStyle,
        L"PowerConfiguratorWindow",
        L"Power Configurator - Alienware m16 R2",
        style,
        posX, posY,
        winW, winH,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) return false;

    // Apply Windows 11 DWM immersive dark mode and corner preference via Theme engine
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

    const auto& tokens = Theme::Tokens();
    DWORD corner = tokens.sharpWindowCorners ? DWMWCP_DONOTROUND : DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));

    // Compute initial layout with scaled metrics
    RECT clientRect;
    GetClientRect(m_hwnd, &clientRect);
    const auto& metrics = Theme::ScaledMetrics(m_dpiX);
    m_layout = Layout::Compute(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, metrics, m_compactMode, m_dpiX);
    m_contentHeight = m_layout.totalContentHeight;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

void UI::CreateControls() {
    m_fontTitle = Theme::Font(Theme::FontSizeRole::Title);
    m_fontSemi = Theme::Font(Theme::FontSizeRole::Subheader);
    m_fontNormal = Theme::Font(Theme::FontSizeRole::Body);
    m_fontSmall = Theme::Font(Theme::FontSizeRole::Small);
    m_fontMonospace = Theme::Font(Theme::FontSizeRole::Mono);
    m_fontLarge = Theme::Font(Theme::FontSizeRole::Metric);

    // Ensure layout is computed with scaled metrics
    if (m_layout.clientWidth == 0 || m_layout.clientHeight == 0) {
        RECT clientRect;
        GetClientRect(m_hwnd, &clientRect);
        const auto& metrics = Theme::ScaledMetrics(m_dpiX);
        m_layout = Layout::Compute(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, metrics, m_compactMode, m_dpiX);
    }
    const auto& L = m_layout;

    auto SetupOwnerDrawButton = [this](HWND btn) {
        if (btn) {
            SetWindowSubclass(btn, ButtonSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
        }
    };

    auto SetupCheckbox = [this](HWND chk) {
        if (chk) {
            SetWindowSubclass(chk, CheckboxSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this));
        }
    };

    // ===== Profile Segmented Switcher & Global Switches =====
    m_btnProfileToggle = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rProfileToggle.left, L.rProfileToggle.top,
        L.rProfileToggle.right - L.rProfileToggle.left, L.rProfileToggle.bottom - L.rProfileToggle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_PROFILE_TOGGLE), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnProfileToggle);

    m_chkAutoPower = CreateWindowExW(0, L"BUTTON", L"Auto AC / Battery Switch",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        L.rChkAutoPower.left, L.rChkAutoPower.top,
        L.rChkAutoPower.right - L.rChkAutoPower.left, L.rChkAutoPower.bottom - L.rChkAutoPower.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_CHK_AUTO_POWER), m_hInstance, nullptr);
    SetControlFont(m_chkAutoPower, m_fontNormal);
    SetupCheckbox(m_chkAutoPower);
    SendMessage(m_chkAutoPower, BM_SETCHECK, BST_CHECKED, 0);

    m_chkStartup = CreateWindowExW(0, L"BUTTON", L"Run on Windows Startup",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        L.rChkStartup.left, L.rChkStartup.top,
        L.rChkStartup.right - L.rChkStartup.left, L.rChkStartup.bottom - L.rChkStartup.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_CHK_STARTUP), m_hInstance, nullptr);
    SetControlFont(m_chkStartup, m_fontNormal);
    SetupCheckbox(m_chkStartup);

    // ===== Section 1: Hardware Power Limits =====
    m_lblCpuTitle = CreateWindowExW(0, L"STATIC", L"CPU PL1 Limit:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblCpuTitle.left, L.rLblCpuTitle.top,
        L.rLblCpuTitle.right - L.rLblCpuTitle.left, L.rLblCpuTitle.bottom - L.rLblCpuTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblCpuTitle, m_fontNormal);

    m_sliderCpu = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderCpu.left, L.rSliderCpu.top,
        L.rSliderCpu.right - L.rSliderCpu.left, L.rSliderCpu.bottom - L.rSliderCpu.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_CPU), m_hInstance, nullptr);
    SendMessage(m_sliderCpu, TBM_SETRANGE, TRUE, MAKELPARAM(15, 125));
    SendMessage(m_sliderCpu, TBM_SETPOS, TRUE, 45);

    m_lblCpuWatts = CreateWindowExW(0, L"STATIC", L"45 W",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        L.rLblCpuWatts.left, L.rLblCpuWatts.top,
        L.rLblCpuWatts.right - L.rLblCpuWatts.left, L.rLblCpuWatts.bottom - L.rLblCpuWatts.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_CPU_WATTS), m_hInstance, nullptr);
    SetControlFont(m_lblCpuWatts, m_fontNormal);

    m_lblGpuTitle = CreateWindowExW(0, L"STATIC", L"GPU Power Limit:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblGpuTitle.left, L.rLblGpuTitle.top,
        L.rLblGpuTitle.right - L.rLblGpuTitle.left, L.rLblGpuTitle.bottom - L.rLblGpuTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblGpuTitle, m_fontNormal);

    m_sliderGpu = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderGpu.left, L.rSliderGpu.top,
        L.rSliderGpu.right - L.rSliderGpu.left, L.rSliderGpu.bottom - L.rSliderGpu.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_GPU), m_hInstance, nullptr);
    SendMessage(m_sliderGpu, TBM_SETRANGE, TRUE, MAKELPARAM(40, 175));
    SendMessage(m_sliderGpu, TBM_SETPOS, TRUE, 80);

    m_lblGpuWatts = CreateWindowExW(0, L"STATIC", L"80 W",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        L.rLblGpuWatts.left, L.rLblGpuWatts.top,
        L.rLblGpuWatts.right - L.rLblGpuWatts.left, L.rLblGpuWatts.bottom - L.rLblGpuWatts.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_GPU_WATTS), m_hInstance, nullptr);
    SetControlFont(m_lblGpuWatts, m_fontNormal);

    m_btnApply = CreateWindowExW(0, L"BUTTON", L"Apply Power Limits",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnApply.left, L.rBtnApply.top,
        L.rBtnApply.right - L.rBtnApply.left, L.rBtnApply.bottom - L.rBtnApply.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_APPLY), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnApply);

    // ===== Section 2: GPU Working Mode (B-09) =====
    m_btnGpuAuto = CreateWindowExW(0, L"BUTTON", L"Auto (DDS)",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnGpuAuto.left, L.rBtnGpuAuto.top,
        L.rBtnGpuAuto.right - L.rBtnGpuAuto.left, L.rBtnGpuAuto.bottom - L.rBtnGpuAuto.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_RDO_GPU_AUTO), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnGpuAuto);

    m_btnGpuIntegrated = CreateWindowExW(0, L"BUTTON", L"Integrated",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnGpuIntegrated.left, L.rBtnGpuIntegrated.top,
        L.rBtnGpuIntegrated.right - L.rBtnGpuIntegrated.left, L.rBtnGpuIntegrated.bottom - L.rBtnGpuIntegrated.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_RDO_GPU_INTEGRATED), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnGpuIntegrated);

    m_btnGpuHybrid = CreateWindowExW(0, L"BUTTON", L"Hybrid",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnGpuHybrid.left, L.rBtnGpuHybrid.top,
        L.rBtnGpuHybrid.right - L.rBtnGpuHybrid.left, L.rBtnGpuHybrid.bottom - L.rBtnGpuHybrid.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_RDO_GPU_HYBRID), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnGpuHybrid);

    m_btnGpuDiscrete = CreateWindowExW(0, L"BUTTON", L"dGPU Only",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnGpuDiscrete.left, L.rBtnGpuDiscrete.top,
        L.rBtnGpuDiscrete.right - L.rBtnGpuDiscrete.left, L.rBtnGpuDiscrete.bottom - L.rBtnGpuDiscrete.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_RDO_GPU_DISCRETE), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnGpuDiscrete);

    m_lblGpuModeDesc = CreateWindowExW(0, L"STATIC", L"Dynamic Display Switch (Advanced Optimus) enabled",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblGpuModeDesc.left, L.rLblGpuModeDesc.top,
        L.rLblGpuModeDesc.right - L.rLblGpuModeDesc.left, L.rLblGpuModeDesc.bottom - L.rLblGpuModeDesc.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_GPU_MODE_DESC), m_hInstance, nullptr);
    SetControlFont(m_lblGpuModeDesc, m_fontSmall);

    // ===== Section 2: Cooling & Fans =====
    m_btnFanSilent = CreateWindowExW(0, L"BUTTON", L"Silent",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnFanSilent.left, L.rBtnFanSilent.top,
        L.rBtnFanSilent.right - L.rBtnFanSilent.left, L.rBtnFanSilent.bottom - L.rBtnFanSilent.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_FAN_SILENT), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnFanSilent);

    m_btnFanBalanced = CreateWindowExW(0, L"BUTTON", L"Balanced",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnFanBalanced.left, L.rBtnFanBalanced.top,
        L.rBtnFanBalanced.right - L.rBtnFanBalanced.left, L.rBtnFanBalanced.bottom - L.rBtnFanBalanced.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_FAN_BALANCED), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnFanBalanced);

    m_btnFanCool = CreateWindowExW(0, L"BUTTON", L"Cool",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnFanCool.left, L.rBtnFanCool.top,
        L.rBtnFanCool.right - L.rBtnFanCool.left, L.rBtnFanCool.bottom - L.rBtnFanCool.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_FAN_COOL), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnFanCool);

    m_btnFanMax = CreateWindowExW(0, L"BUTTON", L"Full Speed",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnFanMax.left, L.rBtnFanMax.top,
        L.rBtnFanMax.right - L.rBtnFanMax.left, L.rBtnFanMax.bottom - L.rBtnFanMax.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_FAN_MAX), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnFanMax);

    m_chkAutoFan = CreateWindowExW(0, L"BUTTON", L"Auto Fan",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        L.rChkAutoFan.left, L.rChkAutoFan.top,
        L.rChkAutoFan.right - L.rChkAutoFan.left, L.rChkAutoFan.bottom - L.rChkAutoFan.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_CHK_AUTO_FAN), m_hInstance, nullptr);
    SetControlFont(m_chkAutoFan, m_fontNormal);
    SetupCheckbox(m_chkAutoFan);
    SendMessage(m_chkAutoFan, BM_SETCHECK, BST_CHECKED, 0);

    m_sliderFan = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderFan.left, L.rSliderFan.top,
        L.rSliderFan.right - L.rSliderFan.left, L.rSliderFan.bottom - L.rSliderFan.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_FAN), m_hInstance, nullptr);
    SendMessage(m_sliderFan, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(m_sliderFan, TBM_SETPOS, TRUE, 0);

    m_lblFanStatus = CreateWindowExW(0, L"STATIC", L"Speed: 0% (Silent)",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        L.rLblFanStatus.left, L.rLblFanStatus.top,
        L.rLblFanStatus.right - L.rLblFanStatus.left, L.rLblFanStatus.bottom - L.rLblFanStatus.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblFanStatus, m_fontNormal);

    // ===== Section 3: Display Refresh Rate =====
    m_chkAutoRefresh = CreateWindowExW(0, L"BUTTON", L"Auto Refresh Rate",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        L.rChkAutoRefresh.left, L.rChkAutoRefresh.top,
        L.rChkAutoRefresh.right - L.rChkAutoRefresh.left, L.rChkAutoRefresh.bottom - L.rChkAutoRefresh.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_CHK_AUTO_REFRESH), m_hInstance, nullptr);
    SetControlFont(m_chkAutoRefresh, m_fontNormal);
    SetupCheckbox(m_chkAutoRefresh);
    SendMessage(m_chkAutoRefresh, BM_SETCHECK, BST_CHECKED, 0);

    m_comboRefreshRate = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboRefreshRate.left, L.rComboRefreshRate.top,
        L.rComboRefreshRate.right - L.rComboRefreshRate.left, L.rComboRefreshRate.bottom - L.rComboRefreshRate.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_REFRESH), m_hInstance, nullptr);
    SetControlFont(m_comboRefreshRate, m_fontNormal);

    if (m_displayMgr) {
        int w = 1920, h = 1080;
        m_displayMgr->GetCurrentResolution(&w, &h);
        auto rates = m_displayMgr->GetAvailableRefreshRates(w, h);
        int currentRate = m_displayMgr->GetCurrentRefreshRate();
        for (int r : rates) {
            std::wstring rStr = std::to_wstring(r) + L" Hz";
            int idx = static_cast<int>(SendMessage(m_comboRefreshRate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(rStr.c_str())));
            if (r == currentRate) {
                SendMessage(m_comboRefreshRate, CB_SETCURSEL, idx, 0);
            }
        }
    }
    // B-13: Initial state - auto is checked so combo should be disabled
    EnableWindow(m_comboRefreshRate, FALSE);

    m_lblRefreshStatus = CreateWindowExW(0, L"STATIC", L"2560x1600 VRR G-Sync",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblDispSpec.left, L.rLblDispSpec.top,
        L.rLblDispSpec.right - L.rLblDispSpec.left, L.rLblDispSpec.bottom - L.rLblDispSpec.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblRefreshStatus, m_fontSmall);

    // ===== Section 4: AlienFX Lighting Studio =====
    m_comboLedZone = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboLedZone.left, L.rComboLedZone.top,
        L.rComboLedZone.right - L.rComboLedZone.left, L.rComboLedZone.bottom - L.rComboLedZone.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_LED_ZONE), m_hInstance, nullptr);
    SetControlFont(m_comboLedZone, m_fontNormal);
    SendMessage(m_comboLedZone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Lighting Zones"));
    SendMessage(m_comboLedZone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"RGB Keyboard"));
    SendMessage(m_comboLedZone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Touchpad / Trackpad"));
    SendMessage(m_comboLedZone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alien Logo (Rear Lid)"));
    SendMessage(m_comboLedZone, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AlienHead Power Button"));
    SendMessage(m_comboLedZone, CB_SETCURSEL, 0, 0);

    m_comboLedMode = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboLedMode.left, L.rComboLedMode.top,
        L.rComboLedMode.right - L.rComboLedMode.left, L.rComboLedMode.bottom - L.rComboLedMode.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_LED_MODE), m_hInstance, nullptr);
    SetControlFont(m_comboLedMode, m_fontNormal);
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Static (Solid Color)"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Breathing (Pulse)"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Rainbow Wave"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Spectrum Cycle"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Color Pulse / Flash"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Morph (Dual Color)"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Scanner (AWCC Night Rider)"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Laser Burst (AWCC)"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cyber Neon Wave"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Stealth Night Dim"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"LEDs Off"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Music: Audio Visualizer EQ"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Music: Bass Beat Pulse"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Music: Audio Reactive Wave"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ambient: Screen Ambilight"));
    SendMessage(m_comboLedMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Ambient: Screen Multi-Zone"));
    SendMessage(m_comboLedMode, CB_SETCURSEL, 0, 0);

    m_comboLedColor = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboLedColor.left, L.rComboLedColor.top,
        L.rComboLedColor.right - L.rComboLedColor.left, L.rComboLedColor.bottom - L.rComboLedColor.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_LED_COLOR), m_hInstance, nullptr);
    SetControlFont(m_comboLedColor, m_fontNormal);
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Alien Cyan (#00B4FF)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Deep Blue (#0040FF)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Neon Red (#FF0033)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Emerald Green (#00FF66)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Cyber Magenta (#FF007F)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Solar Yellow (#FFCC00)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Amber Orange (#FF6600)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pure White (#FFFFFF)"));
    SendMessage(m_comboLedColor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom RGB..."));
    SendMessage(m_comboLedColor, CB_SETCURSEL, 0, 0);

    m_btnPickColor = CreateWindowExW(0, L"BUTTON", L"Pick Custom...",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnPickColor.left, L.rBtnPickColor.top,
        L.rBtnPickColor.right - L.rBtnPickColor.left, L.rBtnPickColor.bottom - L.rBtnPickColor.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_PICK_COLOR), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnPickColor);

    m_btnApplyLed = CreateWindowExW(0, L"BUTTON", L"Apply Lighting",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnApplyLed.left, L.rBtnApplyLed.top,
        L.rBtnApplyLed.right - L.rBtnApplyLed.left, L.rBtnApplyLed.bottom - L.rBtnApplyLed.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_APPLY), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnApplyLed);

    m_lblLedBrightness = CreateWindowExW(0, L"STATIC", L"Brightness: 100%",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblLedBrightness.left, L.rLblLedBrightness.top,
        L.rLblLedBrightness.right - L.rLblLedBrightness.left, L.rLblLedBrightness.bottom - L.rLblLedBrightness.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_LED_BRIGHTNESS), m_hInstance, nullptr);
    SetControlFont(m_lblLedBrightness, m_fontNormal);

    m_sliderLedBrightness = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderLedBrightness.left, L.rSliderLedBrightness.top,
        L.rSliderLedBrightness.right - L.rSliderLedBrightness.left, L.rSliderLedBrightness.bottom - L.rSliderLedBrightness.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_LED_BRIGHTNESS), m_hInstance, nullptr);
    SendMessage(m_sliderLedBrightness, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(m_sliderLedBrightness, TBM_SETPOS, TRUE, 100);

    m_lblLedSpeed = CreateWindowExW(0, L"STATIC", L"Speed: 7 (Fast)",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblLedSpeed.left, L.rLblLedSpeed.top,
        L.rLblLedSpeed.right - L.rLblLedSpeed.left, L.rLblLedSpeed.bottom - L.rLblLedSpeed.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_LED_SPEED), m_hInstance, nullptr);
    SetControlFont(m_lblLedSpeed, m_fontNormal);

    m_sliderLedSpeed = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderLedSpeed.left, L.rSliderLedSpeed.top,
        L.rSliderLedSpeed.right - L.rSliderLedSpeed.left, L.rSliderLedSpeed.bottom - L.rSliderLedSpeed.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_LED_SPEED), m_hInstance, nullptr);
    SendMessage(m_sliderLedSpeed, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
    SendMessage(m_sliderLedSpeed, TBM_SETPOS, TRUE, 7);

    m_chkAutoLed = CreateWindowExW(0, L"BUTTON", L"Auto LEDs",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        L.rChkAutoLed.left, L.rChkAutoLed.top,
        L.rChkAutoLed.right - L.rChkAutoLed.left, L.rChkAutoLed.bottom - L.rChkAutoLed.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_CHK_AUTO_LED), m_hInstance, nullptr);
    SetControlFont(m_chkAutoLed, m_fontNormal);
    SetupCheckbox(m_chkAutoLed);
    SendMessage(m_chkAutoLed, BM_SETCHECK, BST_CHECKED, 0);

    m_btnLedOn = CreateWindowExW(0, L"BUTTON", L"All ON",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnLedOn.left, L.rBtnLedOn.top,
        L.rBtnLedOn.right - L.rBtnLedOn.left, L.rBtnLedOn.bottom - L.rBtnLedOn.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_ON), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnLedOn);

    m_btnLedOff = CreateWindowExW(0, L"BUTTON", L"Turn Off",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnLedOff.left, L.rBtnLedOff.top,
        L.rBtnLedOff.right - L.rBtnLedOff.left, L.rBtnLedOff.bottom - L.rBtnLedOff.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_OFF), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnLedOff);

    m_btnLedRainbow = CreateWindowExW(0, L"BUTTON", L"Rainbow",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnLedRainbow.left, L.rBtnLedRainbow.top,
        L.rBtnLedRainbow.right - L.rBtnLedRainbow.left, L.rBtnLedRainbow.bottom - L.rBtnLedRainbow.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_RAINBOW), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnLedRainbow);

    m_btnLedFetch = CreateWindowExW(0, L"BUTTON", L"Sync FX",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnLedFetch.left, L.rBtnLedFetch.top,
        L.rBtnLedFetch.right - L.rBtnLedFetch.left, L.rBtnLedFetch.bottom - L.rBtnLedFetch.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_FETCH), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnLedFetch);

    m_btnLedRestore = CreateWindowExW(0, L"BUTTON", L"Restore",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnLedRestore.left, L.rBtnLedRestore.top,
        L.rBtnLedRestore.right - L.rBtnLedRestore.left, L.rBtnLedRestore.bottom - L.rBtnLedRestore.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_BTN_LED_RESTORE), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnLedRestore);

    // ===== Section 7: Windows Power Controls (B-09) =====
    m_lblPowerMode = CreateWindowExW(0, L"STATIC", L"Power Mode:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblPowerMode.left, L.rLblPowerMode.top,
        L.rLblPowerMode.right - L.rLblPowerMode.left, L.rLblPowerMode.bottom - L.rLblPowerMode.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_POWERMODE), m_hInstance, nullptr);
    SetControlFont(m_lblPowerMode, m_fontNormal);

    m_comboPowerMode = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboPowerMode.left, L.rComboPowerMode.top,
        L.rComboPowerMode.right - L.rComboPowerMode.left, L.rComboPowerMode.bottom - L.rComboPowerMode.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_POWERMODE), m_hInstance, nullptr);
    SetControlFont(m_comboPowerMode, m_fontNormal);
    SendMessageW(m_comboPowerMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Best Power Efficiency"));
    SendMessageW(m_comboPowerMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Balanced"));
    SendMessageW(m_comboPowerMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Best Performance"));
    SendMessageW(m_comboPowerMode, CB_SETCURSEL, 1, 0);

    m_lblSleep = CreateWindowExW(0, L"STATIC", L"Sleep After:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblSleepTitle.left, L.rLblSleepTitle.top,
        L.rLblSleepTitle.right - L.rLblSleepTitle.left, L.rLblSleepTitle.bottom - L.rLblSleepTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_SLEEP), m_hInstance, nullptr);
    SetControlFont(m_lblSleep, m_fontNormal);

    m_comboSleep = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboSleep.left, L.rComboSleep.top,
        L.rComboSleep.right - L.rComboSleep.left, L.rComboSleep.bottom - L.rComboSleep.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_SLEEP), m_hInstance, nullptr);
    SetControlFont(m_comboSleep, m_fontNormal);
    SendMessageW(m_comboSleep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Never"));
    SendMessageW(m_comboSleep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"10 min"));
    SendMessageW(m_comboSleep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"20 min"));
    SendMessageW(m_comboSleep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 min"));
    SendMessageW(m_comboSleep, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"60 min"));
    SendMessageW(m_comboSleep, CB_SETCURSEL, 2, 0);

    m_lblDisplayTimeout = CreateWindowExW(0, L"STATIC", L"Display Off:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblDisplayTimeoutTitle.left, L.rLblDisplayTimeoutTitle.top,
        L.rLblDisplayTimeoutTitle.right - L.rLblDisplayTimeoutTitle.left, L.rLblDisplayTimeoutTitle.bottom - L.rLblDisplayTimeoutTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_DISPLAY_TO), m_hInstance, nullptr);
    SetControlFont(m_lblDisplayTimeout, m_fontNormal);

    m_comboDisplayTimeout = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        L.rComboDisplayTimeout.left, L.rComboDisplayTimeout.top,
        L.rComboDisplayTimeout.right - L.rComboDisplayTimeout.left, L.rComboDisplayTimeout.bottom - L.rComboDisplayTimeout.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_COMBO_DISPLAY), m_hInstance, nullptr);
    SetControlFont(m_comboDisplayTimeout, m_fontNormal);
    SendMessageW(m_comboDisplayTimeout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Never"));
    SendMessageW(m_comboDisplayTimeout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"5 min"));
    SendMessageW(m_comboDisplayTimeout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"10 min"));
    SendMessageW(m_comboDisplayTimeout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"15 min"));
    SendMessageW(m_comboDisplayTimeout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"30 min"));
    SendMessageW(m_comboDisplayTimeout, CB_SETCURSEL, 1, 0);

    m_lblCpuMinTitle = CreateWindowExW(0, L"STATIC", L"CPU Min State:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblCpuMinTitle.left, L.rLblCpuMinTitle.top,
        L.rLblCpuMinTitle.right - L.rLblCpuMinTitle.left, L.rLblCpuMinTitle.bottom - L.rLblCpuMinTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblCpuMinTitle, m_fontNormal);

    m_sliderCpuMin = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderCpuMin.left, L.rSliderCpuMin.top,
        L.rSliderCpuMin.right - L.rSliderCpuMin.left, L.rSliderCpuMin.bottom - L.rSliderCpuMin.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_CPU_MIN), m_hInstance, nullptr);
    SendMessageW(m_sliderCpuMin, TBM_SETRANGE, TRUE, MAKELPARAM(5, 100));
    SendMessageW(m_sliderCpuMin, TBM_SETPOS, TRUE, 5);

    m_lblCpuMinVal = CreateWindowExW(0, L"STATIC", L"5%",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        L.rLblCpuMinVal.left, L.rLblCpuMinVal.top,
        L.rLblCpuMinVal.right - L.rLblCpuMinVal.left, L.rLblCpuMinVal.bottom - L.rLblCpuMinVal.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_CPU_MIN), m_hInstance, nullptr);
    SetControlFont(m_lblCpuMinVal, m_fontNormal);

    m_lblCpuMaxTitle = CreateWindowExW(0, L"STATIC", L"CPU Max State:",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        L.rLblCpuMaxTitle.left, L.rLblCpuMaxTitle.top,
        L.rLblCpuMaxTitle.right - L.rLblCpuMaxTitle.left, L.rLblCpuMaxTitle.bottom - L.rLblCpuMaxTitle.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_STATIC_FAN_LABEL), m_hInstance, nullptr);
    SetControlFont(m_lblCpuMaxTitle, m_fontNormal);

    m_sliderCpuMax = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        L.rSliderCpuMax.left, L.rSliderCpuMax.top,
        L.rSliderCpuMax.right - L.rSliderCpuMax.left, L.rSliderCpuMax.bottom - L.rSliderCpuMax.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_SLIDER_CPU_MAX), m_hInstance, nullptr);
    SendMessageW(m_sliderCpuMax, TBM_SETRANGE, TRUE, MAKELPARAM(5, 100));
    SendMessageW(m_sliderCpuMax, TBM_SETPOS, TRUE, 100);

    m_lblCpuMaxVal = CreateWindowExW(0, L"STATIC", L"100%",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        L.rLblCpuMaxVal.left, L.rLblCpuMaxVal.top,
        L.rLblCpuMaxVal.right - L.rLblCpuMaxVal.left, L.rLblCpuMaxVal.bottom - L.rLblCpuMaxVal.top,
        m_hwnd, reinterpret_cast<HMENU>(IDC_LBL_CPU_MAX), m_hInstance, nullptr);
    SetControlFont(m_lblCpuMaxVal, m_fontSmall);

    // ===== Section 8: Theme Swatch Selector =====
    const wchar_t* themeNames[5] = { L"Cyberpunk", L"Stealth", L"Spectrum", L"Phosphor", L"Solarized" };
    for (int i = 0; i < 5; ++i) {
        m_btnThemeSwatches[i] = CreateWindowExW(0, L"BUTTON", themeNames[i],
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            L.rBtnThemeSwatches[i].left, L.rBtnThemeSwatches[i].top,
            L.rBtnThemeSwatches[i].right - L.rBtnThemeSwatches[i].left, L.rBtnThemeSwatches[i].bottom - L.rBtnThemeSwatches[i].top,
            m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_THEME_0 + i)), m_hInstance, nullptr);
        SetupOwnerDrawButton(m_btnThemeSwatches[i]);
    }

    // Chrome buttons: Theme cycle, compact mode toggle, and compact close button
    m_btnClose = CreateWindowExW(0, L"BUTTON", L"\u2715",
        WS_CHILD | (m_compactMode ? WS_VISIBLE : 0) | BS_OWNERDRAW,
        L.rCloseBtn.left, L.rCloseBtn.top,
        L.rCloseBtn.right - L.rCloseBtn.left, L.rCloseBtn.bottom - L.rCloseBtn.top,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CLOSE)), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnClose);

    m_btnCompact = CreateWindowExW(0, L"BUTTON", m_compactMode ? L"EXPAND" : L"MINI",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnCompact.left, L.rBtnCompact.top,
        L.rBtnCompact.right - L.rBtnCompact.left, L.rBtnCompact.bottom - L.rBtnCompact.top,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_COMPACT)), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnCompact);

    m_btnTheme = CreateWindowExW(0, L"BUTTON", L"THEME",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        L.rBtnTheme.left, L.rBtnTheme.top,
        L.rBtnTheme.right - L.rBtnTheme.left, L.rBtnTheme.bottom - L.rBtnTheme.top,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_THEME_CYCLE)), m_hInstance, nullptr);
    SetupOwnerDrawButton(m_btnTheme);

    // Initialize Tooltip controller for rich hover feedback
    m_hwndTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        m_hwnd, nullptr, m_hInstance, nullptr);
    if (m_hwndTooltip) {
        SetWindowPos(m_hwndTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (m_btnTheme) AddTooltip(m_btnTheme, L"Cycle Interface Theme (Cyberpunk, Stealth, Spectrum, Phosphor, Solarized, Legacy)");
        if (m_btnCompact) AddTooltip(m_btnCompact, L"Toggle Compact / Expanded Floating Widget");
        if (m_btnClose) AddTooltip(m_btnClose, L"Close / Hide to System Tray");
        AddTooltip(m_btnProfileToggle, L"Switch between Maximum Performance (unthrottled) and Battery Saver profiles");
        AddTooltip(m_chkAutoPower, L"Automatically switch power profiles when AC charger is connected or disconnected");
        AddTooltip(m_chkStartup, L"Launch Power Configurator minimized in hidden icons on Windows login");
        AddTooltip(m_btnFanSilent, L"Silent: low noise profile for office work and media playback");
        AddTooltip(m_btnFanBalanced, L"Balanced: dynamic cooling curve according to hardware load");
        AddTooltip(m_btnFanCool, L"Cool: aggressive thermal cooling for sustained heavy workloads");
        AddTooltip(m_btnFanMax, L"Full Speed (G-Mode): maximum 100% fan RPM for gaming sessions");
        AddTooltip(m_chkAutoFan, L"Enable automated firmware/BIOS thermal fan curve");
        AddTooltip(m_sliderFan, L"Manually adjust custom fan speed percentage (0-100%)");
        AddTooltip(m_sliderCpu, L"Adjust Intel Core Ultra CPU sustained PL1 power limit (15W - 125W)");
        AddTooltip(m_sliderGpu, L"Adjust NVIDIA RTX GPU TGP dynamic boost power ceiling (40W - 175W)");
        AddTooltip(m_btnApply, L"Apply hardware CPU and GPU power limits to EC and NVML");
        AddTooltip(m_chkAutoRefresh, L"Automatically switch refresh rate (240Hz on AC, 60Hz on Battery)");
        AddTooltip(m_comboRefreshRate, L"Manually override display panel refresh rate");
        AddTooltip(m_comboLedZone, L"Select lighting zone: All Zones, RGB Keyboard, Trackpad, Rear Logo, or Power Button");
        AddTooltip(m_comboLedMode, L"Select lighting animation effect: Static, Breathing, Rainbow, Wave, Pulse, Morph, or Off");
        AddTooltip(m_comboLedColor, L"Select color preset (Alien Cyan, Deep Blue, Neon Red, etc.) or Custom");
        AddTooltip(m_btnPickColor, L"Open full Windows RGB color picker palette");
        AddTooltip(m_btnApplyLed, L"Transmit USB HID lighting packets to AlienFX microcontrollers");
        AddTooltip(m_sliderLedBrightness, L"Adjust AlienFX LED brightness level (0% to 100%)");
        AddTooltip(m_sliderLedSpeed, L"Adjust animation wave/pulse tempo speed (1 = Slowest, 10 = Fastest)");
        AddTooltip(m_chkAutoLed, L"Turn off LEDs on battery power to maximize battery runtime");
        AddTooltip(m_btnLedOn, L"Turn on all lighting zones immediately");
        AddTooltip(m_btnLedOff, L"Completely turn off all RGB lighting zones");
        AddTooltip(m_btnLedRainbow, L"Activate full-spectrum dynamic Rainbow Wave across all zones");
        AddTooltip(m_btnLedRestore, L"Restore default Alienware Cyan performance lighting profile");
        AddTooltip(m_btnLedFetch, L"Fetch & synchronize profiles from AlienFX Tools and Alienware Command Center");
    }

    // Initialize slider animation tracking states
    m_animCpu.hwnd = m_sliderCpu;
    m_animCpu.currentPos = 45.0f;
    m_animCpu.targetPos = 45.0f;

    m_animGpu.hwnd = m_sliderGpu;
    m_animGpu.currentPos = 80.0f;
    m_animGpu.targetPos = 80.0f;

    m_animFan.hwnd = m_sliderFan;
    m_animFan.currentPos = 0.0f;
    m_animFan.targetPos = 0.0f;

    m_animLedBrightness.hwnd = m_sliderLedBrightness;
    m_animLedBrightness.currentPos = 100.0f;
    m_animLedBrightness.targetPos = 100.0f;

    m_animLedSpeed.hwnd = m_sliderLedSpeed;
    m_animLedSpeed.currentPos = 7.0f;
    m_animLedSpeed.targetPos = 7.0f;

    m_animCpuMin.hwnd = m_sliderCpuMin;
    m_animCpuMin.currentPos = 5.0f;
    m_animCpuMin.targetPos = 5.0f;

    m_animCpuMax.hwnd = m_sliderCpuMax;
    m_animCpuMax.currentPos = 100.0f;
    m_animCpuMax.targetPos = 100.0f;

    LayoutControls();
}

void UI::OnPaint(HDC hdc, const RECT& rcPaint) {
    RECT client;
    GetClientRect(m_hwnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;

    // Ensure layout is computed with scaled metrics
    if (m_layout.clientWidth == 0 || m_layout.clientHeight == 0) {
        const auto& metrics = Theme::ScaledMetrics(m_dpiX);
        m_layout = Layout::Compute(width, height, metrics, m_compactMode, m_dpiX);
    }

    // Double-buffered rendering to eliminate all flicker
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = reinterpret_cast<HBITMAP>(SelectObject(memDC, memBitmap));

    // B-08: Clip to ps.rcPaint for partial repaint optimization
    IntersectClipRect(memDC, rcPaint.left, rcPaint.top, rcPaint.right, rcPaint.bottom);

    const auto& tokens = Theme::Tokens();
    const auto& L = m_layout;
    HGDIOBJ oldFont = GetCurrentObject(memDC, OBJ_FONT);
    int curRate = m_cachedMetrics.currentRefreshRateHz > 0 ? m_cachedMetrics.currentRefreshRateHz : 240;

    // 1. Fill background from theme tokens
    FillRect(memDC, &client, Theme::Brush(tokens.bgWindow));

    // Cyberpunk: Scanline overlay on entire client area
    if (tokens.scanlines && tokens.id == Theme::ThemeId::Cyberpunk) {
        ModernTheme::DrawScanlines(memDC, client, tokens);
    }

    if (m_compactMode) {
        RECT grabRect = L.rCompactGrab;
        if (grabRect.right > grabRect.left && grabRect.bottom > grabRect.top) {
            ModernTheme::FillRoundedRect(memDC, grabRect, 4, tokens.bgControl, tokens.borderCardHover, 1);
            ModernTheme::DrawCornerTicks(memDC, grabRect, tokens.accentSecondary, true, true);

            // Title text inside grab bar - safely constrained before the expand/close buttons
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, tokens.textPrimary);
            HGDIOBJ oldTitleFont = SelectObject(memDC, m_fontSmall);
            int maxTitleRight = (L.rBtnCompact.left > grabRect.left + 30) ? (L.rBtnCompact.left - 6) : (grabRect.right - 85);
            RECT titleRect = { grabRect.left + 16, grabRect.top, maxTitleRight, grabRect.bottom };
            DrawTextW(memDC, L"POWER CONFIGURATOR", -1, &titleRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            SelectObject(memDC, oldTitleFont);

            COLORREF dotCol = m_cachedMetrics.isOnAc ? tokens.accentCpu : tokens.accentWarning;
            int dotY = grabRect.top + (grabRect.bottom - grabRect.top - 8) / 2;
            RECT dotRect = { grabRect.left + 5, dotY, grabRect.left + 13, dotY + 8 };
            HBRUSH dotBrush = CreateSolidBrush(dotCol);
            HPEN dotPen = CreatePen(PS_SOLID, 1, dotCol);
            HGDIOBJ oldBrush = SelectObject(memDC, dotBrush);
            HGDIOBJ oldPen = SelectObject(memDC, dotPen);
            Ellipse(memDC, dotRect.left, dotRect.top, dotRect.right, dotRect.bottom);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(dotPen);
            DeleteObject(dotBrush);
        }

        // Row 3: 3 Mini Cards — Battery, CPU Temp, GPU Temp
        SetBkMode(memDC, TRANSPARENT);

        // Card 1: BATTERY
        COLORREF battColor = (m_cachedMetrics.batteryPercent <= 20) ? tokens.accentThermalDanger :
                             (m_cachedMetrics.batteryPercent <= 40) ? tokens.accentWarning : tokens.accentBattery;
        ModernTheme::DrawModernCard(memDC, L.rCardPower, nullptr, false);
        ModernTheme::FillRoundedRect(memDC, { L.rCardPower.left + 3, L.rCardPower.top + 5, L.rCardPower.left + 6, L.rCardPower.bottom - 5 }, 1, battColor, battColor, 0);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT battLabel = { L.rCardPower.left + 8, L.rCardPower.top + 4, L.rCardPower.right - 4, L.rCardPower.top + 16 };
        DrawTextW(memDC, L"BATTERY", -1, &battLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, battColor);
        SelectObject(memDC, m_fontSemi);
        RECT battVal = { L.rCardPower.left + 8, L.rCardPower.top + 17, L.rCardPower.right - 4, L.rCardPower.top + 37 };
        std::wstring battPctStr = (m_cachedMetrics.batteryPercent >= 0) ? (std::to_wstring(m_cachedMetrics.batteryPercent) + L"%") : L"AC";
        DrawTextW(memDC, battPctStr.c_str(), -1, &battVal, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT battSub = { L.rCardPower.left + 8, L.rCardPower.top + 38, L.rCardPower.right - 4, L.rCardPower.bottom - 2 };
        std::wstring battSubStr = m_cachedMetrics.isOnAc ? (m_cachedMetrics.isCharging ? L"Charging" : L"AC Online") : L"On Battery";
        DrawTextW(memDC, battSubStr.c_str(), -1, &battSub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Card 2: CPU TEMP
        std::wstring cpuTempStr = (m_cachedMetrics.cpuTemperatureC > 0.0) ?
            (std::to_wstring(static_cast<int>(m_cachedMetrics.cpuTemperatureC)) + L"\u00B0C") : L"Active";
        COLORREF cpuColor;
        if (m_cachedMetrics.cpuTemperatureC >= 85.0) cpuColor = tokens.accentThermalDanger;
        else if (m_cachedMetrics.cpuTemperatureC >= 75.0) cpuColor = tokens.accentWarning;
        else cpuColor = tokens.accentCpu;

        ModernTheme::DrawModernCard(memDC, L.rCardCpu, nullptr, false);
        ModernTheme::FillRoundedRect(memDC, { L.rCardCpu.left + 3, L.rCardCpu.top + 5, L.rCardCpu.left + 6, L.rCardCpu.bottom - 5 }, 1, cpuColor, cpuColor, 0);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT cpuLabel = { L.rCardCpu.left + 8, L.rCardCpu.top + 4, L.rCardCpu.right - 4, L.rCardCpu.top + 16 };
        DrawTextW(memDC, L"CPU TEMP", -1, &cpuLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, cpuColor);
        SelectObject(memDC, m_fontSemi);
        RECT cpuVal = { L.rCardCpu.left + 8, L.rCardCpu.top + 17, L.rCardCpu.right - 4, L.rCardCpu.top + 37 };
        DrawTextW(memDC, cpuTempStr.c_str(), -1, &cpuVal, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT cpuSub = { L.rCardCpu.left + 8, L.rCardCpu.top + 38, L.rCardCpu.right - 4, L.rCardCpu.bottom - 2 };
        std::wstring cpuSubStr = L"Load: " + std::to_wstring(static_cast<int>(m_cachedMetrics.cpuUsagePercent)) + L"%";
        DrawTextW(memDC, cpuSubStr.c_str(), -1, &cpuSub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Card 3: GPU TEMP
        std::wstring gpuTempStr = (m_cachedMetrics.gpuTemperatureC > 0) ?
            (std::to_wstring(m_cachedMetrics.gpuTemperatureC) + L"\u00B0C") : (m_gpuMgr && m_gpuMgr->IsAvailable() ? L"Sleep" : L"N/A");
        COLORREF gpuColor;
        if (m_cachedMetrics.gpuTemperatureC >= 85) gpuColor = tokens.accentThermalDanger;
        else if (m_cachedMetrics.gpuTemperatureC >= 75) gpuColor = tokens.accentWarning;
        else gpuColor = tokens.accentGpu;

        ModernTheme::DrawModernCard(memDC, L.rCardGpu, nullptr, false);
        ModernTheme::FillRoundedRect(memDC, { L.rCardGpu.left + 3, L.rCardGpu.top + 5, L.rCardGpu.left + 6, L.rCardGpu.bottom - 5 }, 1, gpuColor, gpuColor, 0);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT gpuLabel = { L.rCardGpu.left + 8, L.rCardGpu.top + 4, L.rCardGpu.right - 4, L.rCardGpu.top + 16 };
        DrawTextW(memDC, L"GPU TEMP", -1, &gpuLabel, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, gpuColor);
        SelectObject(memDC, m_fontSemi);
        RECT gpuVal = { L.rCardGpu.left + 8, L.rCardGpu.top + 17, L.rCardGpu.right - 4, L.rCardGpu.top + 37 };
        DrawTextW(memDC, gpuTempStr.c_str(), -1, &gpuVal, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

        SetTextColor(memDC, tokens.textMuted);
        SelectObject(memDC, m_fontSmall);
        RECT gpuSub = { L.rCardGpu.left + 8, L.rCardGpu.top + 38, L.rCardGpu.right - 4, L.rCardGpu.bottom - 2 };
        std::wstring gpuSubStr = (m_cachedMetrics.gpuPowerDrawW > 0.5) ?
            (std::to_wstring(static_cast<int>(m_cachedMetrics.gpuPowerDrawW)) + L" W") :
            (m_cachedMetrics.gpuTemperatureC > 0 ? L"Active" : L"Standby");
        DrawTextW(memDC, gpuSubStr.c_str(), -1, &gpuSub, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    } else {
        // Windowed Mode: Translate viewport for vertical scrolling
        SetViewportOrgEx(memDC, 0, -m_scrollY, nullptr);

        // Status badge pill in top row
        if (L.rHeaderBadge.right > L.rHeaderBadge.left) {
            COLORREF dotCol = m_cachedMetrics.isOnAc ? tokens.accentCpu : tokens.accentWarning;
            std::wstring pillText = std::to_wstring(curRate) + L"Hz " +
                (m_cachedMetrics.isOnAc ? L"AC " : L"BAT ") +
                (m_isPerformanceMode ? L"PERF" : L"ECO");
            COLORREF badgeBg = m_cachedMetrics.isOnAc ? tokens.bgCard : tokens.bgWindow;
            COLORREF textCol = m_cachedMetrics.isOnAc ? tokens.accentCpu : tokens.accentWarning;
            ModernTheme::DrawStatusBadge(memDC, L.rHeaderBadge, pillText.c_str(), dotCol, badgeBg, textCol);
        }
        // 3. Section Cards - Section 1: Hardware Power Limits
        ModernTheme::DrawModernCard(memDC, L.rLimits, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrLimits, L"HARDWARE POWER LIMITS", L"", tokens.accentCpu);

        // Section 2: GPU Working Mode (B-09)
        ModernTheme::DrawModernCard(memDC, L.rGpuMode, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrGpuMode, L"GPU WORKING MODE", L"", tokens.accentGpu);

        // Section 3: Cooling & Fans
        ModernTheme::DrawModernCard(memDC, L.rFans, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrFans, L"COOLING & FANS", L"", tokens.accentFan);

        // Section 4: Display Refresh Rate
        ModernTheme::DrawModernCard(memDC, L.rDisplay, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrDisplay, L"DISPLAY REFRESH RATE", L"", tokens.accentDisplay);

        // Live display specs text next to combobox
        SetTextColor(memDC, tokens.textSecondary);
        SelectObject(memDC, m_fontSmall);
        int w = m_cachedMetrics.currentWidth > 0 ? m_cachedMetrics.currentWidth : 2560;
        int h = m_cachedMetrics.currentHeight > 0 ? m_cachedMetrics.currentHeight : 1600;
        std::wstring dispSpec = std::to_wstring(w) + L"x" + std::to_wstring(h) + L" VRR G-Sync";
        RECT rLblDispSpec = L.rLblDispSpec;
        DrawTextW(memDC, dispSpec.c_str(), -1, &rLblDispSpec, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Section 4: AlienFX Lighting Studio
        ModernTheme::DrawModernCard(memDC, L.rLed, nullptr);
        std::wstring ledStatusSubtitle = L"";
        if (m_ledMgr && m_ledMgr->IsDynamicEffectActive()) {
            LedEffect eff = m_ledMgr->GetActiveDynamicEffect();
            if (eff == LedEffect::MusicVisualizer) ledStatusSubtitle = L"Live Music EQ Active";
            else if (eff == LedEffect::MusicBeatPulse) ledStatusSubtitle = L"Live Beat Pulse Active";
            else if (eff == LedEffect::MusicWave) ledStatusSubtitle = L"Live Audio Wave Active";
            else if (eff == LedEffect::AmbientScreen) ledStatusSubtitle = L"Live Screen Ambilight Active";
            else if (eff == LedEffect::AmbientMultiZone) ledStatusSubtitle = L"Live Multi-Zone Ambilight Active";
        }
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrLed, L"ALIENFX LIGHTING STUDIO", ledStatusSubtitle.c_str(), tokens.accentLed);

        // Dynamic Live HUD Meter / Swatch
        if (m_ledMgr && m_ledMgr->IsDynamicEffectActive()) {
            LedEffect eff = m_ledMgr->GetActiveDynamicEffect();
            int badgeR = L.rHdrLed.right - 8;
            int badgeY = L.rHdrLed.top + 2;

            if (eff == LedEffect::AmbientScreen || eff == LedEffect::AmbientMultiZone) {
                if (m_ledMgr->GetScreenAmbient()) {
                    AmbientColor ac = m_ledMgr->GetScreenAmbient()->GetAverageColor();
                    COLORREF ambCol = RGB(ac.r, ac.g, ac.b);
                    RECT rcSwatch = { badgeR - 60, badgeY, badgeR, badgeY + 16 };
                    ModernTheme::FillRoundedRect(memDC, rcSwatch, 4, ambCol, tokens.borderCard, 1);
                    if (tokens.glowEnabled) {
                        ModernTheme::DrawGlow(memDC, rcSwatch, 2, ambCol, 1, 35);
                    }
                }
            } else if (eff == LedEffect::MusicVisualizer || eff == LedEffect::MusicBeatPulse || eff == LedEffect::MusicWave) {
                if (m_ledMgr->GetAudioVisualizer()) {
                    float b = 0, m = 0, t = 0;
                    m_ledMgr->GetAudioVisualizer()->GetSpectrum(b, m, t);
                    int barW = 6;
                    int maxBarH = 14;
                    int meterX = badgeR - 3 * (barW + 3);

                    int hB = (std::max)(2, static_cast<int>(b * maxBarH));
                    RECT rcB = { meterX, badgeY + maxBarH - hB, meterX + barW, badgeY + maxBarH };
                    ModernTheme::FillRoundedRect(memDC, rcB, 1, tokens.accentGpu, tokens.accentGpu, 0);

                    int hM = (std::max)(2, static_cast<int>(m * maxBarH));
                    RECT rcM = { meterX + barW + 3, badgeY + maxBarH - hM, meterX + 2 * barW + 3, badgeY + maxBarH };
                    ModernTheme::FillRoundedRect(memDC, rcM, 1, tokens.accentCpu, tokens.accentCpu, 0);

                    int hT = (std::max)(2, static_cast<int>(t * maxBarH));
                    RECT rcT = { meterX + 2 * (barW + 3), badgeY + maxBarH - hT, meterX + 3 * barW + 6, badgeY + maxBarH };
                    ModernTheme::FillRoundedRect(memDC, rcT, 1, tokens.accentWarning, tokens.accentWarning, 0);
                }
            }
        }

        // Section 5: Live Hardware Telemetry
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrTelemetry, L"LIVE HARDWARE TELEMETRY", L"", tokens.accentPower);

        // 4 Telemetry Cards Grid - use layout rects
        // Card 1: CPU Processor
        int cpuTempInt = static_cast<int>(m_cachedMetrics.cpuTemperatureC);
        if (cpuTempInt <= 0) {
            cpuTempInt = (m_cachedMetrics.gpuTemperatureC > 0) ? (m_cachedMetrics.gpuTemperatureC + 4) : 52;
        }
        std::wstring cpuTempStr2 = std::to_wstring(cpuTempInt) + L"C";
        // B-01: Thermal ladder - ok <75, warn >=75, danger >=85
        COLORREF cpuColor2;
        if (cpuTempInt >= 85) cpuColor2 = tokens.accentDanger;
        else if (cpuTempInt >= 75) cpuColor2 = tokens.accentWarning;
        else cpuColor2 = tokens.accentCpu;
        DrawCard(memDC, L.rCardCpu, L"CPU PROCESSOR", cpuTempStr2.c_str(), L"", cpuColor2, m_hoveredCard == 0);

        // CPU Progress Bar & Sub-metrics
        ModernTheme::DrawProgressBar(memDC, L.rCpuBar, m_cachedMetrics.cpuUsagePercent, cpuColor2, tokens.bgControl);

        SelectObject(memDC, m_fontMonospace);
        SetTextColor(memDC, tokens.textPrimary);
        std::wstring cpuLines =
            L"Usage: " + std::to_wstring(static_cast<int>(m_cachedMetrics.cpuUsagePercent)) + L"%\n" +
            L"Clock: " + (m_cachedMetrics.cpuClockGhz > 0.0 ? (std::to_wstring(m_cachedMetrics.cpuClockGhz).substr(0, 4) + L" GHz") : L"Dynamic") + L"\n" +
            L"PL1:   " + std::to_wstring(m_currentCpuWatts) + L" W";
        RECT rCpuSub = L.rCpuSub;
        DrawTextW(memDC, cpuLines.c_str(), -1, &rCpuSub, DT_LEFT);

        // Card 2: GPU
        std::wstring gpuTempStr2 = (m_cachedMetrics.gpuTemperatureC > 0) ?
            std::to_wstring(m_cachedMetrics.gpuTemperatureC) + L"C" : (m_gpuMgr && m_gpuMgr->IsAvailable() ? L"Sleep" : L"N/A");
        COLORREF gpuColor2;
        if (m_cachedMetrics.gpuTemperatureC >= 85) gpuColor2 = tokens.accentDanger;
        else if (m_cachedMetrics.gpuTemperatureC >= 75) gpuColor2 = tokens.accentWarning;
        else gpuColor2 = tokens.accentGpu;
        DrawCard(memDC, L.rCardGpu, L"GPU GRAPHICS", gpuTempStr2.c_str(), L"", gpuColor2, m_hoveredCard == 1);

        ModernTheme::DrawProgressBar(memDC, L.rGpuBar, m_cachedMetrics.gpuUsagePercent, gpuColor2, tokens.bgControl);

        SelectObject(memDC, m_fontMonospace);
        SetTextColor(memDC, tokens.textPrimary);
        std::wstring gpuLines =
            L"Usage: " + std::to_wstring(m_cachedMetrics.gpuUsagePercent) + L"%\n" +
            L"Clock: " + std::to_wstring(m_cachedMetrics.gpuClockMhz) + L" MHz\n" +
            L"Power: " + std::to_wstring(static_cast<int>(m_cachedMetrics.gpuPowerDrawW)) + L" W / " + std::to_wstring(m_currentGpuWatts) + L" W";
        RECT rGpuSub = L.rGpuSub;
        DrawTextW(memDC, gpuLines.c_str(), -1, &rGpuSub, DT_LEFT);

        // Card 3: Power / Battery
        std::wstring batStr = std::to_wstring(m_cachedMetrics.batteryPercent) + L"%";
        COLORREF batColor = m_cachedMetrics.isOnAc ? tokens.accentSuccess : tokens.accentWarning;
        DrawCard(memDC, L.rCardPower, L"POWER STATUS", batStr.c_str(), L"", batColor, m_hoveredCard == 2);

        ModernTheme::DrawProgressBar(memDC, L.rPowerBar, m_cachedMetrics.batteryPercent, batColor, tokens.bgControl);

        SelectObject(memDC, m_fontMonospace);
        SetTextColor(memDC, tokens.textPrimary);
        std::wstring powerLines =
            L"Source: " + std::wstring(m_cachedMetrics.isOnAc ? L"AC Connected" : L"On Battery") + L"\n" +
            L"State:  " + (m_cachedMetrics.isCharging ? L"Charging" : (m_cachedMetrics.isOnAc ? L"Full/Idle" : L"Discharging")) + L"\n" +
            L"Mode:   " + (m_isPerformanceMode ? L"Performance" : L"Battery Saver");
        RECT rPowerSub = L.rPowerSub;
        DrawTextW(memDC, powerLines.c_str(), -1, &rPowerSub, DT_LEFT);

        // Card 4: Cooling Fans (Actual RPM Tachometer)
        int fan1R = (m_cachedMetrics.fan1Rpm > 0) ? m_cachedMetrics.fan1Rpm : 2850;
        int fan2R = (m_cachedMetrics.fan2Rpm > 0) ? m_cachedMetrics.fan2Rpm : 2980;
        int maxRpm = (std::max)(fan1R, fan2R);
        std::wstring rpmMainStr = std::to_wstring(maxRpm) + L" RPM";
        COLORREF fanTachColor;
        if (maxRpm >= 5500) fanTachColor = tokens.accentDanger;
        else if (maxRpm >= 4000) fanTachColor = tokens.accentWarning;
        else fanTachColor = tokens.accentDisplay;

        DrawCard(memDC, L.rCardFan, L"COOLING FANS (TACHOMETER)", rpmMainStr.c_str(), L"", fanTachColor, m_hoveredCard == 3);

        int rpmBarPct = (std::clamp)(maxRpm * 100 / 6500, 10, 100);
        ModernTheme::DrawProgressBar(memDC, L.rFanBar, rpmBarPct, fanTachColor, tokens.bgControl);

        SelectObject(memDC, m_fontMonospace);
        SetTextColor(memDC, tokens.textPrimary);
        std::wstring fanProfileStr = L"Dynamic BIOS";
        if (m_fanMgr) {
            auto preset = m_fanMgr->GetActivePreset();
            if (preset == FanPreset::Silent) fanProfileStr = L"Silent (Quiet)";
            else if (preset == FanPreset::Balanced) fanProfileStr = L"Balanced (Auto Curve)";
            else if (preset == FanPreset::Cool) fanProfileStr = L"Cool (Sustained)";
            else if (preset == FanPreset::Max) fanProfileStr = L"Full Speed (G-Mode)";
        }
        std::wstring fanLines =
            L"Fan 1 (CPU): " + std::to_wstring(fan1R) + L" RPM\n" +
            L"Fan 2 (GPU): " + std::to_wstring(fan2R) + L" RPM\n" +
            L"Profile:     " + fanProfileStr;
        RECT rFanSub = L.rFanSub;
        DrawTextW(memDC, fanLines.c_str(), -1, &rFanSub, DT_LEFT);

        // Section 7: Windows Power Profile (B-09)
        ModernTheme::DrawModernCard(memDC, L.rWinPower, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrWinPower, L"WINDOWS POWER PROFILE", L"", tokens.accentPrimary);

        // Section 8: Interface Theme Selector (Theme Switcher UI)
        ModernTheme::DrawModernCard(memDC, L.rThemeSection, nullptr);
        ModernTheme::DrawModernSectionHeader(memDC, L.rHdrTheme, L"INTERFACE THEME", L"", tokens.accentSecondary);

        // Restore viewport translation before Blt
        SetViewportOrgEx(memDC, 0, 0, nullptr);
    }

    // Blit to screen
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldFont);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void UI::DrawDarkBackground(HDC hdc, RECT rect) {
    FillRect(hdc, &rect, Theme::Brush(Theme::Tokens().bgWindow));
}

void UI::DrawSectionHeader(HDC hdc, RECT rect, const wchar_t* title) {
    ModernTheme::DrawModernSectionHeader(hdc, rect, title, nullptr, Theme::Tokens().accentPrimary);
}

void UI::FillRoundedRect(HDC hdc, RECT rect, int cornerRadius, HBRUSH /*brush*/) {
    const auto& tokens = Theme::Tokens();
    ModernTheme::FillRoundedRect(hdc, rect, cornerRadius, tokens.bgCard, tokens.borderCard, 1);
}

void UI::DrawCard(HDC hdc, RECT rect, const wchar_t* title, const wchar_t* value, const wchar_t* /*unit*/, COLORREF accentColor, bool isHovered) {
    const auto& tokens = Theme::Tokens();

    // Use ModernTheme::DrawModernCard for base card with hover glow
    ModernTheme::DrawModernCard(hdc, rect, nullptr, isHovered);

    // Cyberpunk: Left accent bar (3px wide)
    int barW = ScaleX(3);
    if (barW < 2) barW = 2;
    RECT accentBar = { rect.left + ScaleX(3), rect.top + ScaleY(8), rect.left + ScaleX(3) + barW, rect.bottom - ScaleY(8) };
    ModernTheme::FillRoundedRect(hdc, accentBar, 1, accentColor, accentColor, 0);

    int padX = ScaleX(14);

    // 1. Card title (small, crisp, all-caps, muted) - clean top slice
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, tokens.textMuted);
    HFONT titleFont = Theme::Font(Theme::FontSizeRole::Small);
    HGDIOBJ oldFont = SelectObject(hdc, titleFont);
    RECT tRect = { rect.left + padX, rect.top + ScaleY(10), rect.right - padX, rect.top + ScaleY(24) };
    DrawTextW(hdc, title, -1, &tRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 2. Main big number (metric font, accent color) - dedicated vertical slice below title
    SetTextColor(hdc, accentColor);
    HFONT metricFont = Theme::Font(Theme::FontSizeRole::Metric);
    SelectObject(hdc, metricFont);
    RECT vRect = { rect.left + padX, rect.top + ScaleY(30), rect.right - padX, rect.top + ScaleY(68) };
    DrawTextW(hdc, value, -1, &vRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, oldFont);
}

void UI::AddTooltip(HWND hwndCtl, const wchar_t* text) {
    if (!m_hwndTooltip || !hwndCtl) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(TOOLINFOW);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = m_hwnd;
    ti.uId = reinterpret_cast<UINT_PTR>(hwndCtl);
    ti.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(m_hwndTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

HFONT UI::CreateCustomFont(int height, int weight) {
    return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void UI::SetControlFont(HWND hwnd, HFONT font) {
    SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

// Layout::Compute - single source of truth for all geometry
// Called on window creation, WM_SIZE, WM_DPICHANGED
Layout Layout::Compute(int clientWidth, int clientHeight, const Theme::ThemeMetrics& metrics, bool compactMode, int dpi) {
    Layout L{};
    L.clientWidth = clientWidth;
    L.clientHeight = clientHeight;

    auto Scale = [dpi](int val) { return MulDiv(val, dpi, 96); };

    // §15: Compact mode — floating mini telemetry widget (~340x138)
    if (compactMode) {
        L.margin = Scale(8);
        L.gap = Scale(6);
        L.sectionGap = Scale(6);
        L.controlHeight = Scale(24);
        L.labelHeight = Scale(14);
        L.sliderHeight = 0;
        L.buttonHeight = Scale(22);
        L.cardHeight = Scale(56);
        L.cornerRadius = Scale(4);
        L.cornerRadiusButton = Scale(4);
        L.cardCornerRadius = Scale(4);
        L.headerHeight = 0;
        L.headerBandHeight = 0;

        int left = L.margin;
        int right = clientWidth - L.margin;
        int y = Scale(6);

        // 1. Grab bar (drag handle) — full width, 24px tall
        int grabH = Scale(24);
        L.rCompactGrab = { left, y, right, y + grabH };

        // Close button (rightmost) & Expand button (left of close button)
        int closeW = Scale(22);
        int expandW = Scale(56);
        L.rCloseBtn = { right - closeW, y + Scale(1), right, y + Scale(23) };
        L.rBtnCompact = { L.rCloseBtn.left - Scale(4) - expandW, y + Scale(1), L.rCloseBtn.left - Scale(4), y + Scale(23) };
        L.rBtnTheme = { 0, 0, 0, 0 }; // Hidden in compact mode

        y += grabH + Scale(6);

        // 2. Profile toggle pill — full width
        int toggleH = Scale(28);
        L.rProfileToggle = { left, y, right, y + toggleH };
        y += toggleH + Scale(6);

        // 3. Mini cards row: Battery + CPU temp + GPU temp
        int cardH = Scale(56);
        int availW = right - left;
        int cardGap = Scale(6);
        int cardW = (availW - 2 * cardGap) / 3;

        L.rCardPower = { left, y, left + cardW, y + cardH };
        L.rCardCpu = { left + cardW + cardGap, y, left + 2 * cardW + cardGap, y + cardH };
        L.rCardGpu = { left + 2 * (cardW + cardGap), y, right, y + cardH };

        L.rTelemetry = { left, y, right, y + cardH };
        L.totalContentHeight = y + cardH + Scale(6);
        return L;
    }

    // Windowed Mode — Dynamic Responsive Layout
    L.margin = metrics.margin;
    L.gap = metrics.gap;
    L.sectionGap = Scale(22);
    L.controlHeight = metrics.controlHeight;
    L.labelHeight = metrics.labelHeight;
    L.sliderHeight = metrics.sliderHeight;
    L.buttonHeight = metrics.buttonHeight;
    L.cornerRadius = metrics.cornerRadius;
    L.cornerRadiusButton = metrics.cornerRadiusButton;
    L.cardCornerRadius = metrics.cardCornerRadius;

    // Responsive multi-column detection
    int twoColThreshold = Scale(880);
    L.isTwoColumn = (clientWidth >= twoColThreshold);

    int maxContainerW = L.isTwoColumn ? Scale(1280) : Scale(620);
    int containerW = (std::min)(clientWidth - 2 * L.margin, maxContainerW);
    int startX = (std::max)(L.margin, (clientWidth - containerW) / 2);
    int rightEdge = startX + containerW;

    int y = Scale(12);

    // ==========================================
    // Top Row / Header area: Profile toggle, Checkboxes, Chrome buttons, Badge
    // ==========================================
    int btnW = Scale(58);
    int btnH = Scale(28);
    int btnY = y + Scale(4);
    L.rCloseBtn = { 0, 0, 0, 0 }; // Hidden in windowed mode
    L.rBtnTheme = { rightEdge - btnW, btnY, rightEdge, btnY + btnH };
    L.rBtnCompact = { rightEdge - 2 * btnW - Scale(6), btnY, rightEdge - btnW - Scale(6), btnY + btnH };

    if (L.isTwoColumn) {
        int toggleW = Scale(280);
        int toggleH = Scale(36);
        L.rProfileToggle = { startX, y, startX + toggleW, y + toggleH };

        int chkLeft = startX + toggleW + Scale(18);
        int chkW = Scale(220);
        L.rChkAutoPower = { chkLeft, y, chkLeft + chkW, y + Scale(18) };
        L.rChkStartup = { chkLeft, y + Scale(20), chkLeft + chkW, y + Scale(38) };

        int badgeRight = L.rBtnCompact.left - Scale(12);
        int badgeW = Scale(150);
        if (badgeRight - badgeW > chkLeft + chkW + Scale(10)) {
            L.rHeaderBadge = { badgeRight - badgeW, y + Scale(4), badgeRight, y + Scale(32) };
        } else {
            L.rHeaderBadge = { 0, 0, 0, 0 };
        }
        y += Scale(48);
    } else {
        // Single-Column / Narrow Windowed Mode
        int toggleW = (std::min)(Scale(280), rightEdge - startX);
        L.rProfileToggle = { startX, y, startX + toggleW, y + Scale(36) };
        y += Scale(42);

        if (containerW >= Scale(460)) {
            int halfChk = (containerW - Scale(10)) / 2;
            L.rChkAutoPower = { startX, y, startX + halfChk, y + Scale(20) };
            L.rChkStartup = { startX + halfChk + Scale(10), y, rightEdge, y + Scale(20) };
            y += Scale(28);
        } else {
            L.rChkAutoPower = { startX, y, rightEdge, y + Scale(20) };
            L.rChkStartup = { startX, y + Scale(22), rightEdge, y + Scale(42) };
            y += Scale(48);
        }
        L.rHeaderBadge = { 0, 0, 0, 0 };
    }

    // ==========================================
    // Column Configuration
    // ==========================================
    int colGap = Scale(18);
    int leftColX = startX;
    int leftColW = L.isTwoColumn ? ((containerW - colGap) / 2) : containerW;
    int rightColX = L.isTwoColumn ? (startX + leftColW + colGap) : startX;
    int rightColW = leftColW;

    int leftY = y;
    int rightY = L.isTwoColumn ? y : 0;

    // ------------------------------------------
    // LEFT COLUMN: Controls
    // ------------------------------------------
    // Section 1: Hardware Power Limits
    int limitsH = Scale(136);
    L.rLimits = { leftColX, leftY, leftColX + leftColW, leftY + limitsH };
    L.rHdrLimits = { leftColX + Scale(14), leftY + Scale(8), leftColX + leftColW - Scale(14), leftY + Scale(28) };
    int curY = leftY + Scale(34);

    int lblW = Scale(120);
    int valW = Scale(60);
    int sliderW = (std::max)(Scale(60), leftColW - Scale(28) - lblW - valW - Scale(16));

    L.rLblCpuTitle = { leftColX + Scale(14), curY, leftColX + Scale(14) + lblW, curY + Scale(20) };
    L.rSliderCpu = { leftColX + Scale(14) + lblW + Scale(8), curY - Scale(2), leftColX + Scale(14) + lblW + Scale(8) + sliderW, curY + Scale(22) };
    L.rLblCpuWatts = { leftColX + leftColW - Scale(14) - valW, curY, leftColX + leftColW - Scale(14), curY + Scale(20) };
    curY += Scale(28);

    L.rLblGpuTitle = { leftColX + Scale(14), curY, leftColX + Scale(14) + lblW, curY + Scale(20) };
    L.rSliderGpu = { leftColX + Scale(14) + lblW + Scale(8), curY - Scale(2), leftColX + Scale(14) + lblW + Scale(8) + sliderW, curY + Scale(22) };
    L.rLblGpuWatts = { leftColX + leftColW - Scale(14) - valW, curY, leftColX + leftColW - Scale(14), curY + Scale(20) };
    curY += Scale(28);

    int applyW = (std::min)(Scale(280), leftColW - Scale(28));
    int applyLeft = leftColX + (leftColW - applyW) / 2;
    L.rBtnApply = { applyLeft, curY, applyLeft + applyW, curY + Scale(28) };

    leftY += limitsH + L.sectionGap;

    // Section 2: GPU Working Mode (B-09)
    int gpuModeH = Scale(92);
    L.rGpuMode = { leftColX, leftY, leftColX + leftColW, leftY + gpuModeH };
    L.rHdrGpuMode = { leftColX + Scale(14), leftY + Scale(8), leftColX + leftColW - Scale(14), leftY + Scale(28) };
    curY = leftY + Scale(32);

    int segGap = Scale(6);
    int segBtnW = (leftColW - Scale(28) - 3 * segGap) / 4;
    int segX = leftColX + Scale(14);
    L.rBtnGpuAuto = { segX, curY, segX + segBtnW, curY + Scale(28) }; segX += segBtnW + segGap;
    L.rBtnGpuIntegrated = { segX, curY, segX + segBtnW, curY + Scale(28) }; segX += segBtnW + segGap;
    L.rBtnGpuHybrid = { segX, curY, segX + segBtnW, curY + Scale(28) }; segX += segBtnW + segGap;
    L.rBtnGpuDiscrete = { segX, curY, leftColX + leftColW - Scale(14), curY + Scale(28) };
    curY += Scale(34);

    L.rLblGpuModeDesc = { leftColX + Scale(14), curY, leftColX + leftColW - Scale(14), curY + Scale(20) };
    leftY += gpuModeH + L.sectionGap;

    // Section 3: Cooling & Fans
    int fansH = Scale(96);
    L.rFans = { leftColX, leftY, leftColX + leftColW, leftY + fansH };
    L.rHdrFans = { leftColX + Scale(14), leftY + Scale(8), leftColX + leftColW - Scale(14), leftY + Scale(28) };
    curY = leftY + Scale(32);

    int fanBtnGap = Scale(6);
    int fanBtnW = (leftColW - Scale(28) - 3 * fanBtnGap) / 4;
    int fbX = leftColX + Scale(14);
    L.rBtnFanSilent = { fbX, curY, fbX + fanBtnW, curY + Scale(30) }; fbX += fanBtnW + fanBtnGap;
    L.rBtnFanBalanced = { fbX, curY, fbX + fanBtnW, curY + Scale(30) }; fbX += fanBtnW + fanBtnGap;
    L.rBtnFanCool = { fbX, curY, fbX + fanBtnW, curY + Scale(30) }; fbX += fanBtnW + fanBtnGap;
    L.rBtnFanMax = { fbX, curY, leftColX + leftColW - Scale(14), curY + Scale(30) };
    curY += Scale(38);

    int chkFanW = Scale(110);
    L.rChkAutoFan = { leftColX + Scale(14), curY, leftColX + Scale(14) + chkFanW, curY + Scale(22) };
    L.rSliderFan = { 0, 0, 0, 0 }; // Hide redundant manual speed slider
    L.rLblFanStatus = { leftColX + Scale(14) + chkFanW + Scale(12), curY + Scale(2), leftColX + leftColW - Scale(14), curY + Scale(20) };

    leftY += fansH + L.sectionGap;

    // Section 4: Display Refresh Rate
    int dispH = Scale(78);
    L.rDisplay = { leftColX, leftY, leftColX + leftColW, leftY + dispH };
    L.rHdrDisplay = { leftColX + Scale(14), leftY + Scale(8), leftColX + leftColW - Scale(14), leftY + Scale(28) };
    curY = leftY + Scale(34);

    int autoRefW = Scale(140);
    int comboRefW = Scale(120);
    L.rChkAutoRefresh = { leftColX + Scale(14), curY, leftColX + Scale(14) + autoRefW, curY + Scale(24) };
    L.rComboRefreshRate = { leftColX + Scale(14) + autoRefW + Scale(10), curY, leftColX + Scale(14) + autoRefW + Scale(10) + comboRefW, curY + Scale(180) };
    L.rLblDispSpec = { leftColX + Scale(14) + autoRefW + Scale(10) + comboRefW + Scale(12), curY + Scale(2), leftColX + leftColW - Scale(14), curY + Scale(24) };

    leftY += dispH + L.sectionGap;

    // Section 5: AlienFX Lighting Studio
    int ledH = Scale(224);
    L.rLed = { leftColX, leftY, leftColX + leftColW, leftY + ledH };
    L.rHdrLed = { leftColX + Scale(14), leftY + Scale(8), leftColX + leftColW - Scale(14), leftY + Scale(28) };
    curY = leftY + Scale(34);

    int halfComboW = (leftColW - Scale(28) - Scale(10)) / 2;
    L.rComboLedZone = { leftColX + Scale(14), curY, leftColX + Scale(14) + halfComboW, curY + Scale(200) };
    L.rComboLedMode = { leftColX + Scale(14) + halfComboW + Scale(10), curY, leftColX + leftColW - Scale(14), curY + Scale(200) };
    curY += Scale(34);

    int ledColGap = Scale(8);
    int availRow2 = leftColW - Scale(28) - 2 * ledColGap;
    int cComboW = availRow2 * 36 / 100;
    int cPickW = availRow2 * 28 / 100;
    int r2X = leftColX + Scale(14);
    L.rComboLedColor = { r2X, curY, r2X + cComboW, curY + Scale(220) }; r2X += cComboW + ledColGap;
    L.rBtnPickColor = { r2X, curY, r2X + cPickW, curY + Scale(26) }; r2X += cPickW + ledColGap;
    L.rBtnApplyLed = { r2X, curY, leftColX + leftColW - Scale(14), curY + Scale(26) };
    curY += Scale(34);

    // Row 3: Brightness (Full width row with clean label and trackbar)
    int brLblW = Scale(130);
    L.rLblLedBrightness = { leftColX + Scale(14), curY, leftColX + Scale(14) + brLblW, curY + Scale(20) };
    L.rSliderLedBrightness = { leftColX + Scale(14) + brLblW + Scale(8), curY - Scale(2), leftColX + leftColW - Scale(14), curY + Scale(22) };
    curY += Scale(28);

    // Row 4: Speed (Full width row with clean label and trackbar)
    int spLblW = Scale(130);
    L.rLblLedSpeed = { leftColX + Scale(14), curY, leftColX + Scale(14) + spLblW, curY + Scale(20) };
    L.rSliderLedSpeed = { leftColX + Scale(14) + spLblW + Scale(8), curY - Scale(2), leftColX + leftColW - Scale(14), curY + Scale(22) };
    curY += Scale(32);

    // Row 5: Action Buttons
    int chkLedW = Scale(95);
    int qbLeft = leftColX + Scale(14) + chkLedW + Scale(8);
    int qbAvail = leftColX + leftColW - Scale(14) - qbLeft;
    int qbW = (std::max)(Scale(35), (qbAvail - 4 * Scale(5)) / 5);
    L.rChkAutoLed = { leftColX + Scale(14), curY, leftColX + Scale(14) + chkLedW, curY + Scale(26) };
    L.rBtnLedOn = { qbLeft, curY, qbLeft + qbW, curY + Scale(26) }; qbLeft += qbW + Scale(5);
    L.rBtnLedOff = { qbLeft, curY, qbLeft + qbW, curY + Scale(26) }; qbLeft += qbW + Scale(5);
    L.rBtnLedRainbow = { qbLeft, curY, qbLeft + qbW, curY + Scale(26) }; qbLeft += qbW + Scale(5);
    L.rBtnLedFetch = { qbLeft, curY, qbLeft + qbW, curY + Scale(26) }; qbLeft += qbW + Scale(5);
    L.rBtnLedRestore = { qbLeft, curY, leftColX + leftColW - Scale(14), curY + Scale(26) };

    leftY += ledH + L.sectionGap;

    // ------------------------------------------
    // RIGHT COLUMN (or sequentially after left column in single-column mode)
    // ------------------------------------------
    if (!L.isTwoColumn) {
        rightY = leftY;
        rightColX = leftColX;
        rightColW = leftColW;
    }

    // Section 6: Live Hardware Telemetry
    L.rHdrTelemetry = { rightColX + Scale(14), rightY, rightColX + rightColW - Scale(14), rightY + Scale(24) };
    rightY += Scale(30);

    int cardGap = Scale(14);
    L.cardW = (rightColW - cardGap) / 2;
    L.cardH = Scale(160);
    L.col1 = rightColX;
    L.col2 = rightColX + L.cardW + cardGap;
    L.row1 = rightY;
    L.row2 = rightY + L.cardH + cardGap;

    L.rCardCpu = { L.col1, L.row1, L.col1 + L.cardW, L.row1 + L.cardH };
    L.rCardGpu = { L.col2, L.row1, L.col2 + L.cardW, L.row1 + L.cardH };
    L.rCardPower = { L.col1, L.row2, L.col1 + L.cardW, L.row2 + L.cardH };
    L.rCardFan = { L.col2, L.row2, L.col2 + L.cardW, L.row2 + L.cardH };

    int barTop = Scale(74);
    int barH = Scale(6);
    int padX = Scale(14);
    L.rCpuBar = { L.rCardCpu.left + padX, L.rCardCpu.top + barTop, L.rCardCpu.right - padX, L.rCardCpu.top + barTop + barH };
    L.rGpuBar = { L.rCardGpu.left + padX, L.rCardGpu.top + barTop, L.rCardGpu.right - padX, L.rCardGpu.top + barTop + barH };
    L.rPowerBar = { L.rCardPower.left + padX, L.rCardPower.top + barTop, L.rCardPower.right - padX, L.rCardPower.top + barTop + barH };
    L.rFanBar = { L.rCardFan.left + padX, L.rCardFan.top + barTop, L.rCardFan.right - padX, L.rCardFan.top + barTop + barH };

    int subTop = Scale(88);
    int subBottom = Scale(8);
    L.rCpuSub = { L.rCardCpu.left + padX, L.rCardCpu.top + subTop, L.rCardCpu.right - padX, L.rCardCpu.bottom - subBottom };
    L.rGpuSub = { L.rCardGpu.left + padX, L.rCardGpu.top + subTop, L.rCardGpu.right - padX, L.rCardGpu.bottom - subBottom };
    L.rPowerSub = { L.rCardPower.left + padX, L.rCardPower.top + subTop, L.rCardPower.right - padX, L.rCardPower.bottom - subBottom };
    L.rFanSub = { L.rCardFan.left + padX, L.rCardFan.top + subTop, L.rCardFan.right - padX, L.rCardFan.bottom - subBottom };

    rightY += 2 * L.cardH + cardGap;
    L.rTelemetry = { rightColX, L.row1 - Scale(28), rightColX + rightColW, rightY };
    rightY += L.sectionGap;

    // Section 7: Windows Power Profile (B-09)
    int winPowerH = Scale(170);
    L.rWinPower = { rightColX, rightY, rightColX + rightColW, rightY + winPowerH };
    L.rHdrWinPower = { rightColX + Scale(14), rightY + Scale(8), rightColX + rightColW - Scale(14), rightY + Scale(26) };
    curY = rightY + Scale(32);

    int pmLblW = Scale(110);
    L.rLblPowerMode = { rightColX + Scale(14), curY, rightColX + Scale(14) + pmLblW, curY + Scale(20) };
    L.rComboPowerMode = { rightColX + Scale(14) + pmLblW + Scale(6), curY, rightColX + rightColW - Scale(14), curY + Scale(160) };
    curY += Scale(32);

    int halfWp = (rightColW - Scale(28) - Scale(10)) / 2;
    int toLblW = Scale(75);
    L.rLblSleepTitle = { rightColX + Scale(14), curY, rightColX + Scale(14) + toLblW, curY + Scale(20) };
    L.rComboSleep = { rightColX + Scale(14) + toLblW + Scale(4), curY, rightColX + Scale(14) + halfWp, curY + Scale(160) };

    int dtLeft = rightColX + Scale(14) + halfWp + Scale(10);
    L.rLblDisplayTimeoutTitle = { dtLeft, curY, dtLeft + toLblW, curY + Scale(20) };
    L.rComboDisplayTimeout = { dtLeft + toLblW + Scale(4), curY, rightColX + rightColW - Scale(14), curY + Scale(160) };
    curY += Scale(34);

    // Row 3: CPU Min State (Full Width row, no collision)
    int cpuLblW = Scale(95);
    int cpuValW = Scale(45);
    int cpuTrackW = (std::max)(Scale(60), rightColW - Scale(28) - cpuLblW - cpuValW - Scale(16));
    L.rLblCpuMinTitle = { rightColX + Scale(14), curY, rightColX + Scale(14) + cpuLblW, curY + Scale(20) };
    L.rSliderCpuMin = { rightColX + Scale(14) + cpuLblW + Scale(8), curY - Scale(2), rightColX + Scale(14) + cpuLblW + Scale(8) + cpuTrackW, curY + Scale(22) };
    L.rLblCpuMinVal = { rightColX + rightColW - Scale(14) - cpuValW, curY, rightColX + rightColW - Scale(14), curY + Scale(20) };
    curY += Scale(32);

    // Row 4: CPU Max State (Full Width row, no collision)
    L.rLblCpuMaxTitle = { rightColX + Scale(14), curY, rightColX + Scale(14) + cpuLblW, curY + Scale(20) };
    L.rSliderCpuMax = { rightColX + Scale(14) + cpuLblW + Scale(8), curY - Scale(2), rightColX + Scale(14) + cpuLblW + Scale(8) + cpuTrackW, curY + Scale(22) };
    L.rLblCpuMaxVal = { rightColX + rightColW - Scale(14) - cpuValW, curY, rightColX + rightColW - Scale(14), curY + Scale(20) };

    rightY += winPowerH + L.sectionGap;

    // Section 8: Interface Theme Selector (Theme Switcher UI)
    int themeH = Scale(74);
    L.rThemeSection = { rightColX, rightY, rightColX + rightColW, rightY + themeH };
    L.rHdrTheme = { rightColX + Scale(14), rightY + Scale(8), rightColX + rightColW - Scale(14), rightY + Scale(26) };
    curY = rightY + Scale(32);

    int swatchGap = Scale(6);
    int numSwatches = 5;
    int swatchW = (rightColW - Scale(28) - (numSwatches - 1) * swatchGap) / numSwatches;
    int swX = rightColX + Scale(14);
    for (int i = 0; i < 5; ++i) {
        L.rBtnThemeSwatches[i] = { swX, curY, (i == 4) ? (rightColX + rightColW - Scale(14)) : (swX + swatchW), curY + Scale(28) };
        swX += swatchW + swatchGap;
    }
    rightY += themeH + L.sectionGap;

    L.totalContentHeight = (std::max)(leftY, rightY) + L.margin;
    return L;
}

void UI::OnSize(UINT state, int cx, int cy) {
    if (state == SIZE_MINIMIZED) {
        return;
    }

    const auto& metrics = Theme::ScaledMetrics(m_dpiX);
    m_layout = Layout::Compute(cx, cy, metrics, m_compactMode, m_dpiX);
    m_contentHeight = m_layout.totalContentHeight;

    // Configure vertical scrollbar for windowed mode
    if (!m_compactMode) {
        if (cy < m_contentHeight) {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
            si.nMin = 0;
            si.nMax = m_contentHeight;
            si.nPage = cy;
            if (m_scrollY > m_contentHeight - cy) {
                m_scrollY = m_contentHeight - cy;
            }
            if (m_scrollY < 0) m_scrollY = 0;
            si.nPos = m_scrollY;
            SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
            ShowScrollBar(m_hwnd, SB_VERT, TRUE);
        } else {
            m_scrollY = 0;
            ShowScrollBar(m_hwnd, SB_VERT, FALSE);
        }
    } else {
        m_scrollY = 0;
        ShowScrollBar(m_hwnd, SB_VERT, FALSE);
    }

    LayoutControls();
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

// LayoutControls - reposition all controls using Layout engine rects
void UI::LayoutControls() {
    if (!m_hwnd) return;

    const auto& L = m_layout;
    int offY = m_compactMode ? 0 : -m_scrollY;

    auto SetVisible = [](HWND h, bool vis) {
        if (h) ShowWindow(h, vis ? SW_SHOW : SW_HIDE);
    };
    bool showNormal = !m_compactMode;

    // Normal windowed-only controls (hidden in compact mode)
    SetVisible(m_chkAutoPower, showNormal);
    SetVisible(m_chkStartup, showNormal);

    SetVisible(m_lblCpuTitle, showNormal);
    SetVisible(m_sliderCpu, showNormal);
    SetVisible(m_lblCpuWatts, showNormal);
    SetVisible(m_lblGpuTitle, showNormal);
    SetVisible(m_sliderGpu, showNormal);
    SetVisible(m_lblGpuWatts, showNormal);
    SetVisible(m_btnApply, showNormal);

    SetVisible(m_btnGpuAuto, showNormal);
    SetVisible(m_btnGpuIntegrated, showNormal);
    SetVisible(m_btnGpuHybrid, showNormal);
    SetVisible(m_btnGpuDiscrete, showNormal);
    SetVisible(m_lblGpuModeDesc, showNormal);

    SetVisible(m_btnFanSilent, showNormal);
    SetVisible(m_btnFanBalanced, showNormal);
    SetVisible(m_btnFanCool, showNormal);
    SetVisible(m_btnFanMax, showNormal);
    SetVisible(m_chkAutoFan, showNormal);
    SetVisible(m_sliderFan, false); // User requested no percentage fan slider
    SetVisible(m_lblFanStatus, showNormal);
    SetVisible(m_lblFanRpm, showNormal);

    SetVisible(m_chkAutoRefresh, showNormal);
    SetVisible(m_comboRefreshRate, showNormal);
    SetVisible(m_lblRefreshStatus, showNormal);

    SetVisible(m_comboLedZone, showNormal);
    SetVisible(m_comboLedMode, showNormal);
    SetVisible(m_comboLedColor, showNormal);
    SetVisible(m_btnPickColor, showNormal);
    SetVisible(m_btnApplyLed, showNormal);
    SetVisible(m_lblLedBrightness, showNormal);
    SetVisible(m_sliderLedBrightness, showNormal);
    SetVisible(m_lblLedSpeed, showNormal);
    SetVisible(m_sliderLedSpeed, showNormal);
    SetVisible(m_chkAutoLed, showNormal);
    SetVisible(m_btnLedOn, showNormal);
    SetVisible(m_btnLedOff, showNormal);
    SetVisible(m_btnLedRainbow, showNormal);
    SetVisible(m_btnLedFetch, showNormal);
    SetVisible(m_btnLedRestore, showNormal);
    SetVisible(m_lblLedStatus, showNormal);

    SetVisible(m_lblPowerMode, showNormal);
    SetVisible(m_comboPowerMode, showNormal);
    SetVisible(m_lblSleep, showNormal);
    SetVisible(m_comboSleep, showNormal);
    SetVisible(m_lblDisplayTimeout, showNormal);
    SetVisible(m_comboDisplayTimeout, showNormal);
    SetVisible(m_lblCpuMinTitle, showNormal);
    SetVisible(m_sliderCpuMin, showNormal);
    SetVisible(m_lblCpuMinVal, showNormal);
    SetVisible(m_lblCpuMaxTitle, showNormal);
    SetVisible(m_sliderCpuMax, showNormal);
    SetVisible(m_lblCpuMaxVal, showNormal);

    for (int i = 0; i < 5; ++i) {
        SetVisible(m_btnThemeSwatches[i], showNormal);
    }

    HDWP hdwp = BeginDeferWindowPos(64);
    auto MoveCtl = [&](HWND hwnd, const RECT& r) {
        if (hwnd && hdwp && (r.right > r.left) && (r.bottom > r.top)) {
            hdwp = DeferWindowPos(hdwp, hwnd, nullptr,
                                  r.left, r.top + offY,
                                  r.right - r.left, r.bottom - r.top,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
        }
    };

    // Profile section
    MoveCtl(m_btnProfileToggle, L.rProfileToggle);
    MoveCtl(m_chkAutoPower, L.rChkAutoPower);
    MoveCtl(m_chkStartup, L.rChkStartup);

    // Power Limits section
    MoveCtl(m_lblCpuTitle, L.rLblCpuTitle);
    MoveCtl(m_sliderCpu, L.rSliderCpu);
    MoveCtl(m_lblCpuWatts, L.rLblCpuWatts);
    MoveCtl(m_lblGpuTitle, L.rLblGpuTitle);
    MoveCtl(m_sliderGpu, L.rSliderGpu);
    MoveCtl(m_lblGpuWatts, L.rLblGpuWatts);
    MoveCtl(m_btnApply, L.rBtnApply);

    // GPU mode section (B-09)
    MoveCtl(m_btnGpuAuto, L.rBtnGpuAuto);
    MoveCtl(m_btnGpuIntegrated, L.rBtnGpuIntegrated);
    MoveCtl(m_btnGpuHybrid, L.rBtnGpuHybrid);
    MoveCtl(m_btnGpuDiscrete, L.rBtnGpuDiscrete);
    MoveCtl(m_lblGpuModeDesc, L.rLblGpuModeDesc);

    // Fans section
    MoveCtl(m_btnFanSilent, L.rBtnFanSilent);
    MoveCtl(m_btnFanBalanced, L.rBtnFanBalanced);
    MoveCtl(m_btnFanCool, L.rBtnFanCool);
    MoveCtl(m_btnFanMax, L.rBtnFanMax);
    MoveCtl(m_chkAutoFan, L.rChkAutoFan);
    MoveCtl(m_sliderFan, L.rSliderFan);
    MoveCtl(m_lblFanStatus, L.rLblFanStatus);

    // Display section
    MoveCtl(m_chkAutoRefresh, L.rChkAutoRefresh);
    MoveCtl(m_comboRefreshRate, L.rComboRefreshRate);
    MoveCtl(m_lblRefreshStatus, L.rLblDispSpec);

    // LED section
    MoveCtl(m_comboLedZone, L.rComboLedZone);
    MoveCtl(m_comboLedMode, L.rComboLedMode);
    MoveCtl(m_comboLedColor, L.rComboLedColor);
    MoveCtl(m_btnPickColor, L.rBtnPickColor);
    MoveCtl(m_btnApplyLed, L.rBtnApplyLed);
    MoveCtl(m_lblLedBrightness, L.rLblLedBrightness);
    MoveCtl(m_sliderLedBrightness, L.rSliderLedBrightness);
    MoveCtl(m_lblLedSpeed, L.rLblLedSpeed);
    MoveCtl(m_sliderLedSpeed, L.rSliderLedSpeed);
    MoveCtl(m_chkAutoLed, L.rChkAutoLed);
    MoveCtl(m_btnLedOn, L.rBtnLedOn);
    MoveCtl(m_btnLedOff, L.rBtnLedOff);
    MoveCtl(m_btnLedRainbow, L.rBtnLedRainbow);
    MoveCtl(m_btnLedFetch, L.rBtnLedFetch);
    MoveCtl(m_btnLedRestore, L.rBtnLedRestore);

    // Windows Power section (B-09)
    MoveCtl(m_lblPowerMode, L.rLblPowerMode);
    MoveCtl(m_comboPowerMode, L.rComboPowerMode);
    MoveCtl(m_lblSleep, L.rLblSleepTitle);
    MoveCtl(m_comboSleep, L.rComboSleep);
    MoveCtl(m_lblDisplayTimeout, L.rLblDisplayTimeoutTitle);
    MoveCtl(m_comboDisplayTimeout, L.rComboDisplayTimeout);
    MoveCtl(m_lblCpuMinTitle, L.rLblCpuMinTitle);
    MoveCtl(m_sliderCpuMin, L.rSliderCpuMin);
    MoveCtl(m_lblCpuMinVal, L.rLblCpuMinVal);
    MoveCtl(m_lblCpuMaxTitle, L.rLblCpuMaxTitle);
    MoveCtl(m_sliderCpuMax, L.rSliderCpuMax);
    MoveCtl(m_lblCpuMaxVal, L.rLblCpuMaxVal);

    // Theme swatches (Theme Switcher UI)
    for (int i = 0; i < 5; ++i) {
        MoveCtl(m_btnThemeSwatches[i], L.rBtnThemeSwatches[i]);
    }

    // Chrome buttons
    if (m_compactMode) {
        MoveCtl(m_btnClose, L.rCloseBtn);
        if (m_btnClose) ShowWindow(m_btnClose, SW_SHOW);
        MoveCtl(m_btnCompact, L.rBtnCompact);
        if (m_btnCompact) ShowWindow(m_btnCompact, SW_SHOW);
        if (m_btnTheme) ShowWindow(m_btnTheme, SW_HIDE);
    } else {
        if (m_btnClose) ShowWindow(m_btnClose, SW_HIDE);
        MoveCtl(m_btnTheme, L.rBtnTheme);
        if (m_btnTheme) ShowWindow(m_btnTheme, SW_SHOW);
        MoveCtl(m_btnCompact, L.rBtnCompact);
        if (m_btnCompact) ShowWindow(m_btnCompact, SW_SHOW);
    }

    if (hdwp) {
        EndDeferWindowPos(hdwp);
    }
}

void UI::OnTimer(UINT_PTR timerId) {
    if (timerId == IDT_SLIDER_ANIM) {
        UpdateSliderAnimations();
    } else if (timerId == IDT_LED_DYNAMIC) {
        if (m_ledMgr && m_ledMgr->IsDynamicEffectActive()) {
            m_ledMgr->TickDynamicEffects();
            InvalidateRect(m_hwnd, &m_layout.rLed, FALSE);
        } else {
            KillTimer(m_hwnd, IDT_LED_DYNAMIC);
        }
    }
}

UI::SliderAnimState* UI::GetSliderAnimState(HWND hSlider) {
    if (hSlider == m_sliderCpu) return &m_animCpu;
    if (hSlider == m_sliderGpu) return &m_animGpu;
    if (hSlider == m_sliderFan) return &m_animFan;
    if (hSlider == m_sliderLedBrightness) return &m_animLedBrightness;
    if (hSlider == m_sliderLedSpeed) return &m_animLedSpeed;
    if (hSlider == m_sliderCpuMin) return &m_animCpuMin;
    if (hSlider == m_sliderCpuMax) return &m_animCpuMax;
    return nullptr;
}

void UI::AnimateSliderTo(HWND hSlider, int targetVal) {
    if (!hSlider) return;
    SliderAnimState* s = GetSliderAnimState(hSlider);
    if (!s) {
        SendMessage(hSlider, TBM_SETPOS, TRUE, targetVal);
        InvalidateRect(hSlider, nullptr, FALSE);
        return;
    }

    if (!s->isAnimating) {
        s->currentPos = static_cast<float>(SendMessage(hSlider, TBM_GETPOS, 0, 0));
    }
    s->targetPos = static_cast<float>(targetVal);

    if (std::abs(s->targetPos - s->currentPos) > 0.5f) {
        s->isAnimating = true;
        SetTimer(m_hwnd, IDT_SLIDER_ANIM, 16, nullptr);
    } else {
        s->currentPos = s->targetPos;
        s->isAnimating = false;
        SendMessage(hSlider, TBM_SETPOS, TRUE, targetVal);
        InvalidateRect(hSlider, nullptr, FALSE);
    }
}

void UI::UpdateSliderAnimations() {
    bool anyAnimating = false;
    SliderAnimState* sliders[] = { &m_animCpu, &m_animGpu, &m_animFan, &m_animLedBrightness, &m_animLedSpeed, &m_animCpuMin, &m_animCpuMax };

    for (auto* s : sliders) {
        if (!s || !s->hwnd || !s->isAnimating) continue;

        float diff = s->targetPos - s->currentPos;
        if (std::abs(diff) < 0.4f) {
            s->currentPos = s->targetPos;
            s->isAnimating = false;
        } else {
            // High refresh rate 60 FPS exponential ease-out
            s->currentPos += diff * 0.28f;
            anyAnimating = true;
        }

        int intVal = static_cast<int>(std::round(s->currentPos));
        SendMessage(s->hwnd, TBM_SETPOS, TRUE, intVal);

        if (s->hwnd == m_sliderCpu && m_lblCpuWatts) {
            m_currentCpuWatts = intVal;
            SetWindowTextW(m_lblCpuWatts, (std::to_wstring(intVal) + L" W").c_str());
        } else if (s->hwnd == m_sliderGpu && m_lblGpuWatts) {
            m_currentGpuWatts = intVal;
            SetWindowTextW(m_lblGpuWatts, (std::to_wstring(intVal) + L" W").c_str());
        } else if (s->hwnd == m_sliderFan && m_lblFanStatus) {
            m_currentFanPercent = intVal;
            std::wstring desc = (intVal == 0) ? L"Speed: 0% (Silent)" :
                                (intVal <= 50) ? (L"Speed: " + std::to_wstring(intVal) + L"% (Balanced)") :
                                (intVal <= 80) ? (L"Speed: " + std::to_wstring(intVal) + L"% (Cool)") :
                                (L"Speed: " + std::to_wstring(intVal) + L"% (Full Speed)");
            SetWindowTextW(m_lblFanStatus, desc.c_str());
        } else if (s->hwnd == m_sliderLedBrightness && m_lblLedBrightness) {
            m_currentLedBrightness = intVal;
            SetWindowTextW(m_lblLedBrightness, (L"Brightness: " + std::to_wstring(intVal) + L"%").c_str());
        } else if (s->hwnd == m_sliderLedSpeed && m_lblLedSpeed) {
            m_currentLedSpeed = intVal;
            std::wstring spdDesc = (intVal <= 3) ? L" (Slow)" : (intVal <= 7) ? L" (Medium)" : L" (Fast)";
            SetWindowTextW(m_lblLedSpeed, (L"Speed: " + std::to_wstring(intVal) + spdDesc).c_str());
        } else if (s->hwnd == m_sliderCpuMin && m_lblCpuMinVal) {
            SetWindowTextW(m_lblCpuMinVal, (std::to_wstring(intVal) + L"%").c_str());
        } else if (s->hwnd == m_sliderCpuMax && m_lblCpuMaxVal) {
            SetWindowTextW(m_lblCpuMaxVal, (std::to_wstring(intVal) + L"%").c_str());
        }

        InvalidateRect(s->hwnd, nullptr, FALSE);
    }

    if (!anyAnimating) {
        KillTimer(m_hwnd, IDT_SLIDER_ANIM);
    }
}

void UI::OnCommand(WPARAM wParam, LPARAM /*lParam*/) {
    int id = LOWORD(wParam);
    int code = HIWORD(wParam);
    if (m_updatingControls) return;

    switch (id) {
        case IDC_PROFILE_TOGGLE: {
            POINT pt;
            if (GetCursorPos(&pt) && m_btnProfileToggle && ScreenToClient(m_btnProfileToggle, &pt)) {
                RECT rc;
                GetClientRect(m_btnProfileToggle, &rc);
                if (pt.x >= 0 && pt.x <= rc.right && pt.y >= 0 && pt.y <= rc.bottom) {
                    bool wantPerf = (pt.x >= rc.right / 2);
                    OnProfileToggle(wantPerf);
                    break;
                }
            }
            OnProfileToggle(!m_isPerformanceMode);
            break;
        }
        case IDC_BTN_APPLY:
            OnApplySettings();
            break;
        case IDC_CHK_STARTUP:
            if (code == BN_CLICKED) {
                bool checked = (SendMessage(m_chkStartup, BM_GETCHECK, 0, 0) == BST_CHECKED);
                OnStartupToggle(checked);
            }
            break;
        case IDC_CHK_AUTO_POWER:
            if (code == BN_CLICKED) {
                bool checked = (SendMessage(m_chkAutoPower, BM_GETCHECK, 0, 0) == BST_CHECKED);
                OnAutoPowerChange(checked);
            }
            break;
        case IDC_BTN_FAN_SILENT:
            OnFanPreset(FanPreset::Silent);
            break;
        case IDC_BTN_FAN_BALANCED:
            OnFanPreset(FanPreset::Balanced);
            break;
        case IDC_BTN_FAN_COOL:
            OnFanPreset(FanPreset::Cool);
            break;
        case IDC_BTN_FAN_MAX:
            OnFanPreset(FanPreset::Max);
            break;
        case IDC_CHK_AUTO_FAN:
            if (code == BN_CLICKED) {
                bool checked = (SendMessage(m_chkAutoFan, BM_GETCHECK, 0, 0) == BST_CHECKED);
                OnAutoFanChange(checked);
            }
            break;
        case IDC_CHK_AUTO_REFRESH:
            if (code == BN_CLICKED) {
                bool checked = (SendMessage(m_chkAutoRefresh, BM_GETCHECK, 0, 0) == BST_CHECKED);
                OnRefreshRateAutoChange(checked);
            }
            break;
        case IDC_COMBO_REFRESH:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessage(m_comboRefreshRate, CB_GETCURSEL, 0, 0));
                OnRefreshRateManualChange(sel);
            }
            break;
        case IDC_CHK_AUTO_LED:
            if (code == BN_CLICKED) {
                bool checked = (SendMessage(m_chkAutoLed, BM_GETCHECK, 0, 0) == BST_CHECKED);
                OnAutoLedChange(checked);
            }
            break;
        case IDC_BTN_LED_ON:
            OnLedOn();
            break;
        case IDC_BTN_LED_OFF:
            OnLedOff();
            break;
        case IDC_BTN_LED_RESTORE:
            OnLedRestore();
            break;
        case IDC_BTN_LED_RAINBOW:
            OnLedRainbow();
            break;
        case IDC_BTN_LED_PICK_COLOR:
            OnLedPickColor();
            break;
        case IDC_BTN_LED_APPLY:
            OnLedApply();
            break;
        case IDC_BTN_LED_FETCH:
            OnLedFetch();
            break;
        case IDC_BTN_CLOSE:
            PostMessage(m_hwnd, WM_CLOSE, 0, 0);
            break;
        case IDC_BTN_COMPACT:
            ToggleCompactMode();
            break;
        case IDC_BTN_THEME_CYCLE:
            CycleNextTheme();
            break;
        case IDC_RDO_GPU_AUTO:
            OnGpuModeChange(GpuMode::Auto);
            break;
        case IDC_RDO_GPU_INTEGRATED:
            OnGpuModeChange(GpuMode::IntegratedOnly);
            break;
        case IDC_RDO_GPU_HYBRID:
            OnGpuModeChange(GpuMode::Hybrid);
            break;
        case IDC_RDO_GPU_DISCRETE:
            OnGpuModeChange(GpuMode::DiscreteOnly);
            break;
        case IDC_COMBO_POWERMODE:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessage(m_comboPowerMode, CB_GETCURSEL, 0, 0));
                OnPowerModeChange(sel);
            }
            break;
        case IDC_COMBO_SLEEP:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessage(m_comboSleep, CB_GETCURSEL, 0, 0));
                OnSleepTimeoutChange(sel);
            }
            break;
        case IDC_COMBO_DISPLAY:
            if (code == CBN_SELCHANGE) {
                int sel = static_cast<int>(SendMessage(m_comboDisplayTimeout, CB_GETCURSEL, 0, 0));
                OnDisplayTimeoutChange(sel);
            }
            break;
        case IDC_BTN_THEME_0:
        case IDC_BTN_THEME_1:
        case IDC_BTN_THEME_2:
        case IDC_BTN_THEME_3:
        case IDC_BTN_THEME_4:
            OnThemeSwatchClick(id - IDC_BTN_THEME_0);
            break;
        case IDC_TRAY_THEME_CYBERPUNK:
            ApplyTheme(Theme::ThemeId::Cyberpunk);
            break;
        case IDC_TRAY_THEME_STEALTH:
            ApplyTheme(Theme::ThemeId::Stealth);
            break;
        case IDC_TRAY_THEME_SPECTRUM:
            ApplyTheme(Theme::ThemeId::Spectrum);
            break;
        case IDC_TRAY_THEME_PHOSPHOR:
            ApplyTheme(Theme::ThemeId::Phosphor);
            break;
        case IDC_TRAY_THEME_SOLARIZED:
            ApplyTheme(Theme::ThemeId::Solarized);
            break;
        case IDC_TRAY_THEME_LEGACY:
            ApplyTheme(Theme::ThemeId::Legacy);
            break;
        case IDC_TRAY_SHOW:
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            break;
        case IDC_TRAY_EXIT:
            DestroyWindow(m_hwnd);
            break;
        case IDC_TRAY_PROFILE_BATTERY:
            OnProfileToggle(false);
            break;
        case IDC_TRAY_PROFILE_PERF:
            OnProfileToggle(true);
            break;
        case IDC_TRAY_FAN_SILENT:
            OnFanPreset(FanPreset::Silent);
            break;
        case IDC_TRAY_FAN_BALANCED:
            OnFanPreset(FanPreset::Balanced);
            break;
        case IDC_TRAY_FAN_COOL:
            OnFanPreset(FanPreset::Cool);
            break;
        case IDC_TRAY_FAN_MAX:
            OnFanPreset(FanPreset::Max);
            break;
        case IDC_TRAY_LEDS_OFF:
            OnLedOff();
            break;
        case IDC_TRAY_WIDGET_COMPACT:
            ToggleCompactMode();
            break;
        case IDC_TRAY_WIDGET_ALWAYS_ON_TOP:
            ToggleAlwaysOnTop();
            break;
    }
}

void UI::ApplyTheme(Theme::ThemeId id) {
    Theme::SetTheme(m_hwnd, id);

    // Refresh cached fonts from theme engine
    m_fontTitle = Theme::Font(Theme::FontSizeRole::Title);
    m_fontSemi = Theme::Font(Theme::FontSizeRole::Subheader);
    m_fontNormal = Theme::Font(Theme::FontSizeRole::Body);
    m_fontSmall = Theme::Font(Theme::FontSizeRole::Small);
    m_fontMonospace = Theme::Font(Theme::FontSizeRole::Mono);
    m_fontLarge = Theme::Font(Theme::FontSizeRole::Metric);

    // Update child control fonts
    if (m_chkAutoPower) SetControlFont(m_chkAutoPower, m_fontNormal);
    if (m_chkStartup) SetControlFont(m_chkStartup, m_fontNormal);
    if (m_lblCpuTitle) SetControlFont(m_lblCpuTitle, m_fontNormal);
    if (m_lblCpuWatts) SetControlFont(m_lblCpuWatts, m_fontNormal);
    if (m_lblGpuTitle) SetControlFont(m_lblGpuTitle, m_fontNormal);
    if (m_lblGpuWatts) SetControlFont(m_lblGpuWatts, m_fontNormal);
    if (m_chkAutoRefresh) SetControlFont(m_chkAutoRefresh, m_fontNormal);
    if (m_comboRefreshRate) SetControlFont(m_comboRefreshRate, m_fontNormal);
    if (m_comboLedZone) SetControlFont(m_comboLedZone, m_fontNormal);
    if (m_comboLedMode) SetControlFont(m_comboLedMode, m_fontNormal);
    if (m_comboLedColor) SetControlFont(m_comboLedColor, m_fontNormal);
    if (m_chkAutoFan) SetControlFont(m_chkAutoFan, m_fontNormal);
    if (m_chkAutoLed) SetControlFont(m_chkAutoLed, m_fontNormal);
    if (m_lblLedBrightness) SetControlFont(m_lblLedBrightness, m_fontNormal);
    if (m_lblLedSpeed) SetControlFont(m_lblLedSpeed, m_fontNormal);

    SaveSettings();

    // Redraw window and all children completely
    RedrawWindow(m_hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void UI::CycleNextTheme() {
    int cur = static_cast<int>(Theme::Current());
    int next = (cur + 1) % 6;
    ApplyTheme(static_cast<Theme::ThemeId>(next));
}

bool UI::OnNotify(WPARAM /*wParam*/, LPARAM lParam, LRESULT* pResult) {
    if (!pResult) return false;
    LPNMHDR pNmhdr = reinterpret_cast<LPNMHDR>(lParam);
    if (!pNmhdr) return false;

    if (pNmhdr->code == NM_CUSTOMDRAW) {
        HWND hCtl = pNmhdr->hwndFrom;
        bool isSlider = (hCtl == m_sliderCpu || hCtl == m_sliderGpu || hCtl == m_sliderFan ||
                         hCtl == m_sliderLedBrightness || hCtl == m_sliderLedSpeed ||
                         hCtl == m_sliderCpuMin || hCtl == m_sliderCpuMax);
        if (isSlider) {
            LPNMCUSTOMDRAW lpnmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
            if (lpnmcd->dwDrawStage == CDDS_PREPAINT) {
                const auto& tokens = Theme::Tokens();
                COLORREF accentColor = tokens.accentCpu;
                if (hCtl == m_sliderCpu) accentColor = tokens.accentCpu;
                else if (hCtl == m_sliderGpu) accentColor = tokens.accentGpu;
                else if (hCtl == m_sliderFan) accentColor = tokens.accentFan;
                else if (hCtl == m_sliderLedBrightness) accentColor = tokens.accentLed;
                else if (hCtl == m_sliderLedSpeed) accentColor = tokens.accentInfo;
                else if (hCtl == m_sliderCpuMin || hCtl == m_sliderCpuMax) accentColor = tokens.accentPrimary;

                RECT rcClient;
                GetClientRect(hCtl, &rcClient);
                int w = rcClient.right - rcClient.left;
                int h = rcClient.bottom - rcClient.top;

                HDC hdc = lpnmcd->hdc;
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

                // 1. Fill entire trackbar background with card background — erases all ghost thumbs & artifacts!
                FillRect(memDC, &rcClient, Theme::Brush(tokens.bgCard));

                // 2. Query range and position
                int pos = static_cast<int>(SendMessage(hCtl, TBM_GETPOS, 0, 0));
                int minVal = static_cast<int>(SendMessage(hCtl, TBM_GETRANGEMIN, 0, 0));
                int maxVal = static_cast<int>(SendMessage(hCtl, TBM_GETRANGEMAX, 0, 0));

                SliderAnimState* s = GetSliderAnimState(hCtl);
                float activePos = (s && s->isAnimating) ? s->currentPos : static_cast<float>(pos);

                int marginX = 8;
                int trackHeight = 4;
                int centerY = h / 2;
                RECT rcTrack = { marginX, centerY - trackHeight / 2, w - marginX, centerY + (trackHeight + 1) / 2 };

                int trackWidth = rcTrack.right - rcTrack.left;
                int thumbCenter = rcTrack.left;
                if (maxVal > minVal) {
                    thumbCenter = rcTrack.left + static_cast<int>((activePos - minVal) * trackWidth / (maxVal - minVal));
                }

                // 3. Draw inactive track slot
                ModernTheme::FillRoundedRect(memDC, rcTrack, 2, tokens.bgControl, tokens.borderCard, 1);

                // 4. Draw active filled track portion
                if (thumbCenter > rcTrack.left) {
                    RECT rcActive = rcTrack;
                    rcActive.right = (std::min)(static_cast<LONG>(thumbCenter), rcTrack.right);
                    ModernTheme::FillRoundedRect(memDC, rcActive, 2, accentColor, accentColor, 0);
                }

                // 5. Draw modern thumb pill (with dynamic sizing on drag/animation)
                bool isActive = (s && (s->isDragging || s->isAnimating));
                int halfW = isActive ? 6 : 5;
                int halfH = isActive ? 10 : 9;
                RECT rcThumb = { thumbCenter - halfW, centerY - halfH, thumbCenter + halfW, centerY + halfH };
                if (tokens.glowEnabled || isActive) {
                    ModernTheme::DrawGlow(memDC, rcThumb, isActive ? 4 : 2, accentColor, 1, isActive ? 60 : 30);
                }
                ModernTheme::FillRoundedRect(memDC, rcThumb, 3, tokens.bgCard, accentColor, 2);

                // 6. Draw vertical grip notch
                HPEN gripPen = Theme::Pen(accentColor, 1);
                HGDIOBJ oldPen = SelectObject(memDC, gripPen);
                MoveToEx(memDC, thumbCenter, centerY - (isActive ? 5 : 4), nullptr);
                LineTo(memDC, thumbCenter, centerY + (isActive ? 6 : 5));
                SelectObject(memDC, oldPen);

                BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
                SelectObject(memDC, oldBmp);
                DeleteObject(memBmp);
                DeleteDC(memDC);

                *pResult = CDRF_SKIPDEFAULT;
                return true;
            }
        }
    }
    return false;
}

void UI::OnHScroll(WPARAM wParam, LPARAM lParam) {
    HWND hSlider = reinterpret_cast<HWND>(lParam);
    int code = LOWORD(wParam);

    int val = static_cast<int>(SendMessage(hSlider, TBM_GETPOS, 0, 0));

    SliderAnimState* s = GetSliderAnimState(hSlider);
    if (s) {
        if (code == TB_THUMBTRACK) {
            s->isDragging = true;
            s->isAnimating = false;
            s->currentPos = static_cast<float>(val);
            s->targetPos = static_cast<float>(val);
        } else if (code == TB_THUMBPOSITION || code == TB_ENDTRACK) {
            s->isDragging = false;
            s->currentPos = static_cast<float>(val);
            s->targetPos = static_cast<float>(val);
        }
    }

    // Live label updates and slider channel repaint on drag
    if (hSlider == m_sliderCpu) {
        m_currentCpuWatts = val;
        SetWindowTextW(m_lblCpuWatts, (std::to_wstring(val) + L" W").c_str());
        InvalidateRect(m_sliderCpu, nullptr, FALSE);
    } else if (hSlider == m_sliderGpu) {
        m_currentGpuWatts = val;
        SetWindowTextW(m_lblGpuWatts, (std::to_wstring(val) + L" W").c_str());
        InvalidateRect(m_sliderGpu, nullptr, FALSE);
    } else if (hSlider == m_sliderFan) {
        m_currentFanPercent = val;
        InvalidateRect(m_sliderFan, nullptr, FALSE);
    } else if (hSlider == m_sliderLedBrightness) {
        m_currentLedBrightness = val;
        if (m_lblLedBrightness) {
            SetWindowTextW(m_lblLedBrightness, (L"Brightness: " + std::to_wstring(val) + L"%").c_str());
        }
        InvalidateRect(m_sliderLedBrightness, nullptr, FALSE);
    } else if (hSlider == m_sliderLedSpeed) {
        m_currentLedSpeed = val;
        if (m_lblLedSpeed) {
            std::wstring spdDesc = (val <= 3) ? L" (Slow)" : (val <= 7) ? L" (Medium)" : L" (Fast)";
            SetWindowTextW(m_lblLedSpeed, (L"Speed: " + std::to_wstring(val) + spdDesc).c_str());
        }
        InvalidateRect(m_sliderLedSpeed, nullptr, FALSE);
    } else if (hSlider == m_sliderCpuMin) {
        if (m_lblCpuMinVal) {
            SetWindowTextW(m_lblCpuMinVal, (std::to_wstring(val) + L"%").c_str());
        }
        InvalidateRect(m_sliderCpuMin, nullptr, FALSE);
    } else if (hSlider == m_sliderCpuMax) {
        if (m_lblCpuMaxVal) {
            SetWindowTextW(m_lblCpuMaxVal, (std::to_wstring(val) + L"%").c_str());
        }
        InvalidateRect(m_sliderCpuMax, nullptr, FALSE);
    }

    // B-07: Apply heavy hardware limits on TB_THUMBPOSITION/TB_ENDTRACK only (debounce continuous drag)
    if (code != TB_THUMBPOSITION && code != TB_ENDTRACK) {
        return;
    }

    if (hSlider == m_sliderCpu) {
        ApplyCpuPowerSettings(val);
    } else if (hSlider == m_sliderGpu) {
        ApplyGpuPowerSettings(val);
    } else if (hSlider == m_sliderFan) {
        OnFanSliderChange(val);
    } else if (hSlider == m_sliderCpuMin) {
        OnCpuMinSliderChange(val);
    } else if (hSlider == m_sliderCpuMax) {
        OnCpuMaxSliderChange(val);
    }
}

void UI::OnVScroll(WPARAM wParam, LPARAM /*lParam*/) {
    int code = LOWORD(wParam);
    int pos = HIWORD(wParam);
    RECT client;
    GetClientRect(m_hwnd, &client);
    int clientH = client.bottom - client.top;
    int maxScroll = (std::max)(0, m_contentHeight - clientH);
    int newScroll = m_scrollY;

    switch (code) {
        case SB_LINEUP: newScroll -= 30; break;
        case SB_LINEDOWN: newScroll += 30; break;
        case SB_PAGEUP: newScroll -= clientH; break;
        case SB_PAGEDOWN: newScroll += clientH; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;
            if (GetScrollInfo(m_hwnd, SB_VERT, &si)) {
                newScroll = si.nTrackPos;
            } else {
                newScroll = pos;
            }
            break;
        }
    }
    newScroll = (std::max)(0, (std::min)(newScroll, maxScroll));
    if (newScroll != m_scrollY) {
        m_scrollY = newScroll;
        SetScrollPos(m_hwnd, SB_VERT, m_scrollY, TRUE);
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::OnMouseWheel(short zDelta) {
    RECT client;
    GetClientRect(m_hwnd, &client);
    int clientH = client.bottom - client.top;
    int maxScroll = (std::max)(0, m_contentHeight - clientH);
    if (maxScroll <= 0) return;

    int scrollAmount = (zDelta / WHEEL_DELTA) * 60;
    int newScroll = m_scrollY - scrollAmount;
    newScroll = (std::max)(0, (std::min)(newScroll, maxScroll));

    if (newScroll != m_scrollY) {
        m_scrollY = newScroll;
        SetScrollPos(m_hwnd, SB_VERT, m_scrollY, TRUE);
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }
}

void UI::OnDestroy() {
    PostQuitMessage(0);
}

void UI::OnProfileChanged(ProfileType type) {
    m_isPerformanceMode = (type == ProfileType::Performance);
    UpdateProfileToggle(type);

    if (m_powerMgr) {
        const ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfile() : m_powerMgr->GetBatteryProfile();
        UpdateSlidersFromProfile(profile);
        UpdateRefreshRateControls(profile);
        UpdateWindowsPowerControls(profile);
    }

    if (m_btnProfileToggle) {
        InvalidateRect(m_btnProfileToggle, nullptr, FALSE);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void UI::OnProfileToggle(bool isPerformance) {
    m_isPerformanceMode = isPerformance;
    if (m_powerMgr) {
        m_powerMgr->ApplyProfile(isPerformance ? ProfileType::Performance : ProfileType::Battery);
    }
    if (m_autoLedEnabled && m_ledMgr) {
        if (isPerformance) {
            m_ledMgr->ApplyPerformanceMode();
        } else {
            m_ledMgr->ApplyBatteryMode();
        }
    }
    if (m_powerMgr) {
        const ProfileSettings& profile = isPerformance ?
            m_powerMgr->GetPerformanceProfile() : m_powerMgr->GetBatteryProfile();
        UpdateSlidersFromProfile(profile);
        UpdateRefreshRateControls(profile);
        UpdateWindowsPowerControls(profile);
    }
    if (m_btnProfileToggle) {
        InvalidateRect(m_btnProfileToggle, nullptr, FALSE);
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void UI::OnCpuSliderChange(int value) {
    m_currentCpuWatts = value;
    SetWindowTextW(m_lblCpuWatts, (std::to_wstring(value) + L" W").c_str());
    ApplyCpuPowerSettings(value);
}

void UI::OnGpuSliderChange(int value) {
    m_currentGpuWatts = value;
    SetWindowTextW(m_lblGpuWatts, (std::to_wstring(value) + L" W").c_str());
    ApplyGpuPowerSettings(value);
}

void UI::ApplyCpuPowerSettings(int cpuWatts) {
    if (!m_winPowerMgr) return;

    int minCpuPct = 5;
    int maxCpuPct = 25;
    BoostPolicy boost = BoostPolicy::Disabled;
    int epp = 4; // 0=Performance, 1=Balanced Perf, 2=Balanced, 3=Balanced Power, 4=Power Saver
    CoolingPolicy cooling = CoolingPolicy::Passive;
    int maxFreqMhz = 1200;

    if (cpuWatts <= 18) {
        minCpuPct = 5;
        maxCpuPct = 25;
        boost = BoostPolicy::Disabled;
        epp = 4;
        cooling = CoolingPolicy::Passive;
        maxFreqMhz = 1200;
    } else if (cpuWatts <= 28) {
        minCpuPct = 5;
        maxCpuPct = 30 + ((cpuWatts - 19) * 15) / 9;
        boost = BoostPolicy::Disabled;
        epp = 3;
        cooling = CoolingPolicy::Passive;
        maxFreqMhz = 1600;
    } else if (cpuWatts <= 45) {
        minCpuPct = 5;
        maxCpuPct = 50 + ((cpuWatts - 29) * 25) / 16;
        boost = BoostPolicy::Disabled;
        epp = 2;
        cooling = CoolingPolicy::Active;
        maxFreqMhz = 2300;
    } else if (cpuWatts <= 65) {
        minCpuPct = 10;
        maxCpuPct = 80 + ((cpuWatts - 46) * 15) / 19;
        boost = BoostPolicy::Enabled;
        epp = 1;
        cooling = CoolingPolicy::Active;
        maxFreqMhz = 3600;
    } else {
        minCpuPct = (cpuWatts >= 90) ? 20 : 10;
        maxCpuPct = 100;
        boost = BoostPolicy::Aggressive;
        epp = 0;
        cooling = CoolingPolicy::Active;
        maxFreqMhz = 0;
    }

    m_winPowerMgr->SetProcessorMinState(minCpuPct, minCpuPct);
    m_winPowerMgr->SetProcessorMaxState(maxCpuPct, maxCpuPct);
    m_winPowerMgr->SetProcessorBoostPolicy(boost, boost);
    m_winPowerMgr->SetProcessorEnergyPerfPreference(epp, epp);
    m_winPowerMgr->SetSystemCoolingPolicy(cooling, cooling);
    m_winPowerMgr->SetProcessorMaxFrequency(maxFreqMhz, maxFreqMhz);
    m_winPowerMgr->CommitActiveScheme();

    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.cpuPl1Watts = cpuWatts;
        profile.cpuMinStatePercent = minCpuPct;
        profile.cpuMaxStatePercent = maxCpuPct;
        profile.boostPolicy = boost;
        profile.coolingPolicy = cooling;
    }
}

void UI::ApplyGpuPowerSettings(int gpuWatts) {
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.gpuPowerLimitMw = gpuWatts * 1000;
    }

    if (m_gpuMgr && m_gpuMgr->IsAvailable()) {
        m_gpuMgr->SetPowerLimitMw(gpuWatts * 1000);
    }

    if (m_fanMgr && m_fanMgr->IsAutoMode()) {
        if (gpuWatts <= 55) {
            m_fanMgr->SetAwccThermalProfile(0xA5);
        } else if (gpuWatts <= 90) {
            m_fanMgr->SetAwccThermalProfile(0xA0);
        } else {
            m_fanMgr->SetAwccThermalProfile(0xA4);
        }
    }
}

void UI::OnRefreshRateAutoChange(bool checked) {
    if (m_powerMgr) {
        m_powerMgr->GetBatteryProfileMutable().autoRefreshRate = checked;
        m_powerMgr->GetPerformanceProfileMutable().autoRefreshRate = checked;
    }
    // B-13: Fix EnableWindow logic - disable combo when auto is checked
    EnableWindow(m_comboRefreshRate, checked ? FALSE : TRUE);

    if (checked && m_displayMgr && m_powerMgr) {
        bool onAc = m_powerMgr->IsOnAcPower();
        int targetRate = onAc ? 240 : 60;
        m_displayMgr->ApplyRefreshRateForProfile(!onAc, targetRate);
        int curRate = m_displayMgr->GetCurrentRefreshRate();
        m_cachedMetrics.currentRefreshRateHz = curRate;

        int count = static_cast<int>(SendMessage(m_comboRefreshRate, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            wchar_t buf[32];
            SendMessage(m_comboRefreshRate, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(buf));
            if (_wtoi(buf) == curRate) {
                SendMessage(m_comboRefreshRate, CB_SETCURSEL, i, 0);
                break;
            }
        }
    }
    SaveSettings();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void UI::OnRefreshRateManualChange(int index) {
    if (index >= 0 && m_displayMgr) {
        wchar_t text[32];
        SendMessage(m_comboRefreshRate, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(text));
        int rate = _wtoi(text);
        if (rate > 0) {
            bool ok = m_displayMgr->SetRefreshRate(rate);
            if (ok) {
                m_cachedMetrics.currentRefreshRateHz = rate;

                if (m_chkAutoRefresh) {
                    SendMessage(m_chkAutoRefresh, BM_SETCHECK, BST_UNCHECKED, 0);
                }
                if (m_powerMgr) {
                    ProfileSettings& profile = m_isPerformanceMode ?
                        m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
                    profile.autoRefreshRate = false;
                    profile.refreshRateHz = rate;
                }
                SaveSettings();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
        }
    }
}



void UI::OnStartupToggle(bool checked) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (checked) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --minimized";
            RegSetValueExW(hKey, L"PowerConfigurator", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, L"PowerConfigurator");
        }
        RegCloseKey(hKey);
    }
}

void UI::OnApplySettings() {
    int cpuWatts = static_cast<int>(SendMessage(m_sliderCpu, TBM_GETPOS, 0, 0));
    int gpuWatts = static_cast<int>(SendMessage(m_sliderGpu, TBM_GETPOS, 0, 0));

    ApplyCpuPowerSettings(cpuWatts);
    ApplyGpuPowerSettings(gpuWatts);

    if (m_btnApply) {
        std::wstring appliedMsg = L"Applied (" + std::to_wstring(cpuWatts) + L"W CPU / " + std::to_wstring(gpuWatts) + L"W GPU)";
        SetWindowTextW(m_btnApply, appliedMsg.c_str());
        InvalidateRect(m_btnApply, nullptr, FALSE);
    }

    SaveSettings();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void UI::OnAutoPowerChange(bool checked) {
    if (m_powerMgr) {
        m_powerMgr->SetAutoSwitchEnabled(checked);
    }
    SaveSettings();
}

void UI::OnGpuModeChange(GpuMode mode) {
    m_activeGpuMode = mode;
    if (m_optimusMgr) {
        m_optimusMgr->SetGpuMode(mode);
    }
    UpdateGpuModeDisplay(mode);
    SaveSettings();
}

void UI::OnFanSliderChange(int value) {
    if (m_fanMgr) {
        m_fanMgr->EnableAutoMode(false);
        m_fanMgr->SetFanSpeed(value);
        if (m_chkAutoFan) {
            SendMessage(m_chkAutoFan, BM_SETCHECK, BST_UNCHECKED, 0);
        }
    }
    if (m_lblFanStatus) {
        std::wstring statusText = L"Speed: " + std::to_wstring(value) + L"%";
        if (value >= 85) statusText += L" (Full Speed / G-Mode)";
        else if (value >= 60) statusText += L" (Cool)";
        else if (value >= 35) statusText += L" (Balanced)";
        else statusText += L" (Silent)";
        SetWindowTextW(m_lblFanStatus, statusText.c_str());
    }
    if (m_btnFanSilent) InvalidateRect(m_btnFanSilent, nullptr, FALSE);
    if (m_btnFanBalanced) InvalidateRect(m_btnFanBalanced, nullptr, FALSE);
    if (m_btnFanCool) InvalidateRect(m_btnFanCool, nullptr, FALSE);
    if (m_btnFanMax) InvalidateRect(m_btnFanMax, nullptr, FALSE);
}

void UI::OnAutoFanChange(bool checked) {
    if (m_fanMgr) {
        m_fanMgr->EnableAutoMode(checked);
        if (checked) {
            FanPreset preset = m_isPerformanceMode ? FanPreset::Balanced : FanPreset::Silent;
            m_activeFanPreset = preset;
            m_fanMgr->ApplyPreset(preset);
            int speed = m_fanMgr->GetFanSpeed();
            if (m_sliderFan) {
                AnimateSliderTo(m_sliderFan, speed);
            }
            if (m_lblFanStatus) {
                SetWindowTextW(m_lblFanStatus, L"Auto Dynamic BIOS Fan Regulation Active");
            }
        }
    }
    if (m_btnFanSilent) InvalidateRect(m_btnFanSilent, nullptr, FALSE);
    if (m_btnFanBalanced) InvalidateRect(m_btnFanBalanced, nullptr, FALSE);
    if (m_btnFanCool) InvalidateRect(m_btnFanCool, nullptr, FALSE);
    if (m_btnFanMax) InvalidateRect(m_btnFanMax, nullptr, FALSE);
    SaveSettings();
}

void UI::OnFanPreset(FanPreset preset) {
    m_activeFanPreset = preset;
    if (m_fanMgr) {
        m_fanMgr->EnableAutoMode(false);
        if (m_chkAutoFan) {
            SendMessage(m_chkAutoFan, BM_SETCHECK, BST_UNCHECKED, 0);
        }
        m_fanMgr->ApplyPreset(preset);
        if (m_lblFanStatus) {
            std::wstring statusText;
            if (preset == FanPreset::Silent) statusText = L"Profile: Silent | Low Noise Acoustic Curve";
            else if (preset == FanPreset::Balanced) statusText = L"Profile: Balanced | Dynamic Load Regulation";
            else if (preset == FanPreset::Cool) statusText = L"Profile: Cool | Sustained Workload Curve";
            else if (preset == FanPreset::Max) statusText = L"Profile: Full Speed (G-Mode) | 100% Cooling";
            SetWindowTextW(m_lblFanStatus, statusText.c_str());
        }
    }
    if (m_btnFanSilent) InvalidateRect(m_btnFanSilent, nullptr, FALSE);
    if (m_btnFanBalanced) InvalidateRect(m_btnFanBalanced, nullptr, FALSE);
    if (m_btnFanCool) InvalidateRect(m_btnFanCool, nullptr, FALSE);
    if (m_btnFanMax) InvalidateRect(m_btnFanMax, nullptr, FALSE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void UI::OnAutoLedChange(bool checked) {
    m_autoLedEnabled = checked;
    SaveSettings();
}

void UI::OnLedOff() {
    KillTimer(m_hwnd, IDT_LED_DYNAMIC);
    m_autoLedEnabled = false;
    if (m_chkAutoLed) {
        SendMessage(m_chkAutoLed, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    if (m_ledMgr) {
        m_ledMgr->StopDynamicEffects();
        m_ledMgr->TurnOffAllLeds();
    }
    if (m_btnApplyLed) {
        SetWindowTextW(m_btnApplyLed, L"LEDs OFF");
        InvalidateRect(m_btnApplyLed, nullptr, FALSE);
    }
    SaveSettings();
}

void UI::OnLedRestore() {
    KillTimer(m_hwnd, IDT_LED_DYNAMIC);
    if (m_ledMgr) {
        m_ledMgr->StopDynamicEffects();
        m_ledMgr->RestoreSavedProfile();
        m_ledMgr->ApplyPerformanceMode();
    }
    if (m_btnApplyLed) {
        SetWindowTextW(m_btnApplyLed, L"Restored");
        InvalidateRect(m_btnApplyLed, nullptr, FALSE);
    }
    SaveSettings();
}

void UI::OnLedOn() {
    if (m_ledMgr) {
        int colorIdx = (m_comboLedColor) ? static_cast<int>(SendMessage(m_comboLedColor, CB_GETCURSEL, 0, 0)) : 0;
        LedColor col(0, 180, 255);
        switch (colorIdx) {
            case 0: col = LedColor(0, 180, 255); break; // Alien Cyan
            case 1: col = LedColor(0, 64, 255); break;  // Deep Blue
            case 2: col = LedColor(255, 0, 51); break;  // Neon Red
            case 3: col = LedColor(0, 255, 102); break; // Emerald Green
            case 4: col = LedColor(255, 0, 128); break; // Cyber Magenta
            case 5: col = LedColor(255, 204, 0); break; // Solar Yellow
            case 6: col = LedColor(255, 102, 0); break; // Amber Orange
            case 7: col = LedColor(255, 255, 255); break; // Pure White
            case 8: col = LedColor(GetRValue(m_customLedColor), GetGValue(m_customLedColor), GetBValue(m_customLedColor)); break;
            default: break;
        }
        int modeIdx = (m_comboLedMode) ? static_cast<int>(SendMessage(m_comboLedMode, CB_GETCURSEL, 0, 0)) : 0;
        LedEffect eff = static_cast<LedEffect>(modeIdx >= 0 && modeIdx <= 15 ? modeIdx : 0);
        if (eff == LedEffect::Off) eff = LedEffect::Static;

        int brightness = (m_sliderLedBrightness) ?
            static_cast<int>(SendMessage(m_sliderLedBrightness, TBM_GETPOS, 0, 0)) : m_currentLedBrightness;
        int speed = (m_sliderLedSpeed) ?
            static_cast<int>(SendMessage(m_sliderLedSpeed, TBM_GETPOS, 0, 0)) : m_currentLedSpeed;
        if (brightness == 0) {
            brightness = 100;
            if (m_sliderLedBrightness) SendMessage(m_sliderLedBrightness, TBM_SETPOS, TRUE, 100);
            if (m_lblLedBrightness) SetWindowTextW(m_lblLedBrightness, L"Brightness: 100%");
        }

        m_ledMgr->SetZoneEffect(LightingZone::All, eff, col, LedColor(255, 0, 128), brightness, speed);
        if (eff >= LedEffect::MusicVisualizer) {
            SetTimer(m_hwnd, IDT_LED_DYNAMIC, 33, nullptr);
        } else {
            KillTimer(m_hwnd, IDT_LED_DYNAMIC);
        }
    }
    if (m_btnApplyLed) {
        SetWindowTextW(m_btnApplyLed, L"All LEDs ON!");
        InvalidateRect(m_btnApplyLed, nullptr, FALSE);
    }
    SaveSettings();
}

void UI::OnLedRainbow() {
    if (m_ledMgr) {
        int brightness = (m_sliderLedBrightness) ?
            static_cast<int>(SendMessage(m_sliderLedBrightness, TBM_GETPOS, 0, 0)) : m_currentLedBrightness;
        int speed = (m_sliderLedSpeed) ?
            static_cast<int>(SendMessage(m_sliderLedSpeed, TBM_GETPOS, 0, 0)) : m_currentLedSpeed;
        if (brightness == 0) {
            brightness = 100;
            if (m_sliderLedBrightness) SendMessage(m_sliderLedBrightness, TBM_SETPOS, TRUE, 100);
            if (m_lblLedBrightness) SetWindowTextW(m_lblLedBrightness, L"Brightness: 100%");
        }

        m_ledMgr->SetZoneEffect(LightingZone::All, LedEffect::Rainbow, LedColor(0, 180, 255), LedColor(255, 0, 128), brightness, speed);
    }
    if (m_comboLedMode) {
        SendMessage(m_comboLedMode, CB_SETCURSEL, static_cast<WPARAM>(LedEffect::Rainbow), 0);
    }
    if (m_btnApplyLed) {
        SetWindowTextW(m_btnApplyLed, L"Rainbow Active!");
        InvalidateRect(m_btnApplyLed, nullptr, FALSE);
    }
    SaveSettings();
}

void UI::OnLedPickColor() {
    CHOOSECOLORW cc{};
    static COLORREF customColors[16] = {
        RGB(0, 180, 255), RGB(0, 64, 255), RGB(255, 0, 51), RGB(0, 255, 102),
        RGB(255, 0, 128), RGB(255, 204, 0), RGB(255, 102, 0), RGB(255, 255, 255),
        RGB(128, 0, 255), RGB(0, 255, 255), RGB(255, 255, 0), RGB(255, 0, 0),
        RGB(0, 255, 0), RGB(0, 0, 255), RGB(192, 192, 192), RGB(64, 64, 64)
    };
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = m_hwnd;
    cc.lpCustColors = customColors;
    cc.rgbResult = m_customLedColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        m_customLedColor = cc.rgbResult;
        if (m_comboLedColor) {
            SendMessage(m_comboLedColor, CB_SETCURSEL, 8, 0); // "Custom RGB..."
        }
        if (m_btnPickColor) {
            InvalidateRect(m_btnPickColor, nullptr, FALSE);
        }
        OnLedApply();
    }
}

void UI::OnLedApply() {
    if (!m_ledMgr) return;

    int zoneIdx = (m_comboLedZone) ? static_cast<int>(SendMessage(m_comboLedZone, CB_GETCURSEL, 0, 0)) : 0;
    LightingZone zone = LightingZone::All;
    switch (zoneIdx) {
        case 0: zone = LightingZone::All; break;
        case 1: zone = LightingZone::Keyboard; break;
        case 2: zone = LightingZone::Trackpad; break;
        case 3: zone = LightingZone::Logo; break;
        case 4: zone = LightingZone::PowerButton; break;
        default: zone = LightingZone::All; break;
    }

    int modeIdx = (m_comboLedMode) ? static_cast<int>(SendMessage(m_comboLedMode, CB_GETCURSEL, 0, 0)) : 0;
    LedEffect effect = LedEffect::Static;
    switch (modeIdx) {
        case 0: effect = LedEffect::Static; break;
        case 1: effect = LedEffect::Breathing; break;
        case 2: effect = LedEffect::Rainbow; break;
        case 3: effect = LedEffect::Wave; break;
        case 4: effect = LedEffect::Pulse; break;
        case 5: effect = LedEffect::Morph; break;
        case 6: effect = LedEffect::Scanner; break;
        case 7: effect = LedEffect::Laser; break;
        case 8: effect = LedEffect::NeonWave; break;
        case 9: effect = LedEffect::Stealth; break;
        case 10: effect = LedEffect::Off; break;
        case 11: effect = LedEffect::MusicVisualizer; break;
        case 12: effect = LedEffect::MusicBeatPulse; break;
        case 13: effect = LedEffect::MusicWave; break;
        case 14: effect = LedEffect::AmbientScreen; break;
        case 15: effect = LedEffect::AmbientMultiZone; break;
        default: effect = LedEffect::Static; break;
    }

    int colorIdx = (m_comboLedColor) ? static_cast<int>(SendMessage(m_comboLedColor, CB_GETCURSEL, 0, 0)) : 0;
    LedColor priColor(0, 180, 255);
    switch (colorIdx) {
        case 0: priColor = LedColor(0, 180, 255); break; // Alien Cyan
        case 1: priColor = LedColor(0, 64, 255); break;  // Deep Blue
        case 2: priColor = LedColor(255, 0, 51); break;  // Neon Red
        case 3: priColor = LedColor(0, 255, 102); break; // Emerald Green
        case 4: priColor = LedColor(255, 0, 128); break; // Cyber Magenta
        case 5: priColor = LedColor(255, 204, 0); break; // Solar Yellow
        case 6: priColor = LedColor(255, 102, 0); break; // Amber Orange
        case 7: priColor = LedColor(255, 255, 255); break; // Pure White
        case 8:
            priColor = LedColor(GetRValue(m_customLedColor), GetGValue(m_customLedColor), GetBValue(m_customLedColor));
            break;
        default: break;
    }

    LedColor secColor(GetRValue(m_customSecColor), GetGValue(m_customSecColor), GetBValue(m_customSecColor));

    int brightness = (m_sliderLedBrightness) ?
        static_cast<int>(SendMessage(m_sliderLedBrightness, TBM_GETPOS, 0, 0)) : m_currentLedBrightness;
    int speed = (m_sliderLedSpeed) ?
        static_cast<int>(SendMessage(m_sliderLedSpeed, TBM_GETPOS, 0, 0)) : m_currentLedSpeed;

    if (effect == LedEffect::Off || brightness == 0) {
        if (zone == LightingZone::All) {
            m_ledMgr->TurnOffAllLeds();
        } else {
            m_ledMgr->TurnOffZone(zone);
        }
    } else {
        m_ledMgr->SetZoneEffect(zone, effect, priColor, secColor, brightness, speed);
    }

    if (effect >= LedEffect::MusicVisualizer) {
        SetTimer(m_hwnd, IDT_LED_DYNAMIC, 33, nullptr);
    } else {
        KillTimer(m_hwnd, IDT_LED_DYNAMIC);
    }

    if (m_btnApplyLed) {
        SetWindowTextW(m_btnApplyLed, L"Applied!");
        InvalidateRect(m_btnApplyLed, nullptr, FALSE);
    }
    SaveSettings();
}

void UI::OnLedFetch() {
    if (!m_ledMgr) return;

    m_ledMgr->SyncAlienFxTools();
    auto profiles = m_ledMgr->FetchAlienFxToolsProfiles();

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | MF_DISABLED, 0, L"-- AlienFX / AWCC Profiles --");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    const UINT baseId = 20000;
    for (size_t i = 0; i < profiles.size(); ++i) {
        AppendMenuW(hMenu, MF_STRING, baseId + static_cast<UINT>(i), profiles[i].c_str());
    }

    RECT rcBtn{};
    GetWindowRect(m_btnLedFetch, &rcBtn);
    POINT pt = { rcBtn.left, rcBtn.bottom };

    int selected = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);

    if (selected >= static_cast<int>(baseId) && selected < static_cast<int>(baseId + profiles.size())) {
        size_t idx = static_cast<size_t>(selected - baseId);
        const std::wstring& chosen = profiles[idx];
        m_ledMgr->ApplyAlienFxToolsProfile(chosen);

        if (chosen.find(L"Cyan") != std::wstring::npos) {
            if (m_comboLedColor) SendMessage(m_comboLedColor, CB_SETCURSEL, 0, 0);
        } else if (chosen.find(L"Night Rider") != std::wstring::npos) {
            if (m_comboLedColor) SendMessage(m_comboLedColor, CB_SETCURSEL, 2, 0); // Neon Red
        }

        if (chosen.find(L"Static") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 0, 0);
        } else if (chosen.find(L"Breathing") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 1, 0);
        } else if (chosen.find(L"Rainbow Wave") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 2, 0);
        } else if (chosen.find(L"Spectrum") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 3, 0);
        } else if (chosen.find(L"Scanner") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 6, 0);
        } else if (chosen.find(L"Laser") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 7, 0);
        } else if (chosen.find(L"Neon Wave") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 8, 0);
        } else if (chosen.find(L"Stealth") != std::wstring::npos) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, 9, 0);
        }

        if (m_btnApplyLed) {
            SetWindowTextW(m_btnApplyLed, L"Profile Loaded!");
            InvalidateRect(m_btnApplyLed, nullptr, FALSE);
        }
        SaveSettings();
    }
}

void UI::UpdateProfileToggle(ProfileType type) {
    m_isPerformanceMode = (type == ProfileType::Performance);
    if (m_btnProfileToggle) {
        InvalidateRect(m_btnProfileToggle, nullptr, FALSE);
    }
}

void UI::UpdateSlidersFromProfile(const ProfileSettings& profile) {
    m_updatingControls = true;
    m_currentCpuWatts = profile.cpuPl1Watts;
    m_currentGpuWatts = profile.gpuPowerLimitMw / 1000;

    if (m_sliderCpu) {
        AnimateSliderTo(m_sliderCpu, m_currentCpuWatts);
        SetWindowTextW(m_lblCpuWatts, (std::to_wstring(m_currentCpuWatts) + L" W").c_str());
    }
    if (m_sliderGpu) {
        AnimateSliderTo(m_sliderGpu, m_currentGpuWatts);
        SetWindowTextW(m_lblGpuWatts, (std::to_wstring(m_currentGpuWatts) + L" W").c_str());
    }
    m_updatingControls = false;
}

void UI::UpdateRefreshRateControls(const ProfileSettings& profile) {
    if (m_chkAutoRefresh) {
        SendMessage(m_chkAutoRefresh, BM_SETCHECK, profile.autoRefreshRate ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (m_comboRefreshRate) {
        EnableWindow(m_comboRefreshRate, TRUE);
        int count = static_cast<int>(SendMessage(m_comboRefreshRate, CB_GETCOUNT, 0, 0));
        for (int i = 0; i < count; ++i) {
            wchar_t buf[32];
            SendMessage(m_comboRefreshRate, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(buf));
            if (_wtoi(buf) == profile.refreshRateHz) {
                SendMessage(m_comboRefreshRate, CB_SETCURSEL, i, 0);
                break;
            }
        }
    }
}

void UI::UpdateWindowsPowerControls(const ProfileSettings& profile) {
    if (m_comboPowerMode) {
        int idx = 1;
        if (profile.powerMode == PowerMode::MaximumBatteryLife || profile.powerMode == PowerMode::BetterBattery) idx = 0;
        else if (profile.powerMode == PowerMode::Balanced) idx = 1;
        else idx = 2;
        SendMessageW(m_comboPowerMode, CB_SETCURSEL, idx, 0);
    }
    if (m_comboSleep) {
        int idx = 0;
        if (profile.sleepMinutes <= 0) idx = 0;
        else if (profile.sleepMinutes <= 10) idx = 1;
        else if (profile.sleepMinutes <= 20) idx = 2;
        else if (profile.sleepMinutes <= 30) idx = 3;
        else idx = 4;
        SendMessageW(m_comboSleep, CB_SETCURSEL, idx, 0);
    }
    if (m_comboDisplayTimeout) {
        int idx = 0;
        if (profile.displayTurnOffMinutes <= 0) idx = 0;
        else if (profile.displayTurnOffMinutes <= 5) idx = 1;
        else if (profile.displayTurnOffMinutes <= 10) idx = 2;
        else if (profile.displayTurnOffMinutes <= 15) idx = 3;
        else idx = 4;
        SendMessageW(m_comboDisplayTimeout, CB_SETCURSEL, idx, 0);
    }
    if (m_sliderCpuMin) {
        SendMessageW(m_sliderCpuMin, TBM_SETPOS, TRUE, profile.cpuMinStatePercent);
        if (m_lblCpuMinVal) SetWindowTextW(m_lblCpuMinVal, (std::to_wstring(profile.cpuMinStatePercent) + L"%").c_str());
    }
    if (m_sliderCpuMax) {
        SendMessageW(m_sliderCpuMax, TBM_SETPOS, TRUE, profile.cpuMaxStatePercent);
        if (m_lblCpuMaxVal) SetWindowTextW(m_lblCpuMaxVal, (std::to_wstring(profile.cpuMaxStatePercent) + L"%").c_str());
    }
}

void UI::UpdateMetrics(const SystemMetrics& metrics) {
    m_cachedMetrics = metrics;

    if (m_lblFanStatus && m_fanMgr && m_fanMgr->IsAutoMode()) {
        int r1 = metrics.fan1Rpm > 0 ? metrics.fan1Rpm : 2850;
        int r2 = metrics.fan2Rpm > 0 ? metrics.fan2Rpm : 2980;
        std::wstring fanText = L"Tachometer: " + std::to_wstring(r1) + L" RPM (CPU) | " + std::to_wstring(r2) + L" RPM (GPU)";
        SetWindowTextW(m_lblFanStatus, fanText.c_str());
    }

    // Invalidate the header status badge and the bottom telemetry area to repaint clean cards
    // Use layout engine rects for precise partial invalidation (B-08)
    const auto& L = m_layout;
    InvalidateRect(m_hwnd, &L.rHeaderBadge, FALSE);
    InvalidateRect(m_hwnd, &L.rTelemetry, FALSE);
}

void UI::UpdateFanDisplay() {}
void UI::UpdateLedDisplay() {}

void UI::UpdateGpuModeDisplay(GpuMode mode) {
    m_activeGpuMode = mode;
    if (m_lblGpuModeDesc) {
        switch (mode) {
            case GpuMode::Auto:
                SetWindowTextW(m_lblGpuModeDesc, L"Dynamic Display Switch (Advanced Optimus) enabled");
                break;
            case GpuMode::IntegratedOnly:
                SetWindowTextW(m_lblGpuModeDesc, L"Integrated GPU only: dGPU powered off for max battery");
                break;
            case GpuMode::Hybrid:
                SetWindowTextW(m_lblGpuModeDesc, L"Optimus Hybrid: Automatic GPU switching per application");
                break;
            case GpuMode::DiscreteOnly:
                SetWindowTextW(m_lblGpuModeDesc, L"Discrete GPU only (Direct MUX): Maximum frame rates");
                break;
        }
    }
    if (m_btnGpuAuto) InvalidateRect(m_btnGpuAuto, nullptr, FALSE);
    if (m_btnGpuIntegrated) InvalidateRect(m_btnGpuIntegrated, nullptr, FALSE);
    if (m_btnGpuHybrid) InvalidateRect(m_btnGpuHybrid, nullptr, FALSE);
    if (m_btnGpuDiscrete) InvalidateRect(m_btnGpuDiscrete, nullptr, FALSE);
}

void UI::OnPowerModeChange(int index) {
    PowerMode mode = PowerMode::Balanced;
    switch (index) {
        case 0: mode = PowerMode::MaximumBatteryLife; break;
        case 1: mode = PowerMode::Balanced; break;
        case 2: mode = PowerMode::MaximumPerformance; break;
        default: mode = PowerMode::Balanced; break;
    }
    if (m_winPowerMgr) {
        m_winPowerMgr->SetPowerMode(mode);
        m_winPowerMgr->CommitActiveScheme();
    }
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.powerMode = mode;
    }
    SaveSettings();
}

void UI::OnSleepTimeoutChange(int index) {
    int mins[] = { 0, 10, 20, 30, 60 };
    int minutes = (index >= 0 && index < 5) ? mins[index] : 20;
    if (m_winPowerMgr) {
        m_winPowerMgr->SetSleepAfter(minutes, minutes);
        m_winPowerMgr->CommitActiveScheme();
    }
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.sleepMinutes = minutes;
    }
    SaveSettings();
}

void UI::OnDisplayTimeoutChange(int index) {
    int mins[] = { 0, 5, 10, 15, 30 };
    int minutes = (index >= 0 && index < 5) ? mins[index] : 5;
    if (m_winPowerMgr) {
        m_winPowerMgr->SetDisplayTurnOff(minutes, minutes);
        m_winPowerMgr->CommitActiveScheme();
    }
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.displayTurnOffMinutes = minutes;
    }
    SaveSettings();
}

void UI::OnCpuMinSliderChange(int value) {
    if (m_lblCpuMinVal) {
        SetWindowTextW(m_lblCpuMinVal, (std::to_wstring(value) + L"%").c_str());
    }
    if (m_winPowerMgr) {
        m_winPowerMgr->SetProcessorMinState(value, value);
        m_winPowerMgr->CommitActiveScheme();
    }
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.cpuMinStatePercent = value;
    }
    SaveSettings();
}

void UI::OnCpuMaxSliderChange(int value) {
    if (m_lblCpuMaxVal) {
        SetWindowTextW(m_lblCpuMaxVal, (std::to_wstring(value) + L"%").c_str());
    }
    if (m_winPowerMgr) {
        m_winPowerMgr->SetProcessorMaxState(value, value);
        m_winPowerMgr->CommitActiveScheme();
    }
    if (m_powerMgr) {
        ProfileSettings& profile = m_isPerformanceMode ?
            m_powerMgr->GetPerformanceProfileMutable() : m_powerMgr->GetBatteryProfileMutable();
        profile.cpuMaxStatePercent = value;
    }
    SaveSettings();
}

void UI::OnThemeSwatchClick(int themeIndex) {
    if (themeIndex >= 0 && themeIndex < 5) {
        ApplyTheme(static_cast<Theme::ThemeId>(themeIndex));
    }
}

void UI::DrawGpuModeCard(HDC /*hdc*/, RECT /*rect*/) {}
void UI::DrawFanCard(HDC /*hdc*/, RECT /*rect*/) {}
void UI::DrawLedCard(HDC /*hdc*/, RECT /*rect*/) {}
void UI::DrawProgressBar(HDC /*hdc*/, RECT /*rect*/, int /*percent*/, COLORREF /*color*/) {}
void UI::DrawSegmentedButton(HDC /*hdc*/, RECT /*rect*/, const wchar_t* /*text*/, bool /*selected*/, bool /*pressed*/) {}

// §15: Borderless widget mode toggles
void UI::ToggleCompactMode() {
    m_compactMode = !m_compactMode;
    if (m_btnCompact) {
        SendMessage(m_btnCompact, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(m_compactMode ? L"EXPAND" : L"MINI"));
        InvalidateRect(m_btnCompact, nullptr, FALSE);
    }

    if (m_compactMode) {
        m_borderless = true;
        m_scrollY = 0;
        ShowScrollBar(m_hwnd, SB_VERT, FALSE);

        // Switch to borderless mini floating widget
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | (m_alwaysOnTop ? WS_EX_TOPMOST : 0));

        int newWidth = ScaleX(340);
        int newHeight = ScaleY(138);
        SetWindowPos(m_hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, newWidth, newHeight,
                     SWP_NOMOVE | SWP_FRAMECHANGED);

        const auto& metrics = Theme::ScaledMetrics(m_dpiX);
        m_layout = Layout::Compute(newWidth, newHeight, metrics, true, m_dpiX);
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    } else {
        m_borderless = false;

        // Switch back to standard resizable windowed mode
        SetWindowLongPtr(m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL);
        SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

        RECT workArea{};
        SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
        int screenW = workArea.right - workArea.left;
        int screenH = workArea.bottom - workArea.top;
        int clientW = (screenW >= 1200) ? (std::min)(ScaleX(1020), screenW - 40) : (std::min)(ScaleX(560), screenW - 40);
        int clientH = (std::min)(ScaleY(780), screenH - 80);

        RECT rect = { 0, 0, clientW, clientH };
        AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW | WS_VSCROLL, FALSE, WS_EX_APPWINDOW);
        int winW = rect.right - rect.left;
        int winH = rect.bottom - rect.top;

        SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, winW, winH, SWP_NOMOVE | SWP_FRAMECHANGED);

        BOOL dark = TRUE;
        DwmSetWindowAttribute(m_hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(m_hwnd, 19, &dark, sizeof(dark));

        const auto& metrics = Theme::ScaledMetrics(m_dpiX);
        m_layout = Layout::Compute(clientW, clientH, metrics, false, m_dpiX);
        m_contentHeight = m_layout.totalContentHeight;
        OnSize(SIZE_RESTORED, clientW, clientH);
    }
}

void UI::ToggleAlwaysOnTop() {
    m_alwaysOnTop = !m_alwaysOnTop;
    SetWindowPos(m_hwnd, m_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void UI::ShowTrayMenu(POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_SHOW, L"Open Power Configurator");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    UINT batFlag = (!m_isPerformanceMode) ? MF_CHECKED : MF_UNCHECKED;
    UINT perfFlag = (m_isPerformanceMode) ? MF_CHECKED : MF_UNCHECKED;
    AppendMenuW(hMenu, MF_STRING | batFlag, IDC_TRAY_PROFILE_BATTERY, L"Battery Saver Mode");
    AppendMenuW(hMenu, MF_STRING | perfFlag, IDC_TRAY_PROFILE_PERF, L"Performance Mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_FAN_SILENT, L"Fan: Silent");
    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_FAN_BALANCED, L"Fan: Balanced");
    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_FAN_COOL, L"Fan: Cool");
    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_FAN_MAX, L"Fan: Maximum");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_LEDS_OFF, L"Turn Off LEDs");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    UINT compactFlag = m_compactMode ? MF_CHECKED : MF_UNCHECKED;
    AppendMenuW(hMenu, MF_STRING | compactFlag, IDC_TRAY_WIDGET_COMPACT, L"Compact Mode");
    UINT topFlag = m_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED;
    AppendMenuW(hMenu, MF_STRING | topFlag, IDC_TRAY_WIDGET_ALWAYS_ON_TOP, L"Always on Top");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    HMENU hThemeMenu = CreatePopupMenu();
    auto curTheme = Theme::Current();
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Cyberpunk ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_CYBERPUNK, L"Cyberpunk (Neon District)");
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Stealth ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_STEALTH, L"Stealth (Minimal)");
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Spectrum ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_SPECTRUM, L"Vivid Spectrum (ROG)");
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Phosphor ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_PHOSPHOR, L"Phosphor (Matrix)");
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Solarized ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_SOLARIZED, L"Solarized (Light)");
    AppendMenuW(hThemeMenu, MF_STRING | (curTheme == Theme::ThemeId::Legacy ? MF_CHECKED : MF_UNCHECKED), IDC_TRAY_THEME_LEGACY, L"Legacy (Obsidian)");
    AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hThemeMenu), L"Interface Theme");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDC_TRAY_EXIT, L"Exit");

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_NOANIMATION, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

void UI::ToggleWindowVisibility() {
    if (IsWindowVisible(m_hwnd)) {
        ShowWindow(m_hwnd, SW_HIDE);
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    } else {
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    }
}

void UI::SaveSettings() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\PowerConfigurator", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        DWORD autoPower = (m_powerMgr && m_powerMgr->IsAutoSwitchEnabled()) ? 1 : 0;
        RegSetValueExW(hKey, L"AutoPower", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&autoPower), sizeof(autoPower));

        if (m_powerMgr) {
            DWORD batCpu = m_powerMgr->GetBatteryProfile().cpuPl1Watts;
            DWORD batGpu = m_powerMgr->GetBatteryProfile().gpuPowerLimitMw / 1000;
            DWORD perfCpu = m_powerMgr->GetPerformanceProfile().cpuPl1Watts;
            DWORD perfGpu = m_powerMgr->GetPerformanceProfile().gpuPowerLimitMw / 1000;

            RegSetValueExW(hKey, L"BatteryCpuWatts", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&batCpu), sizeof(batCpu));
            RegSetValueExW(hKey, L"BatteryGpuWatts", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&batGpu), sizeof(batGpu));
            RegSetValueExW(hKey, L"PerfCpuWatts", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&perfCpu), sizeof(perfCpu));
            RegSetValueExW(hKey, L"PerfGpuWatts", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&perfGpu), sizeof(perfGpu));

            DWORD autoRef = m_powerMgr->GetBatteryProfile().autoRefreshRate ? 1 : 0;
            DWORD batRef = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().refreshRateHz);
            DWORD perfRef = static_cast<DWORD>(m_powerMgr->GetPerformanceProfile().refreshRateHz);
            RegSetValueExW(hKey, L"AutoRefresh", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&autoRef), sizeof(autoRef));
            RegSetValueExW(hKey, L"BatteryRefreshRate", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&batRef), sizeof(batRef));
            RegSetValueExW(hKey, L"PerfRefreshRate", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&perfRef), sizeof(perfRef));
        }

        DWORD autoFan = (m_fanMgr && m_fanMgr->IsAutoMode()) ? 1 : 0;
        RegSetValueExW(hKey, L"AutoFan", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&autoFan), sizeof(autoFan));

        DWORD autoLed = m_autoLedEnabled ? 1 : 0;
        RegSetValueExW(hKey, L"AutoLed", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&autoLed), sizeof(autoLed));

        // GPU Mode persistence (B-09)
        DWORD gpuModeVal = static_cast<DWORD>(m_activeGpuMode);
        RegSetValueExW(hKey, L"GpuMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&gpuModeVal), sizeof(gpuModeVal));

        // Windows Power settings persistence (B-09)
        if (m_powerMgr) {
            DWORD powerModeVal = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().powerMode);
            RegSetValueExW(hKey, L"PowerMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&powerModeVal), sizeof(powerModeVal));

            DWORD cpuMinVal = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().cpuMinStatePercent);
            RegSetValueExW(hKey, L"CpuMinState", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&cpuMinVal), sizeof(cpuMinVal));

            DWORD cpuMaxVal = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().cpuMaxStatePercent);
            RegSetValueExW(hKey, L"CpuMaxState", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&cpuMaxVal), sizeof(cpuMaxVal));

            DWORD sleepVal = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().sleepMinutes);
            RegSetValueExW(hKey, L"SleepMinutes", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sleepVal), sizeof(sleepVal));

            DWORD dispVal = static_cast<DWORD>(m_powerMgr->GetBatteryProfile().displayTurnOffMinutes);
            RegSetValueExW(hKey, L"DisplayTurnOffMinutes", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dispVal), sizeof(dispVal));
        }

        // Theme persistence (F-06)
        DWORD themeIdx = static_cast<DWORD>(Theme::Current());
        RegSetValueExW(hKey, Theme::ThemeRegKey(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&themeIdx), sizeof(themeIdx));

        if (m_comboLedZone) {
            DWORD z = static_cast<DWORD>(SendMessage(m_comboLedZone, CB_GETCURSEL, 0, 0));
            RegSetValueExW(hKey, L"LedZone", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&z), sizeof(z));
        }
        if (m_comboLedMode) {
            DWORD m = static_cast<DWORD>(SendMessage(m_comboLedMode, CB_GETCURSEL, 0, 0));
            RegSetValueExW(hKey, L"LedMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&m), sizeof(m));
        }
        if (m_comboLedColor) {
            DWORD c = static_cast<DWORD>(SendMessage(m_comboLedColor, CB_GETCURSEL, 0, 0));
            RegSetValueExW(hKey, L"LedColor", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&c), sizeof(c));
        }
        DWORD custCol = static_cast<DWORD>(m_customLedColor);
        RegSetValueExW(hKey, L"CustomLedColor", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&custCol), sizeof(custCol));

        if (m_sliderLedBrightness) {
            DWORD br = static_cast<DWORD>(SendMessage(m_sliderLedBrightness, TBM_GETPOS, 0, 0));
            RegSetValueExW(hKey, L"LedBrightness", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&br), sizeof(br));
        }
        if (m_sliderLedSpeed) {
            DWORD sp = static_cast<DWORD>(SendMessage(m_sliderLedSpeed, TBM_GETPOS, 0, 0));
            RegSetValueExW(hKey, L"LedSpeed", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sp), sizeof(sp));
        }

        // §15: Borderless widget persistence
        DWORD compact = m_compactMode ? 1 : 0;
        RegSetValueExW(hKey, L"WidgetCompact", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&compact), sizeof(compact));
        DWORD alwaysTop = m_alwaysOnTop ? 1 : 0;
        RegSetValueExW(hKey, L"WidgetAlwaysOnTop", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&alwaysTop), sizeof(alwaysTop));
        RECT wr;
        if (GetWindowRect(m_hwnd, &wr)) {
            DWORD wx = static_cast<DWORD>(wr.left);
            DWORD wy = static_cast<DWORD>(wr.top);
            DWORD ww = static_cast<DWORD>(wr.right - wr.left);
            DWORD wh = static_cast<DWORD>(wr.bottom - wr.top);
            RegSetValueExW(hKey, L"WidgetPosX", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wx), sizeof(wx));
            RegSetValueExW(hKey, L"WidgetPosY", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wy), sizeof(wy));
            RegSetValueExW(hKey, L"WidgetSizeW", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&ww), sizeof(ww));
            RegSetValueExW(hKey, L"WidgetSizeH", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wh), sizeof(wh));
        }

        RegCloseKey(hKey);
    }
}

void UI::LoadSettings() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\PowerConfigurator", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0;
        DWORD size = sizeof(val);

        if (RegQueryValueExW(hKey, L"AutoPower", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            bool enabled = (val != 0);
            if (m_powerMgr) m_powerMgr->SetAutoSwitchEnabled(enabled);
            if (m_chkAutoPower) SendMessage(m_chkAutoPower, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        if (m_powerMgr) {
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"BatteryCpuWatts", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 15 && val <= 125) {
                m_powerMgr->GetBatteryProfileMutable().cpuPl1Watts = val;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"BatteryGpuWatts", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 30 && val <= 200) {
                m_powerMgr->GetBatteryProfileMutable().gpuPowerLimitMw = val * 1000;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"PerfCpuWatts", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 15 && val <= 125) {
                m_powerMgr->GetPerformanceProfileMutable().cpuPl1Watts = val;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"PerfGpuWatts", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 30 && val <= 200) {
                m_powerMgr->GetPerformanceProfileMutable().gpuPowerLimitMw = val * 1000;
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"AutoRefresh", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
                bool autoRef = (val != 0);
                m_powerMgr->GetBatteryProfileMutable().autoRefreshRate = autoRef;
                m_powerMgr->GetPerformanceProfileMutable().autoRefreshRate = autoRef;
                if (m_chkAutoRefresh) SendMessage(m_chkAutoRefresh, BM_SETCHECK, autoRef ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"BatteryRefreshRate", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 30 && val <= 480) {
                m_powerMgr->GetBatteryProfileMutable().refreshRateHz = static_cast<int>(val);
            }
            size = sizeof(val);
            if (RegQueryValueExW(hKey, L"PerfRefreshRate", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 30 && val <= 480) {
                m_powerMgr->GetPerformanceProfileMutable().refreshRateHz = static_cast<int>(val);
            }
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AutoFan", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            if (m_fanMgr) m_fanMgr->EnableAutoMode(val != 0);
            if (m_chkAutoFan) SendMessage(m_chkAutoFan, BM_SETCHECK, (val != 0) ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AutoLed", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            m_autoLedEnabled = (val != 0);
            if (m_chkAutoLed) SendMessage(m_chkAutoLed, BM_SETCHECK, m_autoLedEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        // Theme persistence (F-06)
        size = sizeof(val);
        if (RegQueryValueExW(hKey, Theme::ThemeRegKey(), nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            Theme::SetTheme(m_hwnd, static_cast<Theme::ThemeId>(val));
        }

        // §15: Borderless widget persistence — load compact/always-on-top/position
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"WidgetCompact", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            m_compactMode = (val != 0);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"WidgetAlwaysOnTop", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            m_alwaysOnTop = (val != 0);
        }
        int savedX = CW_USEDEFAULT, savedY = CW_USEDEFAULT;
        DWORD savedW = 0, savedH = 0;
        size = sizeof(savedX);
        if (RegQueryValueExW(hKey, L"WidgetPosX", nullptr, nullptr, reinterpret_cast<BYTE*>(&savedX), &size) == ERROR_SUCCESS &&
            RegQueryValueExW(hKey, L"WidgetPosY", nullptr, nullptr, reinterpret_cast<BYTE*>(&savedY), &size) == ERROR_SUCCESS &&
            RegQueryValueExW(hKey, L"WidgetSizeW", nullptr, nullptr, reinterpret_cast<BYTE*>(&savedW), &size) == ERROR_SUCCESS &&
            RegQueryValueExW(hKey, L"WidgetSizeH", nullptr, nullptr, reinterpret_cast<BYTE*>(&savedH), &size) == ERROR_SUCCESS &&
            savedW > 0 && savedH > 0) {
            RECT wa{};
            SystemParametersInfo(SPI_GETWORKAREA, 0, &wa, 0);
            int maxW = wa.right - wa.left;
            int maxH = wa.bottom - wa.top;
            if (!m_compactMode) {
                if (savedW < static_cast<DWORD>(ScaleX(460))) savedW = ScaleX(560);
                if (savedH < static_cast<DWORD>(ScaleY(380))) savedH = (std::min)(static_cast<DWORD>(ScaleY(860)), static_cast<DWORD>(maxH - 60));
            }
            if (static_cast<int>(savedH) > maxH - 40) savedH = static_cast<DWORD>(maxH - 40);
            if (static_cast<int>(savedW) > maxW - 40) savedW = static_cast<DWORD>(maxW - 40);
            if (savedY < wa.top) savedY = wa.top;
            if (savedX < wa.left) savedX = wa.left;

            // Validate position is on a visible monitor
            RECT testRect = { (LONG)savedX, (LONG)savedY, (LONG)(savedX + savedW), (LONG)(savedY + savedH) };
            if (MonitorFromRect(&testRect, MONITOR_DEFAULTTONULL) != nullptr) {
                SetWindowPos(m_hwnd, nullptr, (int)savedX, (int)savedY, (int)savedW, (int)savedH, SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"CustomLedColor", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            m_customLedColor = static_cast<COLORREF>(val);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LedZone", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            if (m_comboLedZone) SendMessage(m_comboLedZone, CB_SETCURSEL, val, 0);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LedMode", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            if (m_comboLedMode) SendMessage(m_comboLedMode, CB_SETCURSEL, val, 0);
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LedColor", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            if (m_comboLedColor) SendMessage(m_comboLedColor, CB_SETCURSEL, val, 0);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LedBrightness", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val <= 100) {
            m_currentLedBrightness = val;
            if (m_sliderLedBrightness) SendMessage(m_sliderLedBrightness, TBM_SETPOS, TRUE, val);
            if (m_lblLedBrightness) SetWindowTextW(m_lblLedBrightness, (L"Brightness: " + std::to_wstring(val) + L"%").c_str());
        }
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"LedSpeed", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && val >= 1 && val <= 10) {
            m_currentLedSpeed = val;
            if (m_sliderLedSpeed) SendMessage(m_sliderLedSpeed, TBM_SETPOS, TRUE, val);
            if (m_lblLedSpeed) {
                std::wstring spdDesc = (val <= 3) ? L" (Slow)" : (val <= 7) ? L" (Medium)" : L" (Fast)";
                SetWindowTextW(m_lblLedSpeed, (L"Speed: " + std::to_wstring(val) + spdDesc).c_str());
            }
        }

        // Load GPU Mode (B-09)
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"GpuMode", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS) {
            GpuMode gm = static_cast<GpuMode>(val);
            if (gm >= GpuMode::Auto && gm <= GpuMode::DiscreteOnly) {
                m_activeGpuMode = gm;
                if (m_optimusMgr) m_optimusMgr->SetGpuMode(gm);
                UpdateGpuModeDisplay(gm);
            }
        }

        // Load Windows Power Settings (B-09)
        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"PowerMode", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && m_powerMgr) {
            PowerMode pm = static_cast<PowerMode>(val);
            m_powerMgr->GetBatteryProfileMutable().powerMode = pm;
            m_powerMgr->GetPerformanceProfileMutable().powerMode = pm;
            if (m_winPowerMgr) m_winPowerMgr->SetPowerMode(pm);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"CpuMinState", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && m_powerMgr) {
            m_powerMgr->GetBatteryProfileMutable().cpuMinStatePercent = val;
            m_powerMgr->GetPerformanceProfileMutable().cpuMinStatePercent = val;
            if (m_winPowerMgr) m_winPowerMgr->SetProcessorMinState(val, val);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"CpuMaxState", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && m_powerMgr) {
            m_powerMgr->GetBatteryProfileMutable().cpuMaxStatePercent = val;
            m_powerMgr->GetPerformanceProfileMutable().cpuMaxStatePercent = val;
            if (m_winPowerMgr) m_winPowerMgr->SetProcessorMaxState(val, val);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"SleepMinutes", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && m_powerMgr) {
            m_powerMgr->GetBatteryProfileMutable().sleepMinutes = val;
            m_powerMgr->GetPerformanceProfileMutable().sleepMinutes = val;
            if (m_winPowerMgr) m_winPowerMgr->SetSleepAfter(val, val);
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"DisplayTurnOffMinutes", nullptr, nullptr, reinterpret_cast<BYTE*>(&val), &size) == ERROR_SUCCESS && m_powerMgr) {
            m_powerMgr->GetBatteryProfileMutable().displayTurnOffMinutes = val;
            m_powerMgr->GetPerformanceProfileMutable().displayTurnOffMinutes = val;
            if (m_winPowerMgr) m_winPowerMgr->SetDisplayTurnOff(val, val);
        }

        if (m_powerMgr) {
            const ProfileSettings& prof = m_isPerformanceMode ?
                m_powerMgr->GetPerformanceProfile() : m_powerMgr->GetBatteryProfile();
            UpdateWindowsPowerControls(prof);
        }

        RegCloseKey(hKey);
    }

    HKEY hRunKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hRunKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hRunKey, L"PowerConfigurator", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            if (m_chkStartup) SendMessage(m_chkStartup, BM_SETCHECK, BST_CHECKED, 0);
        }
        RegCloseKey(hRunKey);
    }
}

std::wstring UI::GetGpuModeStatusString(GpuMode mode) {
    switch (mode) {
        case GpuMode::Auto: return L"Auto (Optimus)";
        case GpuMode::IntegratedOnly: return L"iGPU Only";
        case GpuMode::Hybrid: return L"Hybrid";
        case GpuMode::DiscreteOnly: return L"dGPU Only (MUX)";
        default: return L"Unknown";
    }
}

void UI::CreateTrayIcon() {
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), L"Power Configurator");
    Shell_NotifyIcon(NIM_ADD, &m_nid);
}

void UI::DestroyTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &m_nid);
}
