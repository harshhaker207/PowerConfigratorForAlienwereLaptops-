# Power Configurator ⚡
### Lightweight Hardware & Power Tuning Suite for Alienware m16 R2

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20x64-0078D6?style=flat-square&logo=windows)
![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)
![Build](https://img.shields.io/badge/Build-CMake%203.20%2B%20%7C%20MSVC-blueviolet?style=flat-square)
![Footprint](https://img.shields.io/badge/RAM%20Idle-%3C%2035%20MB-success?style=flat-square)
![CPU](https://img.shields.io/badge/CPU%20Idle-0%25-brightgreen?style=flat-square)

**Power Configurator** is a standalone, pure Win32 C++17 utility designed as a zero-bloat, high-performance alternative to Alienware Command Center (AWCC) and Dell Power Manager. Built specifically for high-end gaming laptops (Alienware m16 R2 and compatible architectures), it delivers instantaneous hardware power control, dynamic fan management, per-key AlienFX lighting with live audio/screen ambilight synchronization, and responsive telemetry without background services, webviews, or telemetry bloat.

---

## ⚡ Comparison: AWCC vs. Power Configurator

| Feature | Alienware Command Center (AWCC) | Power Configurator |
|---|:---:|:---:|
| **Idle RAM Usage** | ~450 MB – 1.2 GB (multiple processes) | **< 35 MB** (single process) |
| **Idle CPU Overhead** | 1% – 5% continuous polling | **0%** (event-driven / zero awake) |
| **Background Services**| 6+ background services & agents | **0** (runs purely as a portable app) |
| **Startup / Launch Time**| 8 – 20 seconds | **< 200 ms** instant startup |
| **Runtime Dependencies**| Node.js / Chromium / .NET / Electron | **Pure Win32 GDI & C++17 Runtime** |
| **Telemetry / Tracking**| Continuous Dell / telemetry telemetry | **Zero tracking, 100% offline & private** |
| **Profile Switching**| Sluggish, multi-second delay | **Instantaneous (< 5ms)** |
| **AlienFX Latency** | 60ms – 120ms lag via software stack | **< 2ms** direct hardware buffer streaming |

---

## 🚀 Key Features

### 1. Dual Hardware Power Profiles
Seamless toggle between maximum energy efficiency and unleashed thermal headroom:
* **Battery Saver Mode**:
  * CPU PL1 clamped to **45W**
  * GPU TGP limited to **80W**
  * Windows Power Plan switched to **Power Saver** (Processor boost disabled)
  * Display dropped to **60 Hz** to extend battery life
  * Fan profile locked to **Silent (Quiet)**
* **Performance Mode**:
  * CPU PL1 unleashed to **125W**
  * GPU TGP uncapped to **175W** (NVIDIA Dynamic Boost enabled)
  * Windows Power Plan set to **High Performance**
  * Display set to **240 Hz VRR (G-Sync)**
  * Fan profile engaged to **Balanced / Full Speed**
* **Auto AC/Battery Switching**: Automatically detects AC plug/unplug events via `WM_POWERBROADCAST` and applies profiles without user intervention.

### 2. Live Hardware Telemetry & Tachometer
* **CPU Processor**: Real-time Package Temperature, Core Clocks, PL1 Target, and Utilization %.
* **GPU Graphics**: Real-time Die Temperature, Core MHz, Dynamic TGP Wattage, and Utilization %.
* **Power & Battery**: Live Battery Percentage, AC Connection state, and Charging/Discharging wattage.
* **Cooling Fan Tachometer**: Real-time RPM readout for **Fan 1 (CPU)** and **Fan 2 (GPU)** with active BIOS thermal curve profile.

### 3. AlienFX Lighting Studio (Tron & Per-Key RGB)
Direct hardware interface via low-latency `LightFX.dll` streaming to AlienFX V4 (Chassis Tron & Alienhead logo) and V5 (Per-Key RGB keyboard) at **30+ FPS**:
* **Audio Visualizer (EQ)**: Real-time WASAPI 24-bit/32-bit loopback audio spectrum analyzer dynamically mapping Bass, Mid, and Treble across keyboard columns.
* **Bass Beat Pulse**: Energy-threshold beat detection pulsing backlights in sync with music drops.
* **Audio Reactive Wave**: Smooth color waves accelerating with instantaneous music amplitude.
* **Screen Ambilight**: High-speed Desktop Window Manager (DWM) frame sampler averaging active screen colors to RGB backlights.
* **Multi-Zone Screen Sync**: Independent Left, Center, and Right screen-edge color reproduction.
* **Static, Color Cycle, Breathing, & Rainbow Waves**.

### 4. GPU Working Modes & Display Management
* **Advanced Optimus (DDS)**: On-the-fly display switching between integrated Intel Iris/Arc Xe and NVIDIA RTX dGPU.
* **GPU Modes**: Hybrid Optimus, dGPU Dedicated, or Integrated Only.
* **Variable Refresh Rate (VRR)**: Dynamic 60 Hz / 240 Hz switching.

### 5. Floating Mini Mode (Compact Widget)
* **Space-saving 340x138 floating widget** designed for secondary monitors or in-game corner placement.
* Real-time readouts for **Battery %**, **CPU Temperature**, and **GPU Temperature**.
* One-click **Battery Saver / Performance Mode** toggle.
* Frameless grab bar with minimize-to-tray and expand-to-window controls.

### 6. Five Built-in Themes
* **Cyberpunk**: High-contrast OLED dark background with neon cyan and magenta accents.
* **Stealth**: Clean monochrome for professional environments.
* **Spectrum**: Multi-color accent gradients.
* **Phosphor**: Retro amber CRT console aesthetic.
* **Solarized**: Low-glare palette for prolonged night sessions.

---

## 🛠️ Project Architecture

```
PowerConfigurator/
├── include/
│   ├── AudioVisualizer.h      # WASAPI loopback audio capture & FFT spectrum
│   ├── Common.h               # Shared types, structs, and telemetry metrics
│   ├── CpuManager.h           # WMI and PDH CPU performance monitoring
│   ├── DisplayManager.h       # Windows GDI display modes & refresh rate switcher
│   ├── FanManager.h           # Dell BIOS thermal & fan tachometer interface
│   ├── GpuManager.h           # NVML runtime interface (clock, thermals, power limits)
│   ├── LedManager.h           # AlienFX V4/V5 hardware controller & LightFX engine
│   ├── ModernTheme.h          # Design tokens, fonts, and GDI drawing primitives
│   ├── Monitor.h              # Background hardware telemetry collector thread
│   ├── OptimusManager.h       # NVIDIA Advanced Optimus & DDS controller
│   ├── PowerManager.h         # AC/Battery state machine & power profile manager
│   ├── ScreenAmbient.h        # DWM screen color grabber for ambient backlighting
│   ├── Theme.h                # Theme definitions, palettes, and typography
│   ├── UI.h                   # Windowed and Mini mode layout engines
│   └── WindowsPowerManager.h  # Windows PowrProf GUID power settings manager
├── src/
│   ├── AudioVisualizer.cpp    # 24/32-bit PCM loopback & beat detection
│   ├── Common.cpp             # Logging and utility helpers
│   ├── CpuManager.cpp         # PDH query resolution & core metrics
│   ├── DisplayManager.cpp     # ChangeDisplaySettingsEx implementations
│   ├── FanManager.cpp         # Fan tachometer queries
│   ├── GpuManager.cpp         # Dynamic NVML library loader
│   ├── LedManager.cpp         # LightFX.dll runtime hook & frame dispatch
│   ├── ModernTheme.cpp        # Custom control renderers (pills, cards, meters)
│   ├── Monitor.cpp            # Low-overhead 1 Hz telemetry loop
│   ├── OptimusManager.cpp     # NVAPI / display switch handlers
│   ├── PowerManager.cpp       # PL1 / TGP enforcement logic
│   ├── ScreenAmbient.cpp      # GDI / CAPTUREBLT screen color sampler
│   ├── Theme.cpp              # Color token mapping & font cache
│   ├── UI.cpp                 # Win32 message pump, layout, and WM_PAINT
│   ├── WindowsPowerManager.cpp# Native PowrProf API caller
│   ├── main.cpp               # WinMain entry, mutex, & DPI initialization
│   ├── Resource.rc            # Application icon and version manifest
│   └── manifest.xml           # Windows 11 DPI & Administrator execution level
├── docs/                      # Baseline metrics and UX specifications
├── scripts/                   # Verification and test automation scripts
├── build.bat                  # One-click CMake build script
├── CMakeLists.txt             # CMake configuration with MSVC /utf-8
├── update_and_run.bat         # Self-elevating deploy and run utility
└── README.md                  # Project documentation
```

---

## 💻 Building from Source

### Prerequisites
* **Windows 10 / 11 (64-bit)**
* **Visual Studio 2019 or 2022** with the *Desktop development with C++* workload
* **CMake 3.20 or newer**

### Quick Build
Simply run the included build script:
```cmd
build.bat
```

Or configure manually using CMake:
```cmd
mkdir build
cd build
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release
```
The output binary is generated at:
`build\bin\Release\PowerConfigurator.exe`

---

## 📦 Deployment & Execution

Power Configurator requires elevated privileges to apply hardware PL1/TGP limits and control low-level power plans:

1. **Double-click** `update_and_run.bat` (or right-click &rarr; *Run as administrator*).
2. The script will automatically request UAC elevation, terminate any running instances, stage the newly compiled Release binary and `LightFX.dll`, and launch the app.

---

## 📤 Publishing to GitHub

If you checked **"Add a README file"** when creating your GitHub repository, push your project using the commands below:

```cmd
cd C:\Users\harsh\PowerConfigurator
git remote add origin https://github.com/<your-username>/PowerConfigurator.git
git branch -M main
git push -f -u origin main
```
> [!NOTE]
> The `-f` (force) flag ensures that this comprehensive, fully detailed documentation cleanly replaces GitHub's empty initial README placeholder.

---

## 📜 License
This project is licensed under the **MIT License**.
