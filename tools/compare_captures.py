#!/usr/bin/env python3
"""Compare 0x0A LED data and config between captures."""
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
            packets.append({'frame': frame_num, 'dir': 'OUT' if irp_info == 0 else 'IN', 'data': payload})
    return packets

def get_sessions(packets):
    sessions, cur = [], []
    for pkt in packets:
        if cur and len(cur) >= 8:
            sessions.append(cur)
            cur = []
        cur.append(pkt)
    if cur: sessions.append(cur)
    return sessions

def extract_report(pkt):
    if pkt['dir'] == 'OUT' and len(pkt['data']) > 8:
        return pkt['data'][8:]
    return pkt['data']

def main():
    files = sys.argv[1:]
    labels = [
        # capture1 sessions
        "C1-S1: W=mavi",
        "C1-S2: Tum=kirmizi",
        "C1-S3: Brightness 4->1",
        "C1-S4: Gokkusagi",
        # capture2 sessions
        "C2-S1: Tum=yesil",
        "C2-S2: Tum=beyaz",
        "C2-S3: LED kapat",
    ]

    all_sessions = []
    for f in files:
        packets = parse_usbpcap(f)
        sessions = get_sessions(packets)
        all_sessions.extend(sessions)

    print("=" * 70)
    print("0x0A CUSTOM PROFILE - LED 0 karsilastirmasi")
    print("=" * 70)

    for si, session in enumerate(all_sessions):
        label = labels[si] if si < len(labels) else f"Session {si+1}"
        for pkt in session:
            report = extract_report(pkt)
            if len(report) >= 40 and report[1] == 0x0A and pkt['dir'] == 'OUT':
                # LED 0 is at report offset 29 (after 8-byte header + 21-byte padding)
                led0_r = report[29]
                led0_g = report[30]
                led0_b = report[31]
                led1_r = report[32]
                led1_g = report[33]
                led1_b = report[34]
                led2_r = report[35]
                led2_g = report[36]
                led2_b = report[37]
                print(f"\n  {label}:")
                print(f"    LED[0]: R={led0_r:02x} G={led0_g:02x} B={led0_b:02x}")
                print(f"    LED[1]: R={led1_r:02x} G={led1_g:02x} B={led1_b:02x}")
                print(f"    LED[2]: R={led2_r:02x} G={led2_g:02x} B={led2_b:02x}")

    print("\n" + "=" * 70)
    print("CONFIG WRITE karsilastirmasi")
    print("=" * 70)

    for si, session in enumerate(all_sessions):
        label = labels[si] if si < len(labels) else f"Session {si+1}"
        for pkt in session:
            report = extract_report(pkt)
            if len(report) >= 68 and report[1] == 0x04 and pkt['dir'] == 'OUT':
                print(f"\n  {label}:")
                print(f"    effect_id = 0x{report[18]:02x} ({report[18]})")
                print(f"    byte_17   = 0x{report[17]:02x} ({report[17]})")
                print(f"    brightness = {report[66]}")
                print(f"    byte_67   = 0x{report[67]:02x}")

    print("\n" + "=" * 70)
    print("CONFIG READ response karsilastirmasi")
    print("=" * 70)

    for si, session in enumerate(all_sessions):
        label = labels[si] if si < len(labels) else f"Session {si+1}"
        for pkt in session:
            report = extract_report(pkt)
            if len(report) >= 68 and report[1] == 0x84 and pkt['dir'] == 'IN':
                print(f"\n  {label} (onceki durum):")
                print(f"    effect_id = 0x{report[18]:02x} ({report[18]})")
                print(f"    byte_17   = 0x{report[17]:02x}")
                print(f"    brightness = {report[66]}")


if __name__ == '__main__':
    main()
