#pragma once

#include "Common.h"
#include "Theme.h"
#include "ModernTheme.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>

// Control IDs - Organized by section
// ===== Profile & Quick Actions =====
#define IDC_PROFILE_TOGGLE      1001
#define IDC_BTN_APPLY           1002
#define IDC_CHK_STARTUP         1003
#define IDC_BTN_CLOSE           1004      // Borderless window close button (top-right of grab bar)
#define IDC_BTN_COMPACT         1005      // Compact/normal mode toggle
#define IDC_BTN_THEME_CYCLE     1006      // Cycle UI theme (Cyberpunk, Stealth, Spectrum, etc.)

// ===== CPU Power =====
#define IDC_SLIDER_CPU          1010
#define IDC_LBL_CPU_WATTS       1011

// ===== GPU Power =====
#define IDC_SLIDER_GPU          1020
#define IDC_LBL_GPU_WATTS       1021

// ===== Display =====
#define IDC_CHK_AUTO_REFRESH    1030
#define IDC_COMBO_REFRESH       1031
#define IDC_CHK_REFRESH_LOCK    1032

// ===== Windows Power =====
#define IDC_COMBO_POWERMODE       1040
#define IDC_SLIDER_CPU_MIN        1041
#define IDC_SLIDER_CPU_MAX        1042
#define IDC_COMBO_SLEEP           1043
#define IDC_COMBO_DISPLAY         1044
#define IDC_CHK_AUTO_POWER        1045
#define IDC_LBL_POWERMODE         1046
#define IDC_LBL_CPU_MIN           1047
#define IDC_LBL_CPU_MAX           1048
#define IDC_LBL_SLEEP             1049
#define IDC_LBL_DISPLAY_TO        1055

// ===== GPU Mode =====
#define IDC_RDO_GPU_AUTO          1050
#define IDC_RDO_GPU_INTEGRATED    1051
#define IDC_RDO_GPU_HYBRID        1052
#define IDC_RDO_GPU_DISCRETE      1053
#define IDC_LBL_GPU_MODE_DESC     1054

// ===== Theme Swatch Selector =====
#define IDC_BTN_THEME_0           1090
#define IDC_BTN_THEME_1           1091
#define IDC_BTN_THEME_2           1092
#define IDC_BTN_THEME_3           1093
#define IDC_BTN_THEME_4           1094

// ===== Fan Control =====
#define IDC_SLIDER_FAN          1060
#define IDC_CHK_AUTO_FAN        1061
#define IDC_BTN_FAN_SILENT      1062
#define IDC_BTN_FAN_BALANCED    1063
#define IDC_BTN_FAN_COOL        1064
#define IDC_BTN_FAN_MAX         1065

// ===== AlienFX Lighting Control =====
#define IDC_CHK_AUTO_LED          1070
#define IDC_BTN_LED_OFF           1071
#define IDC_BTN_LED_ON            1072
#define IDC_BTN_LED_RAINBOW       1073
#define IDC_COMBO_LED_ZONE        1074
#define IDC_COMBO_LED_MODE        1075
#define IDC_COMBO_LED_COLOR       1076
#define IDC_BTN_LED_PICK_COLOR    1077
#define IDC_BTN_LED_APPLY         1078
#define IDC_BTN_LED_RESTORE       1079
#define IDC_SLIDER_LED_BRIGHTNESS 1080
#define IDC_SLIDER_LED_SPEED      1081
#define IDC_LBL_LED_BRIGHTNESS    1082
#define IDC_LBL_LED_SPEED         1083
#define IDC_BTN_LED_FETCH         1084

