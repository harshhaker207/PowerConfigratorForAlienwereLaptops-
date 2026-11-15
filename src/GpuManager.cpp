#include "GpuManager.h"

GpuManager::GpuManager() = default;

GpuManager::~GpuManager() {
    Shutdown();
}

void GpuManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

bool GpuManager::LoadNvmlFunctions() {
#ifdef HAVE_NVML
    m_nvmlModule = LoadLibraryW(L"nvml.dll");
    if (!m_nvmlModule) {
        Log(L"Failed to load nvml.dll");
        return false;
    }

    m_nvmlInit = reinterpret_cast<NvmlInitFn>(GetProcAddress(m_nvmlModule, "nvmlInit_v2"));
    m_nvmlShutdown = reinterpret_cast<NvmlShutdownFn>(GetProcAddress(m_nvmlModule, "nvmlShutdown"));
    m_nvmlDeviceGetHandleByIndex = reinterpret_cast<NvmlDeviceGetHandleByIndexFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetHandleByIndex_v2"));
    m_nvmlDeviceGetName = reinterpret_cast<NvmlDeviceGetNameFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetName"));
    m_nvmlDeviceGetTemperature = reinterpret_cast<NvmlDeviceGetTemperatureFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetTemperature"));
    m_nvmlDeviceGetClockInfo = reinterpret_cast<NvmlDeviceGetClockInfoFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetClockInfo"));
    m_nvmlDeviceGetPowerUsage = reinterpret_cast<NvmlDeviceGetPowerUsageFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetPowerUsage"));
    m_nvmlDeviceGetPowerManagementLimit = reinterpret_cast<NvmlDeviceGetPowerManagementLimitFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetPowerManagementLimit"));
    m_nvmlDeviceSetPowerManagementLimit = reinterpret_cast<NvmlDeviceSetPowerManagementLimitFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceSetPowerManagementLimit"));
    m_nvmlDeviceGetUtilizationRates = reinterpret_cast<NvmlDeviceGetUtilizationRatesFn>(GetProcAddress(m_nvmlModule, "nvmlDeviceGetUtilizationRates"));

    if (!m_nvmlInit || !m_nvmlShutdown || !m_nvmlDeviceGetHandleByIndex ||
        !m_nvmlDeviceGetName || !m_nvmlDeviceGetTemperature || !m_nvmlDeviceGetClockInfo ||
        !m_nvmlDeviceGetPowerUsage || !m_nvmlDeviceGetPowerManagementLimit ||
        !m_nvmlDeviceSetPowerManagementLimit || !m_nvmlDeviceGetUtilizationRates) {
        Log(L"Failed to load one or more NVML functions");
        FreeLibrary(m_nvmlModule);
        m_nvmlModule = nullptr;
        return false;
    }

    return true;
#else
    return false;
#endif
}

void GpuManager::UnloadNvmlFunctions() {
#ifdef HAVE_NVML
    if (m_nvmlModule) {
        FreeLibrary(m_nvmlModule);
        m_nvmlModule = nullptr;
    }
    m_nvmlInit = nullptr;
    m_nvmlShutdown = nullptr;
    m_nvmlDeviceGetHandleByIndex = nullptr;
    m_nvmlDeviceGetName = nullptr;
    m_nvmlDeviceGetTemperature = nullptr;
    m_nvmlDeviceGetClockInfo = nullptr;
    m_nvmlDeviceGetPowerUsage = nullptr;
    m_nvmlDeviceGetPowerManagementLimit = nullptr;
    m_nvmlDeviceSetPowerManagementLimit = nullptr;
    m_nvmlDeviceGetUtilizationRates = nullptr;
#endif
}

bool GpuManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;

