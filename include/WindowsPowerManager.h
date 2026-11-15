#pragma once

#include "Common.h"

class WindowsPowerManager {
public:
    WindowsPowerManager();
    ~WindowsPowerManager();

    bool Initialize(LogCallback logCallback = nullptr);

    // Apply all power settings for a profile
    bool ApplyProfileSettings(const ProfileSettings& profile);

    // Individual setting controls
    bool SetPowerMode(PowerMode mode);
    bool SetProcessorMinState(int acPercent, int dcPercent);
    bool SetProcessorMaxState(int acPercent, int dcPercent);
    bool SetSystemCoolingPolicy(CoolingPolicy acPolicy, CoolingPolicy dcPolicy);
    bool SetProcessorBoostPolicy(BoostPolicy acPolicy, BoostPolicy dcPolicy);
    bool SetProcessorEnergyPerfPreference(int acValue, int dcValue);
    bool SetProcessorMaxFrequency(int acMhz, int dcMhz);
    bool CommitActiveScheme();
    bool SetDisplayTurnOff(int acMinutes, int dcMinutes);
    bool SetDisplayDim(int acMinutes, int dcMinutes);
    bool SetSleepAfter(int acMinutes, int dcMinutes);
    bool SetHardDiskTurnOff(int acMinutes, int dcMinutes);
    bool SetUsbSelectiveSuspend(UsbSelectiveSuspend acSetting, UsbSelectiveSuspend dcSetting);
    bool SetPciExpressLinkPower(PciExpressLinkPower acSetting, PciExpressLinkPower dcSetting);
    bool SetIntelGraphicsPowerPlan(IntelGraphicsPowerPlan acPlan, IntelGraphicsPowerPlan dcPlan);
    bool SetAlienFxEnabled(bool enable);

    // Get current values
    bool GetCurrentSettings(ProfileSettings& outSettings) const;
    PowerMode GetCurrentPowerMode() const;
    std::wstring GetActivePowerPlanName() const;

    // Power plan management
    bool SetActivePowerPlan(const GUID& planGuid);
    bool GetActivePowerPlan(GUID* planGuid) const;

private:
    void Log(const std::wstring& msg);
    bool WritePowerValue(const GUID& subGroupGuid, const GUID& powerSettingGuid,
                         bool isAc, DWORD value);
    bool ReadPowerValue(const GUID& subGroupGuid, const GUID& powerSettingGuid,
                        bool isAc, DWORD* value) const;
    bool SetActiveScheme(const GUID& planGuid);
    bool GetPowerSettingIndex(const GUID& planGuid, const GUID& subGroupGuid,
                              const GUID& powerSettingGuid, bool isAc, DWORD* value) const;

    LogCallback m_logCallback;
    GUID m_activeSchemeGuid{};
};