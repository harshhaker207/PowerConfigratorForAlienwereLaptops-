#pragma once

#include "Common.h"
#include <vector>
#include <map>
#include <string>
#include <memory>

class AudioVisualizer;
class ScreenAmbient;

// Target lighting zones on Alienware m16 R2
enum class LightingZone {
    All = 0,
    Keyboard = 1,
    Trackpad = 2,
    Logo = 3,
    PowerButton = 4
};

// LED modes/effects supported natively by AlienFX hardware & AWCC
enum class LedEffect {
    Static = 0,    // Solid static color (all keys 0..135)
    Breathing = 1, // Smooth pulse in/out
    Rainbow = 2,   // Full spectrum cycle
    Wave = 3,      // Color wave across keys/zones
    Pulse = 4,     // Rhythmic strobe / pulse
    Morph = 5,     // Transition between primary & secondary colors
    Scanner = 6,   // AWCC Night Rider / Scanner (V5 opcode 0x0A)
    Laser = 7,     // AWCC Laser Burst (V5 opcode 0x0B)
    NeonWave = 8,  // Cyberpunk Dual Neon Wave (Cyan + Magenta)
    Stealth = 9,   // Dim Stealth Night Mode
    Off = 10,      // Complete power off
    MusicVisualizer = 11, // Graphic Equalizer across keyboard
    MusicBeatPulse = 12,  // Bass Beat Flash
    MusicWave = 13,       // Audio Reactive Wave
    AmbientScreen = 14,   // Full Screen Ambient Ambilight
    AmbientMultiZone = 15 // Multi-Zone Screen Sync (Left/Center/Right/Top)
};

// LED color structure
struct LedColor {
    BYTE red = 0;
    BYTE green = 0;
    BYTE blue = 0;
    BYTE white = 0; // For RGBW LEDs

    LedColor() = default;
    LedColor(BYTE r, BYTE g, BYTE b, BYTE w = 0) : red(r), green(g), blue(b), white(w) {}
    LedColor(DWORD rgb) {
        red = (rgb >> 16) & 0xFF;
        green = (rgb >> 8) & 0xFF;
        blue = rgb & 0xFF;
    }
};

// Configuration per zone
struct ZoneConfig {
    LightingZone zone = LightingZone::All;
    LedEffect effect = LedEffect::Static;
    LedColor color = LedColor(0, 180, 255); // Default Alienware Cyan
    LedColor secondaryColor = LedColor(255, 0, 128); // Accent Magenta
    int brightness = 100; // 0-100%
    int speed = 7;        // 1-10
};

// LED zone state
struct LedZone {
    std::wstring name;
    bool isOn = false;
    LedColor color;
    int brightness = 0; // 0-100
};

class WindowsPowerManager;

class LedManager {
public:
    LedManager();
    ~LedManager();

    bool Initialize(LogCallback logCallback = nullptr, WindowsPowerManager* winPowerMgr = nullptr);
    void Shutdown();

    // Profile-based control
    bool ApplyBatteryMode();     // Turn OFF all LEDs
    bool ApplyPerformanceMode(); // Restore saved profile / full power
    bool TurnOnAllLeds(const LedColor& color = LedColor(0, 180, 255), LedEffect effect = LedEffect::Static);
    bool TurnOffAllLeds();

    // Multi-zone and advanced effects
    bool SetZoneEffect(LightingZone zone, LedEffect effect, const LedColor& color,
                       const LedColor& secColor = LedColor(0, 0, 0), int brightness = 100, int speed = 7);
    bool TurnOffZone(LightingZone zone);

    // Legacy zone compatibility
    bool SetZone(const std::wstring& zoneName, const LedColor& color, int brightness = 100);
    bool TurnOffZone(const std::wstring& zoneName);
    bool SetAllLeds(const LedColor& color, int brightness = 100, LedEffect effect = LedEffect::Static);

    // Zone management
    std::vector<std::wstring> GetAvailableZones() const { return m_zoneNames; }
    bool IsZoneAvailable(const std::wstring& zoneName) const;

    // Profile save/restore
    bool SaveCurrentProfile();
    bool RestoreSavedProfile();
    bool SaveCustomProfile(const std::wstring& name, const LedColor& color, int brightness, LedEffect effect);
    std::wstring GetActiveProfileName() const { return m_activeProfileName; }
    bool RunAlienFxCli(const std::wstring& args);

    // Status
    bool IsAvailable() const { return m_initialized; }
    bool IsAlienwareSystem() const { return m_isAlienware; }
    bool IsDellSystem() const { return m_isDell; }
    std::wstring GetStatusString() const;

    // HID control
    bool SendHidCommand(const std::vector<BYTE>& command);
    bool SetAlienFxDeviceState(bool on);

