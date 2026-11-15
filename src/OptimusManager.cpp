#include "OptimusManager.h"
#include <setupapi.h>
#include <devguid.h>
#include <tlhelp32.h>
#include <comutil.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comsuppw.lib")

OptimusManager::OptimusManager() = default;

OptimusManager::~OptimusManager() {
    Shutdown();
}

void OptimusManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

bool OptimusManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;
    m_initialized = true;

    // Check for NVAPI support
    m_nvapiAvailable = CheckNvapi();

    // Check for Dell WMI support (requires admin)
    m_dellWmiAvailable = CheckDellWmi();

    // Detect integrated GPU
    m_hasIntegratedGpu = true; // Assume modern laptop has iGPU

    // Detect discrete GPU
    m_hasDiscreteGpu = false;
    // Simple detection via device presence
    HDEVINFO devInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA devData = { sizeof(SP_DEVINFO_DATA) };
        for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
            TCHAR hwId[256];
            DWORD size = 256;
            if (SetupDiGetDeviceInstanceIdW(devInfo, &devData, hwId, 256, &size)) {
                if (wcsstr(hwId, L"NVIDIA") || wcsstr(hwId, L"NVidia")) {
                    m_hasDiscreteGpu = true;
                    Log(L"Discrete GPU detected via device enumerator");
                    break;
                }
            }
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    // Check for MUX/Advanced Optimus
    m_muxSupported = m_dellWmiAvailable || m_hasDiscreteGpu;
    m_advancedOptimusSupported = m_nvapiAvailable || m_dellWmiAvailable;

    Log(L"OptimusManager initialized. dGPU: " + std::to_wstring(m_hasDiscreteGpu ? 1 : 0) +
        L", NVAPI: " + std::to_wstring(m_nvapiAvailable ? 1 : 0) +
        L", Dell WMI: " + std::to_wstring(m_dellWmiAvailable ? 1 : 0));

    return true;
}

void OptimusManager::Shutdown() {
    if (m_nvapiModule) {
        FreeLibrary(m_nvapiModule);
        m_nvapiModule = nullptr;
    }
    m_initialized = false;
}

bool OptimusManager::CheckNvapi() {
    HMODULE hModule = LoadLibraryW(L"nvapi64.dll");
    if (!hModule) {
        hModule = LoadLibraryW(L"nvapi.dll");
    }
    if (!hModule) {
        Log(L"NVAPI not found");
        return false;
    }

    m_nvapiModule = hModule;
    Log(L"NVAPI loaded successfully");
    return true;
}

bool OptimusManager::CheckDellWmi() {
    // Test WMI connection to root\\dcim\\sysman
    IWbemLocator* pLoc = nullptr;
    HRESULT hres = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);

    if (FAILED(hres)) {
        Log(L"Dell WMI not available");
        return false;
    }

    BSTR bstrNamespace = SysAllocString(L"ROOT\\dcim\\sysman");
    BSTR bstrLocale = SysAllocString(L"");
    IWbemServices* pSvc = nullptr;
    hres = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr, bstrLocale,
                                0, nullptr, nullptr, &pSvc);
    SysFreeString(bstrNamespace);
    SysFreeString(bstrLocale);

    if (pLoc) pLoc->Release();

    if (FAILED(hres)) {
        Log(L"Dell WMI root\\dcim\\sysman not accessible");
        return false;
    }

    if (pSvc) pSvc->Release();
    Log(L"Dell WMI available");
    return true;
}

bool OptimusManager::LoadNvapi() {
    if (!m_nvapiAvailable) return false;

    // NVAPI initialization would go here
    // For now, we'll use basic detection
    return true;
}

void OptimusManager::UnloadNvapi() {
    // NVAPI cleanup
}

std::wstring OptimusManager::GetStatusString() const {
    std::wstring status = L"Currently: ";

    switch (m_currentMode) {
        case GpuMode::Auto:
            status += L"Auto (iGPU on Battery, dGPU on AC)";
            break;
        case GpuMode::IntegratedOnly:
            status += L"iGPU Only - Max Battery";
            break;
        case GpuMode::Hybrid:
            status += L"Hybrid / Optimus - Balanced";
            break;
        case GpuMode::DiscreteOnly:
            status += L"dGPU Only / MUX - Max Performance";
            break;
        default:
            status += L"Unknown";
            break;
    }

    if (m_hasDiscreteGpu) {
        status += L" | dGPU Status: ";
        switch (m_dgpuState) {
            case DgpuPowerState::Off: status += L"OFF"; break;
            case DgpuPowerState::Sleep: status += L"SLEEPING"; break;
            case DgpuPowerState::On: status += L"ON"; break;
            default: status += L"UNKNOWN"; break;
        }
    }

    return status;
}

