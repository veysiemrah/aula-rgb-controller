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

Read feature report (GET_REPORT), 520 bytes response.
Byte 13 of response = model ID:
- 0x0B = Aula F87 Pro

## Direct Per-Key RGB (Command 0x08)

This is the main lighting control command. Sends RGB data for all LEDs
in a single 520-byte feature report.

```
Byte 0x00: 0x06  (report ID)
Byte 0x01: 0x08  (command: set LEDs direct)
Byte 0x02: 0x00
Byte 0x03: 0x00
Byte 0x04: 0x01
Byte 0x05: 0x00
Byte 0x06: 0x7A  (122 decimal - LED count for buffer)
Byte 0x07: 0x01
Bytes 0x08+: RGB data, 3 bytes per LED (R, G, B)
             LED index * 3 + 0x08 = offset for that LED's red byte
```

Maximum LED index that fits: (520 - 8) / 3 = 170 LEDs
Actual keyboard uses indices up to ~101 (87 keys, non-sequential addressing).

## Keepalive

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
**Note: the command to select hardware effects is NOT yet known.**
Only direct per-key RGB (command 0x08) is implemented.

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

| Date       | File | Action                | Notes                                      |
|------------|------|-----------------------|--------------------------------------------|
| 2026-03-27 | --   | KB.ini analysis       | Extracted VID/PID, LED map, effects from RE |
| 2026-03-27 | --   | OpenRGB RE findings   | HID feature reports, 520-byte packets, model query, direct LED command |
