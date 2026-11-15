#include "WindowsPowerManager.h"
#include <algorithm>

WindowsPowerManager::WindowsPowerManager() = default;

WindowsPowerManager::~WindowsPowerManager() = default;

// Helper for Clamp (C++17 but may not be available in older MSVC)
template<typename T>
static T Clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}

void WindowsPowerManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

bool WindowsPowerManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;
    GetActivePowerPlan(&m_activeSchemeGuid);
    Log(L"WindowsPowerManager initialized");
    return true;
}

bool WindowsPowerManager::WritePowerValue(const GUID& subGroupGuid, const GUID& powerSettingGuid,
                                          bool isAc, DWORD value) {
    DWORD result = isAc ?
        PowerWriteACValueIndex(nullptr, const_cast<GUID*>(&m_activeSchemeGuid),
                               const_cast<GUID*>(&subGroupGuid),
                               const_cast<GUID*>(&powerSettingGuid),
                               value) :
        PowerWriteDCValueIndex(nullptr, const_cast<GUID*>(&m_activeSchemeGuid),
                               const_cast<GUID*>(&subGroupGuid),
                               const_cast<GUID*>(&powerSettingGuid),
                               value);
    if (result != ERROR_SUCCESS) {
        Log(L"PowerWrite" + std::wstring(isAc ? L"AC" : L"DC") + L"ValueIndex failed for " +
            GuidToString(powerSettingGuid) + L": " + std::to_wstring(result));
        return false;
    }
    return true;
}

bool WindowsPowerManager::ReadPowerValue(const GUID& subGroupGuid, const GUID& powerSettingGuid,
                                         bool isAc, DWORD* value) const {
    if (!value) return false;
    DWORD result = isAc ?
        PowerReadACValueIndex(nullptr, const_cast<GUID*>(&m_activeSchemeGuid),
                              const_cast<GUID*>(&subGroupGuid),
                              const_cast<GUID*>(&powerSettingGuid),
                              value) :
        PowerReadDCValueIndex(nullptr, const_cast<GUID*>(&m_activeSchemeGuid),
                              const_cast<GUID*>(&subGroupGuid),
                              const_cast<GUID*>(&powerSettingGuid),
                              value);
    if (result != ERROR_SUCCESS) {
        return false;
    }
    return true;
}

bool WindowsPowerManager::SetActiveScheme(const GUID& planGuid) {
    DWORD result = PowerSetActiveScheme(nullptr, const_cast<GUID*>(&planGuid));
    if (result == ERROR_SUCCESS) {
        m_activeSchemeGuid = planGuid;
        return true;
    }
    Log(L"PowerSetActiveScheme failed: " + std::to_wstring(result));
    return false;
}

bool WindowsPowerManager::GetActivePowerPlan(GUID* planGuid) const {
    GUID* activeGuid = nullptr;
    DWORD result = PowerGetActiveScheme(nullptr, &activeGuid);
    if (result == ERROR_SUCCESS && activeGuid) {
        if (planGuid) *planGuid = *activeGuid;
        LocalFree(activeGuid);
        return true;
    }
    return false;
}

bool WindowsPowerManager::SetPowerMode(PowerMode mode) {
    // The Power Mode Slider is controlled via the Processor Energy Performance Preference
    // but Windows 11 also has a specific slider setting
    bool success = true;

    // Set via Processor Energy Performance Preference Policy
    // Values: 0=Performance, 1=Balanced Performance, 2=Balanced, 3=Balanced Power, 4=Power Saver
    // But we'll map our PowerMode to these
    DWORD eppValue = 0;
    switch (mode) {
        case PowerMode::MaximumBatteryLife: eppValue = 4; break; // Power Saver
        case PowerMode::BetterBattery: eppValue = 3; break;      // Balanced Power
        case PowerMode::Balanced: eppValue = 2; break;           // Balanced
        case PowerMode::BetterPerformance: eppValue = 1; break;  // Balanced Performance
        case PowerMode::MaximumPerformance: eppValue = 0; break; // Performance
    }

    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF, true, eppValue);
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF, false, eppValue);
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF_1, true, eppValue);
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF_1, false, eppValue);

    return success;
}

