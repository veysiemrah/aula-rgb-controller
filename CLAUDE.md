# F87Control

Aula F87 Pro keyboard RGB lighting control for Linux. Reverse-engineered USB HID protocol.

## Architecture

Three-layer design:
- **libf87** (`lib/`) — shared C library, abstracts USB HID protocol via libusb
- **f87ctl** (`cli/`) — command-line tool
- **f87control** (`gui/`) — GTK4 GUI (not yet implemented)

## Build

```bash
sudo apt install libusb-1.0-0-dev libjson-c-dev cmake build-essential  # Debian/Ubuntu
mkdir build && cd build && cmake .. -DBUILD_GUI=OFF && make
```

For GUI: add `libgtk-4-dev` and `-DBUILD_GUI=ON`

## USB Protocol

- **VID/PID:** 0x258A/0x010C (wired), 0x3554/0xFA09 (wireless)
- **Interface 1**, Usage Page 0xFF00, Usage 1
- **HID Feature Reports** via `libusb_control_transfer` (NOT interrupt transfers)
- **Buffer size:** 520 bytes, Report ID: 0x06
- **Model query:** `{0x06, 0x82, 0x01, 0x00, 0x01, 0x00, 0x06}` → response byte 13 = model ID (0x0B = F87 Pro)
- **Direct LED:** `{0x06, 0x08, 0, 0, 0x01, 0, 0x7A, 0x01, [RGB data at offset 0x08]}`
- **Keepalive:** Direct mode resets after ~1s without updates; send every 500ms
- Protocol source: OpenRGB `SinowealthKeyboard10cController`

## Key Files

- `lib/src/protocol.h` — internal protocol constants, struct definitions
- `lib/src/protocol.c` — packet building, send/recv, 88-key LED index table
- `lib/src/device.c` — USB device enumeration, open/close
- `lib/src/lighting.c` — per-key color, apply (sends 520-byte feature report)
- `lib/src/effects.c` — mode management (OFF/DIRECT)
- `cli/src/main.c` — CLI tool with list/info/brightness/key/raw commands
- `tools/protocol_notes.md` — full protocol documentation
- `docs/plans/` — design and implementation plan documents

## Testing

```bash
# Install udev rule first
sudo cp udev/99-f87.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger

# Run tests (no hardware needed)
cd build && ctest --output-on-failure

# Hardware test
./f87ctl list
./f87ctl info
./f87ctl key set-all ff0000
./f87ctl raw send "06 82 01 00 01 00 06"
```

## Project Status

- Faz 0-2: Complete (lib + CLI + tools + tests)
- Faz 3: Hardware testing needed (Linux)
- Faz 4: GTK GUI (not started)
- Faz 5: Sensor integration, profiles (not started)

## Conventions

- C11, CMake >= 3.16
- Error codes: negative integers (F87_OK=0, F87_ERR_*=-1..-6)
- Public API in `lib/include/f87/`, internal in `lib/src/`
- LED indices: non-sequential, mapped via `f87_led_index[]` array from KB.ini
