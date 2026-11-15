#pragma once

#include "Common.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <algorithm>

struct AmbientColor {
    BYTE r = 0;
    BYTE g = 180;
    BYTE b = 255;

    AmbientColor() = default;
    AmbientColor(BYTE _r, BYTE _g, BYTE _b) : r(_r), g(_g), b(_b) {}
};

class ScreenAmbient {
public:
    ScreenAmbient();
    ~ScreenAmbient();

    bool Initialize();
    void Shutdown();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    AmbientColor GetAverageColor() const;
    AmbientColor GetLeftColor() const;
    AmbientColor GetCenterColor() const;
    AmbientColor GetRightColor() const;
    AmbientColor GetTopColor() const;

    void SetVibrancy(float boost) { m_vibrancy = std::clamp(boost, 1.0f, 2.5f); }
    float GetVibrancy() const { return m_vibrancy; }

private:
    void CaptureThreadFunc();
    static AmbientColor EnhanceVibrancy(BYTE r, BYTE g, BYTE b, float boost);

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_thread;

    mutable std::mutex m_colorMutex;
    AmbientColor m_avgColor{ 0, 180, 255 };
    AmbientColor m_leftColor{ 0, 180, 255 };
    AmbientColor m_centerColor{ 0, 180, 255 };
    AmbientColor m_rightColor{ 0, 180, 255 };
    AmbientColor m_topColor{ 0, 180, 255 };

    float m_smoothR = 0.0f, m_smoothG = 180.0f, m_smoothB = 255.0f;
    float m_leftR = 0.0f, m_leftG = 180.0f, m_leftB = 255.0f;
    float m_centerR = 0.0f, m_centerG = 180.0f, m_centerB = 255.0f;
    float m_rightR = 0.0f, m_rightG = 180.0f, m_rightB = 255.0f;
    float m_topR = 0.0f, m_topG = 180.0f, m_topB = 255.0f;

    float m_vibrancy = 1.35f;
};
