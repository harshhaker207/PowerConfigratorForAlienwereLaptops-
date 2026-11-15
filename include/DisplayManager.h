#pragma once

#include "Common.h"
#include <vector>

struct DisplayMode {
    int width = 0;
    int height = 0;
    int refreshRateHz = 0;
    int bitsPerPixel = 32;
};

class DisplayManager {
public:
    DisplayManager();
    ~DisplayManager();

    bool Initialize(LogCallback logCallback = nullptr);

    // Refresh rate control
    bool SetRefreshRate(int refreshRateHz);
    int GetCurrentRefreshRate() const;
    std::vector<int> GetAvailableRefreshRates(int width, int height) const;
    int GetMaxSupportedRefreshRate(int width, int height) const;
    int GetMinSupportedRefreshRate(int width, int height) const;

    // Display info
    bool GetCurrentResolution(int* width, int* height) const;
    std::vector<DisplayMode> GetAllDisplayModes() const;
    std::wstring GetDisplayDeviceName() const { return m_displayDevice; }

    // Apply profile settings
    bool ApplyRefreshRateForProfile(bool isBattery, int overrideRate = -1);

private:
    void Log(const std::wstring& msg);
    bool EnumDisplayModes(std::vector<DisplayMode>& modes) const;
    HMONITOR GetPrimaryMonitor() const;
    bool GetMonitorInfo(HMONITOR monitor, MONITORINFOEX& info) const;

    std::wstring m_displayDevice = L""; // Empty means primary monitor
    LogCallback m_logCallback;
    int m_currentRefreshRate = 60;
    int m_currentWidth = 1920;
    int m_currentHeight = 1080;
};