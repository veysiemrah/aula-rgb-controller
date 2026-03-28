# Aula F87 Pro -- Protocol Notes

## USB Identifiers (Confirmed)

| Mode         | VID    | PID    | Device Name      |
|--------------|--------|--------|------------------|
| Wired        | 0x258A | 0x010C | "Gaming Keyboard"|
| Wireless 2.4G| 0x3554 | 0xFA09 | "Gaming Keyboard"|

Device name from USB BusReportedDeviceDesc: "Gaming Keyboard"

## Interface Structure

- **MI_00**: Primary keyboard HID (standard key reports)
- **MI_01**: Multi-collection HID, contains vendor-defined collections:
  - COL03, COL05, COL06 with Usage Page 0xFF00, Usage 0x0001
  - These are the lighting / configuration control interfaces

Interface 1 (`F87_IFACE_NUM = 1`) is used for lighting control.

## Communication Protocol (OpenRGB Findings)

**CRITICAL: The keyboard uses HID Feature Reports, NOT interrupt transfers.**

All communication uses USB control transfers with HID SET_REPORT / GET_REPORT:

### SET_REPORT (host -> device)
```
bmRequestType: 0x21  (host-to-device, class, interface)
bRequest:      0x09  (SET_REPORT)
wValue:        0x0306 (feature report type 0x03 | report ID 0x06)
wIndex:        1     (interface number)
data:          520 bytes
```

### GET_REPORT (device -> host)
```
bmRequestType: 0xA1  (device-to-host, class, interface)
bRequest:      0x01  (GET_REPORT)
wValue:        0x0306 (feature report type 0x03 | report ID 0x06)
wIndex:        1     (interface number)
data:          520 bytes
```

### Report Format
- All reports are 520 bytes
- Byte 0 = Report ID (always 0x06)
- Byte 1 = Command byte

## Model Detection / Query

Send feature report:
```
Byte 0: 0x06  (report ID)
Byte 1: 0x82  (command: query model)
Byte 2: 0x01
Byte 3: 0x00
Byte 4: 0x01
Byte 5: 0x00
Byte 6: 0x06
Bytes 7-519: 0x00
```

Read feature report (GET_REPORT). Response is **14 bytes** (not 520!).

**Confirmed response (USB capture):**
```
06 82 01 00 01 00 06 00 03 00 00 00 03 66
```
- Bytes 0-6: echo of query
- Byte 8: 0x03 (matches KB.ini Psd field byte 0)
- Byte 12-13: 0x03 0x66 (matches KB.ini Psd "3,66")
- Model identification: bytes 8+ differentiate models

**Note**: OpenRGB docs say byte 13 = 0x0B for F87 Pro, but actual capture
shows 0x66. Our code should NOT reject based on model ID — just check
that the query returns a valid response.

Aula software sends model query **twice** on startup before any other commands.

## Confirmed Protocol Flow (USB Capture 2026-03-28)

Every lighting operation uses a **4-step sequence**:

1. **SET_REPORT**: Send LED color data (cmd 0x06 or 0x0A)
2. **SET_REPORT**: Send config query trigger (cmd 0x84, mostly zeros)
3. **GET_REPORT**: Read current config (response: **136 bytes**, cmd 0x84)
4. **SET_REPORT**: Write modified config back (cmd 0x04)

### GET_REPORT Response Size
Request asks for 520 bytes but device returns **136 bytes**. This is normal.

## Command 0x06 — Per-Key Color (Single/Planar)

Sets individual key colors using **planar RGB** layout (126 bytes per channel).

```
Byte 0x00: 0x06  (report ID)
Byte 0x01: 0x06  (command: per-key color)
Byte 0x02: 0x00
Byte 0x03: 0x00
Byte 0x04: 0x01
Byte 0x05: 0x00
Byte 0x06: 0x7A  (122 decimal - LED position count)
Byte 0x07: 0x01
Bytes 0x08-0x85:  R values for LED positions 0-121 (126 bytes, 4 padding)
Bytes 0x86-0x103: G values for LED positions 0-121 (126 bytes, 4 padding)
Bytes 0x104-0x181: B values for LED positions 0-121 (126 bytes, 4 padding)
```

**Confirmed**: W key (led_index=14) set to blue → R[14]=0, G[14]=0, B[14]=0xFF at offsets 22, 148, 274. ✓

## Command 0x0A — Custom Color Profile

Writes the stored custom color profile. Format: **7 LEDs × 3 bytes RGB = 21 bytes per group**.