bool WindowsPowerManager::SetProcessorMinState(int acPercent, int dcPercent) {
    acPercent = Clamp(acPercent, 0, 100);
    dcPercent = Clamp(dcPercent, 0, 100);

    bool success = true;
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MINIMUM_STATE, true, static_cast<DWORD>(acPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MINIMUM_STATE, false, static_cast<DWORD>(dcPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MINIMUM_STATE_1, true, static_cast<DWORD>(acPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MINIMUM_STATE_1, false, static_cast<DWORD>(dcPercent));
    return success;
}

bool WindowsPowerManager::SetProcessorMaxState(int acPercent, int dcPercent) {
    acPercent = Clamp(acPercent, 0, 100);
    dcPercent = Clamp(dcPercent, 0, 100);

    bool success = true;
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MAXIMUM_STATE, true, static_cast<DWORD>(acPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MAXIMUM_STATE, false, static_cast<DWORD>(dcPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MAXIMUM_STATE_1, true, static_cast<DWORD>(acPercent));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MAXIMUM_STATE_1, false, static_cast<DWORD>(dcPercent));
    return success;
}

bool WindowsPowerManager::SetSystemCoolingPolicy(CoolingPolicy acPolicy, CoolingPolicy dcPolicy) {
    bool success = true;
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_SYSTEM_COOLING_POLICY, true, static_cast<DWORD>(acPolicy));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_SYSTEM_COOLING_POLICY, false, static_cast<DWORD>(dcPolicy));
    return success;
}

bool WindowsPowerManager::SetProcessorBoostPolicy(BoostPolicy acPolicy, BoostPolicy dcPolicy) {
    bool success = true;
    // Map boost policy to boost mode (0 = Disabled, 2 = Aggressive)
    DWORD acMode = (acPolicy == BoostPolicy::Disabled) ? 0 : 2;
    DWORD dcMode = (dcPolicy == BoostPolicy::Disabled) ? 0 : 2;

    // Actual Turbo Boost Mode GUID (PERFBOOSTMODE)
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_MODE, true, acMode);
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_MODE, false, dcMode);

    // Legacy Boost Policy GUIDs
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_POLICY, true, static_cast<DWORD>(acPolicy));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_POLICY, false, static_cast<DWORD>(dcPolicy));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_POLICY_1, true, static_cast<DWORD>(acPolicy));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_BOOST_POLICY_1, false, static_cast<DWORD>(dcPolicy));
    return success;
}

bool WindowsPowerManager::SetProcessorEnergyPerfPreference(int acValue, int dcValue) {
    acValue = Clamp(acValue, 0, 4);
    dcValue = Clamp(dcValue, 0, 4);

    bool success = true;
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF, true, static_cast<DWORD>(acValue));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF, false, static_cast<DWORD>(dcValue));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF_1, true, static_cast<DWORD>(acValue));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF_1, false, static_cast<DWORD>(dcValue));
    return success;
}

bool WindowsPowerManager::SetProcessorMaxFrequency(int acMhz, int dcMhz) {
    bool success = true;
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_FREQUENCY_LIMIT, true, static_cast<DWORD>(acMhz));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_FREQUENCY_LIMIT, false, static_cast<DWORD>(dcMhz));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_FREQUENCY_LIMIT_1, true, static_cast<DWORD>(acMhz));
    success &= WritePowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_FREQUENCY_LIMIT_1, false, static_cast<DWORD>(dcMhz));
    return success;
}

bool WindowsPowerManager::CommitActiveScheme() {
    DWORD result = PowerSetActiveScheme(nullptr, const_cast<GUID*>(&m_activeSchemeGuid));
    if (result == ERROR_SUCCESS) {
        return true;
    }
    Log(L"PowerSetActiveScheme failed: " + std::to_wstring(result));
    return false;
}

bool WindowsPowerManager::SetDisplayTurnOff(int acMinutes, int dcMinutes) {
    // Convert minutes to seconds
    DWORD acSeconds = static_cast<DWORD>(acMinutes * 60);
    DWORD dcSeconds = static_cast<DWORD>(dcMinutes * 60);
    // 0 = Never

    bool success = true;
    success &= WritePowerValue(GUID_DISPLAY_SUBGROUP, GUID_DISPLAY_TURN_OFF, true, acSeconds);
    success &= WritePowerValue(GUID_DISPLAY_SUBGROUP, GUID_DISPLAY_TURN_OFF, false, dcSeconds);
    return success;
}

