#include "CpuManager.h"
#include <comdef.h>
#include <map>

// Native Windows processor power information struct for CallNtPowerInformation
struct WinProcessorPowerInfo {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

CpuManager::CpuManager() = default;

CpuManager::~CpuManager() {
    Shutdown();
}

void CpuManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

bool CpuManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;

    if (!InitializeWmi()) {
        Log(L"WMI initialization failed - some metrics unavailable");
    }

    if (!InitializePdh()) {
        Log(L"PDH initialization failed - CPU usage unavailable");
    }

    return true;
}

void CpuManager::Shutdown() {
    CleanupWmi();
    CleanupPdh();
}

bool CpuManager::InitializeWmi() {
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void**>(&m_wbemLocator));
    if (FAILED(hr)) {
        Log(L"Failed to create WbemLocator: " + std::to_wstring(hr));
        return false;
    }

    // Connect to ROOT\CIMV2
    BSTR bstrNamespace = SysAllocString(L"ROOT\\CIMV2");
    BSTR bstrLocale = SysAllocString(L"");
    hr = m_wbemLocator->ConnectServer(bstrNamespace, nullptr, nullptr, bstrLocale,
                                      0, nullptr, nullptr, &m_wbemServices);
    SysFreeString(bstrNamespace);
    SysFreeString(bstrLocale);
    if (FAILED(hr)) {
        Log(L"Failed to connect to ROOT\\CIMV2: " + std::to_wstring(hr));
        m_wbemLocator->Release();
        m_wbemLocator = nullptr;
        return false;
    }

    CoSetProxyBlanket(m_wbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                       RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    // Connect to ROOT\WMI (for MSAcpi_ThermalZoneTemperature)
    BSTR bstrWmiNs = SysAllocString(L"ROOT\\WMI");
    bstrLocale = SysAllocString(L"");
    hr = m_wbemLocator->ConnectServer(bstrWmiNs, nullptr, nullptr, bstrLocale,
                                      0, nullptr, nullptr, &m_wbemServicesWmi);
    SysFreeString(bstrWmiNs);
    SysFreeString(bstrLocale);
    if (SUCCEEDED(hr) && m_wbemServicesWmi) {
        CoSetProxyBlanket(m_wbemServicesWmi, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    } else {
        m_wbemServicesWmi = nullptr;
    }

    // Get CPU name
    std::vector<std::map<std::wstring, std::wstring>> results;
    if (QueryWmi(L"SELECT Name FROM Win32_Processor", results) && !results.empty()) {
        m_cpuName = WStringToString(results[0][L"Name"]);
    }

    m_wmiInitialized = true;
    return true;
}

void CpuManager::CleanupWmi() {
    if (m_wbemServicesWmi) {
        m_wbemServicesWmi->Release();
        m_wbemServicesWmi = nullptr;
    }
    if (m_wbemServices) {
        m_wbemServices->Release();
        m_wbemServices = nullptr;
    }
    if (m_wbemLocator) {
        m_wbemLocator->Release();
        m_wbemLocator = nullptr;
    }
    m_wmiInitialized = false;
}

bool CpuManager::QueryWmiNamespace(IWbemServices* pServices, const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results) {
    std::lock_guard<std::mutex> lock(m_wmiMutex);
    if (!pServices) return false;

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR bstrWQL = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(wqlQuery.c_str());
    HRESULT hr = pServices->ExecQuery(bstrWQL, bstrQuery,
                                      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                      nullptr, &enumerator);
    SysFreeString(bstrWQL);
    SysFreeString(bstrQuery);
    if (FAILED(hr) || !enumerator) {
        return false;
    }

    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;

    while (enumerator) {
        hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
        if (FAILED(hr) || returned == 0 || !obj) break;

        VARIANT vtProp;
        SAFEARRAY* saNames = nullptr;
        hr = obj->GetNames(nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY, nullptr, &saNames);
        if (SUCCEEDED(hr) && saNames) {
            long lBound = 0, uBound = 0;
            SafeArrayGetLBound(saNames, 1, &lBound);
            SafeArrayGetUBound(saNames, 1, &uBound);
            std::map<std::wstring, std::wstring> row;
            for (long i = lBound; i <= uBound; i++) {
                BSTR name = nullptr;
                SafeArrayGetElement(saNames, &i, &name);
                if (name) {
                    VariantInit(&vtProp);
                    hr = obj->Get(name, 0, &vtProp, nullptr, nullptr);
                    if (SUCCEEDED(hr) && vtProp.vt != VT_NULL && vtProp.vt != VT_EMPTY) {
                        std::wstring value;
                        if (vtProp.vt == VT_BSTR && vtProp.bstrVal) {
                            value = vtProp.bstrVal;
                        } else if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
                            value = std::to_wstring(vtProp.ulVal);
                        } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
                            value = std::to_wstring(vtProp.ullVal);
                        } else if (vtProp.vt == VT_R4 || vtProp.vt == VT_R8) {
                            value = std::to_wstring(vtProp.dblVal);
                        } else if (vtProp.vt == VT_BOOL) {
                            value = vtProp.boolVal ? L"true" : L"false";
                        }
                        row[name] = value;
                    }
                    VariantClear(&vtProp);
                    SysFreeString(name);
                }
            }
            if (!row.empty()) {
                results.push_back(std::move(row));
            }
            SafeArrayDestroy(saNames);
        }
        obj->Release();
        obj = nullptr;
    }

    if (enumerator) enumerator->Release();
    return !results.empty();
}

bool CpuManager::QueryWmi(const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results) {
    return QueryWmiNamespace(m_wbemServices, wqlQuery, results);
}