```
Byte 0x00: 0x06  (report ID)
Byte 0x01: 0x0A  (command: custom profile)
Byte 0x02-0x05: 0x00
Byte 0x06: 0x00
Byte 0x07: 0x02
Bytes 0x08-0x1C: 21 bytes padding (zeros)
Bytes 0x1D+: LED groups (21 bytes each, 7 LEDs × RGB)
  - 5 groups, 21-byte gap, 2 groups, 21-byte gap, 4 groups, 21-byte gap, 3 groups
  - Total: 14 groups = 98 LED positions
Bytes 0x202-0x203: Terminator 0x5A 0xA5
```

**KEY INSIGHT**: For effect_id=0x01 (static single color), the keyboard uses
**LED[0]** (report bytes 29-31) as the color for ALL keys.

| User Action | LED[0] RGB | effect_id |
|-------------|-----------|-----------|
| All red     | FF 00 00  | 0x01      |
| All green   | 00 FF 00  | 0x01      |
| All white   | FF FF FF  | 0x01      |

## Command 0x84 — Config Read

**Send** (SET_REPORT): 520 bytes, mostly zeros:
```
Byte 0: 0x06, Byte 1: 0x84, Byte 4: 0x01, Byte 6: 0x80
```

Then **GET_REPORT**: receives 136 bytes with current config.

## Command 0x04 — Config Write

Read-modify-write pattern: read via 0x84, modify fields, send back as 0x04.

```
Byte  0: 0x06 (report ID)
Byte  1: 0x04 (command)
Byte  4: 0x01
Byte  6: 0x80
Bytes 8+: config data (copy from 0x84 response, modify as needed)
Byte 18: effect_id
Byte 66: brightness (1-4)
Bytes 68+: keyboard matrix descriptor (do not modify)
```

### Effect ID Map (byte 18 of config) — Confirmed via Hardware Scan

**IDs 6, 9, 14 do not exist** in F87 TK firmware — keyboard skips them.

| ID   | Hex  | Name              | Color? | Speed? | Notes                          |
|------|------|-------------------|--------|--------|--------------------------------|
| 0    | 0x00 | Off               | -      | -      | LEDs turned off                |
| 1    | 0x01 | Static            | Yes    | No     | Color from 0x0A LED[0]         |
| 2    | 0x02 | Breathing         | Yes    | Yes    | Always colorful/random mode    |
| 3    | 0x03 | Wave / Rainbow    | No     | Yes    | Built-in rainbow               |
| 4    | 0x04 | Spectrum          | Yes    | Yes    | Keypress spread outward        |
| 5    | 0x05 | Rain              | Yes    | Yes    |                                |
| 7    | 0x07 | Ripple            | Yes    | Yes    | Keypress spread                |
| 8    | 0x08 | Starlight         | Yes    | Yes    | Random twinkle                 |
| 10   | 0x0A | Snake             | Yes    | Yes    |                                |
| 11   | 0x0B | Aurora            | Yes    | Yes    |                                |
| 12   | 0x0C | Reactive          | Yes    | Yes    | Single pressed key lights up   |
| 13   | 0x0D | Marquee           | Yes    | Yes    |                                |
| 15   | 0x0F | Circle            | No     | Yes    |                                |
| 16   | 0x10 | Rain Down         | No     | Yes    | Top-to-bottom wave             |
| 17   | 0x11 | Center Ripple     | No     | Yes    | Center spread outward          |
| 18   | 0x12 | Custom static     | Per-key| No     | Uses 0x06 data, byte 17=1      |

### Config Response Structure (136 bytes)
```
Bytes  0-7:   Header (06 84 00 00 01 00 80 00)
Bytes  8-15:  Mode config (00 03 03 01 00 00 04 04)
Byte  16:     0x07 (mode count?)
Byte  17:     Custom mode flag (0=hw effect, 1=custom per-key) ← KEY FIELD
Byte  18:     Effect ID ← KEY FIELD
Byte  19:     0x20 (32)
Bytes 20-25:  Parameters (reserved)
Byte  26:     Side light effect ID ← KEY FIELD
              (0=off, 1=rainbow, 2=breath mix, 3=static red, 4=breath red)
Bytes 27-35:  Parameters (reserved)
Byte  36:     Battery light effect ID ← KEY FIELD
              (same values as side light; 0=off shows battery indicator)
Bytes 37-63:  Parameters (reserved)
Bytes 64-65:  0xFF 0xFF (separator)
Bytes 66-133: Per-effect parameters (2 bytes per effect) ← KEY FIELDS
              Formula: offset = 64 + 2 × effect_id
              Byte 0: brightness (1=min, 4=max)
              Byte 1: (speed << 4) | flags
                      speed = upper nibble (0=slowest, 4=fastest)
                      flags = lower nibble (typically 0x7)
              Example: effect_id=3 (Wave) → offset 70-71
Bytes 134-135: 0x5A 0xA5 terminator
```

