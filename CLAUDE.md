# F87Control

Aula F87 TK keyboard RGB lighting control for Linux. Reverse-engineered USB HID protocol.

## Architecture

Four-layer design:
- **libf87** (`lib/`) — shared C library, abstracts USB HID protocol via libusb
  - **animate** — software animation engine: thread management, frame loop, effect dispatch
  - **audio/spectrum** — PulseAudio capture, KissFFT analysis, beat detection (optional, BUILD_AUDIO)
  - **client** — D-Bus proxy client for daemon communication (sd-bus)
- **f87d** (`daemon/`) — D-Bus daemon, owns USB access and effect threads
  - Session bus (`org.f87.Control`), auto-activation, hotplug polling
  - Idle timeout (5min, disabled during SW effects)
- **f87ctl** (`cli/`) — command-line tool (daemon mode default, `--direct` for USB bypass)
- **f87control** (`gui/`) — GTK4 + libadwaita GUI (communicates via daemon)
  - Sidebar layout with HW/SW/music/sensor effect categories
  - 88-key keyboard preview (cairo), dynamic control panel, color palette

## Build

```bash
sudo apt install libusb-1.0-0-dev libjson-c-dev libpulse-dev libsystemd-dev libgtk-4-dev libadwaita-1-dev cmake build-essential  # Debian/Ubuntu
mkdir build && cd build && cmake .. && make
```

Options: `-DBUILD_GUI=ON` (GTK4 GUI), `-DBUILD_DAEMON=ON` (default, D-Bus daemon)

## USB Protocol (Confirmed via USB Capture + Hardware Scan 2026-03-28)

- **VID/PID:** 0x258A/0x010C (wired), 0x3554/0xFA09 (wireless)
- **Interface 1**, Usage Page 0xFF00, Usage 1
- **HID Feature Reports** via `libusb_control_transfer` (NOT interrupt transfers)
- **Buffer size:** 520 bytes send, **136 bytes** config response. Report ID: 0x06
- **4-step protocol**: (1) LED data → (2) config query 0x84 → (3) GET_REPORT → (4) config write 0x04
- **Model query:** `{0x06, 0x82, 0x01, 0x00, 0x01, 0x00, 0x06}` → 14-byte response (byte 13=0x66)
- **Per-key color (0x06):** planar RGB, 126 bytes/channel at offsets 8, 134, 260
- **Static/effect color (0x0A):** All LED positions filled with RGB triplets from offset 29, effect_id=0x01 for static
- **Config byte 17:** custom mode flag (0=hw effect, 1=custom per-key)
- **Config byte 18:** effect_id (see effect table below)
- **Config byte 26:** side light effect (0=off, 1=rainbow, 2=breath mix, 3=static red, 4=breath red)
- **Config byte 36:** battery light effect (same values as side light)
- **Per-effect params:** offset = 64 + 2 × effect_id
  - Byte 0: brightness (1-4)
  - Byte 1: (speed << 4) | flags — speed upper nibble (0-4), flags lower nibble (typically 0x7)
- **Command 0x08:** OpenRGB direct mode — does NOT work on F87 TK firmware (model 0x66)
- Full protocol docs: `tools/protocol_notes.md`

## Effect ID Table (Confirmed via Hardware Scan)

| ID | Hex | Effect | Color? | Speed? |
|----|-----|--------|--------|--------|
| 0 | 0x00 | Off | - | - |
| 1 | 0x01 | Static | Yes (0x0A LED[0]) | No |
| 2 | 0x02 | Breathing (colorful) | Yes | Yes |
| 3 | 0x03 | Wave / Rainbow | No | Yes |
| 4 | 0x04 | Spectrum (keypress spread) | Yes | Yes |
| 5 | 0x05 | Rain | Yes | Yes |
| 7 | 0x07 | Ripple (keypress spread) | Yes | Yes |
| 8 | 0x08 | Starlight (random twinkle) | Yes | Yes |
| 10 | 0x0A | Snake | Yes | Yes |
| 11 | 0x0B | Aurora | Yes | Yes |
| 12 | 0x0C | Reactive (single key) | Yes | Yes |
| 13 | 0x0D | Marquee | Yes | Yes |
| 15 | 0x0F | Circle | No | Yes |
| 16 | 0x10 | Rain Down (top-bottom wave) | No | Yes |
| 17 | 0x11 | Center Ripple (center spread) | No | Yes |
| 18 | 0x12 | Custom static (byte 17=1) | Per-key | No |

