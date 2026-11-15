#include "ScreenAmbient.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

ScreenAmbient::ScreenAmbient() = default;

ScreenAmbient::~ScreenAmbient() {
    Shutdown();
}

bool ScreenAmbient::Initialize() {
    return true;
}

void ScreenAmbient::Shutdown() {
    Stop();
}

bool ScreenAmbient::Start() {
    if (m_running.load()) return true;

    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread(&ScreenAmbient::CaptureThreadFunc, this);
    return true;
}

void ScreenAmbient::Stop() {
    if (!m_running.load()) return;

    m_stopRequested = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running = false;
}

AmbientColor ScreenAmbient::GetAverageColor() const {
    std::lock_guard<std::mutex> lock(m_colorMutex);
    return m_avgColor;
}

AmbientColor ScreenAmbient::GetLeftColor() const {
    std::lock_guard<std::mutex> lock(m_colorMutex);
    return m_leftColor;
}

AmbientColor ScreenAmbient::GetCenterColor() const {
    std::lock_guard<std::mutex> lock(m_colorMutex);
    return m_centerColor;
}

AmbientColor ScreenAmbient::GetRightColor() const {
    std::lock_guard<std::mutex> lock(m_colorMutex);
    return m_rightColor;
}

AmbientColor ScreenAmbient::GetTopColor() const {
    std::lock_guard<std::mutex> lock(m_colorMutex);
    return m_topColor;
}

