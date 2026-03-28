#!/usr/bin/env python3
"""Analyze capture3 - handshake, speed, model query."""
import struct, sys

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
                'ts': ts_sec + ts_usec / 1e6,
                'dir': 'OUT' if irp_info == 0 else 'IN',
                'data': payload,
            })
    return packets

def main():
    packets = parse_usbpcap(sys.argv[1] if len(sys.argv) > 1 else 'capture3.pcap')

    # Group into sessions by 5s gap
    sessions, cur = [], []
    for pkt in packets:
        if cur and pkt['ts'] - cur[-1]['ts'] > 5:
            sessions.append(cur)
            cur = []
        cur.append(pkt)
    if cur: sessions.append(cur)

    print(f"Total sessions: {len(sessions)}")
    print(f"Total control packets: {len(packets)}")

    for si, session in enumerate(sessions):
        print(f"\n{'='*70}")
        print(f"SESSION {si+1} ({len(session)} packets)")
        print(f"{'='*70}")

        for pkt in session:
            data = pkt['data']
            if pkt['dir'] == 'OUT' and len(data) > 8:
                report = data[8:]
            else:
                report = data

            if len(report) < 2:
                continue

            cmd = report[1]
            cmd_names = {
                0x04: 'CONFIG_WRITE', 0x06: 'LED_SINGLE', 0x0A: 'LED_CUSTOM',
                0x82: 'MODEL_QUERY', 0x84: 'CONFIG_READ',
            }
            name = cmd_names.get(cmd, f'CMD_0x{cmd:02x}')

            print(f"\n  Frame {pkt['frame']:5d} {pkt['dir']:3s} -- {name}")

            if cmd == 0x82:
                print(f"    Query: {' '.join(f'{b:02x}' for b in report[:14])}")
                if pkt['dir'] == 'IN':
                    print(f"    Response length: {len(report)} bytes")
                    if len(report) >= 14:
                        print(f"    Byte  8: 0x{report[8]:02x} ({report[8]})")
                        print(f"    Byte 12: 0x{report[12]:02x} ({report[12]})")
                        print(f"    Byte 13: 0x{report[13]:02x} ({report[13]}) <-- model ID?")
                        # Check for F87 Pro model 0x0B
                        for i in range(len(report)):
                            if report[i] == 0x0B:
                                print(f"    ** 0x0B found at byte {i} **")

            elif cmd == 0x84 and pkt['dir'] == 'IN':
                print(f"    Config response ({len(report)} bytes):")
                print(f"    Byte 17 (param):      0x{report[17]:02x} ({report[17]})")
                print(f"    Byte 18 (effect_id):   0x{report[18]:02x} ({report[18]})")
                if len(report) > 66:
                    print(f"    Byte 66 (brightness):  {report[66]}")
                # Look for speed/direction fields - dump bytes 8-40
                print(f"    Bytes 8-39: {' '.join(f'{report[i]:02x}' for i in range(8, min(40, len(report))))}")

            elif cmd == 0x04:
                print(f"    Config write:")
                print(f"    Byte 17 (param):      0x{report[17]:02x} ({report[17]})")
                print(f"    Byte 18 (effect_id):   0x{report[18]:02x} ({report[18]})")
                if len(report) > 66:
                    print(f"    Byte 66 (brightness):  {report[66]}")
                # Full bytes 8-40 for diff
                print(f"    Bytes 8-39: {' '.join(f'{report[i]:02x}' for i in range(8, min(40, len(report))))}")
                # Bytes 40-68
                if len(report) > 68:
                    print(f"    Bytes 40-67: {' '.join(f'{report[i]:02x}' for i in range(40, min(68, len(report))))}")

            elif cmd == 0x0A:
                led0 = report[29:32] if len(report) > 31 else b'\x00\x00\x00'
                print(f"    LED[0]: R={led0[0]:02x} G={led0[1]:02x} B={led0[2]:02x}")


if __name__ == '__main__':
    main()
