#!/usr/bin/env python3
"""usbmon.py - read what an image using src/usb_mon.h says, and send it a byte.

    usbmon.py                 print its text until interrupted
    usbmon.py --for 30        ... for 30 seconds
    usbmon.py --send D        send one command byte (a character or 0xNN) first
    usbmon.py --send C,0,9,1  ... with up to three more bytes (for pedal.c's C:
                              a control message, as kind, control, value)
    usbmon.py --wait 20       wait up to that long for the device to appear

Needs pyusb (and libusb, which dfu-util brings)."""
import argparse
import sys
import time

import usb.core

VID, PID = 0x1209, 0x0001


def find(wait):
    end = time.time() + wait
    while True:
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is not None or time.time() >= end:
            return dev
        time.sleep(0.2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--for', dest='secs', type=float, default=None)
    ap.add_argument('--send', default=None)
    ap.add_argument('--wait', type=float, default=10)
    a = ap.parse_args()
    dev = find(a.wait)
    if dev is None:
        sys.exit('no device %04x:%04x' % (VID, PID))
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