bool CpuManager::QueryRootWmi(const std::wstring& wqlQuery, std::vector<std::map<std::wstring, std::wstring>>& results) {
    return QueryWmiNamespace(m_wbemServicesWmi, wqlQuery, results);
}

bool CpuManager::InitializePdh() {
    PDH_STATUS status = PdhOpenQuery(nullptr, 0, &m_pdhQuery);
    if (status != ERROR_SUCCESS) {
        Log(L"PdhOpenQuery failed: " + std::to_wstring(status));
        return false;
    }

    status = PdhAddEnglishCounter(m_pdhQuery, L"\\Processor(_Total)\\% Processor Time", 0, &m_pdhCounterTotal);
    if (status != ERROR_SUCCESS) {
        Log(L"PdhAddCounter failed: " + std::to_wstring(status));
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
        return false;
    }

    // Initial sample - PdhGetFormattedCounterValue will calculate delta on subsequent calls
    PdhCollectQueryData(m_pdhQuery);

    m_pdhInitialized = true;
    return true;
}

void CpuManager::CleanupPdh() {
    if (m_pdhQuery) {
        PdhCloseQuery(m_pdhQuery);
        m_pdhQuery = nullptr;
    }
    m_pdhInitialized = false;
}

double CpuManager::GetCpuUsage() const {
    if (!m_pdhInitialized) return 0.0;

    PDH_FMT_COUNTERVALUE value{};
    PDH_STATUS status = PdhCollectQueryData(m_pdhQuery);
    if (status != ERROR_SUCCESS) return 0.0;

    status = PdhGetFormattedCounterValue(m_pdhCounterTotal, PDH_FMT_DOUBLE, nullptr, &value);
    if (status != ERROR_SUCCESS) return 0.0;

    return value.doubleValue;
}

bool CpuManager::UpdateMetrics(SystemMetrics& metrics) {
    // CPU Usage via PDH
    metrics.cpuUsagePercent = GetCpuUsage();

    // Fast, zero-overhead CPU Frequency query via CallNtPowerInformation (powrprof)
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    DWORD numProcs = sysInfo.dwNumberOfProcessors;
    if (numProcs > 0) {
        std::vector<WinProcessorPowerInfo> ppi(numProcs);
        ULONG bufSize = static_cast<ULONG>(sizeof(WinProcessorPowerInfo) * numProcs);
        if (CallNtPowerInformation(static_cast<POWER_INFORMATION_LEVEL>(11), nullptr, 0, ppi.data(), bufSize) == 0) {
            ULONG maxMhz = 0;
            for (const auto& info : ppi) {
                if (info.CurrentMhz > maxMhz) {
                    maxMhz = info.CurrentMhz;
                }
            }
            if (maxMhz > 0) {
                metrics.cpuClockGhz = maxMhz / 1000.0;
            }
        }
    }

    // Temperature and other metrics via WMI
    if (m_wmiInitialized) {
        std::vector<std::map<std::wstring, std::wstring>> results;

        // 1. CPU Temperature via ROOT\WMI: MSAcpi_ThermalZoneTemperature
        if (QueryRootWmi(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature", results)) {
            for (const auto& row : results) {
                auto it = row.find(L"CurrentTemperature");
                if (it != row.end()) {
                    // WMI returns temperature in tenths of Kelvin
                    double tempK = _wtof(it->second.c_str()) / 10.0;
                    double tempC = tempK - 273.15;
                    if (tempC > 0.0 && tempC < 130.0) {
                        metrics.cpuTemperatureC = tempC;
                        break;
                    }
                }
            }
        }

        // 2. Fallback: Try Win32_TemperatureProbe in ROOT\CIMV2
        if (metrics.cpuTemperatureC == 0.0) {
            results.clear();
            if (QueryWmi(L"SELECT CurrentReading FROM Win32_TemperatureProbe", results)) {
                for (const auto& row : results) {
                    auto it = row.find(L"CurrentReading");
                    if (it != row.end()) {
                        double tempC = _wtof(it->second.c_str()) / 10.0;
                        if (tempC > 0.0 && tempC < 130.0) {
                            metrics.cpuTemperatureC = tempC;
                            break;
                        }
                    }
                }
            }
        }

        // 3. Fallback for processor frequency if CallNtPowerInformation returned 0
        if (metrics.cpuClockGhz == 0.0) {
            results.clear();
            if (QueryWmi(L"SELECT CurrentClockSpeed FROM Win32_Processor", results)) {
                for (const auto& row : results) {
                    auto itCur = row.find(L"CurrentClockSpeed");
                    if (itCur != row.end()) {
                        int currentMhz = _wtoi(itCur->second.c_str());
                        metrics.cpuClockGhz = currentMhz / 1000.0;
                        break;
                    }
                }
            }
        }
    }

    // Package Power - estimate based on PL1 limit and usage if not available via MSR
    if (metrics.cpuPl1LimitW > 0) {
        metrics.cpuPackagePowerW = metrics.cpuPl1LimitW * (metrics.cpuUsagePercent / 100.0);
    } else {
        metrics.cpuPackagePowerW = 45.0 * (metrics.cpuUsagePercent / 100.0);
    }

    return true;
}

bool CpuManager::SetCpuPowerLimitWatts(int pl1Watts) {
    // Note: Setting CPU PL1 via MSR (0x610) requires kernel driver (WinRing0, etc.)
    // Without a driver, we can only use Windows Power APIs which control
    // processor performance states (P-states) but not direct PL1 MSR.
    // This is a placeholder for when a driver is available.
    (void)pl1Watts;
    return false;
}

bool CpuManager::GetCpuPowerLimitWatts(int* pl1Watts) const {
    // Similarly, reading PL1 from MSR requires kernel driver
    (void)pl1Watts;
    return false;
}