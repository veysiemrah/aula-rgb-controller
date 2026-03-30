[English](README.md) | [Türkçe](README.tr.md)

# aula-rgb-controller

Linux RGB lighting control for **AULA** mechanical keyboards. Built from reverse-engineered USB HID protocol analysis.

> Full per-key RGB control, 18 hardware effects, 12 software effects, music-reactive lighting, and system sensor visualization — all without the Windows-only vendor software.

## Supported Keyboards

Currently tested with the **AULA F87 TK**. Other AULA keyboards using the same SinoWealth chipset (F75, F87 Pro, F99) may also work — contributions and testing reports welcome!

## Features

- **18 hardware effects** — wave, breathing, rain, ripple, starlight, snake, aurora, and more
- **12 software effects** — fire, matrix, plasma, heatmap, radar, lightning, explode, ripple, typewriter, game of life, keyheat, sensor overlay
- **5 music visualizers** — spectrum, beat pulse, energy wave, VU meter, frequency map (PulseAudio/PipeWire)
- **Sensor monitoring** — CPU temperature/load, GPU temperature, RAM usage mapped to keyboard keys with color gradients
- **Per-key RGB** — set individual key colors via CLI or GUI paint mode
- **GTK4 GUI** — full-featured control panel with live keyboard preview, HSV color picker, and drag-and-drop sensor editor
- **D-Bus daemon** — background service with hotplug detection and auto-reconnect

## Screenshots

| Hardware Effect (Circle) | Software Effect (Matrix) |
|:---:|:---:|
| ![Circle](screenshots/Screenshot_20260331_011912.png) | ![Matrix](screenshots/Screenshot_20260331_011932-1.png) |

| Music Visualizer (Spectrum) | Music Visualizer (VU Meter) |
|:---:|:---:|
| ![Spectrum](screenshots/Screenshot_20260331_011946.png) | ![VU Meter](screenshots/Screenshot_20260331_012028.png) |

## Requirements

### Build Dependencies

**Debian / Ubuntu:**
```bash
sudo apt install libusb-1.0-0-dev libjson-c-dev libpulse-dev libsystemd-dev cmake build-essential
```

**Fedora:**
```bash
sudo dnf install libusb1-devel json-c-devel pulseaudio-libs-devel systemd-devel cmake gcc make
```

**Arch Linux:**
```bash
sudo pacman -S libusb json-c libpulse systemd cmake base-devel
```

**Optional (for GUI):**

**Debian / Ubuntu:**
```bash
sudo apt install libgtk-4-dev libadwaita-1-dev
```

**Fedora:**
```bash
sudo dnf install gtk4-devel libadwaita-devel
```

**Arch Linux:**
```bash
sudo pacman -S gtk4 libadwaita
```

## Quick Start

```bash
# Clone
git clone https://github.com/veysiemrah/aula-rgb-controller.git
cd aula-rgb-controller

# Setup udev rules (required for non-root USB access)
sudo cp udev/99-f87.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Install (optional)
sudo make install
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_CLI` | ON | Command-line tool (`f87ctl`) |
| `BUILD_DAEMON` | ON | D-Bus daemon (`f87d`) |
| `BUILD_GUI` | OFF | GTK4 GUI (`f87control`) |

```bash
# Build everything including GUI
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON
```

## Usage

### Daemon Mode (Recommended)

Start the daemon:
```bash
# Manual
./build/f87d &

# Or via systemd
systemctl --user enable --now f87d
```

Then use the CLI or GUI:
```bash
# Device info
f87ctl info

# Hardware effects
f87ctl effect wave --brightness 4 --speed 2
f87ctl effect breathing --color ff0000

# Per-key colors
f87ctl key set ESC ff0000
f87ctl key set-all 00ff00

# Software effects
f87ctl animate fire
f87ctl animate matrix

# Music-reactive
f87ctl music spectrum
f87ctl music beat

# Brightness
f87ctl brightness 3

# Stop effects / turn off
f87ctl stop
f87ctl off
```

### Direct Mode (Debug)

Bypass the daemon for direct USB access:
```bash
f87ctl --direct info
f87ctl --direct effect wave
f87ctl --direct raw send "06 82 01 00 01 00 06"
```

### GUI

```bash
f87control
```

The GTK4 GUI provides:
- Visual keyboard preview with live effect animation
- Point-and-click per-key color assignment
- HSV color picker with hex input and presets
- Sensor monitoring editor with drag-and-drop key mapping

## Architecture

```
F87Control
├── libf87 (lib/)         Shared C library — USB HID protocol, effects, audio, sensors
├── f87d (daemon/)        D-Bus daemon — device management, effect lifecycle
├── f87ctl (cli/)         Command-line tool
└── f87control (gui/)     GTK4 + libadwaita GUI
```

Communication flow:
```
GUI / CLI  ──D-Bus──>  f87d daemon  ──USB HID──>  AULA F87 TK keyboard
```

## Supported Hardware

| Model | VID:PID | Connection | Status |
|-------|---------|------------|--------|
| AULA F87 TK | `258A:010C` | USB Wired | Fully tested |
| AULA F87 Pro | `258A:010C` | USB Wired | Should work (untested) |
| AULA F75 / F99 | `258A:*` | USB | May work (untested) |

> **Note:** Many keyboard brands use SinoWealth chips (VID `258A`), but protocols differ between models. Using this tool with an unsupported keyboard may not work. If you have an unlisted AULA model, please open an issue with your `f87ctl --direct info` output.

## Protocol Documentation

The USB HID protocol was reverse-engineered from Windows USB captures. See [`tools/protocol_notes.md`](tools/protocol_notes.md) for full protocol documentation including:

- HID feature report structure
- 4-step write protocol
- Per-key color encoding (planar RGB)
- Effect parameter encoding
- Direct mode animation protocol

## Contributing

Contributions are welcome! This project uses:

- **C11** with CMake >= 3.16
- **libusb** for USB HID communication
- **sd-bus** (systemd) for D-Bus daemon
- **GTK4 + libadwaita** for the GUI
- **PulseAudio** for audio capture
- **KissFFT** for spectrum analysis

### For Reactive Effects (Input Capture)

```bash
sudo usermod -aG input $USER
# Logout and login for group change to take effect
```

## License

This project is licensed under the **GNU General Public License v3.0** — see the [LICENSE](LICENSE) file for details.