// ===== Tray Menu =====
#define IDC_TRAY_SHOW           2001
#define IDC_TRAY_EXIT           2002
#define IDC_TRAY_PROFILE_BATTERY 2003
#define IDC_TRAY_PROFILE_PERF   2004
#define IDC_TRAY_FAN_SILENT     2005
#define IDC_TRAY_FAN_BALANCED   2006
#define IDC_TRAY_FAN_COOL       2007
#define IDC_TRAY_FAN_MAX        2008
#define IDC_TRAY_LEDS_OFF       2009
#define IDC_TRAY_WIDGET_COMPACT 2010
#define IDC_TRAY_WIDGET_ALWAYS_ON_TOP 2011
#define IDC_TRAY_THEME_CYBERPUNK 2020
#define IDC_TRAY_THEME_STEALTH   2021
#define IDC_TRAY_THEME_SPECTRUM  2022
#define IDC_TRAY_THEME_PHOSPHOR  2023
#define IDC_TRAY_THEME_SOLARIZED 2024
#define IDC_TRAY_THEME_LEGACY    2025

// Static control for fan label
#define IDC_STATIC_FAN_LABEL    -1

// Custom messages
#define WM_TRAYICON             (WM_USER + 1)
#define WM_APP_METRICS_UPDATED  (WM_APP + 1)

// Forward declarations
class PowerManager;
class GpuManager;
class CpuManager;
class DisplayManager;
class WindowsPowerManager;
class OptimusManager;
class LedManager;
class FanManager;
class Monitor;

// Visual style constants - Modern Alienware/ROG Gaming Theme
// (Removed in the audit refactor, F-01: all colors now live in the theme
//  token engine — see Theme.h / Theme::Tokens().)
// UI Layout constants
// (Static constants replaced by Theme::Metrics() so themes can adjust
//  spacing; see Theme.h ThemeMetrics. Layout::Compute in Phase 2.5 owns
//  the actual geometry derivation.)

// Layout engine — single source of truth for all geometry
struct Layout {
    // Client dimensions (scaled)
    int clientWidth = 0;
    int clientHeight = 0;

    // Outer margins
    int margin = 0;
    int gap = 0;
    int sectionGap = 0;

    // Control sizes
    int controlHeight = 0;
    int labelHeight = 0;
    int sliderHeight = 0;
    int buttonHeight = 0;
    int cardHeight = 0;
    int headerHeight = 0;
    int headerBandHeight = 0;

    // Corner radii
    int cornerRadius = 0;
    int cornerRadiusButton = 0;
    int cardCornerRadius = 0;

    // Computed section rects
    RECT rProfile = {0};
    RECT rLimits = {0};
    RECT rFans = {0};
    RECT rDisplay = {0};
    RECT rLed = {0};
    RECT rTelemetry = {0};

    // Telemetry card grid
    int cardW = 0;
    int cardH = 0;
    int col1 = 0;
    int col2 = 0;
    int row1 = 0;
    int row2 = 0;

    // Individual control rects (for hit-test, cursor, paint, invalidation)
    RECT rProfileToggle = {0};
    RECT rChkAutoPower = {0};
    RECT rChkStartup = {0};
    RECT rLblCpuTitle = {0};
    RECT rSliderCpu = {0};
    RECT rLblCpuWatts = {0};
    RECT rLblGpuTitle = {0};
    RECT rSliderGpu = {0};
    RECT rLblGpuWatts = {0};
    RECT rBtnApply = {0};
    RECT rBtnFanSilent = {0};
    RECT rBtnFanBalanced = {0};
    RECT rBtnFanCool = {0};
    RECT rBtnFanMax = {0};
    RECT rChkAutoFan = {0};
    RECT rSliderFan = {0};
    RECT rLblFanStatus = {0};
    RECT rChkAutoRefresh = {0};
    RECT rComboRefreshRate = {0};
    RECT rLblDispSpec = {0};
    RECT rComboLedZone = {0};
    RECT rComboLedMode = {0};
    RECT rComboLedColor = {0};
    RECT rBtnPickColor = {0};
    RECT rBtnApplyLed = {0};
    RECT rLblLedBrightness = {0};
    RECT rSliderLedBrightness = {0};
    RECT rLblLedSpeed = {0};
    RECT rSliderLedSpeed = {0};
    RECT rChkAutoLed = {0};
    RECT rBtnLedOn = {0};
    RECT rBtnLedOff = {0};
    RECT rBtnLedRainbow = {0};
    RECT rBtnLedRestore = {0};
    RECT rBtnLedFetch = {0};

