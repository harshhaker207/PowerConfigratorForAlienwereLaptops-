#include "LedManager.h"
#include "WindowsPowerManager.h"
#include "AudioVisualizer.h"
#include "ScreenAmbient.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <algorithm>
#include <comutil.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comsuppw.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

template<typename T>
static inline T Clamp(T value, T min, T max) {
    return value < min ? min : (value > max ? max : value);
}

LedManager::LedManager() {
    m_audioViz = std::make_unique<AudioVisualizer>();
    m_screenAmbient = std::make_unique<ScreenAmbient>();

    m_zoneNames = { L"All Zones", L"RGB Keyboard", L"Touchpad (Trackpad)", L"Alien Logo (Rear Lid)", L"Power Button" };

    m_ledZones[L"Keyboard"].name = L"RGB Keyboard";
    m_ledZones[L"Trackpad"].name = L"Touchpad (Trackpad)";
    m_ledZones[L"Logo"].name = L"Alien Logo (Rear Lid)";
    m_ledZones[L"PowerButton"].name = L"Power Button";
}

LedManager::~LedManager() {
    Shutdown();
}

void LedManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
    FILE* f = nullptr;
    if (_wfopen_s(&f, L"C:\\Users\\harsh\\PowerConfigurator\\debug.log", L"a, ccs=UTF-8") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%02d:%02d:%02d.%03d] [LED] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg.c_str());
        fclose(f);
    }
}

bool LedManager::EnsureDevicesOpen() {
    if (m_hV4Device != INVALID_HANDLE_VALUE && m_hV5Device != INVALID_HANDLE_VALUE) {
        return true;
    }

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA ifData{};
        ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &ifData); i++) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, nullptr, 0, &reqSize, nullptr);
            if (reqSize == 0) continue;

            std::vector<BYTE> detailBuf(reqSize);
            SP_DEVICE_INTERFACE_DETAIL_DATA_W* pDetail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.data());
            pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, pDetail, reqSize, nullptr, nullptr)) {
                std::wstring path = pDetail->DevicePath;
                std::wstring lower = path;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

                // AlienFX V4 Chassis/Logo controller (VID 0x187C, OutputReportByteLength == 34)
                if (m_hV4Device == INVALID_HANDLE_VALUE && lower.find(L"187c") != std::wstring::npos) {
                    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (h != INVALID_HANDLE_VALUE) {
                        PHIDP_PREPARSED_DATA pData = nullptr;
                        if (HidD_GetPreparsedData(h, &pData)) {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(pData, &caps)) {
                                if (caps.OutputReportByteLength == 34) {
                                    m_hV4Device = h;
                                    m_v4Path = path;
                                    m_isAlienware = true;
                                    Log(L"AlienFX V4 controller opened: " + path);
                                }
                            }
                            HidD_FreePreparsedData(pData);
                        }
                        if (m_hV4Device != h) {
                            CloseHandle(h);
                        }
                    }
                }

                // Alienware Keyboard V5 controller (VID 0x0D62, FeatureReportByteLength == 64)
                if (m_hV5Device == INVALID_HANDLE_VALUE && lower.find(L"0d62") != std::wstring::npos) {
                    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (h != INVALID_HANDLE_VALUE) {
                        PHIDP_PREPARSED_DATA pData = nullptr;
                        if (HidD_GetPreparsedData(h, &pData)) {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(pData, &caps)) {
                                if (caps.FeatureReportByteLength == 64) {
                                    m_hV5Device = h;
                                    m_v5Path = path;
                                    m_isAlienware = true;
                                    Log(L"Alienware Keyboard V5 controller opened: " + path);
                                }
                            }
                            HidD_FreePreparsedData(pData);
                        }
                        if (m_hV5Device != h) {
                            CloseHandle(h);
                        }
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }

    return (m_hV4Device != INVALID_HANDLE_VALUE || m_hV5Device != INVALID_HANDLE_VALUE);
}

void LedManager::CloseDevices() {
    if (m_hV4Device != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hV4Device);
        m_hV4Device = INVALID_HANDLE_VALUE;
    }
    if (m_hV5Device != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hV5Device);
        m_hV5Device = INVALID_HANDLE_VALUE;
    }
}

bool LedManager::Initialize(LogCallback logCallback, WindowsPowerManager* winPowerMgr) {
    m_logCallback = logCallback;
    m_winPowerMgr = winPowerMgr;
    m_initialized = true;

    // Detect system type & open HID devices
    EnsureDevicesOpen();
    LoadLightFxDll();
    m_isAlienware = (m_hV4Device != INVALID_HANDLE_VALUE || m_hV5Device != INVALID_HANDLE_VALUE) || DetectAlienwareSystem();
    m_isDell = DetectDellSystem();

    Log(L"LedManager initialized. Alienware: " + std::to_wstring(m_isAlienware ? 1 : 0) +
        L" (V4: " + std::to_wstring(m_hV4Device != INVALID_HANDLE_VALUE ? 1 : 0) +
        L", V5: " + std::to_wstring(m_hV5Device != INVALID_HANDLE_VALUE ? 1 : 0) +
        L"), Dell: " + std::to_wstring(m_isDell ? 1 : 0));

    // Load saved profile if exists
    m_savedProfile.color = LedColor(0, 180, 255); // Default Alienware Cyan
    m_savedProfile.brightness = 100;
    m_savedProfile.effect = LedEffect::Static;

    RestoreSavedProfile();

    return true;
}

void LedManager::Shutdown() {
    StopDynamicEffects();
    TurnOffAllLeds();
    CloseDevices();

    if (m_lightFxModule) {
        FreeLibrary(m_lightFxModule);
        m_lightFxModule = nullptr;
    }

    m_initialized = false;
}

bool LedManager::DetectAlienwareSystem() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Alienware", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        Log(L"Alienware registry key found");
        return true;
    }

    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Dell\\Alienware", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        Log(L"Dell Alienware registry key found");
        return true;
    }

    SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hService = OpenServiceW(hSCM, L"AlienwareCommandCenterService", SERVICE_QUERY_STATUS);
        if (hService) {
            SERVICE_STATUS status;
            if (QueryServiceStatus(hService, &status)) {
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCM);
                Log(L"Alienware Command Center service found");
                return true;
            }
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }

    return false;
}

bool LedManager::DetectDellSystem() {
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
        pLoc->Release();
        return false;
    }

    pSvc->Release();
    pLoc->Release();
    Log(L"Dell WMI available - system detected");
    return true;
}

