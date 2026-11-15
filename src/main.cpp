#include "UI.h"
#include "PowerManager.h"
#include "GpuManager.h"
#include "CpuManager.h"
#include "DisplayManager.h"
#include "WindowsPowerManager.h"
#include "OptimusManager.h"
#include "LedManager.h"
#include "FanManager.h"
#include "Monitor.h"
#include <shellapi.h>
#include <algorithm>

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void OnProfileChanged(ProfileType type);

// Global managers (for callback access)
static PowerManager* g_powerMgr = nullptr;
static GpuManager* g_gpuMgr = nullptr;
static CpuManager* g_cpuMgr = nullptr;
static DisplayManager* g_displayMgr = nullptr;
static WindowsPowerManager* g_winPowerMgr = nullptr;
static OptimusManager* g_optimusMgr = nullptr;
static LedManager* g_ledMgr = nullptr;
static FanManager* g_fanMgr = nullptr;
static Monitor* g_monitor = nullptr;
static UI* g_uiPtr = nullptr;

// Window state
static bool g_isPerformanceMode = false;

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Check for admin rights
    if (!IsAdmin()) {
        // Re-launch as admin with proper executable path
        wchar_t szExePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, szExePath, MAX_PATH)) {
            SHELLEXECUTEINFO sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = szExePath;
            sei.lpParameters = lpCmdLine;
            sei.nShow = nCmdShow;
            if (ShellExecuteEx(&sei)) {
                return 0;
            }
        }
        MessageBoxW(nullptr, L"Administrator privileges required to modify power settings.", L"Power Configurator", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Terminate any previous instance if running
    HWND existing = FindWindowW(L"PowerConfiguratorWindow", nullptr);
    if (existing) {
        DWORD pid = 0;
        GetWindowThreadProcessId(existing, &pid);
        if (pid != 0 && pid != GetCurrentProcessId()) {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                TerminateProcess(hProc, 0);
                CloseHandle(hProc);
                Sleep(400);
            }
        }
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\PowerConfiguratorMutex");

    // Initialize COM for WMI
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM.", L"Power Configurator", MB_ICONERROR | MB_OK);
        return 1;
    }

    CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr
    );

    // Create managers
    PowerManager powerMgr;
    GpuManager gpuMgr;
    CpuManager cpuMgr;
    DisplayManager displayMgr;
    WindowsPowerManager winPowerMgr;
    OptimusManager optimusMgr;
    LedManager ledMgr;
    FanManager fanMgr;
    Monitor monitor;

    // Store globals for callbacks
    g_powerMgr = &powerMgr;
    g_gpuMgr = &gpuMgr;
    g_cpuMgr = &cpuMgr;
    g_displayMgr = &displayMgr;
    g_winPowerMgr = &winPowerMgr;
    g_optimusMgr = &optimusMgr;
    g_ledMgr = &ledMgr;
    g_fanMgr = &fanMgr;
    g_monitor = &monitor;

    // Initialize all managers
    auto logCallback = [](const std::wstring& msg) {
        OutputDebugStringW((msg + L"\n").c_str());
    };

    powerMgr.Initialize(logCallback);
    gpuMgr.Initialize(logCallback);
    cpuMgr.Initialize(logCallback);
    displayMgr.Initialize(logCallback);
    winPowerMgr.Initialize(logCallback);
    optimusMgr.Initialize(logCallback);
    ledMgr.Initialize(logCallback, &winPowerMgr);
    fanMgr.Initialize(logCallback, &cpuMgr, &gpuMgr, &winPowerMgr);

    // Support headless CLI switches (e.g. for shortcuts or testing)
    std::wstring cmd = lpCmdLine ? lpCmdLine : L"";
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::towlower);
    if (cmd.find(L"leds-off") != std::wstring::npos || cmd.find(L"ledsoff") != std::wstring::npos) {
        ledMgr.TurnOffAllLeds();
        ledMgr.Shutdown();
        CoUninitialize();
        return 0;
    }
    if (cmd.find(L"leds-on") != std::wstring::npos || cmd.find(L"ledson") != std::wstring::npos || cmd.find(L"leds-restore") != std::wstring::npos) {
        ledMgr.TurnOnAllLeds(LedColor(0, 180, 255), LedEffect::Static);
        ledMgr.Shutdown();
        CoUninitialize();
        return 0;
    }
    if (cmd.find(L"leds-rainbow") != std::wstring::npos || cmd.find(L"rainbow") != std::wstring::npos) {
        ledMgr.SetZoneEffect(LightingZone::All, LedEffect::Rainbow, LedColor(0, 180, 255));
        ledMgr.Shutdown();
        CoUninitialize();
        return 0;
    }
    if (cmd.find(L"leds-breathing") != std::wstring::npos || cmd.find(L"breathing") != std::wstring::npos) {
        ledMgr.SetZoneEffect(LightingZone::All, LedEffect::Breathing, LedColor(0, 180, 255));
        ledMgr.Shutdown();
        CoUninitialize();
        return 0;
    }

    // Create UI
    UI ui;
    g_uiPtr = &ui;

    // Set up profile change callback
    powerMgr.SetProfileChangedCallback([](ProfileType type) {
        // Apply all settings when profile changes
        ProfileSettings& profile = (type == ProfileType::Performance) ?
            g_powerMgr->GetPerformanceProfileMutable() : g_powerMgr->GetBatteryProfileMutable();

        // 1. Apply Windows power settings
        g_winPowerMgr->ApplyProfileSettings(profile);

        // 2. Apply GPU power limit
        if (g_gpuMgr->IsAvailable()) {
            g_gpuMgr->SetPowerLimitMw(profile.gpuPowerLimitMw);
        }

        // 3. Apply display refresh rate
        if (profile.autoRefreshRate) {
            g_displayMgr->ApplyRefreshRateForProfile(profile.isBatteryProfile, profile.refreshRateHz);
        } else {
            g_displayMgr->SetRefreshRate(profile.refreshRateHz);
        }

        // 4. Apply GPU mode (Optimus/MUX)
        GpuMode targetMode = (type == ProfileType::Performance) ?
            GpuMode::Hybrid : GpuMode::IntegratedOnly;
        g_optimusMgr->SetGpuMode(targetMode);

        // 5. Apply LED control
        if (type == ProfileType::Battery) {
            g_ledMgr->ApplyBatteryMode();
        } else {
            g_ledMgr->ApplyPerformanceMode();
        }

        // 6. Apply fan settings (Quiet & Balanced: DO NOT ramp up fans to 100% on AC!)
        if (type == ProfileType::Performance) {
            g_fanMgr->ApplyPreset(FanPreset::Balanced);
        } else {
            g_fanMgr->ApplyPreset(FanPreset::Silent);
        }

        if (g_uiPtr) {
            g_uiPtr->OnProfileChanged(type);
        }
    });

    // Detect initial power source and immediately apply corresponding profile
    powerMgr.RefreshAcStatus();
    ProfileType initialProfile = powerMgr.IsOnAcPower() ? ProfileType::Performance : ProfileType::Battery;
    powerMgr.ApplyProfile(initialProfile);

    // Create monitor with UI message notification
    monitor.Initialize(&powerMgr, &gpuMgr, &cpuMgr, &displayMgr, &winPowerMgr,
                       &optimusMgr, &ledMgr, &fanMgr,
        [&ui](const SystemMetrics&) {
            if (ui.GetWindowHandle()) {
                PostMessage(ui.GetWindowHandle(), WM_APP_METRICS_UPDATED, 0, 0);
            }
        },
        logCallback);

    if (!ui.Initialize(hInstance, nCmdShow, &powerMgr, &gpuMgr, &cpuMgr, &displayMgr,
                       &winPowerMgr, &optimusMgr, &ledMgr, &fanMgr, &monitor)) {
        MessageBoxW(nullptr, L"Failed to create UI.", L"Power Configurator", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Register for OS power setting notifications
    HPOWERNOTIFY hPwrSrc = RegisterPowerSettingNotification(ui.GetWindowHandle(), &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY hBatPct = RegisterPowerSettingNotification(ui.GetWindowHandle(), &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_WINDOW_HANDLE);
    HPOWERNOTIFY hPwrMode = RegisterPowerSettingNotification(ui.GetWindowHandle(), &GUID_POWER_MODE_SLIDER, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Run message loop
    int result = ui.RunMessageLoop();

    // Unregister power notifications
    if (hPwrSrc) UnregisterPowerSettingNotification(hPwrSrc);
    if (hBatPct) UnregisterPowerSettingNotification(hBatPct);
    if (hPwrMode) UnregisterPowerSettingNotification(hPwrMode);

    // Cleanup
    ui.Shutdown();
    monitor.Stop();
    gpuMgr.Shutdown();
    cpuMgr.Shutdown();
    fanMgr.Shutdown();
    ledMgr.Shutdown();
    optimusMgr.Shutdown();
    powerMgr.Shutdown();
    CoUninitialize();
    CloseHandle(hMutex);

    // Clear globals
    g_powerMgr = nullptr;
    g_gpuMgr = nullptr;
    g_cpuMgr = nullptr;
    g_displayMgr = nullptr;
    g_winPowerMgr = nullptr;
    g_optimusMgr = nullptr;
    g_ledMgr = nullptr;
    g_fanMgr = nullptr;
    g_monitor = nullptr;
    g_uiPtr = nullptr;

    return result;
}