AmbientColor ScreenAmbient::EnhanceVibrancy(BYTE r, BYTE g, BYTE b, float boost) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float maxC = (std::max)({ rf, gf, bf });
    float minC = (std::min)({ rf, gf, bf });
    float delta = maxC - minC;

    float h = 0.0f;
    float s = (maxC > 0.001f) ? (delta / maxC) : 0.0f;
    float v = maxC;

    if (delta > 0.0001f) {
        if (maxC == rf) {
            h = 60.0f * std::fmod(((gf - bf) / delta), 6.0f);
        } else if (maxC == gf) {
            h = 60.0f * (((bf - rf) / delta) + 2.0f);
        } else {
            h = 60.0f * (((rf - gf) / delta) + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;
    }

    // Boost saturation and elevate dark values for vivid LED output
    s = (std::min)(1.0f, s * boost);
    if (v < 0.20f) v = 0.20f; // Minimum visible threshold for RGB LEDs so they never stay dark

    // Convert back to RGB
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rOut = 0.0f, gOut = 0.0f, bOut = 0.0f;
    if (h < 60.0f) { rOut = c; gOut = x; bOut = 0; }
    else if (h < 120.0f) { rOut = x; gOut = c; bOut = 0; }
    else if (h < 180.0f) { rOut = 0; gOut = c; bOut = x; }
    else if (h < 240.0f) { rOut = 0; gOut = x; bOut = c; }
    else if (h < 300.0f) { rOut = x; gOut = 0; bOut = c; }
    else { rOut = c; gOut = 0; bOut = x; }

    BYTE finalR = static_cast<BYTE>(std::clamp(static_cast<int>((rOut + m) * 255.0f), 0, 255));
    BYTE finalG = static_cast<BYTE>(std::clamp(static_cast<int>((gOut + m) * 255.0f), 0, 255));
    BYTE finalB = static_cast<BYTE>(std::clamp(static_cast<int>((bOut + m) * 255.0f), 0, 255));

    return AmbientColor(finalR, finalG, finalB);
}

void ScreenAmbient::CaptureThreadFunc() {
    const int sampleW = 32;
    const int sampleH = 18;

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sampleW;
    bmi.bmiHeader.biHeight = -sampleH; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    SetStretchBltMode(hdcMem, HALFTONE);
    POINT ptOrg{};
    SetBrushOrgEx(hdcMem, 0, 0, &ptOrg);

    while (!m_stopRequested.load()) {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        if (screenW > 0 && screenH > 0) {
            // Re-verify hdcScreen
            if (!hdcScreen) {
                hdcScreen = GetDC(nullptr);
            }

            bool bltOk = false;
            if (pBits && hdcScreen) {
                // SRCCOPY | CAPTUREBLT (0x40CC0020) is required for modern DWM composited desktops
                bltOk = StretchBlt(hdcMem, 0, 0, sampleW, sampleH, hdcScreen, 0, 0, screenW, screenH, SRCCOPY | CAPTUREBLT) != FALSE;
            }

            unsigned long long totalR = 0, totalG = 0, totalB = 0;
            unsigned long long leftR = 0, leftG = 0, leftB = 0; int leftCount = 0;
            unsigned long long centerR = 0, centerG = 0, centerB = 0; int centerCount = 0;
            unsigned long long rightR = 0, rightG = 0, rightB = 0; int rightCount = 0;
            unsigned long long topR = 0, topG = 0, topB = 0; int topCount = 0;

            const DWORD* pixels = reinterpret_cast<const DWORD*>(pBits);
            if (bltOk && pixels) {
                for (int y = 0; y < sampleH; ++y) {
                    for (int x = 0; x < sampleW; ++x) {
                        DWORD p = pixels[y * sampleW + x];
                        BYTE b = p & 0xFF;
                        BYTE g = (p >> 8) & 0xFF;
                        BYTE r = (p >> 16) & 0xFF;

                        totalR += r; totalG += g; totalB += b;

                        if (x < 11) {
                            leftR += r; leftG += g; leftB += b; leftCount++;
                        } else if (x < 21) {
                            centerR += r; centerG += g; centerB += b; centerCount++;
                        } else {
                            rightR += r; rightG += g; rightB += b; rightCount++;
                        }

                        if (y < 6) {
                            topR += r; topG += g; topB += b; topCount++;
                        }
                    }
                }
            }

            // Fallback: If StretchBlt was blocked by DWM or returned all black, sample direct screen grid
            if (totalR == 0 && totalG == 0 && totalB == 0 && hdcScreen) {
                struct SamplePt { int x; int y; int zone; }; // 0: left, 1: center, 2: right, 3: top
                SamplePt pts[] = {
                    { screenW / 8, screenH / 4, 0 }, { screenW / 8, screenH / 2, 0 }, { screenW / 8, screenH * 3 / 4, 0 },
                    { screenW * 3 / 8, screenH / 2, 1 }, { screenW / 2, screenH / 2, 1 }, { screenW * 5 / 8, screenH / 2, 1 },
                    { screenW * 7 / 8, screenH / 4, 2 }, { screenW * 7 / 8, screenH / 2, 2 }, { screenW * 7 / 8, screenH * 3 / 4, 2 },
                    { screenW / 4, screenH / 8, 3 }, { screenW / 2, screenH / 8, 3 }, { screenW * 3 / 4, screenH / 8, 3 }
                };

                for (const auto& pt : pts) {
                    COLORREF cr = GetPixel(hdcScreen, pt.x, pt.y);
                    if (cr != CLR_INVALID) {
                        BYTE r = GetRValue(cr);
                        BYTE g = GetGValue(cr);
                        BYTE b = GetBValue(cr);
                        totalR += r; totalG += g; totalB += b;
                        if (pt.zone == 0) { leftR += r; leftG += g; leftB += b; leftCount++; }
                        else if (pt.zone == 1) { centerR += r; centerG += g; centerB += b; centerCount++; }
                        else if (pt.zone == 2) { rightR += r; rightG += g; rightB += b; rightCount++; }
                        if (pt.zone == 3 || pt.y < screenH / 4) { topR += r; topG += g; topB += b; topCount++; }
                    }
                }
            }

            int totalCount = leftCount + centerCount + rightCount;
            if (totalCount > 0) {
                AmbientColor rawAvg = EnhanceVibrancy(static_cast<BYTE>(totalR / (totalCount ? totalCount : 1)),
                                                      static_cast<BYTE>(totalG / (totalCount ? totalCount : 1)),
                                                      static_cast<BYTE>(totalB / (totalCount ? totalCount : 1)), m_vibrancy);

                AmbientColor rawLeft = (leftCount > 0) ?
                    EnhanceVibrancy(static_cast<BYTE>(leftR / leftCount), static_cast<BYTE>(leftG / leftCount), static_cast<BYTE>(leftB / leftCount), m_vibrancy) : rawAvg;

                AmbientColor rawCenter = (centerCount > 0) ?
                    EnhanceVibrancy(static_cast<BYTE>(centerR / centerCount), static_cast<BYTE>(centerG / centerCount), static_cast<BYTE>(centerB / centerCount), m_vibrancy) : rawAvg;

                AmbientColor rawRight = (rightCount > 0) ?
                    EnhanceVibrancy(static_cast<BYTE>(rightR / rightCount), static_cast<BYTE>(rightG / rightCount), static_cast<BYTE>(rightB / rightCount), m_vibrancy) : rawAvg;

                AmbientColor rawTop = (topCount > 0) ?
                    EnhanceVibrancy(static_cast<BYTE>(topR / topCount), static_cast<BYTE>(topG / topCount), static_cast<BYTE>(topB / topCount), m_vibrancy) : rawAvg;

                // Exponential smoothing for fluid cinematic transitions
                const float alpha = 0.35f;
                m_smoothR = m_smoothR * (1.0f - alpha) + rawAvg.r * alpha;
                m_smoothG = m_smoothG * (1.0f - alpha) + rawAvg.g * alpha;
                m_smoothB = m_smoothB * (1.0f - alpha) + rawAvg.b * alpha;

                m_leftR = m_leftR * (1.0f - alpha) + rawLeft.r * alpha;
                m_leftG = m_leftG * (1.0f - alpha) + rawLeft.g * alpha;
                m_leftB = m_leftB * (1.0f - alpha) + rawLeft.b * alpha;

                m_centerR = m_centerR * (1.0f - alpha) + rawCenter.r * alpha;
                m_centerG = m_centerG * (1.0f - alpha) + rawCenter.g * alpha;
                m_centerB = m_centerB * (1.0f - alpha) + rawCenter.b * alpha;

                m_rightR = m_rightR * (1.0f - alpha) + rawRight.r * alpha;
                m_rightG = m_rightG * (1.0f - alpha) + rawRight.g * alpha;
                m_rightB = m_rightB * (1.0f - alpha) + rawRight.b * alpha;

                m_topR = m_topR * (1.0f - alpha) + rawTop.r * alpha;
                m_topG = m_topG * (1.0f - alpha) + rawTop.g * alpha;
                m_topB = m_topB * (1.0f - alpha) + rawTop.b * alpha;

                {
                    std::lock_guard<std::mutex> lock(m_colorMutex);
                    m_avgColor = AmbientColor(static_cast<BYTE>(m_smoothR), static_cast<BYTE>(m_smoothG), static_cast<BYTE>(m_smoothB));
                    m_leftColor = AmbientColor(static_cast<BYTE>(m_leftR), static_cast<BYTE>(m_leftG), static_cast<BYTE>(m_leftB));
                    m_centerColor = AmbientColor(static_cast<BYTE>(m_centerR), static_cast<BYTE>(m_centerG), static_cast<BYTE>(m_centerB));
                    m_rightColor = AmbientColor(static_cast<BYTE>(m_rightR), static_cast<BYTE>(m_rightG), static_cast<BYTE>(m_rightB));
                    m_topColor = AmbientColor(static_cast<BYTE>(m_topR), static_cast<BYTE>(m_topG), static_cast<BYTE>(m_topB));
                }
            }
        }

        Sleep(33); // ~30 FPS screen sampling
    }

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
}
