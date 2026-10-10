#!/usr/bin/env python3
"""dfu_load.py - minimal DFU downloader for the LPC43xx boot ROM (VID 1fc9:000c).

Sends the image in small single-packet DNLOAD blocks with the full DFU status
handshake, to get past the macOS dfu-util 0.11 download failure. Non-destructive:
the LPC ROM DFU loads into RAM and executes; a power-cycle restores stock.

Usage: dfu_load.py <image.dfu> [blocksize]
"""
import sys, os, time
import usb.core, usb.util, usb.backend.libusb1

VID, PID, INTF = 0x1fc9, 0x000c, 0

def backend():
    be = usb.backend.libusb1.get_backend()
    if be: return be
    for p in ('/opt/homebrew/lib/libusb-1.0.dylib','/usr/local/lib/libusb-1.0.dylib'):
        if os.path.exists(p):
            return usb.backend.libusb1.get_backend(find_library=lambda x: p)
    return None

# DFU class requests
DNLOAD, GETSTATUS, CLRSTATUS, GETSTATE = 1, 3, 4, 5
def get_status(dev):
    d = dev.ctrl_transfer(0xA1, GETSTATUS, 0, INTF, 6, timeout=5000)
    status = d[0]; poll = d[1] | (d[2]<<8) | (d[3]<<16); state = d[4]
    return status, poll, state
def clr_status(dev):
    dev.ctrl_transfer(0x21, CLRSTATUS, 0, INTF, None, timeout=5000)
def dnload(dev, block, data):
    return dev.ctrl_transfer(0x21, DNLOAD, block, INTF, data, timeout=5000)

def main(path, bs):
    img = open(path,'rb').read()
    dev = usb.core.find(idVendor=VID, idProduct=PID, backend=backend())
    if dev is None: sys.exit("no LPC DFU device (1fc9:000c) — is it in DFU mode?")
    try: dev.set_configuration()
    except Exception: pass
    st, poll, state = get_status(dev)
    print(f"initial DFU state={state} status={st}")
    if state != 2:            # not dfuIDLE
        clr_status(dev); get_status(dev)
    print(f"image {len(img)} bytes, block size {bs}")
    block = 0
    for off in range(0, len(img), bs):
        chunk = img[off:off+bs]
        n = dnload(dev, block, chunk)
        st, poll, state = get_status(dev)         # advances the state machine
        if poll: time.sleep(poll/1000.0)
        if st != 0:
            sys.exit(f"DFU error: status={st} state={state} at block {block}")
        block += 1
        if block % 8 == 0 or off+bs >= len(img):
            print(f"  sent {min(off+bs,len(img))}/{len(img)} bytes", flush=True)
    # zero-length DNLOAD -> manifest / execute
    dnload(dev, block, b'')
    try:
        st, poll, state = get_status(dev)
        print(f"manifest: state={state} status={st}")
    except Exception as e:
        print(f"(device left DFU after final block: {e}) — expected: it is now running the image")
    print("DONE: image sent; the LPC ROM should now execute it from RAM.")

if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 64)