    // GPU Mode section rects
    RECT rGpuMode = {0};
    RECT rHdrGpuMode = {0};
    RECT rBtnGpuAuto = {0};
    RECT rBtnGpuIntegrated = {0};
    RECT rBtnGpuHybrid = {0};
    RECT rBtnGpuDiscrete = {0};
    RECT rLblGpuModeDesc = {0};

    // Windows Power section rects
    RECT rWinPower = {0};
    RECT rHdrWinPower = {0};
    RECT rLblPowerMode = {0};
    RECT rComboPowerMode = {0};
    RECT rLblCpuMinTitle = {0};
    RECT rSliderCpuMin = {0};
    RECT rLblCpuMinVal = {0};
    RECT rLblCpuMaxTitle = {0};
    RECT rSliderCpuMax = {0};
    RECT rLblCpuMaxVal = {0};
    RECT rLblSleepTitle = {0};
    RECT rComboSleep = {0};
    RECT rLblDisplayTimeoutTitle = {0};
    RECT rComboDisplayTimeout = {0};

    // Theme Swatches section rects
    RECT rThemeSection = {0};
    RECT rHdrTheme = {0};
    RECT rBtnThemeSwatches[5] = {0};

    // Header buttons (Chrome)
    RECT rBtnTheme = {0};
    RECT rBtnCompact = {0};

    // Responsive mode flag
    bool isTwoColumn = false;

    // Telemetry card rects (for hover, cursor, paint)
    RECT rCardCpu = {0};
    RECT rCardGpu = {0};
    RECT rCardPower = {0};
    RECT rCardFan = {0};

    // Section header rects
    RECT rHdrLimits = {0};
    RECT rHdrFans = {0};
    RECT rHdrDisplay = {0};
    RECT rHdrLed = {0};
    RECT rHdrTelemetry = {0};

    // Progress bar rects inside cards
    RECT rCpuBar = {0};
    RECT rGpuBar = {0};
    RECT rPowerBar = {0};
    RECT rFanBar = {0};

    // Sub-metric text rects inside cards
    RECT rCpuSub = {0};
    RECT rGpuSub = {0};
    RECT rPowerSub = {0};
    RECT rFanSub = {0};

    // Header badge rect
    RECT rHeaderBadge = {0};

    // Title rects
    RECT rTitle = {0};
    RECT rSubtitle = {0};

    // Borderless / compact widget rects (§15)
    RECT rCompactGrab = {0};   // drag handle bar
    RECT rCloseBtn = {0};      // close button (top-right of grab bar)
    int totalContentHeight = 0; // total vertical height of all sections

    static Layout Compute(int clientWidth, int clientHeight, const Theme::ThemeMetrics& metrics, bool compactMode = false, int dpi = 96);
};

class UI {
public:
    UI();
    ~UI();

    bool Initialize(HINSTANCE hInstance, int nCmdShow,
                    PowerManager* powerMgr, GpuManager* gpuMgr,
                    CpuManager* cpuMgr, DisplayManager* displayMgr,
                    WindowsPowerManager* winPowerMgr, OptimusManager* optimusMgr,
                    LedManager* ledMgr, FanManager* fanMgr, Monitor* monitor);

    int RunMessageLoop();
    void Shutdown();

    // Update UI with latest metrics
    void UpdateMetrics(const SystemMetrics& metrics);

    // Handle profile change
    void OnProfileChanged(ProfileType type);

    // Window handle
    HWND GetWindowHandle() const { return m_hwnd; }

private:
    // Window class and creation
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK CheckboxSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool RegisterWindowClass(HINSTANCE hInstance);
    bool CreateMainWindow(HINSTANCE hInstance, int nCmdShow);
    void CreateControls();
    void CreateTrayIcon();
    void DestroyTrayIcon();

    // Layout and painting
    void LayoutControls();
    void OnPaint(HDC hdc, const RECT& rcPaint);
    void OnSize(UINT state, int cx, int cy);
    void OnTimer(UINT_PTR timerId);
    void OnCommand(WPARAM wParam, LPARAM lParam);
    bool OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
    void OnHScroll(WPARAM wParam, LPARAM lParam);
    void OnVScroll(WPARAM wParam, LPARAM lParam);
    void OnMouseWheel(short zDelta);
    void OnClose();
    void OnDestroy();

