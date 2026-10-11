#!/usr/bin/env python3
"""usbmon.py - read what an image using src/usb_mon.h says, and send it a byte.

    usbmon.py                 print its text until interrupted
    usbmon.py --for 30        ... for 30 seconds
    usbmon.py --send D        send one command byte (a character or 0xNN) first
    usbmon.py --send C,0,9,1  ... with up to three more bytes (for pedal.c's C:
                              a control message, as kind, control, value)
    usbmon.py --wait 20       wait up to that long for the device to appear
    usbmon.py --shot s.png    ask stomp.c for its screen (command F) and write it

Needs pyusb (and libusb, which dfu-util brings)."""
import argparse
import struct
import sys
import time
import zlib

import usb.core

VID, PID = 0x1209, 0x0001


def find(wait):
    end = time.time() + wait
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None or time.time() >= end:
            return dev
        time.sleep(0.2)


def png(path, w, h, px):
    """Write w x h pixels of 5:6:5 as a PNG."""
    rows = b''
    for y in range(h):
        rows += b'\0' + b''.join(bytes(((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
                                 for v in px[y * w:(y + 1) * w])

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def shot(dev, path, secs=30):
    """stomp.c's answer to F: 'shot 320 240\\n', then runs of one colour, each a
    count less one and the colour's two bytes, low first."""
    dev.ctrl_transfer(0x40, 2, ord('F'), 0, None, timeout=500)
    head, w, h = b'shot 320 240\n', 320, 240
    got, px, end = b'', [], time.time() + secs
    while len(px) < w * h:
        if time.time() > end:
            sys.exit('shot: %d of %d pixels, then nothing' % (len(px), w * h))
        try:
            got += bytes(dev.ctrl_transfer(0xC0, 1, 0, 0, 64, timeout=500))
        except usb.core.USBTimeoutError:
            continue
        if head is not None:
            at = got.find(head)
            if at < 0:
                continue
            got, head = got[at + len(head):], None
        while len(got) >= 3:
            px += [got[1] | got[2] << 8] * (got[0] + 1)
            got = got[3:]
    png(path, w, h, px[:w * h])
    print(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--for', dest='secs', type=float, default=None)
    ap.add_argument('--send', default=None)
    ap.add_argument('--wait', type=float, default=10)
    ap.add_argument('--shot', default=None)
    a = ap.parse_args()
    dev = find(a.wait)
    if dev is None:
        sys.exit('no device %04x:%04x' % (VID, PID))
    if a.shot is not None:
        shot(dev, a.shot)
        return
    if a.send is not None:
        f = a.send.split(',')
        b = int(f[0], 16) if f[0].lower().startswith('0x') else ord(f[0][0])
        x = [int(v, 0) & 0xFF for v in f[1:4]] + [0, 0, 0]
        try:
            dev.ctrl_transfer(0x40, 2, b | x[0] << 8, x[1] | x[2] << 8, None, timeout=500)
        except usb.core.USBError as e:      # it may be gone before it answers
            print('send:', e, file=sys.stderr)
        if a.secs is None:
            return
    end = None if a.secs is None else time.time() + a.secs
    try:
        while end is None or time.time() < end:
            try:
                data = bytes(dev.ctrl_transfer(0xC0, 1, 0, 0, 64, timeout=500))
            except usb.core.USBTimeoutError:        # it is busy for a moment
                continue
            if data:
                sys.stdout.write(data.decode('ascii', 'replace'))
                sys.stdout.flush()
            else:
                time.sleep(0.01)
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
