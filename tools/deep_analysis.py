#!/usr/bin/env python3
"""Deep analysis of Aula F87 Pro capture - decode color data format."""
import struct
import sys

def parse_usbpcap(filepath):
    with open(filepath, 'rb') as f:
        magic = f.read(4)
        endian = '<' if magic == b'\xd4\xc3\xb2\xa1' else '>'
        f.read(20)
        packets = []
        frame_num = 0
        while True:
            hdr = f.read(16)
            if len(hdr) < 16: break
            ts_sec, ts_usec, incl_len, _ = struct.unpack(endian + 'IIII', hdr)
            data = f.read(incl_len)
            if len(data) < incl_len: break
            frame_num += 1
            if len(data) < 27: continue
            header_len = struct.unpack_from('<H', data, 0)[0]
            irp_info = data[16]
            transfer_type = data[22]
            if transfer_type != 2: continue
            payload = data[header_len:]
            packets.append({
                'frame': frame_num,
                'dir': 'OUT' if irp_info == 0 else 'IN',
                'data': payload,
            })
    return packets

def analyze_0x06_command(report):
    """Analyze LED_SINGLE command (planar RGB format)."""
    print("\n  === LED_SINGLE (0x06) Format Analysis ===")
    print(f"  Header: {' '.join(f'{b:02x}' for b in report[:8])}")
    print(f"  LED count field (byte 6): {report[6]} (0x{report[6]:02x})")

    # Planar RGB: 126 bytes per channel (122 LEDs + 4 padding)
    plane_size = 126
    r_start = 8
    g_start = 8 + plane_size    # 134
    b_start = 8 + plane_size*2  # 260

    print(f"  R plane: offset {r_start}-{r_start+plane_size-1}")
    print(f"  G plane: offset {g_start}-{g_start+plane_size-1}")
    print(f"  B plane: offset {b_start}-{b_start+plane_size-1}")

    for led_idx in range(122):
        r = report[r_start + led_idx] if r_start + led_idx < len(report) else 0
        g = report[g_start + led_idx] if g_start + led_idx < len(report) else 0
        b = report[b_start + led_idx] if b_start + led_idx < len(report) else 0
        if r or g or b:
            print(f"  LED[{led_idx:3d}]: R={r:02x} G={g:02x} B={b:02x}")


def analyze_0x0a_command(report):
    """Analyze LED_CUSTOM command."""
    print("\n  === LED_CUSTOM (0x0A) Format Analysis ===")
    print(f"  Header: {' '.join(f'{b:02x}' for b in report[:8])}")

    # Data starts at byte 8, terminates with 5A A5
    term_pos = -1
    for i in range(len(report)-1):
        if report[i] == 0x5A and report[i+1] == 0xA5:
            term_pos = i
            break
    print(f"  Terminator at: {term_pos}")

    # Try 21-byte group interpretation (7 LEDs x 3 bytes RGB)
    print(f"\n  21-byte group analysis (7 LEDs x RGB per group):")
    data_start = 8
    group_num = 0
    gap_count = 0

    # Skip initial padding (bytes 8-28 are zeros)
    pos = data_start
    while pos < len(report) and report[pos] == 0:
        pos += 1

    first_data = pos
    print(f"  First non-zero byte at: {first_data} (skip {first_data - data_start} bytes)")

    # Analyze groups starting from first data
    pos = first_data
    total_leds = 0
    while pos + 21 <= term_pos if term_pos > 0 else len(report):
        group_data = report[pos:pos+21]
        has_data = any(b != 0 for b in group_data)

        if has_data:
            if gap_count > 0:
                print(f"  --- Gap: {gap_count} zero bytes ---")
                gap_count = 0

            print(f"  Group {group_num} @{pos}:")
            for led in range(7):
                r = group_data[led*3]
                g = group_data[led*3 + 1]
                b = group_data[led*3 + 2]
                color = ""
                if r == 0xFF and g == 0 and b == 0: color = "RED"
                elif r == 0 and g == 0xFF and b == 0: color = "GREEN"
                elif r == 0 and g == 0 and b == 0xFF: color = "BLUE"
                elif r == 0xFF and g == 0xFF and b == 0: color = "YELLOW"
                elif r == 0xFF and g == 0 and b == 0xFF: color = "MAGENTA"
                elif r == 0 and g == 0xFF and b == 0xFF: color = "CYAN"
                elif r == 0xFF and g == 0xFF and b == 0xFF: color = "WHITE"
                elif r == 0 and g == 0 and b == 0: color = "(off)"
                else: color = f"({r},{g},{b})"

                led_global = total_leds + led
                print(f"    LED[{led_global:3d}] @{pos+led*3:3d}: "
                      f"R={r:02x} G={g:02x} B={b:02x}  {color}")

            total_leds += 7
            group_num += 1
            pos += 21
        else:
            gap_count += 21
            pos += 21

    if gap_count > 0:
        print(f"  --- Trailing gap: {gap_count} zero bytes ---")

    print(f"\n  Total LED groups: {group_num}")
    print(f"  Total LEDs: {total_leds}")


