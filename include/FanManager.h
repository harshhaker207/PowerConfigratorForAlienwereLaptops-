#pragma once

#include "Common.h"
#include <atomic>
#include <thread>
#include <memory>

// Forward declarations
class CpuManager;
class GpuManager;
class WindowsPowerManager;

// Temperature thresholds for auto protection
struct TemperatureThresholds {
    int warnCelsius = 88;     // Warning at 88°C
    int protectCelsius = 95;  // Auto 50% at 95°C
    int criticalCelsius = 100; // Emergency 100% only at 100°C (Meteor Lake TJMax is 105°C)
};

class FanManager {
public:
    FanManager();
    ~FanManager();

    bool Initialize(LogCallback logCallback = nullptr,
                   CpuManager* cpuMgr = nullptr,
                   GpuManager* gpuMgr = nullptr,
                   WindowsPowerManager* winPowerMgr = nullptr);
    void Shutdown();

    // Fan control
    bool SetFanSpeed(int percent); // 0-100
    int GetFanSpeed() const { return m_currentSpeed; }
    bool SetMode(FanControlMode mode) { m_mode = mode; return true; }
    FanControlMode GetMode() const { return m_mode; }

    // Preset buttons
    void ApplyPreset(FanPreset preset);
    FanPreset GetActivePreset() const { return m_activePreset; }

    // Overheat protection
    void SetTemperatureThresholds(const TemperatureThresholds& thresholds);
    TemperatureThresholds GetTemperatureThresholds() const { return m_thresholds; }
    bool CheckOverheatProtection(int cpuTemp, int gpuTemp);
    bool IsOverheatProtectionActive() const { return m_overheatProtectionActive; }

    // Live data
    int GetFan1Rpm() const { return m_fan1Rpm; }
    int GetFan2Rpm() const { return m_fan2Rpm; }
    int GetCpuTemperature() const { return m_cpuTemperature; }
    int GetGpuTemperature() const { return m_gpuTemperature; }
    std::wstring GetStatusString() const;

    // Dell & Alienware WMI support
    bool SetDellFanSpeed(int percent);
    bool ReadDellFanRpm(int* fan1Rpm, int* fan2Rpm);
    bool SetAwccThermalProfile(DWORD profileId);
    bool SetGameShift(bool enable);
    bool SetDirectFanSpeed(DWORD fanId, int percent);
    bool CallAwccMethod(const wchar_t* methodName, DWORD arg2, DWORD* argr = nullptr);
    const std::vector<DWORD>& GetDetectedFanIds() const { return m_detectedFanIds; }

    // Auto/manual mode
    bool IsAutoMode() const { return m_mode == FanControlMode::Auto; }
    void EnableAutoMode(bool enable) { m_mode = enable ? FanControlMode::Auto : FanControlMode::Manual; }

    // Temperature feed from Monitor
    void UpdateTemperatures(int cpuTemp, int gpuTemp);

    // Status
    bool IsAvailable() const { return m_initialized; }

private:
    void Log(const std::wstring& msg);
    bool InitializeDellWmi();
    bool InitializeAwccWmi();
    void CleanupDellWmi();
    bool InitializeEc();
    bool LoadWinRing0();
    bool UnloadWinRing0();
    bool SetEcFanSpeed(int percent);
    void ReadTemperatures();
    void WorkerThread();

    bool m_initialized = false;
    bool m_dellWmiAvailable = false;
    bool m_awccWmiAvailable = false;
    std::wstring m_awccInstancePath;
    bool m_winRing0Loaded = false;
    bool m_ecAvailable = false;

    IWbemLocator* m_dellWbemLocator = nullptr;
    IWbemServices* m_dellWbemServices = nullptr;
    HMODULE m_winRing0Module = nullptr;
    mutable std::recursive_mutex m_fanWmiMutex;
    std::vector<DWORD> m_detectedFanIds;

    CpuManager* m_cpuMgr = nullptr;
    GpuManager* m_gpuMgr = nullptr;
    WindowsPowerManager* m_winPowerMgr = nullptr;
    FanPreset m_activePreset = FanPreset::Balanced;

    FanControlMode m_mode = FanControlMode::Auto;
    std::atomic<int> m_currentSpeed{0};
    std::atomic<int> m_fan1Rpm{0};
    std::atomic<int> m_fan2Rpm{0};
    std::atomic<int> m_cpuTemperature{40};
    std::atomic<int> m_gpuTemperature{40};
    std::atomic<bool> m_overheatProtectionActive{false};

    TemperatureThresholds m_thresholds;
    std::atomic<bool> m_running{false};
    std::thread m_workerThread;

    LogCallback m_logCallback;
};