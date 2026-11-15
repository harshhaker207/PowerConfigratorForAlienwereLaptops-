#include "AudioVisualizer.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ole32.lib")

// Biquad filter state for real-time DSP frequency band splitting
struct BiquadFilter {
    float a0, a1, a2, b1, b2;
    float z1 = 0.0f, z2 = 0.0f;

    void SetLowpass(float cutoff, float sampleRate, float q = 0.707f) {
        float w0 = 2.0f * 3.14159265f * cutoff / sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float cosw0 = std::cos(w0);
        float b0 = (1.0f - cosw0) / 2.0f;
        a0 = b0 / (1.0f + alpha);
        a1 = (1.0f - cosw0) / (1.0f + alpha);
        a2 = b0 / (1.0f + alpha);
        b1 = (-2.0f * cosw0) / (1.0f + alpha);
        b2 = (1.0f - alpha) / (1.0f + alpha);
    }

    void SetHighpass(float cutoff, float sampleRate, float q = 0.707f) {
        float w0 = 2.0f * 3.14159265f * cutoff / sampleRate;
        float alpha = std::sin(w0) / (2.0f * q);
        float cosw0 = std::cos(w0);
        float b0 = (1.0f + cosw0) / 2.0f;
        a0 = b0 / (1.0f + alpha);
        a1 = -(1.0f + cosw0) / (1.0f + alpha);
        a2 = b0 / (1.0f + alpha);
        b1 = (-2.0f * cosw0) / (1.0f + alpha);
        b2 = (1.0f - alpha) / (1.0f + alpha);
    }

    float Process(float in) {
        float out = a0 * in + z1;
        z1 = a1 * in - b1 * out + z2;
        z2 = a2 * in - b2 * out;
        return out;
    }
};

AudioVisualizer::AudioVisualizer() = default;

AudioVisualizer::~AudioVisualizer() {
    Shutdown();
}

bool AudioVisualizer::Initialize() {
    return true;
}

void AudioVisualizer::Shutdown() {
    Stop();
}

bool AudioVisualizer::Start() {
    if (m_running.load()) return true;

    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread(&AudioVisualizer::CaptureThreadFunc, this);
    return true;
}

void AudioVisualizer::Stop() {
    if (!m_running.load()) return;

    m_stopRequested = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running = false;
    m_peakLevel = 0.0f;
    m_bass = 0.0f;
    m_mid = 0.0f;
    m_treble = 0.0f;
}