**IDs 6, 9, 14 do not exist** in F87 TK firmware — keyboard skips them.

## Key Files

- `lib/src/protocol.h` — internal protocol constants, config offsets, struct definitions
- `lib/src/protocol.c` — packet building, send/recv, config read/write, 88-key LED index table
- `lib/src/device.c` — USB device enumeration, open/close
- `lib/src/lighting.c` — per-key color, apply (4-step protocol), brightness control
- `lib/src/effects.c` — 16 hardware effects, side/battery light control
- `lib/src/animate.c` — animation thread core, frame loop, input capture
- `lib/src/animate_internal.h` — internal types: effect interface, context, frame buffer
- `lib/src/ring_buffer.c` — lock-free SPSC ring buffer for audio data
- `lib/src/effects_sw.c` — 7 non-reactive software effects (fire, matrix, plasma, heatmap, radar, lightning, sensor)
- `lib/src/effects_sw_reactive.c` — 5 reactive software effects (explode, ripple, typewriter, life, keyheat)
- `lib/src/sensor.h` / `sensor.c` — sensor plugin interface, built-in sensors (cpu_temp, cpu_load, gpu_temp, ram_usage)
- `lib/src/sensor_config.h` / `sensor_config.c` — JSON config parser for sensor-key mappings
- `configs/sensor/*.json` — sensor profile configs (developer, gamer, system)
- `lib/src/audio.c` — PulseAudio capture thread
- `lib/src/spectrum.c` — KissFFT FFT, band grouping, beat detection
- `lib/src/visualizer.c` — 5 music-reactive visualizers (spectrum, beat, energy, VU, freqmap)
- `lib/include/f87/animate.h` — animation public API
- `lib/include/f87/audio_types.h` — audio data types
- `cli/src/main.c` — CLI tool: list/info/brightness/effect/color/key/animate/music/raw
- `gui/src/main.c` — GTK4 GUI entry point
- `gui/src/window.c` — main window layout (sidebar + paned)
- `gui/src/sidebar.c` — effect category list
- `gui/src/controls.c` — dynamic control panel (sliders, color, dropdowns)
- `gui/src/keyboard_view.c` — 88-key cairo keyboard preview widget
- `gui/src/app_state.c` — daemon client connection, effect lifecycle
- `daemon/src/main.c` — daemon entry point, sd-bus event loop
- `daemon/src/dbus_interface.c` — D-Bus method/signal/property handlers
- `daemon/src/device_manager.c` — hotplug monitoring, auto-reconnect
- `daemon/src/effect_manager.c` — effect lifecycle management
- `daemon/src/idle_monitor.c` — idle timeout (5min, disabled during SW effects)
- `lib/include/f87/client.h` — D-Bus proxy client API
- `lib/src/client.c` — D-Bus proxy implementation
- `dbus/org.f87.Control.service` — D-Bus auto-activation
- `systemd/f87d.service` — systemd user service unit
- `tools/protocol_notes.md` — full protocol documentation
- `tools/*.pcap` — USB capture files from Windows
- `tools/*.py` — capture analysis scripts
- `docs/plans/` — design and implementation plan documents

## Testing

