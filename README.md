# Power Configurator (Alienware m16 R2 Edition)

Lightweight, high-performance Windows System Power & Hardware Configurator — pure native Win32 replacement for Alienware Command Center (AWCC) and Dell Power Manager.

**Key Constraints & Performance:**
- **Zero bloat**: No background telemetry services, no web views, pure native Win32/C++17.
- **Ultra-lightweight**: < 5 MB footprint, < 35 MB RAM idle, 0% CPU at rest.
- **Instant response**: Sub-millisecond hardware telemetry and profile switching.

---

## Features

### 1. Hardware Power Profiles
| Setting | Battery Saver | Performance Mode |
|---|---|---|
| **CPU PL1 Limit** | 45W | 125W |
| **GPU Power Limit** | 80W | 175W |
| **Windows Power Plan** | Power Saver | High Performance |
| **Power Mode** | Best Power Efficiency | Best Performance |
| **Display Refresh Rate**| 60 Hz | 240 Hz VRR (G-Sync) |
| **System Cooling** | Silent (Quiet) | Balanced / Full Speed |
| **AlienFX Lighting** | Eco / Dimmed | Cyberpunk / Reactive |

### 2. Live Hardware Telemetry
- **CPU**: Real-time Package Temp, Clock Speed, PL1 Power, Usage %.
- **GPU**: Real-time Die Temp, Clock MHz, TGP Power Draw / Limit, Usage %.
- **Power**: Battery %, AC vs Battery status, Charging / Discharging rate.
- **Cooling Tachometer**: Real-time Fan 1 (CPU) and Fan 2 (GPU) RPM with active BIOS profile.

### 3. AlienFX Lighting Studio
- **Native Hardware Streaming**: Direct `LightFX.dll` interface communicating with AlienFX V4 (Tron/Chassis/Logo) and V5 (Per-Key RGB Keyboard) at 30+ FPS with < 2ms latency and zero CPU spikes.
- **Dynamic Lighting Modes**:
  - **Audio Visualizer EQ**: Real-time WASAPI 24-bit/32-bit loopback spectrum analyzer splitting keyboard into Bass, Mid, and Treble zones.
  - **Bass Beat Pulse**: Dynamic energy beat detection pulsing chassis and key backlights.
  - **Audio Reactive Wave**: Smooth color wave accelerating with audio amplitude.
  - **Screen Ambilight**: High-speed DWM desktop color sampling matching your screen.
  - **Screen Multi-Zone**: Independent Left, Center, and Right screen-edge lighting.
  - **Static / Color Cycle / Breathing / Rainbow Wave**.

### 4. GPU Working Modes & Display Control
- Automatic Display Switch (Advanced Optimus / DDS).
- Hybrid (Optimus), Integrated Only, or dGPU Dedicated.
- Instant refresh rate switching with automatic AC/Battery triggers.

### 5. Floating Mini Mode
- Compact 340x138 floating desktop widget.
- Live Battery, CPU Temp, and GPU Temp mini-cards.
- Instant profile switcher and window grab bar.
- Minimizes cleanly to system tray.

### 6. Themes
- **Cyberpunk**: High-contrast dark with neon magenta and cyan accents.
- **Stealth**: Minimalist monochrome.
- **Spectrum**: Vibrant rainbow gradients.
- **Phosphor**: Classic amber CRT console aesthetic.
- **Solarized**: Low-glare palette for prolonged sessions.

---

## Building from Source

### Prerequisites
- Visual Studio 2019 or 2022 with Desktop Development with C++ (MSVC).
- CMake 3.20+.
- Windows 10/11 x64.

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
Output executable: `build/bin/Release/PowerConfigurator.exe`

---

## Deployment & Running
To deploy the latest release build and copy necessary hardware libraries:
```cmd
update_and_run.bat
```
*(Runs with Administrator privileges required for hardware power limits, NVML, and AlienFX control).*

---

## Architecture
- `main.cpp`: Win32 application entry point, single-instance mutex, and message pump.
- `UI.cpp`: Custom-drawn Win32 interface, high-DPI scaling, and responsive layout engine.
- `ModernTheme.cpp`: Token-based theming engine, smooth gradients, and GDI cache.
- `PowerManager.cpp`: AC power broadcast detection and profile state machine.
- `CpuManager.cpp`: WMI and PDH CPU metric collectors.
- `GpuManager.cpp`: NVML runtime loader for NVIDIA GPU clock, power, and thermal management.
- `LedManager.cpp`: AlienFX V4/V5 hardware controller and LightFX API dynamic engine.
- `AudioVisualizer.cpp`: WASAPI loopback audio capture, FFT spectrum, and beat detection.
- `ScreenAmbient.cpp`: DWM desktop screen color sampler.
- `FanManager.cpp`: BIOS thermal and fan tachometer interface.

---

## License
MIT
