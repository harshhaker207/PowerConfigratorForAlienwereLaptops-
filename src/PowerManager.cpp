#include "PowerManager.h"
#include <vector>

PowerManager::PowerManager() {
    // Battery Profile
    m_batteryProfile = ProfileSettings{};
    m_batteryProfile.isBatteryProfile = true;
    m_batteryProfile.cpuPl1Watts = 20;
    m_batteryProfile.cpuMinStatePercent = 5;
    m_batteryProfile.cpuMaxStatePercent = 25;
    m_batteryProfile.coolingPolicy = CoolingPolicy::Passive;
    m_batteryProfile.boostPolicy = BoostPolicy::Disabled;
    m_batteryProfile.gpuPowerLimitMw = 40000;
    m_batteryProfile.refreshRateHz = 60;
    m_batteryProfile.autoRefreshRate = true;
    m_batteryProfile.powerMode = PowerMode::MaximumBatteryLife;
    m_batteryProfile.displayTurnOffMinutes = 2;
    m_batteryProfile.displayDimMinutes = 1;
    m_batteryProfile.sleepMinutes = 5;
    m_batteryProfile.hardDiskTurnOffMinutes = 5;
    m_batteryProfile.usbSelectiveSuspend = UsbSelectiveSuspend::Enabled;
    m_batteryProfile.pciExpressLinkPower = PciExpressLinkPower::MaximumPowerSavings;
    m_batteryProfile.intelGraphicsPlan = IntelGraphicsPowerPlan::MaximumBatteryLife;

    // Performance Profile
    m_performanceProfile = ProfileSettings{};
    m_performanceProfile.isBatteryProfile = false;
    m_performanceProfile.cpuPl1Watts = 115;
    m_performanceProfile.cpuMinStatePercent = 10;
    m_performanceProfile.cpuMaxStatePercent = 100;
    m_performanceProfile.coolingPolicy = CoolingPolicy::Active;
    m_performanceProfile.boostPolicy = BoostPolicy::Aggressive;
    m_performanceProfile.gpuPowerLimitMw = 135000;
    m_performanceProfile.refreshRateHz = 240;
    m_performanceProfile.autoRefreshRate = true;
    m_performanceProfile.powerMode = PowerMode::MaximumPerformance;
    m_performanceProfile.displayTurnOffMinutes = 10;
    m_performanceProfile.displayDimMinutes = 5;
    m_performanceProfile.sleepMinutes = 0; // Never
    m_performanceProfile.hardDiskTurnOffMinutes = 20;
    m_performanceProfile.usbSelectiveSuspend = UsbSelectiveSuspend::Disabled;
    m_performanceProfile.pciExpressLinkPower = PciExpressLinkPower::Off;
    m_performanceProfile.intelGraphicsPlan = IntelGraphicsPowerPlan::MaximumPerformance;
}

PowerManager::~PowerManager() = default;

bool PowerManager::Shutdown() {
    m_initialized = false;
    return true;
}

bool PowerManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;
    RefreshAcStatus();
    m_initialized = true;
    Log(L"PowerManager initialized");
    return true;
}

void PowerManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

void PowerManager::RefreshAcStatus() {
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status)) {
        bool wasOnAc = m_isOnAc;
        m_isOnAc = (status.ACLineStatus == 1);
        if (wasOnAc != m_isOnAc) {
            Log(m_isOnAc ? L"AC Power connected" : L"AC Power disconnected (on battery)");
            if (m_autoSwitchEnabled) {
                ApplyProfile(m_isOnAc ? ProfileType::Performance : ProfileType::Battery);
            }
        }
    }
}

void PowerManager::OnPowerBroadcast(WPARAM wParam, LPARAM /*lParam*/) {
    switch (wParam) {
        case PBT_APMPOWERSTATUSCHANGE:
            Log(L"PBT_APMPOWERSTATUSCHANGE received");
            RefreshAcStatus();
            break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            Log(L"System resume detected");
            RefreshAcStatus();
            break;
        default:
            break;
    }
}

bool PowerManager::ApplyProfile(ProfileType type) {
    const ProfileSettings* profile = nullptr;
    ProfileType newProfile = type;

    if (type == ProfileType::Battery) {
        profile = &m_batteryProfile;
        newProfile = ProfileType::Battery;
    } else if (type == ProfileType::Performance) {
        profile = &m_performanceProfile;
        newProfile = ProfileType::Performance;
    } else {
        return false;
    }

    if (!profile) return false;

    Log(L"Applying " + std::wstring(type == ProfileType::Battery ? L"Battery" : L"Performance") + L" profile...");

    // Set Windows Power Plan with fallback to Balanced if specific plan does not exist
    GUID planGuid = (type == ProfileType::Battery) ? GUID_POWER_SCHEME_POWER_SAVER : GUID_POWER_SCHEME_PERFORMANCE;
    if (!SetPowerPlan(planGuid)) {
        Log(L"Requested power plan not found; activating Balanced scheme as base");
        SetPowerPlan(GUID_POWER_SCHEME_BALANCED);
    }

    // Apply CPU power settings via PowerManager (handled by WindowsPowerManager)
    // Apply GPU power settings via GpuManager
    // Apply Display refresh rate via DisplayManager

    m_currentProfile = newProfile;

    if (m_profileChangedCallback) {
        m_profileChangedCallback(newProfile);
    }

    Log(L"Profile applied successfully");
    return true;
}

void PowerManager::ApplyBatteryProfile() {
    ApplyProfile(ProfileType::Battery);
}

void PowerManager::ApplyPerformanceProfile() {
    ApplyProfile(ProfileType::Performance);
}

bool PowerManager::GetActivePowerPlan(GUID* planGuid) {
    GUID* activeGuid = nullptr;
    DWORD result = PowerGetActiveScheme(nullptr, &activeGuid);
    if (result == ERROR_SUCCESS && activeGuid) {
        *planGuid = *activeGuid;
        LocalFree(activeGuid);
        return true;
    }
    return false;
}

std::wstring PowerManager::GetPowerPlanName(const GUID& planGuid) {
    DWORD size = 0;
    PowerReadFriendlyName(nullptr, const_cast<GUID*>(&planGuid), nullptr, nullptr, nullptr, &size);
    if (size == 0) return L"Unknown";

    std::vector<BYTE> buffer(size);
    DWORD result = PowerReadFriendlyName(nullptr, const_cast<GUID*>(&planGuid), nullptr, nullptr, buffer.data(), &size);
    if (result == ERROR_SUCCESS) {
        return std::wstring(reinterpret_cast<wchar_t*>(buffer.data()));
    }
    return L"Unknown";
}

bool PowerManager::SetPowerPlan(const GUID& planGuid) {
    // Use PowerSetActiveScheme to set the active power plan
    // This requires admin privileges for most operations
    DWORD result = PowerSetActiveScheme(nullptr, const_cast<GUID*>(&planGuid));
    if (result == ERROR_SUCCESS) {
        Log(L"Power plan set successfully: " + GetPowerPlanName(planGuid));
        return true;
    }
    Log(L"PowerSetActiveScheme failed with error: " + std::to_wstring(result));
    return false;
}