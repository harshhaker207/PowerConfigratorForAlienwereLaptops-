#pragma once

#include "Common.h"
#include <atomic>
#include <thread>
#include <mutex>

class PowerManager;
class GpuManager;
class CpuManager;
class DisplayManager;
class WindowsPowerManager;
class OptimusManager;
class LedManager;
class FanManager;

class Monitor {
public:
    Monitor();
    ~Monitor();

    bool Initialize(PowerManager* powerMgr, GpuManager* gpuMgr, CpuManager* cpuMgr,
                    DisplayManager* displayMgr, WindowsPowerManager* winPowerMgr,
                    OptimusManager* optimusMgr, LedManager* ledMgr, FanManager* fanMgr,
                    MetricsCallback metricsCallback, LogCallback logCallback);

    void Start();
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // Thread-safe fetch of latest metrics
    SystemMetrics GetLatestMetrics() const;

    // Force an immediate update
    void UpdateOnce();

private:
    void Run();
    void CollectMetrics();

    PowerManager* m_powerMgr = nullptr;
    GpuManager* m_gpuMgr = nullptr;
    CpuManager* m_cpuMgr = nullptr;
    DisplayManager* m_displayMgr = nullptr;
    WindowsPowerManager* m_winPowerMgr = nullptr;
    OptimusManager* m_optimusMgr = nullptr;
    LedManager* m_ledMgr = nullptr;
    FanManager* m_fanMgr = nullptr;

    MetricsCallback m_metricsCallback;
    LogCallback m_logCallback;

    mutable std::mutex m_metricsMutex;
    SystemMetrics m_lastMetrics;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    std::chrono::milliseconds m_interval{1000};
};