```bash
# Install udev rule first
sudo cp udev/99-f87.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# For reactive/ripple animation (input event reading)
sudo usermod -aG input $USER  # then logout/login

# Run tests (no hardware needed)
cd build && ctest --output-on-failure

# Start daemon (or use systemd)
./f87d &
# Or: systemctl --user enable --now f87d

# CLI uses daemon by default
./f87ctl info
./f87ctl effect wave --brightness 4 --speed 2
./f87ctl brightness 3
./f87ctl animate fire
./f87ctl music spectrum
./f87ctl stop
./f87ctl off

# Direct USB mode (bypass daemon, for debug)
./f87ctl --direct info
./f87ctl --direct effect wave
./f87ctl --direct raw send "06 82 01 00 01 00 06"
```

## Project Status

- Faz 0-3: Complete (lib + CLI + protocol + hardware testing)
- Faz 3.5: Software effects + music-reactive lighting (complete)
  - 11 software effects: fire, matrix, plasma, heatmap, radar, lightning, explode, ripple, typewriter, life, keyheat
  - 5 music visualizers: spectrum, beat, energy, VU, freqmap
  - Producer-consumer threading (audio + animation threads)
  - PulseAudio/PipeWire capture, KissFFT spectrum analysis, beat detection
- Faz 4: Sensor integration (complete)
  - Plugin-based sensor interface with built-in sensors (cpu_temp, cpu_load, gpu_temp, ram_usage)
  - JSON config for key-sensor mappings, color/bar display modes
  - Built-in profiles: developer, gamer, system
- Faz 5: GTK4 GUI (complete)
  - Sidebar + keyboard preview + dynamic controls + color palette
  - All effect categories: HW, SW, music, sensor
  - Device auto-detection, error handling, status bar
- Faz 6.1: Daemon mode (complete)
  - D-Bus daemon (sd-bus) with auto-activation and systemd user service
  - Device manager with 5s hotplug polling
  - Effect manager (HW/SW/music/sensor)
  - Idle timeout (5min, disabled during SW effects)
  - Proxy client library (client.h/client.c)
  - CLI/GUI migrated to daemon
- Faz 6.2: Profiles (not started)
- Faz 6.3: Wireless support (not started)

## Known Limitations (Firmware)

These are hardware/firmware constraints that cannot be resolved in software:

- **Side/battery light color:** Only predefined modes (off/rainbow/breath mix/static red/breath red). Color is hardcoded in firmware — not changeable even in Windows software.
- **Software-driven animation:** Direct mode (CMD 0x08) works at 30fps after Report 0x3C enable. Config write (cmd 0x04) causes brief LED reset — use CMD 0x08 for smooth animation instead.
- **Effect IDs 6, 9, 14:** Not present in F87 TK firmware — keyboard skips these IDs.
- **USB timing:** Commands sent too rapidly (~<200ms apart) can cause keyboard reset. 5ms delay between USB transfers required.

## Key Layout Quirks

The `f87_key_layout[]` row/col assignments follow KB.ini grouping, NOT exact physical position.
Navigation cluster keys have mismatched rows:

| Key | Current row | Physical row | Notes |
|-----|-------------|-------------|-------|
| PRTSC, SCRLK, PAUSE | 1 | 0 | Same physical row as F1-F12 |
| INS, HOME, PGUP | 2 | 1 | Same physical row as number keys |
| END, PGDN | 3 | 2 | Same physical row as DEL (QWERTY row) |
| RCTRL | 4 | 5 | Should be in bottom row with LCTRL |

For per-key effects using physical position (gradients, waves), these keys will appear
on the wrong row. Consider normalizing positions if exact physical layout is needed.

## Conventions

- C11, CMake >= 3.16
- Error codes: negative integers (F87_OK=0, F87_ERR_*=-1..-8)
- Public API in `lib/include/f87/`, internal in `lib/src/`
- LED indices: non-sequential, mapped via `f87_led_index[]` array from KB.ini
- Per-effect parameters at config offset 64 + 2 × effect_id
