#!/usr/bin/env python3
"""Read-only Linux verification of the complete v1 64 MiB probe file."""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

SIZE = 64 * 1024 * 1024

def block(offset, length):
    data = bytearray(length)
    for pos in range(0, length, 4):
        x = ((offset + pos) // 4) ^ 0xA53C9E17
        x = ((x ^ (x >> 16)) * 0x7FEB352D) & 0xFFFFFFFF
        x = ((x ^ (x >> 15)) * 0x846CA68B) & 0xFFFFFFFF
        struct.pack_into('<I', data, pos, x ^ (x >> 16))
    return data

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('file', type=Path)
    args = p.parse_args()
    if args.file.stat().st_size != SIZE:
        print(f'FAIL: expected {SIZE} bytes'); return 1
    digest = hashlib.sha256()
    with args.file.open('rb') as source:
        for offset in range(0, SIZE, 256*1024):
            got = source.read(256*1024)
            expected = block(offset, len(got))
            if got != expected or len(got) != 256*1024:
                first = next((i for i,(a,b) in enumerate(zip(expected,got)) if a!=b),len(got))
                print(f'FAIL: mismatch/short read at 0x{offset+first:08x}'); return 1
            digest.update(got)
    print('PASS: every byte matches the v1 pattern')
    print('SHA256:',digest.hexdigest())
    return 0

if __name__ == '__main__':
    sys.exit(main())