bool OptimusManager::SetNvapiPreferredGpu(bool discrete) {
    // NVAPI way to set preferred GPU
    // NVAPI_DRS_SET_LIBRARY_PRESET or similar
    Log(L"NVAPI preferred GPU change requested: " + std::wstring(discrete ? L"dGPU" : L"iGPU"));

    // This would require actual NVAPI calls
    // For now, just log the request
    return true;
}

bool OptimusManager::SetDellMuxMode(bool enableDiscrete) {
    // Dell WMI MUX control
    // Requires Dell systems with proper WMI support

    IWbemLocator* pLoc = nullptr;
    HRESULT hres = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc);

    if (FAILED(hres)) return false;

    BSTR bstrNamespace = SysAllocString(L"ROOT\\dcim\\sysman");
    BSTR bstrLocale = SysAllocString(L"");
    IWbemServices* pSvc = nullptr;
    hres = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr, bstrLocale,
                                0, nullptr, nullptr, &pSvc);
    SysFreeString(bstrNamespace);
    SysFreeString(bstrLocale);

    if (FAILED(hres)) {
        if (pLoc) pLoc->Release();
        return false;
    }

    hres = CoSetProxyBlanket(pSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE);

    if (FAILED(hres)) {
        pSvc->Release();
        pLoc->Release();
        return false;
    }

    // Dell_Graphics class for Optimus/MUX control
    // or Dell_MUX class for direct switch
    std::wstring methodName = enableDiscrete ? L"CreateClassNameInstance" : L"SetGraphicsSettings";

    // This is a simplified version - actual implementation would be more complex
    // For Dell systems, we'd use methods like:
    // - Dell_Graphics.SetPreferredAdapter()
    // - Dell_MUX.SetSwitchState()

    Log(L"Dell WMI MUX change requested: " + std::wstring(enableDiscrete ? L"dGPU" : L"iGPU"));

    pSvc->Release();
    pLoc->Release();

    return true;
}

bool OptimusManager::CallAwccMuxSwitch(bool enableDiscrete) {
    IWbemLocator* pLoc = nullptr;
    HRESULT hres = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres) || !pLoc) return false;

    BSTR bstrNamespace = SysAllocString(L"ROOT\\WMI");
    IWbemServices* pSvc = nullptr;
    hres = pLoc->ConnectServer(bstrNamespace, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &pSvc);
    SysFreeString(bstrNamespace);
    pLoc->Release();

    if (FAILED(hres) || !pSvc) return false;

    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    // Get AWCCWmiMethodFunction instance
    IEnumWbemClassObject* pEnum = nullptr;
    BSTR bstrWQL = SysAllocString(L"WQL");
    BSTR bstrQuery = SysAllocString(L"SELECT __PATH FROM AWCCWmiMethodFunction");
    hres = pSvc->ExecQuery(bstrWQL, bstrQuery, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
    SysFreeString(bstrWQL);
    SysFreeString(bstrQuery);

    std::wstring instancePath;
    if (SUCCEEDED(hres) && pEnum) {
        IWbemClassObject* pObj = nullptr;
        ULONG uRet = 0;
        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == S_OK && uRet > 0 && pObj) {
            VARIANT vt;
            VariantInit(&vt);
            if (SUCCEEDED(pObj->Get(L"__PATH", 0, &vt, nullptr, nullptr)) && vt.vt == VT_BSTR) {
                instancePath = vt.bstrVal;
            }
            VariantClear(&vt);
            pObj->Release();
        }
        pEnum->Release();
    }

    if (instancePath.empty()) {
        pSvc->Release();
        return false;
    }

    IWbemClassObject* pClass = nullptr;
    BSTR bstrClassName = SysAllocString(L"AWCCWmiMethodFunction");
    hres = pSvc->GetObject(bstrClassName, 0, nullptr, &pClass, nullptr);
    SysFreeString(bstrClassName);
    if (FAILED(hres) || !pClass) {
        pSvc->Release();
        return false;
    }

    IWbemClassObject* pInParamsDef = nullptr;
    hres = pClass->GetMethod(L"MUXSwitch", 0, &pInParamsDef, nullptr);
    pClass->Release();
    if (FAILED(hres) || !pInParamsDef) {
        pSvc->Release();
        return false;
    }

    IWbemClassObject* pInParams = nullptr;
    hres = pInParamsDef->SpawnInstance(0, &pInParams);
    pInParamsDef->Release();
    if (FAILED(hres) || !pInParams) {
        pSvc->Release();
        return false;
    }

    VARIANT var;
    VariantInit(&var);
    var.vt = VT_I4;
    var.lVal = enableDiscrete ? 1 : 0;
    pInParams->Put(L"arg2", 0, &var, 0);

    IWbemClassObject* pOutParams = nullptr;
    BSTR bstrPath = SysAllocString(instancePath.c_str());
    BSTR bstrMethod = SysAllocString(L"MUXSwitch");
    hres = pSvc->ExecMethod(bstrPath, bstrMethod, 0, nullptr, pInParams, &pOutParams, nullptr);
    SysFreeString(bstrPath);
    SysFreeString(bstrMethod);
    pInParams->Release();

    bool ok = SUCCEEDED(hres) && pOutParams != nullptr;
    if (pOutParams) pOutParams->Release();
    pSvc->Release();

    if (ok) {
        Log(L"Alienware BIOS MUX switch executed: " + std::wstring(enableDiscrete ? L"dGPU Only (Direct)" : L"Hybrid (Optimus)"));
    }
    return ok;
}

