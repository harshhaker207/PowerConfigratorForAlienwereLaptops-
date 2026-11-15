#pragma once

#include <windows.h>
#include <powrprof.h>
#include <pdh.h>
#include <wbemidl.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <optional>
#include <functional>
#include <algorithm>

#ifdef HAVE_NVML
#include <nvml.h>
#endif

// ============================================================================
// Constants and GUIDs - Using inline for C++17 compatible GUID definitions
// ============================================================================

// Power Plans - Standard Windows power scheme GUIDs
inline const GUID GUID_POWER_SCHEME_PERFORMANCE = { 0x8c5e7fda, 0xe8bf, 0x4a96, { 0x9a, 0x85, 0xa5, 0xe9, 0x31, 0xae, 0x2d, 0xb7 } };
inline const GUID GUID_POWER_SCHEME_BALANCED    = { 0x381b4222, 0xf694, 0x41f0, { 0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e } };
inline const GUID GUID_POWER_SCHEME_POWER_SAVER = { 0xa1841308, 0x3541, 0x4fab, { 0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a } };

// Windows 11 Power Mode Slider
// {961CC777-2547-4F9D-8174-7D86181B8A7A}
inline const GUID GUID_POWER_MODE_SLIDER = { 0x961cc777, 0x2547, 0x4f9d, { 0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a } };

// Processor Power Management subgroup
// {54533251-82BE-4824-96C1-47B60B740D00}
inline const GUID GUID_PROCESSOR_POWER_SUBGROUP = { 0x54533251, 0x82be, 0x4824, { 0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00 } };

// Minimum Processor State
// {893DEE8E-2BEF-41E0-89C6-B55D0929964C}
inline const GUID GUID_PROCESSOR_MINIMUM_STATE = { 0x893dee8e, 0x2bef, 0x41e0, { 0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4c } };

// Maximum Processor State
// {BC5038F7-23E0-4960-96DA-33ABAF5935EC}
inline const GUID GUID_PROCESSOR_MAXIMUM_STATE = { 0xbc5038f7, 0x23e0, 0x4960, { 0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec } };

// System Cooling Policy
// {94D3A615-A899-4AC5-AE2B-E4D8F634367F}
inline const GUID GUID_SYSTEM_COOLING_POLICY = { 0x94d3a615, 0xa899, 0x4ac5, { 0xae, 0x2b, 0xe4, 0xd8, 0xf6, 0x34, 0x36, 0x7f } };

// Processor Performance Boost Policy (Percentage: 0-100)
// {45BCC044-D885-43E2-8605-EE0EC6E96B59}
inline const GUID GUID_PROCESSOR_BOOST_POLICY = { 0x45bcc044, 0xd885, 0x43e2, { 0x86, 0x05, 0xee, 0x0e, 0xc6, 0xe9, 0x6b, 0x59 } };

// Processor Performance Boost Mode (0=Disabled, 1=Enabled, 2=Aggressive)
// {BE337238-0D82-4146-A960-4F3749D470C7}
inline const GUID GUID_PROCESSOR_BOOST_MODE = { 0xbe337238, 0x0d82, 0x4146, { 0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7 } };

// Processor Maximum Frequency (MHz, 0 = unlimited)
// {75B60E4F-3497-49D1-8C2F-234D975D0FDC}
inline const GUID GUID_PROCESSOR_FREQUENCY_LIMIT = { 0x75b60e4f, 0x3497, 0x49d1, { 0x8c, 0x2f, 0x23, 0x4d, 0x97, 0x5d, 0x0f, 0xdc } };
inline const GUID GUID_PROCESSOR_FREQUENCY_LIMIT_1 = { 0x75b60e4f, 0x3497, 0x49d1, { 0x8c, 0x2f, 0x23, 0x4d, 0x97, 0x5d, 0x0f, 0xdd } };

// AlienFX Lighting Power Setting Subgroup & Setting
// {FEA3413E-7E05-4911-9A71-700331F1C294} & {5E6272BE-0EA2-482C-AF74-D665CEA3C71B}
inline const GUID GUID_SUB_NONE = { 0xfea3413e, 0x7e05, 0x4911, { 0x9a, 0x71, 0x70, 0x03, 0x31, 0xf1, 0xc2, 0x94 } };
inline const GUID GUID_ALIENFX_POWER_SETTING = { 0x5e6272be, 0x0ea2, 0x482c, { 0xaf, 0x74, 0xd6, 0x65, 0xce, 0xa3, 0xc7, 0x1b } };

// Processor Energy Performance Preference Policy
// {36687F9E-E3A5-4DBF-B1DC-15EB381C6863}
inline const GUID GUID_PROCESSOR_ENERGY_PERF_PREF = { 0x36687f9e, 0xe3a5, 0x4dbf, { 0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63 } };

// Display subgroup
// {7516B95F-F776-4464-8C53-06167F40CC99}
inline const GUID GUID_DISPLAY_SUBGROUP = { 0x7516b95f, 0xf776, 0x4464, { 0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99 } };