#ifdef HAVE_NVML
    if (!LoadNvmlFunctions()) {
        Log(L"NVML not available - GPU control disabled");
        m_available = false;
        return false;
    }

    nvmlReturn_t result = m_nvmlInit();
    if (result != NVML_SUCCESS) {
        Log(L"NVML initialization failed: " + std::to_wstring(result));
        UnloadNvmlFunctions();
        m_available = false;
        return false;
    }

    // Get first GPU
    result = m_nvmlDeviceGetHandleByIndex(0, &m_deviceHandle);
    if (result != NVML_SUCCESS) {
        Log(L"Failed to get GPU handle: " + std::to_wstring(result));
        m_nvmlShutdown();
        UnloadNvmlFunctions();
        m_available = false;
        return false;
    }

    // Get device name
    char name[NVML_DEVICE_NAME_BUFFER_SIZE];
    result = m_nvmlDeviceGetName(m_deviceHandle, name, sizeof(name));
    if (result == NVML_SUCCESS) {
        m_deviceName = name;
        Log(L"GPU detected: " + StringToWString(m_deviceName));
    }

    m_available = true;
    m_initialized = true;
    return true;
#else
    Log(L"NVML support not compiled in");
    m_available = false;
    return false;
#endif
}

void GpuManager::Shutdown() {
#ifdef HAVE_NVML
    if (m_initialized && m_nvmlShutdown) {
        m_nvmlShutdown();
    }
    UnloadNvmlFunctions();
    m_initialized = false;
    m_available = false;
    m_deviceHandle = nullptr;
#endif
}

bool GpuManager::SetPowerLimitMw(unsigned int powerLimitMw) {
#ifdef HAVE_NVML
    if (!m_available || !m_deviceHandle || !m_nvmlDeviceSetPowerManagementLimit) {
        return false;
    }

    // NVML expects power limit in milliwatts
    nvmlReturn_t result = m_nvmlDeviceSetPowerManagementLimit(m_deviceHandle, powerLimitMw);
    if (result != NVML_SUCCESS) {
        Log(L"Failed to set GPU power limit: " + std::to_wstring(result));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool GpuManager::GetPowerLimitMw(unsigned int* powerLimitMw) const {
#ifdef HAVE_NVML
    if (!m_available || !m_deviceHandle || !m_nvmlDeviceGetPowerManagementLimit || !powerLimitMw) {
        return false;
    }

    nvmlReturn_t result = m_nvmlDeviceGetPowerManagementLimit(m_deviceHandle, powerLimitMw);
    if (result != NVML_SUCCESS) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool GpuManager::UpdateMetrics(SystemMetrics& metrics) {
#ifdef HAVE_NVML
    if (!m_available || !m_deviceHandle) {
        return false;
    }

    // Temperature
    unsigned int temp = 0;
    if (m_nvmlDeviceGetTemperature(m_deviceHandle, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS) {
        metrics.gpuTemperatureC = static_cast<int>(temp);
    }

    // Clock (graphics clock)
    unsigned int clock = 0;
    if (m_nvmlDeviceGetClockInfo(m_deviceHandle, NVML_CLOCK_GRAPHICS, &clock) == NVML_SUCCESS) {
        metrics.gpuClockMhz = static_cast<int>(clock);
    }

    // Power Draw (in milliwatts, convert to watts)
    unsigned int powerMw = 0;
    if (m_nvmlDeviceGetPowerUsage(m_deviceHandle, &powerMw) == NVML_SUCCESS) {
        double w = powerMw / 1000.0;
        // Sanity guard for mobile GPU telemetry (RTX 4060 laptop cannot physically exceed 140W-175W)
        if (w > 250.0) {
            w /= 10.0; // Scaled reporting: driver returned deciwatts / 10x mW
        }
        if (w > 200.0) {
            w = 200.0;
        }
        metrics.gpuPowerDrawW = w;
    }

    // Power Limit
    unsigned int limitMw = 0;
    if (m_nvmlDeviceGetPowerManagementLimit(m_deviceHandle, &limitMw) == NVML_SUCCESS) {
        metrics.gpuPowerLimitW = static_cast<int>(limitMw / 1000);
    }

    // Utilization
    nvmlUtilization_t utilization{};
    if (m_nvmlDeviceGetUtilizationRates(m_deviceHandle, &utilization) == NVML_SUCCESS) {
        metrics.gpuUsagePercent = utilization.gpu;
    }

    return true;
#else
    return false;
#endif
}