bool LedManager::LoadLightFxDll() {
    if (m_lightFxInitialized) return true;

    if (!m_lightFxModule) {
        // 1. Current working directory or application binary directory
        m_lightFxModule = LoadLibraryW(L"LightFX.dll");

        // 2. AlienFX Tools x64 installation
        if (!m_lightFxModule) {
            m_lightFxModule = LoadLibraryW(L"C:\\Users\\harsh\\AppData\\Local\\AlienFX Tools\\LightFX\\x64\\LightFX.dll");
        }

        // 3. Official Alienware Command Center
        if (!m_lightFxModule) {
            m_lightFxModule = LoadLibraryW(L"C:\\Program Files\\Dell\\Alienware Command Center\\LightFX.dll");
        }
    }

    if (m_lightFxModule) {
        m_fnLfxInit = (PFN_LFX_INITIALIZE)GetProcAddress(m_lightFxModule, "LFX_Initialize");
        m_fnLfxRelease = (PFN_LFX_RELEASE)GetProcAddress(m_lightFxModule, "LFX_Release");
        m_fnLfxReset = (PFN_LFX_RESET)GetProcAddress(m_lightFxModule, "LFX_Reset");
        m_fnLfxUpdate = (PFN_LFX_UPDATE)GetProcAddress(m_lightFxModule, "LFX_Update");
        m_fnLfxLight = (PFN_LFX_LIGHT)GetProcAddress(m_lightFxModule, "LFX_Light");
        m_fnLfxSetLightColor = (PFN_LFX_SETLIGHTCOLOR)GetProcAddress(m_lightFxModule, "LFX_SetLightColor");

        if (m_fnLfxInit && m_fnLfxUpdate && m_fnLfxLight) {
            int r = m_fnLfxInit();
            m_lightFxInitialized = (r == 0);
            Log(L"LightFX DLL loaded and initialized (r=" + std::to_wstring(r) + L")");
            return m_lightFxInitialized;
        }
    }

    Log(L"LightFX DLL not available");
    return false;
}

void LedManager::UnloadLightFxDll() {
    if (m_lightFxInitialized && m_fnLfxRelease) {
        m_fnLfxRelease();
        m_lightFxInitialized = false;
    }
    if (m_lightFxModule) {
        FreeLibrary(m_lightFxModule);
        m_lightFxModule = nullptr;
    }
}

// Low-level V4 (Chassis, Trackpad, Logo, Power button) zone control
bool LedManager::SetV4Zone(BYTE lightId, LedEffect effect, const LedColor& color, int brightness, int speed) {
    if (m_hV4Device == INVALID_HANDLE_VALUE) {
        EnsureDevicesOpen();
        if (m_hV4Device == INVALID_HANDLE_VALUE) return false;
    }

    // 1. Reset sequence
    BYTE r1[34] = { 0 };
    r1[0] = 0x00; r1[1] = 0x03; r1[2] = 0x21; r1[3] = 0x00; r1[4] = 0x04; r1[5] = 0xff; r1[6] = 0xff;
    HidD_SetOutputReport(m_hV4Device, r1, sizeof(r1));
    Sleep(8);

    BYTE r2[34] = { 0 };
    r2[0] = 0x00; r2[1] = 0x03; r2[2] = 0x21; r2[3] = 0x00; r2[4] = 0x01; r2[5] = 0xff; r2[6] = 0xff;
    HidD_SetOutputReport(m_hV4Device, r2, sizeof(r2));
    Sleep(8);

    // 2. Dimming (0 = full bright, 100 = dark)
    BYTE dimVal = static_cast<BYTE>(Clamp(100 - brightness, 0, 100));
    BYTE dim[34] = { 0 };
    dim[0] = 0x00; dim[1] = 0x03; dim[2] = 0x26; dim[3] = dimVal; dim[4] = 0x00; dim[5] = 1; dim[6] = lightId;
    HidD_SetOutputReport(m_hV4Device, dim, sizeof(dim));
    Sleep(8);

    // 3. Configure light action
    BYTE sel[34] = { 0 };
    sel[0] = 0x00; sel[1] = 0x03; sel[2] = 0x23; sel[3] = 0x01; sel[4] = 0x00; sel[5] = 0x01; sel[6] = lightId;
    HidD_SetOutputReport(m_hV4Device, sel, sizeof(sel));
    Sleep(8);

    BYTE col[34] = { 0 };
    col[0] = 0x00; col[1] = 0x03; col[2] = 0x24;

    if (effect == LedEffect::Off || brightness == 0) {
        // Off: mode 0xd0, RGB(0,0,0)
        col[3] = 0x00; col[4] = 0x07; col[5] = 0xd0; col[6] = 0x00; col[7] = 0xfa;
        col[8] = 0; col[9] = 0; col[10] = 0;
    } else if (effect == LedEffect::Static) {
        // Static: mode 0xd0
        col[3] = 0x00; col[4] = 0x07; col[5] = 0xd0; col[6] = 0x00; col[7] = 0xfa;
        col[8] = color.red; col[9] = color.green; col[10] = color.blue;
    } else if (effect == LedEffect::Breathing || effect == LedEffect::Pulse) {
        // Breathing / Pulse: mode 0xdc (Higher tempo byte = faster pulse rate)
        BYTE tempoVal = static_cast<BYTE>(Clamp(30 + speed * 14, 0x20, 0xD0));
        col[3] = 0x01; col[4] = 0x07; col[5] = 0xdc; col[6] = 0x00; col[7] = tempoVal;
        col[8] = color.red; col[9] = color.green; col[10] = color.blue;
    } else if (effect == LedEffect::Rainbow || effect == LedEffect::Wave) {
        // Rainbow: mode 0xac (Higher tempo byte = faster cycle rate)
        BYTE tempoVal = static_cast<BYTE>(Clamp(30 + speed * 12, 0x20, 0xC0));
        col[3] = 0x02; col[4] = 0x07; col[5] = 0xac; col[6] = 0x00; col[7] = tempoVal;
        col[8] = color.red; col[9] = color.green; col[10] = color.blue;
    } else if (effect == LedEffect::Morph) {
        // Spectrum / Morph: mode 0x82 (Higher tempo byte = faster transition)
        BYTE tempoVal = static_cast<BYTE>(Clamp(30 + speed * 12, 0x20, 0xC0));
        col[3] = 0x02; col[4] = 0x07; col[5] = 0x82; col[6] = 0x00; col[7] = tempoVal;
        col[8] = color.red; col[9] = color.green; col[10] = color.blue;
    } else if (effect == LedEffect::Scanner || effect == LedEffect::Laser) {
        // Scanner / Laser on chassis: dynamic rapid pulse
        BYTE tempoVal = static_cast<BYTE>(Clamp(40 + speed * 14, 0x30, 0xE0));
        col[3] = 0x01; col[4] = 0x07; col[5] = 0xdc; col[6] = 0x00; col[7] = tempoVal;
        col[8] = color.red; col[9] = color.green; col[10] = color.blue;
    } else if (effect == LedEffect::NeonWave) {
        // Cyberpunk dual wave (Rainbow/Wave mode)
        BYTE tempoVal = static_cast<BYTE>(Clamp(30 + speed * 12, 0x20, 0xC0));
        col[3] = 0x02; col[4] = 0x07; col[5] = 0xac; col[6] = 0x00; col[7] = tempoVal;
        col[8] = 0; col[9] = 180; col[10] = 255; // Cyan
    } else if (effect == LedEffect::Stealth) {
        // Stealth dim amber
        col[3] = 0x00; col[4] = 0x07; col[5] = 0xd0; col[6] = 0x00; col[7] = 0xfa;
        col[8] = 180; col[9] = 20; col[10] = 0;
    }
    HidD_SetOutputReport(m_hV4Device, col, sizeof(col));
    Sleep(8);

    // 4. Power button special zone handling if lightId == 4
    if (lightId == 4) {
        BYTE pwr1[34] = { 0 };
        pwr1[0] = 0x00; pwr1[1] = 0x03; pwr1[2] = 0x21; pwr1[3] = 0x00; pwr1[4] = 0x04; pwr1[5] = 0x00; pwr1[6] = 0x61;
        HidD_SetOutputReport(m_hV4Device, pwr1, sizeof(pwr1));
        Sleep(8);
        BYTE pwr2[34] = { 0 };
        pwr2[0] = 0x00; pwr2[1] = 0x03; pwr2[2] = 0x21; pwr2[3] = 0x00; pwr2[4] = 0x01; pwr2[5] = 0x00; pwr2[6] = 0x61;
        HidD_SetOutputReport(m_hV4Device, pwr2, sizeof(pwr2));
        Sleep(8);
    }

    // 5. Commit / Play
    BYTE upd[34] = { 0 };
    upd[0] = 0x00; upd[1] = 0x03; upd[2] = 0x21; upd[3] = 0x00; upd[4] = 0x03; upd[5] = 0xff; upd[6] = 0xff;
    return HidD_SetOutputReport(m_hV4Device, upd, sizeof(upd)) != FALSE;
}