// Turn Off Display After
// {3C0BC021-C8A8-4E07-A973-6B14CBCB2B7E}
inline const GUID GUID_DISPLAY_TURN_OFF = { 0x3c0bc021, 0xc8a8, 0x4e07, { 0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e } };

// Dim Display After
// {17AAA29B-8B43-4B94-AAFE-35F64DAAF1EE}
inline const GUID GUID_DISPLAY_DIM = { 0x17aaa29b, 0x8b43, 0x4b94, { 0xaa, 0xfe, 0x35, 0xf6, 0x4d, 0xaa, 0xf1, 0xee } };

// Sleep subgroup
// {238C9FA8-0AAD-41ED-83F4-97BE242C8F20}
inline const GUID GUID_SLEEP_SUBGROUP = { 0x238c9fa8, 0x0aad, 0x41ed, { 0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20 } };

// Sleep After
// {29F6C1DB-86DA-48C5-9FDB-F2B67B1F44DA}
inline const GUID GUID_SLEEP_AFTER = { 0x29f6c1db, 0x86da, 0x48c5, { 0x9f, 0xdb, 0xf2, 0xb6, 0x7b, 0x1f, 0x44, 0xda } };

// Allow Hybrid Sleep
// {94AC6D29-73CE-41A6-809F-6363BA21B47E}
inline const GUID GUID_ALLOW_HYBRID_SLEEP = { 0x94ac6d29, 0x73ce, 0x41a6, { 0x80, 0x9f, 0x63, 0x63, 0xba, 0x21, 0xb4, 0x7e } };

// Hard Disk subgroup
// {0012EE47-9041-4B5D-9B77-535FBA8B1442}
inline const GUID GUID_HARD_DISK_SUBGROUP = { 0x0012ee47, 0x9041, 0x4b5d, { 0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42 } };

// Turn Off Hard Disk After
// {6738E2C4-E8A5-4A42-B16A-E040E769756E}
inline const GUID GUID_HARD_DISK_TURN_OFF = { 0x6738e2c4, 0xe8a5, 0x4a42, { 0xb1, 0x6a, 0xe0, 0x40, 0xe7, 0x69, 0x75, 0x6e } };

// USB Settings subgroup
// {2A737441-1930-4402-8D77-B2BEBBA308A3}
inline const GUID GUID_USB_SUBGROUP = { 0x2a737441, 0x1930, 0x4402, { 0x8d, 0x77, 0xb2, 0xbe, 0xbb, 0xa3, 0x08, 0xa3 } };

// USB Selective Suspend
// {48E6B7A6-50F5-4782-A5D4-53BB8F07E226}
inline const GUID GUID_USB_SELECTIVE_SUSPEND = { 0x48e6b7a6, 0x50f5, 0x4782, { 0xa5, 0xd4, 0x53, 0xbb, 0x8f, 0x07, 0xe2, 0x26 } };

// PCI Express subgroup
// {501A4D13-42AF-4429-9FD1-A8218C268E20}
inline const GUID GUID_PCIEXPRESS_SUBGROUP = { 0x501a4d13, 0x42af, 0x4429, { 0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20 } };

// PCI Express Link State Power Management
// {EE12F906-D277-404B-B6DA-E5FA1A576DF5}
inline const GUID GUID_PCIEXPRESS_LINK_POWER = { 0xee12f906, 0xd277, 0x404b, { 0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5 } };

// Intel Graphics Power Plan subgroup
// {3619C3F2-AFB2-4AFC-B0E9-E7FEF372DE36}
inline const GUID GUID_INTEL_GRAPHICS_SUBGROUP = { 0x3619c3f2, 0xafb2, 0x4afc, { 0xb0, 0xe9, 0xe7, 0xfe, 0xf3, 0x72, 0xde, 0x36 } };

// Intel Graphics Power Plan
// {3619C3F2-AFB2-4AFC-B0E9-E7FEF372DE36}
inline const GUID GUID_INTEL_GRAPHICS_POWER_PLAN = { 0x3619c3f2, 0xafb2, 0x4afc, { 0xb0, 0xe9, 0xe7, 0xfe, 0xf3, 0x72, 0xde, 0x36 } };

// Graphics Settings subgroup
// {5FB4938D-1EE8-4B0F-9A3C-5036B0AB995C}
inline const GUID GUID_GRAPHICS_SETTINGS_SUBGROUP = { 0x5fb4938d, 0x1ee8, 0x4b0f, { 0x9a, 0x3c, 0x50, 0x36, 0xb0, 0xab, 0x99, 0x5c } };

// Processor Power Efficiency Class 1 sub-GUIDs
inline const GUID GUID_PROCESSOR_MINIMUM_STATE_1 = { 0x893dee8e, 0x2bef, 0x41e0, { 0x89, 0xc6, 0xb5, 0x5d, 0x09, 0x29, 0x96, 0x4d } };
inline const GUID GUID_PROCESSOR_MAXIMUM_STATE_1 = { 0xbc5038f7, 0x23e0, 0x4960, { 0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xed } };
inline const GUID GUID_PROCESSOR_BOOST_POLICY_1 = { 0x45bcc044, 0xd885, 0x43e2, { 0x86, 0x05, 0xee, 0x0e, 0xc6, 0xe9, 0x6b, 0x5a } };
inline const GUID GUID_PROCESSOR_ENERGY_PERF_PREF_1 = { 0x36687f9e, 0xe3a5, 0x4dbf, { 0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x64 } };