    // Theme switching
    void ApplyTheme(Theme::ThemeId id);
    void CycleNextTheme();

    // Control handlers
    void OnProfileToggle(bool isPerformance);
    void OnCpuSliderChange(int value);
    void OnGpuSliderChange(int value);
    void OnRefreshRateAutoChange(bool checked);
    void OnRefreshRateManualChange(int index);
    void OnStartupToggle(bool checked);
    void OnApplySettings();
    void OnAutoPowerChange(bool checked);
    void OnGpuModeChange(GpuMode mode);
    void OnPowerModeChange(int index);
    void OnCpuMinSliderChange(int value);
    void OnCpuMaxSliderChange(int value);
    void OnSleepTimeoutChange(int index);
    void OnDisplayTimeoutChange(int index);
    void OnThemeSwatchClick(int themeIndex);
    void OnFanSliderChange(int value);
    void OnAutoFanChange(bool checked);
    void OnFanPreset(FanPreset preset);
    void OnAutoLedChange(bool checked);
    void OnLedOff();
    void OnLedRestore();
    void OnLedOn();
    void OnLedRainbow();
    void OnLedPickColor();
    void OnLedApply();
    void OnLedFetch();

    // Slider animation system
    static constexpr UINT_PTR IDT_SLIDER_ANIM = 1002;
    static constexpr UINT_PTR IDT_LED_DYNAMIC = 1003;
    struct SliderAnimState {
        HWND hwnd = nullptr;
        float currentPos = 0.0f;
        float targetPos = 0.0f;
        bool isAnimating = false;
        bool isDragging = false;
        bool isHovered = false;
    };
    SliderAnimState m_animCpu;
    SliderAnimState m_animGpu;
    SliderAnimState m_animFan;
    SliderAnimState m_animLedBrightness;
    SliderAnimState m_animLedSpeed;
    SliderAnimState m_animCpuMin;
    SliderAnimState m_animCpuMax;

    void AnimateSliderTo(HWND hSlider, int targetPos);
    void UpdateSliderAnimations();
    SliderAnimState* GetSliderAnimState(HWND hSlider);

    // Hardware power limits
    void ApplyCpuPowerSettings(int cpuWatts);
    void ApplyGpuPowerSettings(int gpuWatts);

    // UI updates
    void UpdateProfileToggle(ProfileType type);
    void UpdateSlidersFromProfile(const ProfileSettings& profile);
    void UpdateRefreshRateControls(const ProfileSettings& profile);
    void UpdateWindowsPowerControls(const ProfileSettings& profile);
    void UpdateMetricsDisplay(const SystemMetrics& metrics);
    void UpdateFanDisplay();
    void UpdateLedDisplay();
    void UpdateGpuModeDisplay(GpuMode mode);

    // Drawing helpers - Modern Alienware visual design
    void DrawCard(HDC hdc, RECT rect, const wchar_t* title, const wchar_t* value, const wchar_t* unit, COLORREF accentColor, bool isHovered = false);
    void AddTooltip(HWND hwndCtl, const wchar_t* text);
    void DrawDarkBackground(HDC hdc, RECT rect);
    void DrawGpuModeCard(HDC hdc, RECT rect);
    void DrawFanCard(HDC hdc, RECT rect);
    void DrawLedCard(HDC hdc, RECT rect);
    void DrawSectionHeader(HDC hdc, RECT rect, const wchar_t* title);
    void DrawProgressBar(HDC hdc, RECT rect, int percent, COLORREF color);
    void DrawSegmentedButton(HDC hdc, RECT rect, const wchar_t* text, bool selected, bool pressed);
    HFONT CreateCustomFont(int height, int weight = FW_NORMAL);
    void SetControlFont(HWND hwnd, HFONT font);
    void FillRoundedRect(HDC hdc, RECT rect, int cornerRadius, HBRUSH brush);

    // Tray icon
    void ShowTrayMenu(POINT pt);
    void ToggleWindowVisibility();

