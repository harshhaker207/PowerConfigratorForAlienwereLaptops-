#pragma once

#include "Common.h"
#include <vector>

class OptimusManager {
public:
    OptimusManager();
    ~OptimusManager();

    bool Initialize(LogCallback logCallback = nullptr);
    void Shutdown();

    // GPU Mode control
    bool SetGpuMode(GpuMode mode);
    GpuMode GetGpuMode() const { return m_currentMode; }
    GpuMode GetCurrentMode() const { return m_currentMode; }
    bool IsMuxSupported() const { return m_muxSupported; }
    bool IsAdvancedOptimusSupported() const { return m_advancedOptimusSupported; }

    // dGPU power control
    bool SetDgpuPowerState(DgpuPowerState state);
    DgpuPowerState GetDgpuPowerState() const { return m_dgpuState; }

    // dGPU process monitoring
    std::vector<std::string> GetProcessesUsingDgpu();
    bool ForceDgpuSleep();

    // Status
    bool IsAvailable() const { return m_initialized; }
    std::wstring GetStatusString() const;

    // WMI methods for system-specific control
    bool SetDellMuxMode(bool enableDiscrete);
    bool CallAwccMuxSwitch(bool enableDiscrete);
    bool ToggleNvidiaPreferredGpu(bool useDiscrete);

private:
    void Log(const std::wstring& msg);
    bool CheckNvapi();
    bool CheckDellWmi();
    bool LoadNvapi();
    void UnloadNvapi();
    bool SetNvapiPreferredGpu(bool discrete);
    bool ApplyDellWmiMux(bool enableDiscrete);
    bool FindAndDisableDgpuDevice();
    bool FindAndEnableDgpuDevice();

#ifdef HAVE_NVML
    bool IsDgpuUsingNvml() const;
#endif

    // NVAPI function pointers (dynamically loaded)
    typedef int* NvAPI_Status;
    typedef int NVAPI_STATUS;

    bool m_initialized = false;
    bool m_nvapiAvailable = false;
    bool m_dellWmiAvailable = false;
    bool m_muxSupported = false;
    bool m_advancedOptimusSupported = false;
    bool m_hasIntegratedGpu = false;
    bool m_hasDiscreteGpu = false;

    GpuMode m_currentMode = GpuMode::Auto;
    DgpuPowerState m_dgpuState = DgpuPowerState::Unknown;

    HMODULE m_nvapiModule = nullptr;
    void* m_nvapiInterface = nullptr;

    LogCallback m_logCallback;
};