bool WindowsPowerManager::SetDisplayDim(int acMinutes, int dcMinutes) {
    DWORD acSeconds = static_cast<DWORD>(acMinutes * 60);
    DWORD dcSeconds = static_cast<DWORD>(dcMinutes * 60);

    bool success = true;
    success &= WritePowerValue(GUID_DISPLAY_SUBGROUP, GUID_DISPLAY_DIM, true, acSeconds);
    success &= WritePowerValue(GUID_DISPLAY_SUBGROUP, GUID_DISPLAY_DIM, false, dcSeconds);
    return success;
}

bool WindowsPowerManager::SetSleepAfter(int acMinutes, int dcMinutes) {
    DWORD acSeconds = (acMinutes <= 0) ? 0 : static_cast<DWORD>(acMinutes * 60);
    DWORD dcSeconds = (dcMinutes <= 0) ? 0 : static_cast<DWORD>(dcMinutes * 60);

    bool success = true;
    success &= WritePowerValue(GUID_SLEEP_SUBGROUP, GUID_SLEEP_AFTER, true, acSeconds);
    success &= WritePowerValue(GUID_SLEEP_SUBGROUP, GUID_SLEEP_AFTER, false, dcSeconds);
    return success;
}

bool WindowsPowerManager::SetHardDiskTurnOff(int acMinutes, int dcMinutes) {
    DWORD acSeconds = static_cast<DWORD>(acMinutes * 60);
    DWORD dcSeconds = static_cast<DWORD>(dcMinutes * 60);

    bool success = true;
    success &= WritePowerValue(GUID_HARD_DISK_SUBGROUP, GUID_HARD_DISK_TURN_OFF, true, acSeconds);
    success &= WritePowerValue(GUID_HARD_DISK_SUBGROUP, GUID_HARD_DISK_TURN_OFF, false, dcSeconds);
    return success;
}

bool WindowsPowerManager::SetUsbSelectiveSuspend(UsbSelectiveSuspend acSetting, UsbSelectiveSuspend dcSetting) {
    bool success = true;
    success &= WritePowerValue(GUID_USB_SUBGROUP, GUID_USB_SELECTIVE_SUSPEND, true, static_cast<DWORD>(acSetting));
    success &= WritePowerValue(GUID_USB_SUBGROUP, GUID_USB_SELECTIVE_SUSPEND, false, static_cast<DWORD>(dcSetting));
    return success;
}

bool WindowsPowerManager::SetPciExpressLinkPower(PciExpressLinkPower acSetting, PciExpressLinkPower dcSetting) {
    bool success = true;
    success &= WritePowerValue(GUID_PCIEXPRESS_SUBGROUP, GUID_PCIEXPRESS_LINK_POWER, true, static_cast<DWORD>(acSetting));
    success &= WritePowerValue(GUID_PCIEXPRESS_SUBGROUP, GUID_PCIEXPRESS_LINK_POWER, false, static_cast<DWORD>(dcSetting));
    return success;
}

bool WindowsPowerManager::SetIntelGraphicsPowerPlan(IntelGraphicsPowerPlan acPlan, IntelGraphicsPowerPlan dcPlan) {
    bool success = true;
    success &= WritePowerValue(GUID_INTEL_GRAPHICS_SUBGROUP, GUID_INTEL_GRAPHICS_POWER_PLAN, true, static_cast<DWORD>(acPlan));
    success &= WritePowerValue(GUID_INTEL_GRAPHICS_SUBGROUP, GUID_INTEL_GRAPHICS_POWER_PLAN, false, static_cast<DWORD>(dcPlan));
    return success;
}

bool WindowsPowerManager::SetAlienFxEnabled(bool enable) {
    DWORD val = enable ? 1 : 0;
    bool success = true;
    success &= WritePowerValue(GUID_SUB_NONE, GUID_ALIENFX_POWER_SETTING, true, val);
    success &= WritePowerValue(GUID_SUB_NONE, GUID_ALIENFX_POWER_SETTING, false, val);
    PowerSetActiveScheme(nullptr, const_cast<GUID*>(&m_activeSchemeGuid));
    Log(L"AlienFX OS Power Setting set to " + std::wstring(enable ? L"ON" : L"OFF"));
    return success;
}

