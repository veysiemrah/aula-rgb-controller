#!/usr/bin/env python3
"""
Aula F87 Pro USB HID packet analyzer.

Usage:
    python pcap_parser.py <capture.pcap>
    python pcap_parser.py diff <a.pcap> <b.pcap>
    python pcap_parser.py --vid 258a --pid 0049 <capture.pcap>
"""
import sys
import struct
from pathlib import Path

try:
    import dpkt
except ImportError:
    print("Install dpkt: pip install dpkt")
    sys.exit(1)


def read_pcap_usb(filepath, vid=None, pid=None):
    """Read USB packets from pcap file."""
    packets = []
    with open(filepath, 'rb') as f:
        try:
            pcap = dpkt.pcap.Reader(f)
        except ValueError:
            pcap = dpkt.pcapng.Reader(f)

        for ts, buf in pcap:
            if len(buf) < 64:
                continue
            header_len = struct.unpack_from('<H', buf, 0)[0]
            if len(buf) <= header_len:
                continue

            direction = buf[header_len - 1] if header_len > 0 else 0
            payload = buf[header_len:]

            if len(payload) >= 1:
                packets.append({
                    'timestamp': ts,
                    'direction': 'OUT' if direction == 0 else 'IN',
                    'data': payload[:64],
                    'length': len(payload)
                })
    return packets


def hexdump(data, prefix=""):
    """Format bytes as hex dump."""
    lines = []
    for i in range(0, len(data), 16):
        hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
        lines.append(f"{prefix}{i:04x}: {hex_part:<48s}  {ascii_part}")
    return '\n'.join(lines)


def analyze(filepath, vid=None, pid=None):
    """Analyze single pcap file."""
    packets = read_pcap_usb(filepath, vid, pid)
    print(f"File: {filepath}")
    print(f"Total packets: {len(packets)}")
    print(f"OUT packets: {sum(1 for p in packets if p['direction'] == 'OUT')}")
    print(f"IN packets:  {sum(1 for p in packets if p['direction'] == 'IN')}")
    print("=" * 70)

    for i, pkt in enumerate(packets):
        print(f"\n[{i:04d}] {pkt['direction']} ({pkt['length']} bytes) t={pkt['timestamp']:.6f}")
        print(hexdump(pkt['data'], "  "))


def diff(file_a, file_b):
    """Compare two captures, highlight differences."""
    pkts_a = read_pcap_usb(file_a)
    pkts_b = read_pcap_usb(file_b)

    out_a = [p for p in pkts_a if p['direction'] == 'OUT']
    out_b = [p for p in pkts_b if p['direction'] == 'OUT']

    print(f"Comparing OUT packets:")
    print(f"  A ({file_a}): {len(out_a)} packets")
    print(f"  B ({file_b}): {len(out_b)} packets")
    print("=" * 70)

    count = min(len(out_a), len(out_b))
    for i in range(count):
        a_data = out_a[i]['data']
        b_data = out_b[i]['data']
        if a_data != b_data:
            print(f"\n[{i:04d}] DIFFERS:")
            diffs = []
            for j in range(min(len(a_data), len(b_data))):
                if a_data[j] != b_data[j]:
                    diffs.append(f"  byte[{j:2d}]: A=0x{a_data[j]:02x}  B=0x{b_data[j]:02x}")
            print('\n'.join(diffs))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    if sys.argv[1] == 'diff' and len(sys.argv) >= 4:
        diff(sys.argv[2], sys.argv[3])
    else:
        vid = pid = None
        args = sys.argv[1:]
        filepath = args[-1]
        for i, a in enumerate(args[:-1]):
            if a == '--vid' and i + 1 < len(args):
                vid = int(args[i + 1], 16)
            if a == '--pid' and i + 1 < len(args):
                pid = int(args[i + 1], 16)
        analyze(filepath, vid, pid)
