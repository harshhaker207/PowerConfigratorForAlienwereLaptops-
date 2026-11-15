#pragma once

#include "Common.h"

class GpuManager {
public:
    GpuManager();
    ~GpuManager();

    bool Initialize(LogCallback logCallback = nullptr);
    void Shutdown();

    // Power limit control
    bool SetPowerLimitMw(unsigned int powerLimitMw);
    bool GetPowerLimitMw(unsigned int* powerLimitMw) const;

    // Monitoring
    bool UpdateMetrics(SystemMetrics& metrics);
    bool IsAvailable() const { return m_initialized && m_deviceHandle != nullptr; }

    // Device info
    std::string GetDeviceName() const { return m_deviceName; }

private:
    void Log(const std::wstring& msg);
    bool LoadNvmlFunctions();
    void UnloadNvmlFunctions();

#ifdef HAVE_NVML
    // Function pointers for dynamic loading
    using NvmlInitFn = nvmlReturn_t (*)(void);
    using NvmlShutdownFn = nvmlReturn_t (*)(void);
    using NvmlDeviceGetHandleByIndexFn = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using NvmlDeviceGetNameFn = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
    using NvmlDeviceGetTemperatureFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlTemperatureSensors_t, unsigned int*);
    using NvmlDeviceGetClockInfoFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlClockType_t, unsigned int*);
    using NvmlDeviceGetPowerUsageFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using NvmlDeviceGetPowerManagementLimitFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
    using NvmlDeviceSetPowerManagementLimitFn = nvmlReturn_t (*)(nvmlDevice_t, unsigned int);
    using NvmlDeviceGetUtilizationRatesFn = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);

    HMODULE m_nvmlModule = nullptr;
    NvmlInitFn m_nvmlInit = nullptr;
    NvmlShutdownFn m_nvmlShutdown = nullptr;
    NvmlDeviceGetHandleByIndexFn m_nvmlDeviceGetHandleByIndex = nullptr;
    NvmlDeviceGetNameFn m_nvmlDeviceGetName = nullptr;
    NvmlDeviceGetTemperatureFn m_nvmlDeviceGetTemperature = nullptr;
    NvmlDeviceGetClockInfoFn m_nvmlDeviceGetClockInfo = nullptr;
    NvmlDeviceGetPowerUsageFn m_nvmlDeviceGetPowerUsage = nullptr;
    NvmlDeviceGetPowerManagementLimitFn m_nvmlDeviceGetPowerManagementLimit = nullptr;
    NvmlDeviceSetPowerManagementLimitFn m_nvmlDeviceSetPowerManagementLimit = nullptr;
    NvmlDeviceGetUtilizationRatesFn m_nvmlDeviceGetUtilizationRates = nullptr;
#endif

    bool m_initialized = false;
    bool m_available = false;
    nvmlDevice_t m_deviceHandle = nullptr;
    std::string m_deviceName;
    LogCallback m_logCallback;
};