## Command 0x08 — Direct Mode (OpenRGB, UNCONFIRMED)

From OpenRGB SinowealthKeyboard10c analysis. NOT observed in Aula software captures.
May work for real-time temporary control (reverts after ~1s keepalive timeout).

```
Byte 0x00: 0x06  (report ID)
Byte 0x01: 0x08  (command: set LEDs direct)
Byte 0x04: 0x01
Byte 0x06: 0x7A  (122)
Byte 0x07: 0x01
Bytes 0x08+: RGB data, 3 bytes per LED (interleaved, NOT planar)
```

## Effect Speed/Brightness — Two Methods

### Method 1: Config bytes (CONFIRMED, IMPLEMENTED)

Each effect stores its own brightness and speed in the config at offset `64 + 2 × effect_id`:
- Byte 0: brightness (1-4)
- Byte 1: (speed << 4) | flags

This is set via the standard config write (cmd 0x04) and works for all effects.

### Method 2: Report IDs 0x35-0x39 (Separate Protocol, NOT IMPLEMENTED)

The Windows software also uses separate HID report IDs for speed/brightness:
- **0x39**: Speed/brightness set (short, ~6-9 bytes)
- **0x35**: Checksum/verification
- **0x36**: Device configuration
- **0x37**: Firmware/data block

These may control additional parameters (e.g., single-color vs colorful mode for Breathing).
Not yet decoded. Method 1 is sufficient for basic brightness/speed control.

## Keepalive (for direct mode 0x08 only)

The keyboard reverts to its stored hardware effect after approximately
1 second without receiving a direct LED update. Applications using direct
mode must send updates at least every 500ms to maintain control.

## Configuration (from KB.ini)

- **ChannelMask**: 3 (binary 11 = supports both wired and wireless)
- **Firmware version**: 24
- **CRC**: Enabled (CRC=1 in driver config)
- **ShowDebounce**: 0x04000101
- **Psd**: 3,0,0,0,3,66 (password/protection config)

## LED Effects (Hardware, stored in keyboard flash)

19 effects total. Extracted from KB.ini LedOpt entries.
**Effect selection uses config byte 18 = UI Index (NOT HW ID!).**
Confirmed: UI_Index 1=Static, 2=Breathing/Respire, 3=Wave/Rainbow, 21(0x15)=Custom.

Format: `LedOpt<n> = ui_index, hw_id, has_speed, has_brightness, has_direction, has_random, has_color`

| # | UI Index | HW ID | Name           | Speed | Bright | Dir | Random | Color |
|---|----------|-------|----------------|-------|--------|-----|--------|-------|
| 1 |  1       |  1    | Static         | No    | Yes    | No  | Yes    | Yes   |
| 2 |  2       |  3    | Breathing      | Yes   | Yes    | No  | Yes    | Yes   |
| 3 |  3       |  2    | Wave (rainbow) | Yes   | Yes    | No  | No     | No    |
| 4 |  4       | 19    | Spectrum       | Yes   | Yes    | No  | Yes    | Yes   |
| 5 |  5       | 15    | Ripple         | Yes   | Yes    | No  | Yes    | Yes   |
| 6 |  6       | 13    | Reactive       | Yes   | Yes    | No  | Yes    | Yes   |
| 7 |  7       | 20    | Starlight      | Yes   | Yes    | No  | Yes    | Yes   |
| 8 |  8       | 16    | Rain           | Yes   | Yes    | No  | Yes    | Yes   |
| 9 |  9       | 18    | Snake          | Yes   | Yes    | No  | Yes    | Yes   |
|10 | 10       |  5    | Marquee        | Yes   | Yes    | No  | Yes    | Yes   |
|11 | 11       |  7    | Aurora         | Yes   | Yes    | No  | Yes    | Yes   |
|12 | 12       | 17    | Laser          | Yes   | Yes    | No  | Yes    | Yes   |
|13 | 13       | 12    | Firework       | Yes   | Yes    | No  | Yes    | Yes   |
|14 | 14       |  8    | Gradient       | Yes   | Yes    | No  | Yes    | Yes   |
|15 | 15       | 28    | Rainbow Wave   | Yes   | Yes    | No  | No     | No    |
|16 | 16       | 30    | Prism          | Yes   | Yes    | No  | No     | No    |
|17 | 17       | 14    | Cycle          | Yes   | Yes    | No  | No     | No    |
|18 | 18       | 29    | Tidal          | Yes   | Yes    | No  | Yes    | Yes   |
|19 | 21       |  0    | Custom (per-key)| No   | No     | No  | No     | No    |