    // Borderless / floating widget (§15)
    void OnNCHitTest(WPARAM wParam, LPARAM lParam, LRESULT* result);
    void OnDragStart(WPARAM wParam, LPARAM lParam);
    void OnDragMove(WPARAM wParam, LPARAM lParam);
    void ToggleCompactMode();
    void ToggleAlwaysOnTop();
    void DrawCloseButton(HDC hdc, RECT rect, bool isHovered, bool isPressed);
    void DrawCompactWidget(HDC hdc);

    // Settings persistence
    void SaveSettings();
    void LoadSettings();

    // Helpers
    std::wstring GetGpuModeStatusString(GpuMode mode);
    int ScaleX(int x) const { return MulDiv(x, m_dpiX, 96); }
    int ScaleY(int y) const { return MulDiv(y, m_dpiY, 96); }

    // ===== Member variables =====
    HWND m_hwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    UINT m_dpiX = 96, m_dpiY = 96;

    // Managers
    PowerManager* m_powerMgr = nullptr;
    GpuManager* m_gpuMgr = nullptr;
    CpuManager* m_cpuMgr = nullptr;
    DisplayManager* m_displayMgr = nullptr;
    WindowsPowerManager* m_winPowerMgr = nullptr;
    OptimusManager* m_optimusMgr = nullptr;
    LedManager* m_ledMgr = nullptr;
    FanManager* m_fanMgr = nullptr;
    Monitor* m_monitor = nullptr;

    // Fonts - Modern typography
    HFONT m_fontTitle = nullptr;      // Header font
    HFONT m_fontSemi = nullptr;       // Subheader font
    HFONT m_fontNormal = nullptr;     // Regular body font
    HFONT m_fontSmall = nullptr;      // Small/hint font
    HFONT m_fontLarge = nullptr;      // Large metric font
    HFONT m_fontMonospace = nullptr;  // Monospace for telemetry

    // Layout engine — single source of truth for all geometry
    Layout m_layout;

    // Controls - Profile section
    HWND m_btnProfileToggle = nullptr;
    HWND m_lblProfileTitle = nullptr;
    HWND m_btnApply = nullptr;
    HWND m_chkStartup = nullptr;
    HWND m_chkAutoPower = nullptr;

    // Controls - Power sliders
    HWND m_lblCpuTitle = nullptr;
    HWND m_sliderCpu = nullptr;
    HWND m_lblCpuWatts = nullptr;
    HWND m_lblGpuTitle = nullptr;
    HWND m_sliderGpu = nullptr;
    HWND m_lblGpuWatts = nullptr;

    // Controls - Display
    HWND m_chkAutoRefresh = nullptr;
    HWND m_comboRefreshRate = nullptr;
    HWND m_lblRefreshStatus = nullptr;

    // Controls - GPU Mode (B-09)
    HWND m_btnGpuAuto = nullptr;
    HWND m_btnGpuIntegrated = nullptr;
    HWND m_btnGpuHybrid = nullptr;
    HWND m_btnGpuDiscrete = nullptr;
    HWND m_lblGpuModeDesc = nullptr;
    GpuMode m_activeGpuMode = GpuMode::Hybrid;

    // Controls - Windows Power (B-09)
    HWND m_comboPowerMode = nullptr;
    HWND m_lblPowerMode = nullptr;
    HWND m_sliderCpuMin = nullptr;
    HWND m_lblCpuMinTitle = nullptr;
    HWND m_lblCpuMinVal = nullptr;
    HWND m_sliderCpuMax = nullptr;
    HWND m_lblCpuMaxTitle = nullptr;
    HWND m_lblCpuMaxVal = nullptr;
    HWND m_comboSleep = nullptr;
    HWND m_lblSleep = nullptr;
    HWND m_comboDisplayTimeout = nullptr;
    HWND m_lblDisplayTimeout = nullptr;

    // Controls - Theme Swatches
    HWND m_btnThemeSwatches[5] = { nullptr };