void AudioVisualizer::CaptureThreadFunc() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioMeterInformation* pMeter = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCapture = nullptr;
    WAVEFORMATEX* pwfx = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);

    if (SUCCEEDED(hr) && pEnumerator) {
        pEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice);
    }

    if (pDevice) {
        // Meter information (always works as a high-precision hardware peak meter)
        pDevice->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&pMeter);

        // Loopback client for frequency analysis
        if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient))) {
            if (SUCCEEDED(pAudioClient->GetMixFormat(&pwfx))) {
                // Initialize in loopback mode
                REFERENCE_TIME hnsRequestedDuration = 1000000; // 100 ms
                hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK,
                                              hnsRequestedDuration, 0, pwfx, nullptr);
                if (SUCCEEDED(hr)) {
                    pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCapture);
                    pAudioClient->Start();
                }
            }
        }
    }

    BiquadFilter bassFilter;
    BiquadFilter trebleFilter;
    float sampleRate = (pwfx && pwfx->nSamplesPerSec > 0) ? static_cast<float>(pwfx->nSamplesPerSec) : 48000.0f;
    bassFilter.SetLowpass(250.0f, sampleRate);
    trebleFilter.SetHighpass(4000.0f, sampleRate);

    while (!m_stopRequested.load()) {
        float peakVal = 0.0f;
        if (pMeter) {
            pMeter->GetPeakValue(&peakVal);
        }

        if (pCapture && pwfx) {
            UINT32 packetLength = 0;
            pCapture->GetNextPacketSize(&packetLength);

            float totalBass = 0.0f;
            float totalTreble = 0.0f;
            float totalMid = 0.0f;
            int sampleCount = 0;

            while (packetLength != 0) {
                BYTE* pData = nullptr;
                UINT32 numFramesAvailable = 0;
                DWORD flags = 0;

                hr = pCapture->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
                if (SUCCEEDED(hr) && pData && numFramesAvailable > 0) {
                    int channels = pwfx->nChannels;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        // Silent
                    } else if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                              (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                               reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
                        const float* samples = reinterpret_cast<const float*>(pData);
                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            float s = samples[i * channels]; // Left / Mono
                            float sBass = bassFilter.Process(s);
                            float sTreb = trebleFilter.Process(s);
                            float sMid = s - (sBass + sTreb);

                            totalBass += sBass * sBass;
                            totalMid += sMid * sMid;
                            totalTreble += sTreb * sTreb;
                            sampleCount++;
                        }
                    } else if (pwfx->wBitsPerSample == 16) {
                        const int16_t* samples = reinterpret_cast<const int16_t*>(pData);
                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            float s = samples[i * channels] / 32768.0f;
                            float sBass = bassFilter.Process(s);
                            float sTreb = trebleFilter.Process(s);
                            float sMid = s - (sBass + sTreb);

                            totalBass += sBass * sBass;
                            totalMid += sMid * sMid;
                            totalTreble += sTreb * sTreb;
                            sampleCount++;
                        }
                    } else if (pwfx->wBitsPerSample == 24) {
                        const BYTE* samples = pData;
                        int bytesPerFrame = pwfx->nBlockAlign;
                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            int32_t sample24 = (samples[i * bytesPerFrame + 2] << 16) |
                                               (samples[i * bytesPerFrame + 1] << 8) |
                                               (samples[i * bytesPerFrame]);
                            if (sample24 & 0x800000) sample24 |= 0xFF000000;
                            float s = sample24 / 8388608.0f;
                            float sBass = bassFilter.Process(s);
                            float sTreb = trebleFilter.Process(s);
                            float sMid = s - (sBass + sTreb);

                            totalBass += sBass * sBass;
                            totalMid += sMid * sMid;
                            totalTreble += sTreb * sTreb;
                            sampleCount++;
                        }
                    } else if (pwfx->wBitsPerSample == 32) {
                        const int32_t* samples = reinterpret_cast<const int32_t*>(pData);
                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            float s = samples[i * channels] / 2147483648.0f;
                            float sBass = bassFilter.Process(s);
                            float sTreb = trebleFilter.Process(s);
                            float sMid = s - (sBass + sTreb);

                            totalBass += sBass * sBass;
                            totalMid += sMid * sMid;
                            totalTreble += sTreb * sTreb;
                            sampleCount++;
                        }
                    }
                    pCapture->ReleaseBuffer(numFramesAvailable);
                }
                pCapture->GetNextPacketSize(&packetLength);
            }

            if (sampleCount > 0) {
                float rmsBass = std::sqrt(totalBass / sampleCount) * 4.0f * m_sensitivity;
                float rmsMid = std::sqrt(totalMid / sampleCount) * 4.0f * m_sensitivity;
                float rmsTreble = std::sqrt(totalTreble / sampleCount) * 4.0f * m_sensitivity;

                float b = (std::min)(1.0f, (std::max)(rmsBass, peakVal * 0.9f));
                float m = (std::min)(1.0f, (std::max)(rmsMid, peakVal * 0.7f));
                float t = (std::min)(1.0f, (std::max)(rmsTreble, peakVal * 0.5f));

                // Smooth interpolation
                m_bass = m_bass.load() * 0.35f + b * 0.65f;
                m_mid = m_mid.load() * 0.35f + m * 0.65f;
                m_treble = m_treble.load() * 0.35f + t * 0.65f;

                // Beat detection
                m_rollingBassAvg = m_rollingBassAvg * 0.90f + b * 0.10f;
                DWORD now = GetTickCount();
                if (b > 0.15f && b > m_rollingBassAvg * 1.25f && (now - m_lastBeatTime > 130)) {
                    m_beatDetected = true;
                    m_lastBeatTime = now;
                }
            } else if (peakVal > 0.005f) {
                // Synthesize from hardware peak meter when packets are between buffers
                float scaledPeak = (std::min)(1.0f, peakVal * m_sensitivity);
                float b = scaledPeak * 1.05f;
                float m = scaledPeak * 0.85f;
                float t = scaledPeak * 0.65f;

                m_bass = m_bass.load() * 0.35f + b * 0.65f;
                m_mid = m_mid.load() * 0.35f + m * 0.65f;
                m_treble = m_treble.load() * 0.35f + t * 0.65f;

                m_rollingBassAvg = m_rollingBassAvg * 0.90f + b * 0.10f;
                DWORD now = GetTickCount();
                if (b > 0.15f && b > m_rollingBassAvg * 1.25f && (now - m_lastBeatTime > 130)) {
                    m_beatDetected = true;
                    m_lastBeatTime = now;
                }
            } else {
                // Decay smoothly when no sound
                m_bass = m_bass.load() * 0.88f;
                m_mid = m_mid.load() * 0.88f;
                m_treble = m_treble.load() * 0.88f;
            }
        } else {
            // Fallback when only IAudioMeterInformation is available
            float scaledPeak = (std::min)(1.0f, peakVal * m_sensitivity);
            m_bass = scaledPeak * 1.1f;
            m_mid = scaledPeak * 0.8f;
            m_treble = scaledPeak * 0.6f;

            m_rollingBassAvg = m_rollingBassAvg * 0.90f + scaledPeak * 0.10f;
            DWORD now = GetTickCount();
            if (scaledPeak > 0.20f && scaledPeak > m_rollingBassAvg * 1.25f && (now - m_lastBeatTime > 130)) {
                m_beatDetected = true;
                m_lastBeatTime = now;
            }
        }

        m_peakLevel = (std::max)(peakVal * m_sensitivity, m_bass.load());
        Sleep(20); // ~50 updates per second
    }

    if (pCapture) pCapture->Release();
    if (pAudioClient) {
        pAudioClient->Stop();
        pAudioClient->Release();
    }
    if (pwfx) CoTaskMemFree(pwfx);
    if (pMeter) pMeter->Release();
    if (pDevice) pDevice->Release();
    if (pEnumerator) pEnumerator->Release();

    if (coInitialized) {
        CoUninitialize();
    }
}