    // Low-level hardware drivers
    bool SetV4Zone(BYTE lightId, LedEffect effect, const LedColor& color, int brightness, int speed);
    bool SetV5Effect(LedEffect effect, const LedColor& color, const LedColor& secColor, int brightness, int speed);

    // AlienFX Tools & AWCC profile synchronization
    std::vector<std::wstring> FetchAlienFxToolsProfiles();
    bool SyncAlienFxTools();
    bool ApplyAlienFxToolsProfile(const std::wstring& profileName);
    std::vector<BYTE> GetV5KeyIds();

    // Dynamic Music & Ambient Screen Reactive Systems
    bool StartMusicReactive(LedEffect mode = LedEffect::MusicVisualizer);
    bool StartScreenAmbient(bool multiZone = false);
    void StopDynamicEffects();
    bool IsDynamicEffectActive() const { return m_activeDynamicEffect != LedEffect::Off; }
    LedEffect GetActiveDynamicEffect() const { return m_activeDynamicEffect; }
    void TickDynamicEffects();
    bool SetPerKeyColors(const std::vector<std::pair<BYTE, LedColor>>& keyColors);

    AudioVisualizer* GetAudioVisualizer() { return m_audioViz.get(); }
    ScreenAmbient* GetScreenAmbient() { return m_screenAmbient.get(); }

private:
    void Log(const std::wstring& msg);
    bool DetectAlienwareSystem();
    bool DetectDellSystem();
    bool LoadLightFxDll();
    void UnloadLightFxDll();
    bool SetAlienFxLed(const LedColor& color, int brightness);
    bool SetDellLed(const LedColor& color, int brightness);
    bool SetHidlLedControl(bool on);
    LedColor LoadSavedProfileOrDefault();

    bool EnsureDevicesOpen();
    void CloseDevices();
    bool TurnOffV4();
    bool TurnOffV5();
    bool SetColorV4(const LedColor& color, int brightness);
    bool SetColorV5(const LedColor& color, int brightness);

    bool m_initialized = false;
    bool m_isAlienware = false;
    bool m_isDell = false;

    // Direct HID device handles for AlienFX V4 (chassis) and V5 (keyboard)
    HANDLE m_hV4Device = INVALID_HANDLE_VALUE;
    HANDLE m_hV5Device = INVALID_HANDLE_VALUE;
    std::wstring m_v4Path;
    std::wstring m_v5Path;

    // LED zones
    std::map<std::wstring, LedZone> m_ledZones;
    std::vector<std::wstring> m_zoneNames;
    std::wstring m_activeProfileName;

    // Lighting profile (saved settings)
    struct LightingProfile {
        LedColor color;
        int brightness = 100;
        LedEffect effect = LedEffect::Static;
    };
    LightingProfile m_savedProfile;

    // Dell WMI support
    bool m_dellLedAvailable = false;

    // LightFX DLL
    HMODULE m_lightFxModule = nullptr;

    // LightFX API function pointers
    typedef int (*PFN_LFX_INITIALIZE)();
    typedef int (*PFN_LFX_RELEASE)();
    typedef int (*PFN_LFX_RESET)();
    typedef int (*PFN_LFX_UPDATE)();
    typedef int (*PFN_LFX_LIGHT)(unsigned int location, unsigned int color);
    struct LFX_COLOR_STRUCT {
        unsigned char red;
        unsigned char green;
        unsigned char blue;
        unsigned char brightness;
    };
    typedef int (*PFN_LFX_SETLIGHTCOLOR)(unsigned int devIndex, unsigned int lightIndex, LFX_COLOR_STRUCT* color);

    PFN_LFX_INITIALIZE m_fnLfxInit = nullptr;
    PFN_LFX_RELEASE m_fnLfxRelease = nullptr;
    PFN_LFX_RESET m_fnLfxReset = nullptr;
    PFN_LFX_UPDATE m_fnLfxUpdate = nullptr;
    PFN_LFX_LIGHT m_fnLfxLight = nullptr;
    PFN_LFX_SETLIGHTCOLOR m_fnLfxSetLightColor = nullptr;
    bool m_lightFxInitialized = false;

    // HID device handle
    HANDLE m_hidDevice = nullptr;

    LogCallback m_logCallback;
    WindowsPowerManager* m_winPowerMgr = nullptr;

    // Dynamic reactive subsystems
    std::unique_ptr<AudioVisualizer> m_audioViz;
    std::unique_ptr<ScreenAmbient> m_screenAmbient;
    LedEffect m_activeDynamicEffect = LedEffect::Off;
    float m_beatFlash = 0.0f;

    // File path for saved profiles
    std::wstring GetProfileStoragePath() const;
};