bool WindowsPowerManager::ApplyProfileSettings(const ProfileSettings& profile) {
    Log(L"Applying Windows power settings for " + std::wstring(profile.isBatteryProfile ? L"Battery" : L"Performance") + L" profile...");

    bool success = true;

    // 1. Set active power plan with fallback
    GUID planGuid = profile.isBatteryProfile ? GUID_POWER_SCHEME_POWER_SAVER : GUID_POWER_SCHEME_PERFORMANCE;
    if (!SetActiveScheme(planGuid)) {
        Log(L"Primary scheme not found, falling back to Balanced base scheme");
        SetActiveScheme(GUID_POWER_SCHEME_BALANCED);
    }

    // 2. Power Mode Slider
    success &= SetPowerMode(profile.powerMode);

    // 3. Processor Power Management
    success &= SetProcessorMinState(profile.cpuMinStatePercent, profile.cpuMinStatePercent);
    success &= SetProcessorMaxState(profile.cpuMaxStatePercent, profile.cpuMaxStatePercent);
    success &= SetSystemCoolingPolicy(profile.coolingPolicy, profile.coolingPolicy);
    success &= SetProcessorBoostPolicy(profile.boostPolicy, profile.boostPolicy);
    int maxFreq = profile.isBatteryProfile ? 1200 : 0;
    success &= SetProcessorMaxFrequency(maxFreq, maxFreq);

    // 4. Display & Sleep
    success &= SetDisplayTurnOff(profile.displayTurnOffMinutes, profile.displayTurnOffMinutes);
    success &= SetDisplayDim(profile.displayDimMinutes, profile.displayDimMinutes);
    success &= SetSleepAfter(profile.sleepMinutes, profile.sleepMinutes);

    // 5. Hard Disk
    success &= SetHardDiskTurnOff(profile.hardDiskTurnOffMinutes, profile.hardDiskTurnOffMinutes);

    // 6. USB
    success &= SetUsbSelectiveSuspend(profile.usbSelectiveSuspend, profile.usbSelectiveSuspend);

    // 7. PCI Express
    success &= SetPciExpressLinkPower(profile.pciExpressLinkPower, profile.pciExpressLinkPower);

    // 8. Intel Graphics
    success &= SetIntelGraphicsPowerPlan(profile.intelGraphicsPlan, profile.intelGraphicsPlan);

    // 9. AlienFX Lighting
    success &= SetAlienFxEnabled(!profile.isBatteryProfile);

    // Re-activate scheme to commit written AC/DC values to active runtime
    PowerSetActiveScheme(nullptr, const_cast<GUID*>(&m_activeSchemeGuid));

    if (success) {
        Log(L"Windows power settings applied successfully");
    } else {
        Log(L"Some Windows power settings failed to apply");
    }

    return success;
}

bool WindowsPowerManager::GetCurrentSettings(ProfileSettings& outSettings) const {
    // This reads current values from the active power scheme
    // For brevity, we'll just get a few key values
    DWORD value = 0;

    if (ReadPowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MINIMUM_STATE, true, &value))
        outSettings.cpuMinStatePercent = static_cast<int>(value);
    if (ReadPowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_MAXIMUM_STATE, true, &value))
        outSettings.cpuMaxStatePercent = static_cast<int>(value);

    outSettings.powerMode = GetCurrentPowerMode();

    return true;
}

PowerMode WindowsPowerManager::GetCurrentPowerMode() const {
    DWORD value = 0;
    if (ReadPowerValue(GUID_PROCESSOR_POWER_SUBGROUP, GUID_PROCESSOR_ENERGY_PERF_PREF, true, &value)) {
        if (value <= 4) return static_cast<PowerMode>(value);
    }
    return PowerMode::Balanced;
}

std::wstring WindowsPowerManager::GetActivePowerPlanName() const {
    GUID activeGuid{};
    if (GetActivePowerPlan(&activeGuid)) {
        DWORD size = 0;
        PowerReadFriendlyName(nullptr, &activeGuid, nullptr, nullptr, nullptr, &size);
        if (size > 0) {
            std::vector<BYTE> buffer(size);
            DWORD result = PowerReadFriendlyName(nullptr, &activeGuid, nullptr, nullptr, buffer.data(), &size);
            if (result == ERROR_SUCCESS) {
                return std::wstring(reinterpret_cast<wchar_t*>(buffer.data()));
            }
        }
    }
    return L"Unknown";
}