// Power Mode values
enum class PowerMode : DWORD {
    MaximumBatteryLife = 0,
    BetterBattery = 1,
    Balanced = 2,
    BetterPerformance = 3,
    MaximumPerformance = 4
};

// Cooling Policy values
enum class CoolingPolicy : DWORD {
    Passive = 0,
    Active = 1
};

// Boost Policy values
enum class BoostPolicy : DWORD {
    Disabled = 0,
    Enabled = 1,
    Aggressive = 2,
    EfficientEnabled = 3,
    AggressiveAtGuaranteed = 4
};

// Intel Graphics Power Plan values
enum class IntelGraphicsPowerPlan : DWORD {
    MaximumBatteryLife = 0,
    Balanced = 1,
    MaximumPerformance = 2
};

// PCI Express Link State Power Management values
enum class PciExpressLinkPower : DWORD {
    Off = 0,
    ModeratePowerSavings = 1,
    MaximumPowerSavings = 2
};

// USB Selective Suspend values
enum class UsbSelectiveSuspend : DWORD {
    Disabled = 0,
    Enabled = 1
};

// GPU Mode enum for Optimus/MUX control
enum class GpuMode {
    Auto,
    IntegratedOnly,
    Hybrid,
    DiscreteOnly
};

// GPU Power State enum
enum class DgpuPowerState {
    Unknown,
    Off,
    Sleep,
    On,
    UnknownError
};

// Fan Preset enum
enum class FanPreset {
    Silent,
    Balanced,
    Cool,
    Max
};

// Fan Control Mode enum
enum class FanControlMode {
    Auto,
    Manual
};

// ============================================================================
// Data Structures
// ============================================================================

struct ProfileSettings {
    // CPU
    int cpuPl1Watts = 45;
    int cpuMinStatePercent = 5;
    int cpuMaxStatePercent = 80;
    CoolingPolicy coolingPolicy = CoolingPolicy::Passive;
    BoostPolicy boostPolicy = BoostPolicy::Disabled;

    // GPU
    int gpuPowerLimitMw = 80000;

    // Display
    int refreshRateHz = 60;
    bool autoRefreshRate = true;

    // Windows Power
    PowerMode powerMode = PowerMode::MaximumBatteryLife;
    int displayTurnOffMinutes = 2;
    int displayDimMinutes = 1;
    int sleepMinutes = 5;
    int hardDiskTurnOffMinutes = 5;
    UsbSelectiveSuspend usbSelectiveSuspend = UsbSelectiveSuspend::Enabled;
    PciExpressLinkPower pciExpressLinkPower = PciExpressLinkPower::MaximumPowerSavings;
    IntelGraphicsPowerPlan intelGraphicsPlan = IntelGraphicsPowerPlan::MaximumBatteryLife;

    // Metadata
    bool isBatteryProfile = true;
};

struct SystemMetrics {
    // CPU
    double cpuTemperatureC = 0.0;
    double cpuClockGhz = 0.0;
    double cpuPackagePowerW = 0.0;
    int cpuPl1LimitW = 0;
    double cpuUsagePercent = 0.0;

    // GPU
    int gpuTemperatureC = 0;
    int gpuClockMhz = 0;
    double gpuPowerDrawW = 0.0;
    int gpuPowerLimitW = 0;
    int gpuUsagePercent = 0;

    // Battery
    int batteryPercent = 0;
    bool isCharging = false;
    int estimatedMinutesRemaining = -1;
    bool isOnAc = false;

    // Display
    int currentRefreshRateHz = 60;
    int currentWidth = 1920;
    int currentHeight = 1080;

    // Windows
    PowerMode currentPowerMode = PowerMode::Balanced;
    std::wstring activePowerPlanName;

    // GPU Mode (Optimus/MUX)
    GpuMode gpuMode = GpuMode::Auto;

    // Fan status
    int fanSpeedPercent = 0;
    int fan1Rpm = 0;
    int fan2Rpm = 0;

    // LED status
    bool ledAvailable = false;

    // Timestamp
    std::chrono::steady_clock::time_point timestamp;
};

enum class ProfileType {
    Battery,
    Performance,
    Custom
};

// ============================================================================
// Utility Functions
// ============================================================================

inline std::wstring GuidToString(const GUID& guid) {
    wchar_t buf[64];
    StringFromGUID2(guid, buf, 64);
    return std::wstring(buf);
}

inline std::string WStringToString(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], size, nullptr, nullptr);
    return s;
}

inline std::wstring StringToWString(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], size);
    return ws;
}

inline DWORD GetAcDcIndex(bool isAc) {
    return isAc ? 0 : 1;  // AC=0, DC=1 per Windows convention
}

inline bool IsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// ============================================================================
// Callback Types
// ============================================================================

using MetricsCallback = std::function<void(const SystemMetrics&)>;
using ProfileChangedCallback = std::function<void(ProfileType)>;
using LogCallback = std::function<void(const std::wstring&)>;