def analyze_config(report, label=""):
    """Analyze CONFIG_READ/WRITE response."""
    print(f"\n  === Config {label} ===")
    print(f"  Byte  9 (mode_config_1): 0x{report[9]:02x}")
    print(f"  Byte 10 (mode_config_2): 0x{report[10]:02x}")
    print(f"  Byte 11 (mode_config_3): 0x{report[11]:02x}")
    print(f"  Byte 14-15: 0x{report[14]:02x} 0x{report[15]:02x}")
    print(f"  Byte 16 (mode_count?): 0x{report[16]:02x} ({report[16]})")
    print(f"  Byte 17 (param_1): 0x{report[17]:02x} ({report[17]})")
    print(f"  Byte 18 (effect_id): 0x{report[18]:02x} ({report[18]})")
    print(f"  Byte 19 (param_2): 0x{report[19]:02x} ({report[19]})")

    # Matrix data at offset 64
    if len(report) > 68:
        print(f"\n  Matrix header @64-67: "
              f"{' '.join(f'{report[64+i]:02x}' for i in range(4))}")
        print(f"    Byte 66 (brightness): {report[66]} (0x{report[66]:02x})")
        print(f"    Byte 67: {report[67]} (0x{report[67]:02x})")

        print(f"  Key sizes @68+:")
        for i in range(68, min(len(report), 136), 2):
            if report[i] != 0 or (i+1 < len(report) and report[i+1] != 0):
                print(f"    @{i}: {report[i]:02x} {report[i+1]:02x}")


def main():
    filepath = sys.argv[1] if len(sys.argv) > 1 else 'capture.pcap'
    packets = parse_usbpcap(filepath)

    sessions = []
    cur = []
    for pkt in packets:
        if cur and len(cur) >= 8:
            sessions.append(cur)
            cur = []
        cur.append(pkt)
    if cur: sessions.append(cur)

    user_actions = [
        "W tusu mavi",
        "Tum tuslar kirmizi (brightness=4)",
        "Brightness 4->1",
        "Gokkusagi efekti (brightness=4, speed=4)",
    ]

    for si, session in enumerate(sessions):
        label = user_actions[si] if si < len(user_actions) else "?"
        print(f"\n{'='*70}")
        print(f"SESSION {si+1}: {label}")
        print(f"{'='*70}")

        for pkt in session:
            data = pkt['data']
            if pkt['dir'] == 'OUT' and len(data) > 8:
                report = data[8:]  # strip USB setup
            else:
                report = data

            if len(report) < 2:
                continue

            cmd = report[1]

            if cmd == 0x06 and pkt['dir'] == 'OUT' and len(report) > 100:
                analyze_0x06_command(report)
            elif cmd == 0x0A and pkt['dir'] == 'OUT' and len(report) > 100:
                analyze_0x0a_command(report)
            elif cmd == 0x84 and pkt['dir'] == 'IN':
                analyze_config(report, "READ response")
            elif cmd == 0x04 and pkt['dir'] == 'OUT' and len(report) > 40:
                analyze_config(report, "WRITE")


if __name__ == '__main__':
    main()
