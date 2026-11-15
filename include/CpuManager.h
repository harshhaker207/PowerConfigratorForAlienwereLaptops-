#pragma once

#include "Common.h"
#include <map>
#include <mutex>

class CpuManager {
public:
    CpuManager();
    ~CpuManager();

    bool Initialize(LogCallback logCallback = nullptr);
    void Shutdown();

    // Monitoring
    bool UpdateMetrics(SystemMetrics& metrics);
    double GetCpuUsage() const;

    // CPU Power Limit control (via MSR or Intel DTT if available)
    bool SetCpuPowerLimitWatts(int pl1Watts);
    bool GetCpuPowerLimitWatts(int* pl1Watts) const;

    // WMI query helpers
    std::string GetCpuName() const { return m_cpuName; }

private:
    void Log(const std::wstring& msg);
    bool InitializeWmi();
    void CleanupWmi();
    bool QueryWmi(const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results);
    bool QueryRootWmi(const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results);
    bool QueryWmiNamespace(IWbemServices* pServices, const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results);

    // PDH counters for CPU usage
    bool InitializePdh();
    void CleanupPdh();

    mutable std::mutex m_wmiMutex;
    IWbemLocator* m_wbemLocator = nullptr;
    IWbemServices* m_wbemServices = nullptr;
    IWbemServices* m_wbemServicesWmi = nullptr;
    bool m_wmiInitialized = false;

    // PDH
    PDH_HQUERY m_pdhQuery = nullptr;
    PDH_HCOUNTER m_pdhCounterTotal = nullptr;
    bool m_pdhInitialized = false;

    std::string m_cpuName;
    LogCallback m_logCallback;
};