#!/usr/bin/env python3
"""Decode Aula F87 Pro USB HID packets from capture."""
import struct
import sys

def parse_usbpcap(filepath):
    with open(filepath, 'rb') as f:
        magic = f.read(4)
        endian = '<' if magic == b'\xd4\xc3\xb2\xa1' else '>'
        f.read(20)  # rest of global header

        packets = []
        frame_num = 0
        while True:
            hdr = f.read(16)
            if len(hdr) < 16:
                break
            ts_sec, ts_usec, incl_len, orig_len = struct.unpack(endian + 'IIII', hdr)
            data = f.read(incl_len)
            if len(data) < incl_len:
                break
            frame_num += 1

            if len(data) < 27:
                continue
            header_len = struct.unpack_from('<H', data, 0)[0]
            irp_info = data[16]
            device = struct.unpack_from('<H', data, 19)[0]
            transfer_type = data[22]

            if transfer_type != 2:
                continue

            payload = data[header_len:]
            packets.append({
                'frame': frame_num,
                'ts': ts_sec + ts_usec / 1e6,
                'dir': 'OUT' if irp_info == 0 else 'IN',
                'device': device,
                'data': payload,
            })
    return packets


def decode_report(data, direction):
    """Decode HID report data (after stripping USB setup)."""
    # For OUT packets, first 8 bytes are USB setup
    if direction == 'OUT' and len(data) > 8:
        setup = data[:8]
        report = data[8:]
        setup_info = {
            'bmRequestType': setup[0],
            'bRequest': setup[1],
            'wValue': struct.unpack_from('<H', setup, 2)[0],
            'wIndex': struct.unpack_from('<H', setup, 4)[0],
            'wLength': struct.unpack_from('<H', setup, 6)[0],
        }
    else:
        report = data
        setup_info = None

    if len(report) < 2:
        return setup_info, report, {}

    report_id = report[0]
    command = report[1]

    cmd_names = {
        0x04: 'CONFIG_WRITE',
        0x06: 'LED_SINGLE',
        0x08: 'LED_DIRECT',
        0x0A: 'LED_CUSTOM',
        0x82: 'MODEL_QUERY',
        0x84: 'CONFIG_READ',
    }

    info = {
        'report_id': report_id,
        'command': command,
        'command_name': cmd_names.get(command, f'UNKNOWN_0x{command:02x}'),
        'header': report[:8],
    }

    return setup_info, report, info


def analyze_led_data(report, command):
    """Try to decode LED color data."""
    if command == 0x06:
        # Single key mode: header + planar RGB?
        print(f"    Header: {' '.join(f'{b:02x}' for b in report[:8])}")
        print(f"    Byte 6-7: count={report[6]} ({hex(report[6])}), mode={report[7]}")

        # Find non-zero bytes in data area
        for i in range(8, len(report)):
            if report[i] != 0:
                print(f"    @{i:3d} (0x{i:03x}): 0x{report[i]:02x}")

    elif command == 0x0A:
        # Custom color mode
        print(f"    Header: {' '.join(f'{b:02x}' for b in report[:8])}")

        # Check for 5A A5 terminator
        for i in range(len(report) - 1):
            if report[i] == 0x5A and report[i+1] == 0xA5:
                print(f"    Terminator 5A A5 at offset {i}")
                data_len = i - 8
                print(f"    Data area: bytes 8-{i-1} ({data_len} bytes)")

        # Dump non-zero data area
        print(f"    Non-zero data bytes:")
        for i in range(8, min(len(report), 520)):
            if report[i] != 0:
                print(f"      @{i:3d}: 0x{report[i]:02x} ({report[i]:3d})")

    elif command == 0x04:
        # Config write
        print(f"    Header: {' '.join(f'{b:02x}' for b in report[:8])}")
        print(f"    Config data (bytes 8-40):")
        for i in range(8, min(40, len(report))):
            if report[i] != 0:
                print(f"      @{i:3d}: 0x{report[i]:02x}")

        # Effect/mode bytes
        if len(report) > 18:
            print(f"    Byte 16-19: {' '.join(f'{b:02x}' for b in report[16:20])}")
            print(f"      brightness={report[17]}, effect_id=0x{report[18]:02x} ({report[18]})")

        # Find keyboard matrix data
        for i in range(40, len(report)):
            if report[i] != 0:
                end = i
                while end < len(report) and report[end] != 0:
                    end += 1
                print(f"    Matrix data @{i}-{end}: {' '.join(f'{b:02x}' for b in report[i:min(end, i+60)])}")
                if end - i > 60:
                    print(f"      ... ({end-i} bytes total)")
                break

    elif command == 0x84:
        # Config read response
        print(f"    Full response ({len(report)} bytes):")
        for off in range(0, min(len(report), 128), 16):
            hex_str = ' '.join(f'{report[off+j]:02x}' for j in range(min(16, len(report)-off)))
            print(f"      {off:04x}: {hex_str}")


def main():
    filepath = sys.argv[1] if len(sys.argv) > 1 else 'capture.pcap'
    packets = parse_usbpcap(filepath)

    # Group into sessions (>5s gap)
    sessions = []
    cur = []
    for pkt in packets:
        if cur and pkt['ts'] - cur[-1]['ts'] > 5:
            sessions.append(cur)
            cur = []
        cur.append(pkt)
    if cur:
        sessions.append(cur)

    for si, session in enumerate(sessions):
        print(f"\n{'='*70}")
        print(f"SESSION {si+1}")
        print(f"{'='*70}")

        for pkt in session:
            setup, report, info = decode_report(pkt['data'], pkt['dir'])

            if not info:
                continue

            req_str = ""
            if setup:
                req = {0x01: 'GET_REPORT', 0x09: 'SET_REPORT'}.get(
                    setup['bRequest'], f"0x{setup['bRequest']:02x}")
                req_str = f" [{req}]"

            print(f"\n  Frame {pkt['frame']:5d} {pkt['dir']}{req_str}"
                  f" — {info['command_name']} (0x{info['command']:02x})"
                  f" — {len(report)} bytes")

            analyze_led_data(report, info['command'])


if __name__ == '__main__':
    main()
