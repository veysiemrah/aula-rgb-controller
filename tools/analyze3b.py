#!/usr/bin/env python3
"""Analyze capture3 - decode CMD_0x20 and model query details."""
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

    print("=" * 70)
    print("MODEL QUERY RESPONSES")
    print("=" * 70)
    for pkt in packets:
        data = pkt['data']
        if pkt['dir'] == 'IN' and len(data) >= 2 and data[1] == 0x82:
            print(f"\n  Frame {pkt['frame']}: {len(data)} bytes")
            print(f"  Full: {' '.join(f'{b:02x}' for b in data)}")
            for i, b in enumerate(data):
                if b != 0:
                    print(f"    Byte {i:2d}: 0x{b:02x} ({b:3d}) {chr(b) if 32 <= b < 127 else ''}")

    print("\n" + "=" * 70)
    print("ALL CMD_0x20 PACKETS")
    print("=" * 70)
    for pkt in packets:
        data = pkt['data']
        if pkt['dir'] == 'OUT' and len(data) > 8:
            report = data[8:]
            if len(report) >= 2 and report[1] == 0x20:
                print(f"\n  Frame {pkt['frame']} {pkt['dir']}:")
                print(f"  Report first 32 bytes: {' '.join(f'{b:02x}' for b in report[:32])}")
                # Show all non-zero bytes
                for i in range(len(report)):
                    if report[i] != 0:
                        print(f"    Byte {i:3d}: 0x{report[i]:02x} ({report[i]:3d})")
        elif pkt['dir'] == 'IN' and len(data) >= 2 and data[1] == 0x20:
            print(f"\n  Frame {pkt['frame']} {pkt['dir']}:")
            print(f"  Response: {' '.join(f'{b:02x}' for b in data[:32])}")

    # Also check for responses after CMD_0x20 (empty ACKs)
    print("\n" + "=" * 70)
    print("CONFIG WRITE DIFFS (session by session)")
    print("=" * 70)

    sessions, cur = [], []
    for pkt in packets:
        if cur and pkt['ts'] - cur[-1]['ts'] > 5:
            sessions.append(cur)
            cur = []
        cur.append(pkt)
    if cur: sessions.append(cur)

    labels = [
        "Handshake (yazilim acilis)",
        "Respire efekti sec (brightness=4, speed=4)",
        "Speed 4->1 (brightness=4)",
        "Baska degisiklik?",
    ]

    for si, session in enumerate(sessions):
        label = labels[si] if si < len(labels) else f"Session {si+1}"
        reads, writes = [], []
        for pkt in session:
            data = pkt['data']
            if pkt['dir'] == 'IN' and len(data) >= 68 and data[1] == 0x84:
                reads.append(data)
            elif pkt['dir'] == 'OUT' and len(data) > 8:
                report = data[8:]
                if len(report) >= 68 and report[1] == 0x04:
                    writes.append(report)

        print(f"\n  {label}:")
        for r in reads:
            print(f"    CONFIG READ:  bytes 16-19: {' '.join(f'{r[i]:02x}' for i in range(16, 20))}"
                  f"  brightness={r[66]}")
        for w in writes:
            print(f"    CONFIG WRITE: bytes 16-19: {' '.join(f'{w[i]:02x}' for i in range(16, 20))}"
                  f"  brightness={w[66]}")


if __name__ == '__main__':
    main()
