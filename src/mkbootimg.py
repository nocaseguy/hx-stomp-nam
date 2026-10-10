#!/usr/bin/env python3
"""mkbootimg.py - wrap a raw Cortex-M binary in an LPC43xx USB-DFU boot header.

Header (little-endian), the form that runs on the HX Stomp's boot ROM:

    offset 0 : 0xDA          AES not active (0x1A) | no hash (0xC0)
    offset 1 : 0xFF
    offset 2 : uint16 nblk   number of 512-byte code blocks that follow
    offset 4 : 12 x 0xFF
    offset 16: <code...>     zero-padded up to nblk * 512 bytes

The ROM loads the code into internal SRAM at 0x10000000 and starts it from its
vector table (initial SP, reset vector). Two things found on the unit:
  - the first DFU block must be a full 2048 bytes: a shorter one makes the ROM
    drop off USB, so small images are padded to four blocks
  - with the header of the stock P36Updater image (1A 00 nblk, zeros) an image
    of ours did not come back from a test that this form passed
"""
import struct, sys
def build(code: bytes) -> bytes:
    nblk = max((len(code) + 511) // 512, 4)    # the ROM wants a full first DFU block (2048 bytes)
    padded = code + b'\x00' * (nblk * 512 - len(code))
    return struct.pack('<BBH', 0xDA, 0xFF, nblk) + b'\xff' * 12 + padded
if __name__ == '__main__':
    code = open(sys.argv[1], 'rb').read()
    img = build(code)
    open(sys.argv[2], 'wb').write(img)
    print(f"[+] {sys.argv[2]}: {len(img)} bytes = 16 + {(len(img)-16)//512}*512 (code {len(code)} B)")
