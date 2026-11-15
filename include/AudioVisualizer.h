#pragma once

#include "Common.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>

// Forward declarations for Windows Core Audio COM interfaces
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioCaptureClient;
struct IAudioMeterInformation;

class AudioVisualizer {
public:
    AudioVisualizer();
    ~AudioVisualizer();

    bool Initialize();
    void Shutdown();

    bool Start();
    void Stop();
    bool IsRunning() const { return m_running; }

    // Instantaneous audio levels (0.0f to 1.0f)
    float GetPeakLevel() const { return m_peakLevel.load(); }
    void GetSpectrum(float& bass, float& mid, float& treble) const {
        bass = m_bass.load();
        mid = m_mid.load();
        treble = m_treble.load();
    }

    // Beat detection (kick drum / bass drop transient)
    bool IsBeatDetected() {
        return m_beatDetected.exchange(false);
    }

    void SetSensitivity(float sens) { m_sensitivity = std::clamp(sens, 0.2f, 3.0f); }
    float GetSensitivity() const { return m_sensitivity; }

private:
    void CaptureThreadFunc();
    void ProcessAudioSamples(const float* samples, size_t numSamples, int channels, int sampleRate);

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_stopRequested{ false };
    std::thread m_thread;

    std::atomic<float> m_peakLevel{ 0.0f };
    std::atomic<float> m_bass{ 0.0f };
    std::atomic<float> m_mid{ 0.0f };
    std::atomic<float> m_treble{ 0.0f };
    std::atomic<bool> m_beatDetected{ false };

    float m_sensitivity = 1.0f;
    float m_rollingBassAvg = 0.05f;
    DWORD m_lastBeatTime = 0;

    IMMDeviceEnumerator* m_pEnumerator = nullptr;
    IMMDevice* m_pDevice = nullptr;
    IAudioMeterInformation* m_pMeter = nullptr;
    IAudioClient* m_pAudioClient = nullptr;
    IAudioCaptureClient* m_pCaptureClient = nullptr;
};