Default effect: UI index 10 (Marquee, HW ID 5).

Notes:
- Effects 3, 15, 16, 17 have no color or random-color control (rainbow / cycling effects).
- Effect 19 (Custom, HW ID 0) is the per-key mode with no parameters.
- Effect 1 (Static) has no speed control.

## Key-to-LED Index Mapping

88 keys total (K1-K88). K88 is the ISO extra key (between LSHIFT and Z).

The hardware LED index is NOT sequential with physical key position. It follows
the internal LED driver addressing. The mapping was extracted from KB.ini K entries
(last number = LED index).

| Key ID | Key Name | LED Index |
|--------|----------|-----------|
|  K1    | ESC      |   0       |
|  K2    | F1       |  12       |
|  K3    | F2       |  18       |
|  K4    | F3       |  24       |
|  K5    | F4       |  30       |
|  K6    | F5       |  36       |
|  K7    | F6       |  42       |
|  K8    | F7       |  48       |
|  K9    | F8       |  54       |
| K10    | F9       |  60       |
| K11    | F10      |  66       |
| K12    | F11      |  72       |
| K13    | F12      |  78       |
| K14    | GRAVE    |   1       |
| K15    | 1        |   7       |
| K16    | 2        |  13       |
| K17    | 3        |  19       |
| K18    | 4        |  25       |
| K19    | 5        |  31       |
| K20    | 6        |  37       |
| K21    | 7        |  43       |
| K22    | 8        |  49       |
| K23    | 9        |  55       |
| K24    | 0        |  61       |
| K25    | -        |  67       |
| K26    | =        |  73       |
| K27    | BKSP     |  79       |
| K28    | PRTSC    |  84       |
| K29    | SCRLK    |  90       |
| K30    | PAUSE    |  96       |
| K31    | TAB      |   2       |
| K32    | Q        |   8       |
| K33    | W        |  14       |
| K34    | E        |  20       |
| K35    | R        |  26       |
| K36    | T        |  32       |
| K37    | Y        |  38       |
| K38    | U        |  44       |
| K39    | I        |  50       |
| K40    | O        |  56       |
| K41    | P        |  62       |
| K42    | [        |  68       |
| K43    | ]        |  74       |
| K44    | ENTER    |  81       |
| K45    | DEL      |  86       |
| K46    | INS      |  85       |
| K47    | HOME     |  91       |
| K48    | PGUP     |  97       |
| K49    | CAPS     |   3       |
| K50    | A        |   9       |
| K51    | S        |  15       |
| K52    | D        |  21       |
| K53    | F        |  27       |
| K54    | G        |  33       |
| K55    | H        |  39       |
| K56    | J        |  45       |
| K57    | K        |  51       |
| K58    | L        |  57       |
| K59    | ;        |  63       |
| K60    | '        |  69       |
| K61    | \        |  75       |
| K62    | END      |  92       |
| K63    | PGDN     |  98       |
| K64    | LSHIFT   |   4       |
| K65    | Z        |  10       |
| K66    | X        |  16       |
| K67    | C        |  22       |
| K68    | V        |  28       |
| K69    | B        |  34       |
| K70    | N        |  40       |
| K71    | M        |  46       |
| K72    | ,        |  52       |
| K73    | .        |  58       |
| K74    | /        |  64       |
| K75    | RSHIFT   |  82       |
| K76    | UP       |  94       |
| K77    | RCTRL    |  83       |
| K78    | LCTRL    |   5       |
| K79    | LWIN     |  11       |
| K80    | LALT     |  17       |
| K81    | SPACE    |  35       |
| K82    | RALT     |  53       |
| K83    | FN       |  59       |
| K84    | APP/MENU |  65       |
| K85    | LEFT     |  89       |
| K86    | DOWN     |  95       |
| K87    | RIGHT    | 101       |
| K88    | ISO      |  76       |

## Capture Log

| Date       | File          | Action                | Notes                                      |
|------------|---------------|-----------------------|--------------------------------------------|
| 2026-03-27 | --            | KB.ini analysis       | Extracted VID/PID, LED map, effects from RE |
| 2026-03-27 | --            | OpenRGB RE findings   | HID feature reports, 520-byte packets, model query, direct LED command |
| 2026-03-28 | capture.pcap  | USB capture #1        | W=blue, all=red, brightness, rainbow — decoded 4-step protocol |
| 2026-03-28 | capture2.pcap | USB capture #2        | all=green, all=white, LED off — confirmed effect_id + LED[0] color mapping |
| 2026-03-28 | capture3.pcap | USB capture #3        | Handshake (model query 0x82), Breathing/speed — discovered report 0x35-0x39 |
