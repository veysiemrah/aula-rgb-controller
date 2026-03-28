#!/usr/bin/env python3
"""Analyze USBPcap capture for Aula F87 Pro HID control transfers."""
import struct
import sys

def parse_usbpcap(filepath):
    """Parse USBPcap pcap file and extract control transfers."""
    with open(filepath, 'rb') as f:
        # Read pcap global header (24 bytes)
        magic = f.read(4)
        if magic == b'\xd4\xc3\xb2\xa1':
            endian = '<'
        elif magic == b'\xa1\xb2\xc3\xd4':
            endian = '>'
        else:
            print(f"Unknown pcap magic: {magic.hex()}")
            return []

        ver_major, ver_minor, tz, sigfigs, snaplen, linktype = struct.unpack(
            endian + 'HHiIII', f.read(20))
        print(f"Link type: {linktype} (249=USBPcap)")

        packets = []
        frame_num = 0

        while True:
            # Read packet header (16 bytes)
            hdr = f.read(16)
            if len(hdr) < 16:
                break

            ts_sec, ts_usec, incl_len, orig_len = struct.unpack(
                endian + 'IIII', hdr)

            data = f.read(incl_len)
            if len(data) < incl_len:
                break

            frame_num += 1

            # Parse USBPcap header
            if len(data) < 27:
                continue

            header_len = struct.unpack_from('<H', data, 0)[0]
            irp_id = struct.unpack_from('<Q', data, 2)[0]
            irp_status = struct.unpack_from('<i', data, 10)[0]
            urb_func = struct.unpack_from('<H', data, 14)[0]
            irp_info = data[16]  # 0=OUT direction (to device), 1=IN (from device)
            bus_id = struct.unpack_from('<H', data, 17)[0]
            device = struct.unpack_from('<H', data, 19)[0]
            endpoint = data[21]
            transfer_type = data[22]  # 0=isoch, 1=interrupt, 2=control, 3=bulk

            if transfer_type != 2:  # Only control transfers
                continue

            payload = data[header_len:]

            direction = "OUT" if irp_info == 0 else "IN"

            # For control transfers, parse setup packet if present
            setup = None
            if header_len >= 28 + 8 and irp_info == 0:
                # Setup packet is at offset 28 in USBPcap header
                setup_offset = 28
                if header_len > setup_offset:
                    bmRequestType = data[setup_offset]
                    bRequest = data[setup_offset + 1]
                    wValue = struct.unpack_from('<H', data, setup_offset + 2)[0]
                    wIndex = struct.unpack_from('<H', data, setup_offset + 4)[0]
                    wLength = struct.unpack_from('<H', data, setup_offset + 6)[0]
                    setup = {
                        'bmRequestType': bmRequestType,
                        'bRequest': bRequest,
                        'wValue': wValue,
                        'wIndex': wIndex,
                        'wLength': wLength,
                    }

            packets.append({
                'frame': frame_num,
                'timestamp': ts_sec + ts_usec / 1e6,
                'direction': direction,
                'device': device,
                'endpoint': endpoint,
                'payload': payload,
                'payload_len': len(payload),
                'setup': setup,
            })

    return packets


def hexdump_line(data, max_bytes=64):
    """Single line hex dump."""
    hex_part = ' '.join(f'{b:02x}' for b in data[:max_bytes])
    if len(data) > max_bytes:
        hex_part += ' ...'
    return hex_part


def main():
    filepath = sys.argv[1] if len(sys.argv) > 1 else 'capture.pcap'

    packets = parse_usbpcap(filepath)

    print(f"\nTotal control transfer packets: {len(packets)}")
    print(f"OUT: {sum(1 for p in packets if p['direction'] == 'OUT')}")
    print(f"IN:  {sum(1 for p in packets if p['direction'] == 'IN')}")
    print()

    # Group by timestamp proximity (sessions)
    sessions = []
    current_session = []
    last_ts = 0

    for pkt in packets:
        if current_session and pkt['timestamp'] - last_ts > 5:
            sessions.append(current_session)
            current_session = []
        current_session.append(pkt)
        last_ts = pkt['timestamp']
    if current_session:
        sessions.append(current_session)

    print(f"Sessions detected: {len(sessions)}")
    print("=" * 80)

    for si, session in enumerate(sessions):
        t0 = session[0]['timestamp']
        print(f"\n{'='*80}")
        print(f"SESSION {si+1} (t={t0:.1f}s, {len(session)} packets)")
        print(f"{'='*80}")

        for pkt in session:
            dt = pkt['timestamp'] - t0
            setup_str = ""
            if pkt['setup']:
                s = pkt['setup']
                req_name = {0x01: 'GET_REPORT', 0x09: 'SET_REPORT'}.get(
                    s['bRequest'], f"0x{s['bRequest']:02x}")
                report_type = (s['wValue'] >> 8)
                report_id = s['wValue'] & 0xFF
                type_name = {1: 'Input', 2: 'Output', 3: 'Feature'}.get(
                    report_type, f"type{report_type}")
                setup_str = (f" [{req_name} {type_name} ID={report_id} "
                           f"iface={s['wIndex']} len={s['wLength']}]")

            payload = pkt['payload']

            # Identify command from first bytes
            cmd_str = ""
            if len(payload) >= 2:
                rid = payload[0]
                cmd = payload[1]
                cmd_names = {
                    0x04: 'CONFIG_WRITE',
                    0x06: 'LED_COLOR',
                    0x08: 'LED_DIRECT',
                    0x82: 'MODEL_QUERY',
                    0x84: 'CONFIG_READ',
                }
                cmd_str = f" CMD={cmd_names.get(cmd, f'0x{cmd:02x}')}"

            print(f"\n  [{pkt['frame']:5d}] {pkt['direction']:3s} dev={pkt['device']} "
                  f"ep=0x{pkt['endpoint']:02x} +{dt:.3f}s "
                  f"({pkt['payload_len']} bytes){setup_str}{cmd_str}")

            if len(payload) > 0:
                # Show first 48 bytes
                print(f"    {hexdump_line(payload, 48)}")

                # Find non-zero regions in the payload
                nonzero_regions = []
                i = 48
                while i < len(payload):
                    if payload[i] != 0:
                        start = i
                        while i < len(payload) and payload[i] != 0:
                            i += 1
                        nonzero_regions.append((start, payload[start:i]))
                    i += 1

                for offset, region in nonzero_regions:
                    hex_str = ' '.join(f'{b:02x}' for b in region[:32])
                    if len(region) > 32:
                        hex_str += ' ...'
                    print(f"    @{offset:3d}: {hex_str}")


if __name__ == '__main__':
    main()