// Enumerates all physical per-key RGB light IDs for Alienware V5 keyboard
std::vector<BYTE> LedManager::GetV5KeyIds() {
    std::vector<BYTE> keys;

    // 1. Scan AlienFX SDK registry if available: HKCU\Software\Alienfx_SDK
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Alienfx_SDK", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD subkeys = 0, maxSubkeyLen = 0;
        if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subkeys, &maxSubkeyLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            std::vector<wchar_t> nameBuf(maxSubkeyLen + 2);
            for (DWORD i = 0; i < subkeys; ++i) {
                DWORD len = static_cast<DWORD>(nameBuf.size());
                if (RegEnumKeyExW(hKey, i, nameBuf.data(), &len, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                    std::wstring name(nameBuf.data(), len);
                    // Match Light<devID>-<keyID> (e.g. Light224580273-0, Light224580273-100)
                    size_t dash = name.find(L'-');
                    if (dash != std::wstring::npos && name.find(L"Light") == 0) {
                        try {
                            int k = std::stoi(name.substr(dash + 1));
                            if (k >= 0 && k <= 255) {
                                keys.push_back(static_cast<BYTE>(k));
                            }
                        } catch (...) {}
                    }
                }
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Exact 85 physical key ID matrix for Alienware m16 R2 (US Layout)
    // Matches devices.csv: Device #1 (VID 0x0D62/PID 0xD2B1 APIv5, 85 lights)
    static const BYTE defaultKeys[] = {
        // Function row & media
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
        // Number row
        20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34,
        // QWERTY row + brackets + Backslash (ID 55 = \)
        40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 55,
        // ASDF row + quotes + Enter (ID 74 = Enter)
        60, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 74,
        // ZXCV row + comma, period, Slash (ID 92 = /), Right Shift (ID 94 = Right Shift), Left Shift (ID 80 = Shift)
        80, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 94,
        // Bottom row: Ctrl (ID 100 = Ctrl), Fn (101), Win (102), Alt (104), Space (106),
        // Win lock (109), Right Alt (111), Right Ctrl (ID 112 = Right Ctrl), Up (ID 114 = Up)
        100, 101, 102, 104, 106, 109, 111, 112, 114,
        // Arrow cluster: Left (133), Down (134), Right (ID 135 = Right)
        133, 134, 135
    };

    for (BYTE k : defaultKeys) {
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) {
            keys.push_back(k);
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Low-level V5 (RGB Keyboard) effect control
bool LedManager::SetV5Effect(LedEffect effect, const LedColor& color, const LedColor& secColor, int brightness, int speed) {
    if (m_hV5Device == INVALID_HANDLE_VALUE) {
        EnsureDevicesOpen();
        if (m_hV5Device == INVALID_HANDLE_VALUE) return false;
    }

    // 1. Reset
    BYTE r1[64] = { 0 };
    r1[0] = 0xcc; r1[1] = 0x94;
    HidD_SetFeature(m_hV5Device, r1, sizeof(r1));
    Sleep(8);

    if (effect == LedEffect::Off || brightness == 0) {
        // Brightness 0
        BYTE br[64] = { 0 };
        br[0] = 0xcc; br[1] = 0x83; br[2] = 0x38; br[3] = 0x9c; br[4] = 0x00;
        HidD_SetFeature(m_hV5Device, br, sizeof(br));
        Sleep(8);

        // Effect off
        BYTE eff[64] = { 0 };
        eff[0] = 0xcc; eff[1] = 0x80; eff[2] = 0x01; eff[3] = 0xfe;
        eff[6] = 0x01; eff[7] = 0x01; eff[8] = 0x01;
        HidD_SetFeature(m_hV5Device, eff, sizeof(eff));
        Sleep(8);
    } else {
        // Brightness
        BYTE br[64] = { 0 };
        br[0] = 0xcc; br[1] = 0x83; br[2] = 0x38; br[3] = 0x9c;
        br[4] = static_cast<BYTE>(Clamp(brightness, 0, 100));
        HidD_SetFeature(m_hV5Device, br, sizeof(br));
        Sleep(8);

        BYTE eff[64] = { 0 };
        eff[0] = 0xcc;
        eff[1] = 0x80;
        eff[6] = 0x01; eff[7] = 0x01; eff[8] = 0x01;

        // For Darfon RGB Keyboard, eff[3] is tick interval/delay:
        // Lower delay (1) = fastest animation; Higher delay (10) = slowest animation.
        // Invert speed so slider 1 (Slow) -> delay 10 (slowest), and slider 10 (Fast) -> delay 1 (fastest).
        BYTE tempoVal = static_cast<BYTE>(Clamp(11 - speed, 1, 10));

        if (effect == LedEffect::Static) {
            // Static color: effType 0, tempo 0
            eff[2] = 0x00; eff[3] = 0x00;
            eff[9] = 0x00;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
        } else if (effect == LedEffect::Breathing) {
            // Breathing: effType 2
            eff[2] = 0x02; eff[3] = tempoVal;
            eff[9] = 0x00;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
        } else if (effect == LedEffect::Rainbow || effect == LedEffect::Wave) {
            // Rainbow Wave: effType 3 (wave)
            eff[2] = 0x03; eff[3] = tempoVal;
            eff[9] = 0x01;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
            eff[13] = secColor.red ? secColor.red : static_cast<BYTE>(255 - color.red);
            eff[14] = secColor.green ? secColor.green : static_cast<BYTE>(255 - color.green);
            eff[15] = secColor.blue ? secColor.blue : static_cast<BYTE>(255 - color.blue);
        } else if (effect == LedEffect::Pulse) {
            // Pulse: effType 8
            eff[2] = 0x08; eff[3] = tempoVal;
            eff[9] = 0x00;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
        } else if (effect == LedEffect::Morph) {
            // Morph / dual wave: effType 4
            eff[2] = 0x04; eff[3] = tempoVal;
            eff[9] = 0x01;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
            eff[13] = secColor.red; eff[14] = secColor.green; eff[15] = secColor.blue;
        } else if (effect == LedEffect::Scanner) {
            // AWCC Night Rider / Scanner: effType 0x0A (10)
            eff[2] = 0x0A; eff[3] = tempoVal;
            eff[9] = 0x00;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
        } else if (effect == LedEffect::Laser) {
            // AWCC Laser Burst: effType 0x0B (11)
            eff[2] = 0x0B; eff[3] = tempoVal;
            eff[9] = 0x00;
            eff[10] = color.red; eff[11] = color.green; eff[12] = color.blue;
        } else if (effect == LedEffect::NeonWave) {
            // Cyberpunk Dual Neon Wave: effType 4 (Cyan -> Magenta)
            eff[2] = 0x04; eff[3] = tempoVal;
            eff[9] = 0x01;
            eff[10] = 0; eff[11] = 180; eff[12] = 255; // Alien Cyan
            eff[13] = 255; eff[14] = 0; eff[15] = 128; // Cyber Magenta
        } else if (effect == LedEffect::Stealth) {
            // Stealth night dim: effType 0
            eff[2] = 0x00; eff[3] = 0x00;
            eff[9] = 0x00;
            eff[10] = 180; eff[11] = 20; eff[12] = 0; // Dark amber
        }
        HidD_SetFeature(m_hV5Device, eff, sizeof(eff));
        Sleep(8);

        // If static or stealth, populate ALL keys across the keyboard!
        // Fix: Do not stop at Home (key 14)! Iterate through all keys in chunks of up to 14 keys per report.
        if (effect == LedEffect::Static || effect == LedEffect::Stealth) {
            std::vector<BYTE> keyList = GetV5KeyIds();
            LedColor activeColor = (effect == LedEffect::Stealth) ? LedColor(180, 20, 0) : color;
            for (size_t i = 0; i < keyList.size(); ) {
                BYTE col[64] = { 0 };
                col[0] = 0xcc; col[1] = 0x8c; col[2] = 0x02;
                BYTE count = 0;
                for (BYTE b = 4; b < 60 && i < keyList.size(); b += 4, ++i) {
                    col[b] = static_cast<BYTE>(keyList[i]);
                    col[b + 1] = activeColor.red;
                    col[b + 2] = activeColor.green;
                    col[b + 3] = activeColor.blue;
                    count++;
                }
                col[3] = count;
                HidD_SetFeature(m_hV5Device, col, sizeof(col));
                Sleep(12);
            }
            // Send COMMV5_loop to tell controller sequence is complete
            BYTE loopPkt[64] = { 0 };
            loopPkt[0] = 0xcc; loopPkt[1] = 0x8c; loopPkt[2] = 0x13;
            HidD_SetFeature(m_hV5Device, loopPkt, sizeof(loopPkt));
            Sleep(8);

            // Sync with alienfx-cli for full coverage (manual static apply only, never during dynamic streaming)
            if (m_activeDynamicEffect == LedEffect::Off) {
                std::wstring cliCmd = L"setall=" + std::to_wstring(activeColor.red) + L"," +
                                      std::to_wstring(activeColor.green) + L"," +
                                      std::to_wstring(activeColor.blue);
                RunAlienFxCli(cliCmd);
            }
        }
    }

    // Commit
    BYTE upd[64] = { 0 };
    upd[0] = 0xcc; upd[1] = 0x8b; upd[2] = 0x01; upd[3] = 0xff;
    return HidD_SetFeature(m_hV5Device, upd, sizeof(upd)) != FALSE;
}

// Multi-zone dispatcher
bool LedManager::SetZoneEffect(LightingZone zone, LedEffect effect, const LedColor& color, const LedColor& secColor, int brightness, int speed) {
    if (effect == LedEffect::MusicVisualizer || effect == LedEffect::MusicBeatPulse || effect == LedEffect::MusicWave) {
        return StartMusicReactive(effect);
    }
    if (effect == LedEffect::AmbientScreen) {
        return StartScreenAmbient(false);
    }
    if (effect == LedEffect::AmbientMultiZone) {
        return StartScreenAmbient(true);
    }

    if (m_activeDynamicEffect != LedEffect::Off) {
        StopDynamicEffects();
    }

    EnsureDevicesOpen();
    bool ok = true;

    const wchar_t* zoneName = L"All";
    switch (zone) {
        case LightingZone::All:
            zoneName = L"All Zones";
            ok &= SetV4Zone(0, effect, color, brightness, speed); // Trackpad
            ok &= SetV4Zone(2, effect, color, brightness, speed); // Rear Logo
            ok &= SetV4Zone(4, effect, color, brightness, speed); // Power Button
            ok &= SetV5Effect(effect, color, secColor, brightness, speed); // Keyboard
            break;
        case LightingZone::Keyboard:
            zoneName = L"RGB Keyboard";
            ok = SetV5Effect(effect, color, secColor, brightness, speed);
            break;
        case LightingZone::Trackpad:
            zoneName = L"Touchpad (Trackpad)";
            ok = SetV4Zone(0, effect, color, brightness, speed);
            break;
        case LightingZone::Logo:
            zoneName = L"Alien Logo (Rear Lid)";
            ok = SetV4Zone(2, effect, color, brightness, speed);
            break;
        case LightingZone::PowerButton:
            zoneName = L"AlienHead Power Button";
            ok = SetV4Zone(4, effect, color, brightness, speed);
            break;
    }

    const wchar_t* effName = (effect == LedEffect::Static) ? L"Static" :
                             (effect == LedEffect::Breathing) ? L"Breathing" :
                             (effect == LedEffect::Rainbow) ? L"Rainbow" :
                             (effect == LedEffect::Wave) ? L"Wave" :
                             (effect == LedEffect::Pulse) ? L"Pulse" :
                             (effect == LedEffect::Morph) ? L"Morph" :
                             (effect == LedEffect::Scanner) ? L"Scanner" :
                             (effect == LedEffect::Laser) ? L"Laser" :
                             (effect == LedEffect::NeonWave) ? L"NeonWave" :
                             (effect == LedEffect::Stealth) ? L"Stealth" : L"Off";

    Log(L"SetZoneEffect: Zone=" + std::wstring(zoneName) + L", Mode=" + std::wstring(effName) +
        L", RGB(" + std::to_wstring(color.red) + L"," + std::to_wstring(color.green) + L"," + std::to_wstring(color.blue) +
        L"), Brightness=" + std::to_wstring(brightness) + L"% -> " + (ok ? L"SUCCESS" : L"FAILED"));

    return ok;
}

bool LedManager::TurnOnAllLeds(const LedColor& color, LedEffect effect) {
    Log(L"Turning ON all LEDs (AlienFX)");
    if (m_winPowerMgr) {
        m_winPowerMgr->SetAlienFxEnabled(true);
    }
    return SetZoneEffect(LightingZone::All, effect, color, LedColor(255, 0, 128), 100, 7);
}

bool LedManager::TurnOffAllLeds() {
    Log(L"Turning OFF all LEDs (AlienFX)");
    if (m_winPowerMgr) {
        m_winPowerMgr->SetAlienFxEnabled(false);
    }
    for (auto& zone : m_ledZones) {
        zone.second.isOn = false;
        zone.second.color = LedColor(0, 0, 0);
        zone.second.brightness = 0;
    }
    return SetZoneEffect(LightingZone::All, LedEffect::Off, LedColor(0, 0, 0), LedColor(0, 0, 0), 0, 0);
}

bool LedManager::TurnOffZone(LightingZone zone) {
    return SetZoneEffect(zone, LedEffect::Off, LedColor(0, 0, 0), LedColor(0, 0, 0), 0, 0);
}

bool LedManager::TurnOffV4() {
    return SetV4Zone(0, LedEffect::Off, LedColor(0,0,0), 0, 0) &&
           SetV4Zone(2, LedEffect::Off, LedColor(0,0,0), 0, 0) &&
           SetV4Zone(4, LedEffect::Off, LedColor(0,0,0), 0, 0);
}

bool LedManager::TurnOffV5() {
    return SetV5Effect(LedEffect::Off, LedColor(0,0,0), LedColor(0,0,0), 0, 0);
}

bool LedManager::SetColorV4(const LedColor& color, int brightness) {
    return SetV4Zone(0, LedEffect::Static, color, brightness, 7) &&
           SetV4Zone(2, LedEffect::Static, color, brightness, 7) &&
           SetV4Zone(4, LedEffect::Static, color, brightness, 7);
}

bool LedManager::SetColorV5(const LedColor& color, int brightness) {
    return SetV5Effect(LedEffect::Static, color, LedColor(0,0,0), brightness, 7);
}

bool LedManager::ApplyBatteryMode() {
    Log(L"Applying Battery Mode: turning off all LEDs");
    return TurnOffAllLeds();
}

bool LedManager::ApplyPerformanceMode() {
    Log(L"Applying Performance Mode: restoring saved LED profile");
    m_activeProfileName = L"Default Performance";

    if (m_winPowerMgr) {
        m_winPowerMgr->SetAlienFxEnabled(true);
    }

    return TurnOnAllLeds(m_savedProfile.color, m_savedProfile.effect);
}

bool LedManager::SetAllLeds(const LedColor& color, int brightness, LedEffect effect) {
    if (!m_initialized) return false;

    brightness = Clamp(brightness, 0, 100);
    if (brightness == 0) {
        return TurnOffAllLeds();
    }

    if (m_winPowerMgr) {
        m_winPowerMgr->SetAlienFxEnabled(true);
    }

    return SetZoneEffect(LightingZone::All, effect, color, LedColor(255, 0, 128), brightness, 7);
}

bool LedManager::SetZone(const std::wstring& zoneName, const LedColor& color, int brightness) {
    LightingZone z = LightingZone::All;
    if (zoneName == L"Keyboard") z = LightingZone::Keyboard;
    else if (zoneName == L"Trackpad" || zoneName == L"Touchpad") z = LightingZone::Trackpad;
    else if (zoneName == L"Logo" || zoneName == L"Alien Logo") z = LightingZone::Logo;
    else if (zoneName == L"PowerButton" || zoneName == L"Power Button") z = LightingZone::PowerButton;

    return SetZoneEffect(z, LedEffect::Static, color, LedColor(0,0,0), brightness, 7);
}

bool LedManager::TurnOffZone(const std::wstring& zoneName) {
    return SetZone(zoneName, LedColor(0, 0, 0), 0);
}

bool LedManager::IsZoneAvailable(const std::wstring& zoneName) const {
    return m_ledZones.find(zoneName) != m_ledZones.end();
}

std::wstring LedManager::GetStatusString() const {
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status)) {
        if (status.ACLineStatus == 0) {
            return L"LEDs: OFF - Battery Saver";
        }
    }
    return L"LEDs: ON - " + m_activeProfileName;
}

std::wstring LedManager::GetProfileStoragePath() const {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        std::wstring result = path;
        result += L"\\PowerConfigurator\\alienfx_profiles.json";
        return result;
    }
    return L"";
}

bool LedManager::SaveCurrentProfile() {
    m_savedProfile.color = LedColor(0, 180, 255);
    m_savedProfile.brightness = 100;
    m_savedProfile.effect = LedEffect::Static;
    m_activeProfileName = L"Saved";

    std::wstring path = GetProfileStoragePath();
    if (path.empty()) return false;

    int pos = static_cast<int>(path.find_last_of(L"\\"));
    if (pos != std::wstring::npos) {
        std::wstring dir = path.substr(0, pos);
        CreateDirectoryW(dir.c_str(), nullptr);
    }

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    std::string json = "{\"color\":{\"r\":0,\"g\":180,\"b\":255,\"w\":0},\"brightness\":100,\"effect\":\"Static\"}";
    DWORD written;
    WriteFile(hFile, json.c_str(), static_cast<DWORD>(json.size()), &written, nullptr);
    CloseHandle(hFile);

    return true;
}

bool LedManager::RestoreSavedProfile() {
    std::wstring path = GetProfileStoragePath();
    if (path.empty() || !PathFileExistsW(path.c_str())) {
        m_savedProfile.color = LedColor(0, 180, 255);
        m_savedProfile.brightness = 100;
        m_savedProfile.effect = LedEffect::Static;
        m_activeProfileName = L"Default Cyan/Blue";
        return true;
    }

    m_savedProfile.color = LedColor(0, 180, 255);
    m_savedProfile.brightness = 100;
    m_savedProfile.effect = LedEffect::Static;
    m_activeProfileName = L"Default Cyan/Blue";
    return true;
}

bool LedManager::SaveCustomProfile(const std::wstring& name, const LedColor& color, int brightness, LedEffect effect) {
    m_activeProfileName = name;
    m_savedProfile.color = color;
    m_savedProfile.brightness = brightness;
    m_savedProfile.effect = effect;
    return SaveCurrentProfile();
}

bool LedManager::SendHidCommand(const std::vector<BYTE>& command) {
    EnsureDevicesOpen();
    if (m_hV4Device != INVALID_HANDLE_VALUE && !command.empty()) {
        BYTE buf[34] = { 0 };
        size_t len = (std::min)(command.size(), sizeof(buf));
        memcpy(buf, command.data(), len);
        return HidD_SetOutputReport(m_hV4Device, buf, sizeof(buf)) != FALSE;
    }
    return false;
}

bool LedManager::SetAlienFxLed(const LedColor& color, int brightness) {
    return SetAllLeds(color, brightness, LedEffect::Static);
}

bool LedManager::SetDellLed(const LedColor& color, int brightness) {
    return SetAlienFxLed(color, brightness);
}

bool LedManager::SetHidlLedControl(bool on) {
    return SetAlienFxDeviceState(on);
}

bool LedManager::SetAlienFxDeviceState(bool on) {
    return on ? ApplyPerformanceMode() : TurnOffAllLeds();
}

std::vector<std::wstring> LedManager::FetchAlienFxToolsProfiles() {
    std::vector<std::wstring> profiles;

    // 1. Enumerate profiles from Alienfxgui registry if user has configured them
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Alienfxgui\\Profiles", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD values = 0, maxValueNameLen = 0, maxValueLen = 0;
        if (RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &values, &maxValueNameLen, &maxValueLen, nullptr, nullptr) == ERROR_SUCCESS) {
            std::vector<wchar_t> nameBuf(maxValueNameLen + 2);
            std::vector<BYTE> dataBuf(maxValueLen + 2);
            for (DWORD i = 0; i < values; ++i) {
                DWORD nameLen = static_cast<DWORD>(nameBuf.size());
                DWORD dataLen = static_cast<DWORD>(dataBuf.size());
                DWORD type = 0;
                if (RegEnumValueW(hKey, i, nameBuf.data(), &nameLen, nullptr, &type, dataBuf.data(), &dataLen) == ERROR_SUCCESS) {
                    std::wstring valName(nameBuf.data(), nameLen);
                    if (valName.find(L"Profile-") == 0 &&
                        valName.find(L"triggers") == std::wstring::npos &&
                        valName.find(L"freq") == std::wstring::npos &&
                        valName.find(L"gflags") == std::wstring::npos) {
                        if (type == REG_SZ && dataLen > sizeof(wchar_t)) {
                            std::wstring profName = reinterpret_cast<wchar_t*>(dataBuf.data());
                            if (!profName.empty() && std::find(profiles.begin(), profiles.end(), profName) == profiles.end()) {
                                profiles.push_back(profName);
                            }
                        }
                    }
                }
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Preconfigured modes from Alienware Command Center & AlienFX Tools
    const std::wstring awccModes[] = {
        L"AWCC Static Cyan",
        L"AWCC Breathing Cyan",
        L"AWCC Rainbow Wave",
        L"AWCC Spectrum Cycle",
        L"AWCC Scanner (Night Rider)",
        L"AWCC Laser Burst",
        L"AWCC Cyber Neon Wave",
        L"AWCC Stealth Night Dim"
    };
    for (const auto& m : awccModes) {
        if (std::find(profiles.begin(), profiles.end(), m) == profiles.end()) {
            profiles.push_back(m);
        }
    }

    Log(L"FetchAlienFxToolsProfiles: Found " + std::to_wstring(profiles.size()) + L" profiles.");
    return profiles;
}

bool LedManager::SyncAlienFxTools() {
    // Check if AlienFX Tools CLI exists locally
    const wchar_t* cliPath = L"C:\\Users\\harsh\\AppData\\Local\\AlienFX Tools\\alienfx-cli.exe";
    bool cliInstalled = (GetFileAttributesW(cliPath) != INVALID_FILE_ATTRIBUTES);

    // Fetch device state & brightness from Alienfx_SDK registry
    HKEY hKey = nullptr;
    DWORD devBright = 255;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Alienfx_SDK", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD size = sizeof(devBright);
        RegQueryValueExW(hKey, L"DevBright#3426_53937", nullptr, nullptr, reinterpret_cast<LPBYTE>(&devBright), &size);
        RegCloseKey(hKey);
    }

    Log(L"SyncAlienFxTools: AlienFX Tools CLI installed=" + std::to_wstring(cliInstalled) +
        L", SDK Keyboard Brightness=" + std::to_wstring(devBright));
    return true;
}

bool LedManager::RunAlienFxCli(const std::wstring& args) {
    const wchar_t* cliPath = L"C:\\Users\\harsh\\AppData\\Local\\AlienFX Tools\\alienfx-cli.exe";
    if (GetFileAttributesW(cliPath) == INVALID_FILE_ATTRIBUTES) return false;

    std::wstring cmd = L"\"" + std::wstring(cliPath) + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}

bool LedManager::ApplyAlienFxToolsProfile(const std::wstring& profileName) {
    Log(L"ApplyAlienFxToolsProfile: Applying " + profileName);
    if (profileName == L"AWCC Static Cyan" || profileName == L"Default") {
        RunAlienFxCli(L"setall=0,180,255");
        return SetZoneEffect(LightingZone::All, LedEffect::Static, LedColor(0, 180, 255), LedColor(0, 0, 0), 100, 7);
    } else if (profileName == L"AWCC Breathing Cyan") {
        RunAlienFxCli(L"setglobal=1,2,1,0,180,255");
        return SetZoneEffect(LightingZone::All, LedEffect::Breathing, LedColor(0, 180, 255), LedColor(0, 0, 0), 100, 5);
    } else if (profileName == L"AWCC Rainbow Wave") {
        RunAlienFxCli(L"setglobal=1,3,1,0,180,255");
        return SetZoneEffect(LightingZone::All, LedEffect::Rainbow, LedColor(0, 180, 255), LedColor(255, 0, 128), 100, 8);
    } else if (profileName == L"AWCC Spectrum Cycle") {
        RunAlienFxCli(L"setglobal=1,4,1,0,180,255,255,0,128");
        return SetZoneEffect(LightingZone::All, LedEffect::Morph, LedColor(0, 180, 255), LedColor(255, 0, 128), 100, 7);
    } else if (profileName == L"AWCC Scanner (Night Rider)") {
        RunAlienFxCli(L"setglobal=1,10,1,255,0,0");
        return SetZoneEffect(LightingZone::All, LedEffect::Scanner, LedColor(255, 0, 51), LedColor(0, 0, 0), 100, 8);
    } else if (profileName == L"AWCC Laser Burst") {
        RunAlienFxCli(L"setglobal=1,11,1,0,180,255");
        return SetZoneEffect(LightingZone::All, LedEffect::Laser, LedColor(0, 180, 255), LedColor(0, 0, 0), 100, 8);
    } else if (profileName == L"AWCC Cyber Neon Wave") {
        RunAlienFxCli(L"setglobal=1,4,1,0,180,255,255,0,128");
        return SetZoneEffect(LightingZone::All, LedEffect::NeonWave, LedColor(0, 180, 255), LedColor(255, 0, 128), 100, 8);
    } else if (profileName == L"AWCC Stealth Night Dim") {
        RunAlienFxCli(L"setdim=1,50 setall=180,20,0");
        return SetZoneEffect(LightingZone::All, LedEffect::Stealth, LedColor(180, 20, 0), LedColor(0, 0, 0), 20, 1);
    }
    // Default fallback
    RunAlienFxCli(L"setall=0,180,255");
    return SetZoneEffect(LightingZone::All, LedEffect::Static, LedColor(0, 180, 255), LedColor(0, 0, 0), 100, 7);
}

bool LedManager::SetPerKeyColors(const std::vector<std::pair<BYTE, LedColor>>& keyColors) {
    if (m_hV5Device == INVALID_HANDLE_VALUE) {
        EnsureDevicesOpen();
        if (m_hV5Device == INVALID_HANDLE_VALUE) return false;
    }

    BYTE br[64] = { 0 };
    br[0] = 0xcc; br[1] = 0x83; br[2] = 0x38; br[3] = 0x9c; br[4] = 100;
    HidD_SetFeature(m_hV5Device, br, sizeof(br));

    for (size_t i = 0; i < keyColors.size(); ) {
        BYTE col[64] = { 0 };
        col[0] = 0xcc; col[1] = 0x8c; col[2] = 0x02;
        BYTE count = 0;
        for (BYTE b = 4; b < 60 && i < keyColors.size(); b += 4, ++i) {
            col[b] = keyColors[i].first;
            col[b + 1] = keyColors[i].second.red;
            col[b + 2] = keyColors[i].second.green;
            col[b + 3] = keyColors[i].second.blue;
            count++;
        }
        col[3] = count;
        HidD_SetFeature(m_hV5Device, col, sizeof(col));
        Sleep(1);
    }

    BYTE loopPkt[64] = { 0 };
    loopPkt[0] = 0xcc; loopPkt[1] = 0x8c; loopPkt[2] = 0x13;
    HidD_SetFeature(m_hV5Device, loopPkt, sizeof(loopPkt));

    BYTE upd[64] = { 0 };
    upd[0] = 0xcc; upd[1] = 0x8b; upd[2] = 0x01; upd[3] = 0xff;
    return HidD_SetFeature(m_hV5Device, upd, sizeof(upd)) != FALSE;
}

bool LedManager::StartMusicReactive(LedEffect mode) {
    StopDynamicEffects();
    EnsureDevicesOpen();
    LoadLightFxDll();
    m_activeDynamicEffect = mode;
    if (m_audioViz) {
        m_audioViz->Start();
    }
    Log(L"StartMusicReactive: Started mode " + std::to_wstring(static_cast<int>(mode)));
    return true;
}

bool LedManager::StartScreenAmbient(bool multiZone) {
    StopDynamicEffects();
    EnsureDevicesOpen();
    LoadLightFxDll();
    m_activeDynamicEffect = multiZone ? LedEffect::AmbientMultiZone : LedEffect::AmbientScreen;
    if (m_screenAmbient) {
        m_screenAmbient->Start();
    }
    Log(L"StartScreenAmbient: Started multiZone=" + std::to_wstring(multiZone));
    return true;
}

void LedManager::StopDynamicEffects() {
    if (m_audioViz && m_audioViz->IsRunning()) {
        m_audioViz->Stop();
    }
    if (m_screenAmbient && m_screenAmbient->IsRunning()) {
        m_screenAmbient->Stop();
    }
    m_activeDynamicEffect = LedEffect::Off;
}

void LedManager::TickDynamicEffects() {
    if (m_activeDynamicEffect == LedEffect::Off) return;

    if (m_lightFxInitialized && m_fnLfxUpdate) {
        if (m_activeDynamicEffect == LedEffect::MusicVisualizer) {
            if (!m_audioViz) return;
            float bass = 0.0f, mid = 0.0f, treble = 0.0f;
            m_audioViz->GetSpectrum(bass, mid, treble);

            BYTE rB = static_cast<BYTE>(Clamp(bass * 255.0f, 0.0f, 255.0f));
            BYTE gB = 0;
            BYTE bB = static_cast<BYTE>(Clamp(bass * 220.0f, 0.0f, 255.0f));

            BYTE rM = 0;
            BYTE gM = static_cast<BYTE>(Clamp(mid * 200.0f, 0.0f, 255.0f));
            BYTE bM = static_cast<BYTE>(Clamp(mid * 255.0f, 0.0f, 255.0f));

            BYTE rT = static_cast<BYTE>(Clamp(treble * 255.0f, 0.0f, 255.0f));
            BYTE gT = static_cast<BYTE>(Clamp(treble * 220.0f, 0.0f, 255.0f));
            BYTE bT = static_cast<BYTE>(Clamp(treble * 80.0f, 0.0f, 255.0f));

            LFX_COLOR_STRUCT colBass = { rB, gB, bB, 255 };
            LFX_COLOR_STRUCT colMid  = { rM, gM, bM, 255 };
            LFX_COLOR_STRUCT colTreb = { rT, gT, bT, 255 };

            if (m_fnLfxSetLightColor) {
                // Device 1: Keyboard (85 keys)
                // Left third (keys 0..27) = Bass, Middle third (keys 28..56) = Mid, Right third (keys 57..84) = Treble
                for (unsigned int k = 0; k < 28; ++k) m_fnLfxSetLightColor(1, k, &colBass);
                for (unsigned int k = 28; k < 57; ++k) m_fnLfxSetLightColor(1, k, &colMid);
                for (unsigned int k = 57; k < 85; ++k) m_fnLfxSetLightColor(1, k, &colTreb);

                // Device 0: Tron lights (0=Touchpad, 1=Back, 2=Power)
                m_fnLfxSetLightColor(0, 0, &colMid);
                m_fnLfxSetLightColor(0, 1, &colBass);
                m_fnLfxSetLightColor(0, 2, &colTreb);
            }
            m_fnLfxUpdate();
            return;
        } else if (m_activeDynamicEffect == LedEffect::MusicBeatPulse) {
            if (!m_audioViz) return;
            if (m_audioViz->IsBeatDetected()) {
                m_beatFlash = 1.0f;
            }
            m_beatFlash *= 0.82f;
            BYTE r = static_cast<BYTE>(Clamp(m_beatFlash * 255.0f, 0.0f, 255.0f));
            BYTE g = static_cast<BYTE>(Clamp(160.0f * (1.0f - m_beatFlash) + m_beatFlash * 255.0f, 0.0f, 255.0f));
            BYTE b = 255;
            BYTE br = static_cast<BYTE>(Clamp(20.0f + m_beatFlash * 80.0f, 20.0f, 100.0f));

            uint32_t colorInt = (static_cast<uint32_t>(br) << 24) |
                                (static_cast<uint32_t>(r) << 16) |
                                (static_cast<uint32_t>(g) << 8) |
                                static_cast<uint32_t>(b);
            if (m_fnLfxLight) {
                m_fnLfxLight(0, colorInt);
            }
            m_fnLfxUpdate();
            return;
        } else if (m_activeDynamicEffect == LedEffect::MusicWave) {
            if (!m_audioViz) return;
            float peak = m_audioViz->GetPeakLevel();
            static float waveHue = 0.0f;
            waveHue += 3.0f + peak * 12.0f;
            if (waveHue >= 360.0f) waveHue -= 360.0f;

            float h = waveHue;
            float c = 1.0f;
            float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
            float rf = 0, gf = 0, bf = 0;
            if (h < 60) { rf = c; gf = x; }
            else if (h < 120) { rf = x; gf = c; }
            else if (h < 180) { gf = c; bf = x; }
            else if (h < 240) { gf = x; bf = c; }
            else if (h < 300) { rf = x; bf = c; }
            else { rf = c; bf = x; }

            BYTE r = static_cast<BYTE>(rf * 255.0f);
            BYTE g = static_cast<BYTE>(gf * 255.0f);
            BYTE b = static_cast<BYTE>(bf * 255.0f);
            uint32_t colorInt = (255u << 24) | (r << 16) | (g << 8) | b;
            if (m_fnLfxLight) {
                m_fnLfxLight(0, colorInt);
            }
            m_fnLfxUpdate();
            return;
        } else if (m_activeDynamicEffect == LedEffect::AmbientScreen) {
            if (!m_screenAmbient) return;
            AmbientColor ac = m_screenAmbient->GetAverageColor();
            uint32_t colorInt = (255u << 24) |
                                (static_cast<uint32_t>(ac.r) << 16) |
                                (static_cast<uint32_t>(ac.g) << 8) |
                                static_cast<uint32_t>(ac.b);
            if (m_fnLfxLight) {
                m_fnLfxLight(0, colorInt);
            }
            m_fnLfxUpdate();
            return;
        } else if (m_activeDynamicEffect == LedEffect::AmbientMultiZone) {
            if (!m_screenAmbient) return;
            AmbientColor leftC = m_screenAmbient->GetLeftColor();
            AmbientColor centerC = m_screenAmbient->GetCenterColor();
            AmbientColor rightC = m_screenAmbient->GetRightColor();
            AmbientColor topC = m_screenAmbient->GetTopColor();

            LFX_COLOR_STRUCT colL = { leftC.r, leftC.g, leftC.b, 255 };
            LFX_COLOR_STRUCT colC = { centerC.r, centerC.g, centerC.b, 255 };
            LFX_COLOR_STRUCT colR = { rightC.r, rightC.g, rightC.b, 255 };
            LFX_COLOR_STRUCT colT = { topC.r, topC.g, topC.b, 255 };

            if (m_fnLfxSetLightColor) {
                // Device 1: Keyboard (85 keys)
                for (unsigned int k = 0; k < 28; ++k) m_fnLfxSetLightColor(1, k, &colL);
                for (unsigned int k = 28; k < 57; ++k) m_fnLfxSetLightColor(1, k, &colC);
                for (unsigned int k = 57; k < 85; ++k) m_fnLfxSetLightColor(1, k, &colR);

                // Device 0: Tron lights (0=Touchpad, 1=Back logo, 2=Power button)
                m_fnLfxSetLightColor(0, 0, &colC);
                m_fnLfxSetLightColor(0, 1, &colT);
                m_fnLfxSetLightColor(0, 2, &colC);
            }
            m_fnLfxUpdate();
            return;
        }
    }

    if (m_activeDynamicEffect == LedEffect::MusicVisualizer) {
        if (!m_audioViz) return;
        float bass = 0.0f, mid = 0.0f, treble = 0.0f;
        m_audioViz->GetSpectrum(bass, mid, treble);

        LedColor colBass(static_cast<BYTE>(Clamp(bass * 255.0f, 0.0f, 255.0f)),
                         0,
                         static_cast<BYTE>(Clamp(bass * 220.0f, 0.0f, 255.0f)));

        LedColor colMid(0,
                        static_cast<BYTE>(Clamp(mid * 200.0f, 0.0f, 255.0f)),
                        static_cast<BYTE>(Clamp(mid * 255.0f, 0.0f, 255.0f)));

        LedColor colTreb(static_cast<BYTE>(Clamp(treble * 255.0f, 0.0f, 255.0f)),
                         static_cast<BYTE>(Clamp(treble * 220.0f, 0.0f, 255.0f)),
                         static_cast<BYTE>(Clamp(treble * 80.0f, 0.0f, 255.0f)));

        auto keys = GetV5KeyIds();
        std::vector<std::pair<BYTE, LedColor>> perKey;
        perKey.reserve(keys.size());

        for (BYTE k : keys) {
            if (k <= 3 || (k >= 20 && k <= 23) || (k >= 40 && k <= 44) ||
                (k >= 60 && k <= 64) || (k >= 80 && k <= 85) || (k >= 100 && k <= 104)) {
                perKey.push_back({ k, colBass });
            } else if ((k >= 4 && k <= 7) || (k >= 24 && k <= 27) || (k >= 45 && k <= 48) ||
                       (k >= 65 && k <= 68) || (k >= 86 && k <= 89) || k == 106) {
                perKey.push_back({ k, colMid });
            } else {
                perKey.push_back({ k, colTreb });
            }
        }
        SetPerKeyColors(perKey);

        int brBass = static_cast<int>(Clamp(bass * 100.0f, 0.0f, 100.0f));
        int brMid = static_cast<int>(Clamp(mid * 100.0f, 0.0f, 100.0f));
        int brTreb = static_cast<int>(Clamp(treble * 100.0f, 0.0f, 100.0f));
        SetV4Zone(0, LedEffect::Static, colMid, brMid, 7);
        SetV4Zone(2, LedEffect::Static, colBass, brBass, 7);
        SetV4Zone(4, LedEffect::Static, colTreb, brTreb, 7);
    } else if (m_activeDynamicEffect == LedEffect::MusicBeatPulse) {
        if (!m_audioViz) return;
        if (m_audioViz->IsBeatDetected()) {
            m_beatFlash = 1.0f;
        }
        m_beatFlash *= 0.78f;
        int br = static_cast<int>(Clamp(15.0f + m_beatFlash * 85.0f, 15.0f, 100.0f));
        LedColor flashCol(static_cast<BYTE>(Clamp(m_beatFlash * 255.0f, 0.0f, 255.0f)),
                          static_cast<BYTE>(Clamp(180.0f + m_beatFlash * 75.0f, 180.0f, 255.0f)),
                          255);
        SetV5Effect(LedEffect::Static, flashCol, LedColor(0, 0, 0), br, 7);
        SetV4Zone(0, LedEffect::Static, flashCol, br, 7);
        SetV4Zone(2, LedEffect::Static, flashCol, br, 7);
        SetV4Zone(4, LedEffect::Static, flashCol, br, 7);
    } else if (m_activeDynamicEffect == LedEffect::MusicWave) {
        if (!m_audioViz) return;
        float peak = m_audioViz->GetPeakLevel();
        int spd = static_cast<int>(Clamp(1.0f + peak * 9.0f, 1.0f, 10.0f));
        int br = static_cast<int>(Clamp(30.0f + peak * 70.0f, 30.0f, 100.0f));
        SetV5Effect(LedEffect::Rainbow, LedColor(0, 180, 255), LedColor(255, 0, 128), br, spd);
        SetV4Zone(0, LedEffect::Rainbow, LedColor(0, 180, 255), br, spd);
        SetV4Zone(2, LedEffect::Rainbow, LedColor(0, 180, 255), br, spd);
        SetV4Zone(4, LedEffect::Rainbow, LedColor(0, 180, 255), br, spd);
    } else if (m_activeDynamicEffect == LedEffect::AmbientScreen) {
        if (!m_screenAmbient) return;
        AmbientColor ac = m_screenAmbient->GetAverageColor();
        LedColor ambCol(ac.r, ac.g, ac.b);
        SetV5Effect(LedEffect::Static, ambCol, LedColor(0, 0, 0), 100, 7);
        SetV4Zone(0, LedEffect::Static, ambCol, 100, 7);
        SetV4Zone(2, LedEffect::Static, ambCol, 100, 7);
        SetV4Zone(4, LedEffect::Static, ambCol, 100, 7);
    } else if (m_activeDynamicEffect == LedEffect::AmbientMultiZone) {
        if (!m_screenAmbient) return;
        AmbientColor leftC = m_screenAmbient->GetLeftColor();
        AmbientColor centerC = m_screenAmbient->GetCenterColor();
        AmbientColor rightC = m_screenAmbient->GetRightColor();
        AmbientColor topC = m_screenAmbient->GetTopColor();

        LedColor colL(leftC.r, leftC.g, leftC.b);
        LedColor colC(centerC.r, centerC.g, centerC.b);
        LedColor colR(rightC.r, rightC.g, rightC.b);
        LedColor colT(topC.r, topC.g, topC.b);

        auto keys = GetV5KeyIds();
        std::vector<std::pair<BYTE, LedColor>> perKey;
        perKey.reserve(keys.size());

        for (BYTE k : keys) {
            if (k <= 3 || (k >= 20 && k <= 23) || (k >= 40 && k <= 44) ||
                (k >= 60 && k <= 64) || (k >= 80 && k <= 85) || (k >= 100 && k <= 104)) {
                perKey.push_back({ k, colL });
            } else if ((k >= 4 && k <= 7) || (k >= 24 && k <= 27) || (k >= 45 && k <= 48) ||
                       (k >= 65 && k <= 68) || (k >= 86 && k <= 89) || k == 106) {
                perKey.push_back({ k, colC });
            } else {
                perKey.push_back({ k, colR });
            }
        }
        SetPerKeyColors(perKey);

        SetV4Zone(0, LedEffect::Static, colC, 100, 7);
        SetV4Zone(2, LedEffect::Static, colT, 100, 7);
        SetV4Zone(4, LedEffect::Static, colC, 100, 7);
    }
}