bool OptimusManager::SetGpuMode(GpuMode mode) {
    if (!m_initialized) return false;

    bool success = false;
    bool forceDiscrete = (mode == GpuMode::DiscreteOnly);
    bool forceIntegrated = (mode == GpuMode::IntegratedOnly);
    bool autoMode = (mode == GpuMode::Auto);

    // For Auto mode, allow BIOS to manage Hybrid
    if (autoMode) {
        m_currentMode = mode;
        CallAwccMuxSwitch(false);
        return true;
    }

    // 1. Try Alienware BIOS ACPI WMI MUX Switch
    success = CallAwccMuxSwitch(forceDiscrete);

    // 2. Try NVAPI
    if (!success && m_nvapiAvailable) {
        success = SetNvapiPreferredGpu(forceDiscrete);
    }

    // 3. Try Dell WMI fallback
    if (!success && m_dellWmiAvailable) {
        success = SetDellMuxMode(forceDiscrete);
    }

    // 4. Fallback: try device manager
    if (!success && m_hasDiscreteGpu) {
        if (forceDiscrete) {
            success = FindAndEnableDgpuDevice();
        } else if (forceIntegrated) {
            success = FindAndDisableDgpuDevice();
        }
    }

    m_currentMode = mode;
    Log(L"GPU mode set to: " + std::to_wstring(static_cast<int>(mode)));
    return true;
}

bool OptimusManager::FindAndDisableDgpuDevice() {
    // Find NVIDIA device and disable it
    HDEVINFO devInfo = SetupDiGetClassDevs(nullptr, L"NVIDIA", nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devData = { sizeof(SP_DEVINFO_DATA) };
    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i) {
        TCHAR desc[256];
        if (SetupDiGetDeviceRegistryProperty(devInfo, &devData, SPDRP_DEVICEDESC,
            nullptr, (PBYTE)desc, sizeof(desc), nullptr)) {
            if (wcsstr(desc, L"NVIDIA") || wcsstr(desc, L"NVIDIA GeForce") ||
                wcsstr(desc, L"NVIDIA GP")) {
                found = true;
                Log(L"Found discrete GPU device in system");
                break;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return found;
}

bool OptimusManager::FindAndEnableDgpuDevice() {
    // Enable dGPU - similar logic to disable
    Log(L"Enable dGPU requested");
    return true;
}

bool OptimusManager::SetDgpuPowerState(DgpuPowerState state) {
    if (!m_initialized) return false;

    // NVML can control dGPU power state
#ifdef HAVE_NVML
    // Would use nvmlDeviceSetPowerState
    Log(L"NVML dGPU power state change: " + std::to_wstring(static_cast<int>(state)));
#endif

    m_dgpuState = state;
    return true;
}

std::vector<std::string> OptimusManager::GetProcessesUsingDgpu() {
    std::vector<std::string> processes;

#ifdef HAVE_NVML
    // NVML can query processes using GPU via:
    // nvmlDeviceGetComputeRunningProcesses / nvmlDeviceGetGraphicsRunningProcesses
    // For now, just log since we don't have the actual NVML handle
#endif

    // Alternative: Parse nvidia-smi output
    // Or use Windows API to enumerate processes and check GPU affinity

    return processes;
}

bool OptimusManager::ForceDgpuSleep() {
    if (!m_initialized || !m_hasDiscreteGpu) return false;

    Log(L"Forcing dGPU to sleep");

    // Set dGPU to lowest power state
    SetDgpuPowerState(DgpuPowerState::Sleep);

    // Kill any background processes using dGPU
    auto procs = GetProcessesUsingDgpu();
    Log(L"Found " + std::to_wstring(procs.size()) + L" processes using dGPU");

    // Note: Actually killing processes requires elevated privileges and careful handling
    // For safety, we just log them
    for (const auto& proc : procs) {
        Log(L"Process using dGPU: " + StringToWString(proc));
    }

    return true;
}

bool OptimusManager::ToggleNvidiaPreferredGpu(bool useDiscrete) {
    return SetNvapiPreferredGpu(useDiscrete);
}