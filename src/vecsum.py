#!/usr/bin/env python3
"""vecsum.py - patch the LPC/Cortex-M vector-table checksum at offset 0x1C.

Required only for images the boot ROM boots from SPIFI/flash on cold reset:
the ROM rejects the image unless word[7] = -(sum of words[0..6]) mod 2^32.
DFU RAM-loaded images (see mkbootimg.py) do NOT need this - verified against
the stock P36Updater, whose vector checksum is intentionally absent.
"""
import struct, sys
def patch(b: bytes) -> bytes:
    a = bytearray(b)
    s = sum(struct.unpack('<7I', a[0:28])) & 0xffffffff
    struct.pack_into('<I', a, 28, (-s) & 0xffffffff)
    return bytes(a)
if __name__ == '__main__':
    data = patch(open(sys.argv[1], 'rb').read())
    open(sys.argv[2] if len(sys.argv) > 2 else sys.argv[1], 'wb').write(data)
    print(f"[+] patched vector checksum: word[7]={struct.unpack('<I', data[28:32])[0]:#010x}")
