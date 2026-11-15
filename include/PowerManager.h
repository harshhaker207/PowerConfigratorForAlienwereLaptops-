#pragma once

#include "Common.h"

class PowerManager {
public:
    PowerManager();
    ~PowerManager();

    // Initialize AC power detection
    bool Initialize(LogCallback logCallback = nullptr);
    bool Shutdown();

    // Power profiles
    const ProfileSettings& GetBatteryProfile() const { return m_batteryProfile; }
    const ProfileSettings& GetPerformanceProfile() const { return m_performanceProfile; }
    ProfileSettings& GetBatteryProfileMutable() { return m_batteryProfile; }
    ProfileSettings& GetPerformanceProfileMutable() { return m_performanceProfile; }

    // AC Power detection
    bool IsOnAcPower() const { return m_isOnAc; }
    void RefreshAcStatus();

    // Apply profile
    bool ApplyProfile(ProfileType type);

    // Get current profile type
    ProfileType GetCurrentProfileType() const { return m_currentProfile; }

    // Auto-switching
    void SetAutoSwitchEnabled(bool enabled) { m_autoSwitchEnabled = enabled; }
    bool IsAutoSwitchEnabled() const { return m_autoSwitchEnabled; }

    // Window message handler for WM_POWERBROADCAST
    void OnPowerBroadcast(WPARAM wParam, LPARAM lParam);

    // Callbacks
    void SetProfileChangedCallback(ProfileChangedCallback cb) { m_profileChangedCallback = cb; }
    void SetLogCallback(LogCallback cb) { m_logCallback = cb; }

private:
    void Log(const std::wstring& msg);
    void ApplyBatteryProfile();
    void ApplyPerformanceProfile();
    bool SetPowerPlan(const GUID& planGuid);
    bool GetActivePowerPlan(GUID* planGuid);
    std::wstring GetPowerPlanName(const GUID& planGuid);

    ProfileSettings m_batteryProfile;
    ProfileSettings m_performanceProfile;
    ProfileType m_currentProfile = ProfileType::Battery;
    bool m_isOnAc = true;
    bool m_autoSwitchEnabled = true;
    bool m_initialized = false;

    LogCallback m_logCallback;
    ProfileChangedCallback m_profileChangedCallback;
};