    // Controls - Fan
    HWND m_lblFanTitle = nullptr;
    HWND m_sliderFan = nullptr;
    HWND m_chkAutoFan = nullptr;
    HWND m_btnFanSilent = nullptr;
    HWND m_btnFanBalanced = nullptr;
    HWND m_btnFanCool = nullptr;
    HWND m_btnFanMax = nullptr;
    HWND m_lblFanStatus = nullptr;
    HWND m_lblFanRpm = nullptr;

    // Controls - AlienFX Lighting
    HWND m_lblLedTitle = nullptr;
    HWND m_comboLedZone = nullptr;
    HWND m_comboLedMode = nullptr;
    HWND m_comboLedColor = nullptr;
    HWND m_btnPickColor = nullptr;
    HWND m_btnApplyLed = nullptr;
    HWND m_sliderLedBrightness = nullptr;
    HWND m_sliderLedSpeed = nullptr;
    HWND m_lblLedBrightness = nullptr;
    HWND m_lblLedSpeed = nullptr;
    HWND m_chkAutoLed = nullptr;
    HWND m_btnLedOn = nullptr;
    HWND m_btnLedOff = nullptr;
    HWND m_btnLedRestore = nullptr;
    HWND m_btnLedRainbow = nullptr;
    HWND m_btnLedFetch = nullptr;
    HWND m_lblLedStatus = nullptr;

    // Controls - Borderless widget chrome (§15) & Theme Switcher
    HWND m_btnClose = nullptr;
    HWND m_btnCompact = nullptr;
    HWND m_btnTheme = nullptr;

    HWND m_hwndTooltip = nullptr;

    int m_currentLedBrightness = 100;
    int m_currentLedSpeed = 7;
    int m_hoveredCard = -1; // -1 = none, 0 = CPU, 1 = GPU, 2 = Power, 3 = Fan
    HWND m_hoveredButton = nullptr;
    FanPreset m_activeFanPreset = FanPreset::Balanced;

    COLORREF m_customLedColor = 0; // Initialized from theme tokens in Initialize()
    COLORREF m_customSecColor = 0; // Initialized from theme tokens in Initialize()

    // Metric display controls
    HWND m_cardCpu = nullptr;
    HWND m_cardGpu = nullptr;
    HWND m_cardBattery = nullptr;
    HWND m_cardDisplay = nullptr;

    // Tray
    NOTIFYICONDATA m_nid{};

    // State
    bool m_isPerformanceMode = false;
    bool m_updatingControls = false;
    bool m_autoGpuModeEnabled = true;
    bool m_autoLedEnabled = true;
    bool m_ledAvailable = true;

    // Window and widget state
    bool m_borderless = false;      // windowed mode (standard resizable app)
    bool m_compactMode = false;     // small widget mode
    bool m_alwaysOnTop = false;     // stay on top of other windows
    bool m_dragging = false;        // drag-in-progress flag
    POINT m_dragOffset = { 0, 0 };  // cursor offset from window origin during drag
    int m_scrollY = 0;              // vertical scroll offset in pixels
    int m_contentHeight = 0;        // total virtual height of all sections

    // Cached metric display strings
    std::wstring m_cpuTempText, m_cpuClockText, m_cpuPowerText, m_cpuPl1Text;
    std::wstring m_gpuTempText, m_gpuClockText, m_gpuPowerText, m_gpuLimitText;
    std::wstring m_batteryText, m_chargingText, m_timeText;
    std::wstring m_displayRefreshText, m_displayResText;
    std::wstring m_powerModeText, m_powerPlanText;
    std::wstring m_gpuModeText;
    std::wstring m_fanStatusText;
    std::wstring m_ledStatusText;

    // Current values for display
    int m_currentCpuWatts = 45;
    int m_currentGpuWatts = 80;
    int m_currentFanPercent = 0;
    int m_currentFan1Rpm = 0;
    int m_currentFan2Rpm = 0;

    // Cached GDI resources (replaced by Theme::Brush()/Pen()/Font() — only
    // transient brushes/pens for one-off drawing remain here)
    HBRUSH m_hBgBrush = nullptr;
    HBRUSH m_hCardBrush = nullptr;
    HBRUSH m_hCardHoverBrush = nullptr;
    HPEN m_hCardBorderPen = nullptr;

    SystemMetrics m_cachedMetrics{};
};
