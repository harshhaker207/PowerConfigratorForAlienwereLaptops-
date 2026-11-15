#include "DisplayManager.h"
#include <algorithm>

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() = default;

void DisplayManager::Log(const std::wstring& msg) {
    if (m_logCallback) m_logCallback(msg);
}

bool DisplayManager::Initialize(LogCallback logCallback) {
    m_logCallback = logCallback;

    int w, h;
    if (GetCurrentResolution(&w, &h)) {
        m_currentWidth = w;
        m_currentHeight = h;
    }

    m_currentRefreshRate = GetCurrentRefreshRate();
    Log(L"Display initialized: " + std::to_wstring(m_currentWidth) + L"x" + std::to_wstring(m_currentHeight) + L"@" + std::to_wstring(m_currentRefreshRate) + L"Hz");
    return true;
}

bool DisplayManager::GetCurrentResolution(int* width, int* height) const {
    DEVMODE dm{};
    dm.dmSize = sizeof(DEVMODE);
    const wchar_t* pDev = m_displayDevice.empty() ? nullptr : m_displayDevice.c_str();
    if (EnumDisplaySettingsEx(pDev, ENUM_CURRENT_SETTINGS, &dm, 0)) {
        if (width) *width = dm.dmPelsWidth;
        if (height) *height = dm.dmPelsHeight;
        return true;
    }
    return false;
}

int DisplayManager::GetCurrentRefreshRate() const {
    DEVMODE dm{};
    dm.dmSize = sizeof(DEVMODE);
    const wchar_t* pDev = m_displayDevice.empty() ? nullptr : m_displayDevice.c_str();
    if (EnumDisplaySettingsEx(pDev, ENUM_CURRENT_SETTINGS, &dm, 0)) {
        return dm.dmDisplayFrequency;
    }
    return 60;
}

bool DisplayManager::EnumDisplayModes(std::vector<DisplayMode>& modes) const {
    modes.clear();
    DEVMODE dm{};
    dm.dmSize = sizeof(DEVMODE);
    const wchar_t* pDev = m_displayDevice.empty() ? nullptr : m_displayDevice.c_str();

    for (DWORD i = 0; EnumDisplaySettingsEx(pDev, i, &dm, 0); ++i) {
        DisplayMode mode;
        mode.width = dm.dmPelsWidth;
        mode.height = dm.dmPelsHeight;
        mode.refreshRateHz = dm.dmDisplayFrequency;
        mode.bitsPerPixel = dm.dmBitsPerPel;
        modes.push_back(mode);
    }
    return !modes.empty();
}

std::vector<DisplayMode> DisplayManager::GetAllDisplayModes() const {
    std::vector<DisplayMode> modes;
    EnumDisplayModes(modes);
    return modes;
}

std::vector<int> DisplayManager::GetAvailableRefreshRates(int width, int height) const {
    std::vector<int> rates;
    std::vector<DisplayMode> modes;
    EnumDisplayModes(modes);

    for (const auto& mode : modes) {
        if (mode.width == width && mode.height == height) {
            if (std::find(rates.begin(), rates.end(), mode.refreshRateHz) == rates.end()) {
                rates.push_back(mode.refreshRateHz);
            }
        }
    }

    std::sort(rates.begin(), rates.end());
    return rates;
}

int DisplayManager::GetMaxSupportedRefreshRate(int width, int height) const {
    auto rates = GetAvailableRefreshRates(width, height);
    if (rates.empty()) return 60;
    return rates.back();
}

int DisplayManager::GetMinSupportedRefreshRate(int width, int height) const {
    auto rates = GetAvailableRefreshRates(width, height);
    if (rates.empty()) return 60;
    return rates.front();
}

bool DisplayManager::SetRefreshRate(int refreshRateHz) {
    // Get current resolution to preserve it
    int width = m_currentWidth;
    int height = m_currentHeight;
    GetCurrentResolution(&width, &height);

    // Check if the requested refresh rate is available
    auto availableRates = GetAvailableRefreshRates(width, height);
    if (std::find(availableRates.begin(), availableRates.end(), refreshRateHz) == availableRates.end()) {
        Log(L"Refresh rate " + std::to_wstring(refreshRateHz) + L"Hz not supported for " +
            std::to_wstring(width) + L"x" + std::to_wstring(height));
        return false;
    }

    const wchar_t* pDev = m_displayDevice.empty() ? nullptr : m_displayDevice.c_str();

    DEVMODE dm{};
    dm.dmSize = sizeof(DEVMODE);
    if (!EnumDisplaySettingsEx(pDev, ENUM_CURRENT_SETTINGS, &dm, 0)) {
        Log(L"Failed to get current display settings");
        return false;
    }

    dm.dmFields = DM_DISPLAYFREQUENCY | DM_PELSWIDTH | DM_PELSHEIGHT;
    dm.dmDisplayFrequency = refreshRateHz;
    dm.dmPelsWidth = width;
    dm.dmPelsHeight = height;

    LONG result = ChangeDisplaySettingsEx(pDev, &dm, nullptr,
                                          CDS_UPDATEREGISTRY, nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        // Fallback: dynamic switch without registry write
        result = ChangeDisplaySettingsEx(pDev, &dm, nullptr, 0, nullptr);
    }

    if (result == DISP_CHANGE_SUCCESSFUL) {
        m_currentRefreshRate = refreshRateHz;
        Log(L"Refresh rate set to " + std::to_wstring(refreshRateHz) + L"Hz");
        return true;
    } else {
        Log(L"Failed to set refresh rate: " + std::to_wstring(result));
        return false;
    }
}

bool DisplayManager::ApplyRefreshRateForProfile(bool isBattery, int overrideRate) {
    int targetRate = overrideRate;
    if (targetRate == -1) {
        targetRate = isBattery ? 60 : 240;
    }

    // Try target rate first
    if (SetRefreshRate(targetRate)) {
        return true;
    }

    // Fallback logic
    int width = m_currentWidth;
    int height = m_currentHeight;
    GetCurrentResolution(&width, &height);

    if (isBattery) {
        // On battery: try to get lowest available rate (fallback to 60Hz)
        int minRate = GetMinSupportedRefreshRate(width, height);
        if (minRate != targetRate) {
            Log(L"Target rate not available, falling back to minimum: " + std::to_wstring(minRate) + L"Hz");
            return SetRefreshRate(minRate);
        }
    } else {
        // On AC: try to get highest available rate (fallback to max supported)
        int maxRate = GetMaxSupportedRefreshRate(width, height);
        if (maxRate != targetRate) {
            Log(L"Target rate not available, falling back to maximum: " + std::to_wstring(maxRate) + L"Hz");
            return SetRefreshRate(maxRate);
        }
    }

    return false;
}