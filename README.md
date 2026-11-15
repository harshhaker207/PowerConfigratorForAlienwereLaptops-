# Power Configurator ⚡
### Lightweight Hardware & Power Tuning Suite for Alienware Laptops

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B)
![Platform](https://img.shields.io/badge/Platform-Windows%2011%20x64-0078D6?style=flat-square&logo=windows)
![License](https://img.shields.io/badge/License-MIT-green?style=flat-square)
![Build](https://img.shields.io/badge/Build-CMake%203.20%2B%20%7C%20MSVC-blueviolet?style=flat-square)
![Footprint](https://img.shields.io/badge/RAM%20Idle-%3C%2035%20MB-success?style=flat-square)
![CPU](https://img.shields.io/badge/CPU%20Idle-0%25-brightgreen?style=flat-square)

**Power Configurator** is a native Win32 C++17 utility designed as a zero-bloat, instantaneous alternative to Alienware Command Center (AWCC) and Dell Power Manager. Built specifically for Alienware m16 R2 and compatible gaming laptops, it delivers hardware power tuning, dynamic cooling controls, per-key AlienFX lighting with live audio visualizer and screen ambilight, and real-time telemetry — with 0% idle CPU and no background telemetry services.

---

## ⚡ AWCC vs. Power Configurator

| Feature | Alienware Command Center (AWCC) | Power Configurator |
|---|:---:|:---:|
| **Memory Footprint** | 450 MB – 1.2 GB (multiple background processes) | **< 35 MB** (single lightweight process) |
| **Idle CPU Overhead** | 1% – 5% continuous polling | **0%** (zero wakeups when idle) |
| **Background Services** | 6+ persistent services & background daemons | **0** (pure portable native app) |
| **Launch Time** | 8 – 20 seconds | **Instantaneous (< 200 ms)** |
| **Frameworks** | Electron / Chromium / Node.js / .NET | **Pure Win32 C++17 & GDI** |
| **Privacy** | Continuous telemetry collection | **100% Offline & Private** |
| **AlienFX Latency** | 60ms – 120ms lag through software stack | **< 2ms** direct hardware buffer streaming |

---

## 🚀 Features

### 1. Dual Hardware Power Profiles
* **Battery Saver Mode**:
  * CPU PL1 clamped to **45W**
  * GPU TGP limited to **80W**
  * Windows Power Plan set to **Power Saver**
  * Display set to battery-friendly **60 Hz**
  * Cooling fan profile set to **Silent**
* **Performance Mode**:
  * CPU PL1 unleashed to **125W**
  * GPU TGP uncapped to **175W** (Dynamic Boost)
  * Windows Power Plan set to **High Performance**
  * Display set to **240 Hz VRR (G-Sync)**
  * Cooling fan profile set to **Balanced / Full Speed**
* **Auto AC / Battery Switching**: Detects AC power plug/unplug events instantly and switches profiles automatically.

### 2. Live Hardware Telemetry
* **CPU**: Temperature, Core Clocks, PL1 Wattage Target, Utilization %.
* **GPU**: Temperature, Core Clock MHz, TGP Power Draw / Limit, Utilization %.
* **Battery & Power**: Real-time percentage, AC status, and charging rate.
* **Cooling Fan Tachometer**: Real-time RPM readout for **Fan 1 (CPU)** and **Fan 2 (GPU)** with active BIOS thermal curve.

### 3. AlienFX Lighting Studio (Tron & Per-Key RGB)
High-performance 30+ FPS hardware lighting with low latency:
* **Audio Visualizer (EQ)**: Real-time WASAPI 24-bit/32-bit loopback audio spectrum analyzer splitting keyboard into Bass, Mid, and Treble columns.
* **Bass Beat Pulse**: Dynamic energy beat detection pulsing backlights in sync with music drops.
* **Audio Reactive Wave**: Smooth color waves accelerating with instantaneous music amplitude.
* **Screen Ambilight**: High-speed desktop screen color sampler matching your backlights to displayed content.
* **Multi-Zone Screen Sync**: Independent Left, Center, and Right screen-edge color reproduction.
* **Static, Color Cycle, Breathing, & Rainbow Waves**.

### 4. GPU Working Modes & Display Control
* **Advanced Optimus (DDS)**: Direct display switching between integrated Intel Iris/Arc graphics and NVIDIA RTX dGPU.
* **Modes**: Hybrid Optimus, dGPU Dedicated, or Integrated Only.
* **Display Refresh Rate**: Dynamic 60 Hz / 240 Hz switching.

### 5. Floating Mini Mode (Compact Widget)
* Compact 340x138 widget for secondary monitors or in-game corners.
* Real-time readouts for **Battery %**, **CPU Temperature**, and **GPU Temperature**.
* One-click **Battery Saver / Performance Mode** toggle.
* Minimizes cleanly to system tray with right-click quick controls.

### 6. Five Built-in Themes
* **Cyberpunk**: High-contrast OLED dark background with neon cyan and magenta accents.
* **Stealth**: Clean monochrome for professional environments.
* **Spectrum**: Vibrant rainbow accent gradients.
* **Phosphor**: Classic amber CRT console aesthetic.
* **Solarized**: Low-glare palette for night sessions.

---

## 🏃 How to Run

> **Note**: Administrator privileges are required to adjust hardware PL1 limits, NVML GPU wattage, and low-level power plans.

### Quick Start
1. Download or compile the latest release.
2. Ensure `LightFX.dll` is present in the same folder as `PowerConfigurator.exe`.
3. Right-click **`update_and_run.bat`** &rarr; **Run as administrator** (or double-click it; it will automatically request elevation via UAC).
4. Power Configurator will start, detect your hardware, and display live telemetry.

### Using the App
* **Switching Modes**: Click the **Battery Saver** or **Performance Mode** pill at the top.
* **Mini Mode**: Click the **`MINI`** button in the upper-right corner to collapse into a floating widget. Click **`EXPAND`** to restore the full dashboard.
* **System Tray**: Closing the window minimizes the app to your Windows system tray. Right-click the tray icon to quickly toggle profiles, switch themes, or exit.

---

## 💻 Building from Source

### Prerequisites
* Windows 10 / 11 (64-bit)
* Visual Studio 2019 or 2022 with *Desktop development with C++*
* CMake 3.20+

### Build Commands
```cmd
build.bat
```
Or with CMake:
```cmd
mkdir build
cd build
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release
```
Output binary: `build\bin\Release\PowerConfigurator.exe`

---

## 🙏 Acknowledgements & Credits

A huge thank you to the open-source projects and communities that made Power Configurator possible:

* **[AlienFX-Tools](https://github.com/T-Troll/alienfx-tools)** by **[T-Troll](https://github.com/T-Troll)**:
  * Special credit and sincere thanks to the AlienFX-Tools project. Their dedicated reverse-engineering of Dell/Alienware proprietary USB-HID communication protocols and the `LightFX.dll` interface made low-latency, native hardware RGB lighting possible without running Alienware Command Center.
* **NVIDIA Management Library (NVML)**: For GPU clock, thermal, and power control APIs.
* **Microsoft Windows Audio Session API (WASAPI)**: For low-latency loopback audio capture powering the music visualizer.

---

## 📜 License
This project is licensed under the **MIT License**.
