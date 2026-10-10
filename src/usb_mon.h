/* usb_mon.h - a line of text to the computer and a byte back, over the USB
 * socket the image was loaded through.
 *
 * The smallest USB device that does that: no class, no endpoints but the
 * control one, polled, full speed. The computer asks with vendor requests
 * (tools/usbmon.py):
 *   IN  request 1, length 64   up to 63 bytes of what usb_putc() has queued
 *   OUT request 2              a command for usb_command(): wValue, and wIndex
 *                              above it
 * The controller is the LPC43xx's USB0 (0x40006000). The boot ROM has just
 * used it for DFU, so its clock (PLL0USB) and transceiver are already up.
 *
 * usb_start() once, then usb_poll() often: the host wants its answers within
 * some tens of milliseconds. */
#ifndef USB_MON_H
#define USB_MON_H
#include <stdint.h>
#include "lpc4350.h"

#define USB0(o)             REG32(0x40006000u + (o))
#define USB_CMD             USB0(0x140)
#define USB_STS             USB0(0x144)
#define USB_DEVICEADDR      USB0(0x154)
#define USB_LISTADDR        USB0(0x158)
#define USB_PORTSC1         USB0(0x184)
#define USB_MODE            USB0(0x1A8)
#define USB_SETUPSTAT       USB0(0x1AC)
#define USB_PRIME           USB0(0x1B0)
#define USB_FLUSH           USB0(0x1B4)
#define USB_COMPLETE        USB0(0x1BC)
#define USB_CTRL0           USB0(0x1C0)

#define USB_VID 0x1209u     /* pid.codes */
#define USB_PID 0x0001u     /* its number for tests */

struct usb_td { uint32_t next, token, page[5], pad; };
struct usb_qh { uint32_t cap, current; struct usb_td td; uint32_t setup[2], pad[4]; };

static struct usb_qh usb_qh[12] __attribute__((aligned(2048)));     /* only 0 (out) and 1 (in) used */
static struct usb_td usb_td[2] __attribute__((aligned(32)));
static uint8_t usb_reply[64] __attribute__((aligned(4)));
static uint8_t usb_ring[2048];
static unsigned usb_head, usb_tail;
static uint32_t usb_cmd;
static int usb_cmd_new;

static const uint8_t usb_device[18] = {
    18, 1, 0x10, 0x01, 0xFF, 0, 0, 64, USB_VID & 0xFF, USB_VID >> 8, USB_PID & 0xFF, USB_PID >> 8,
    0x00, 0x01, 0, 0, 0, 1,
};
static const uint8_t usb_config[18] = {
    9, 2, 18, 0, 1, 1, 0, 0x80, 50,             /* one interface, bus powered or so */
    9, 4, 0, 0, 0, 0xFF, 0, 0, 0,               /* vendor's own, no endpoints */
};

static void usb_putc(char c) {
    unsigned next = (usb_head + 1) % sizeof usb_ring;
    if (next == usb_tail) return;               /* full: drop */
    usb_ring[usb_head] = (uint8_t)c;
    usb_head = next;
}
static void usb_puts(const char *s) { while (*s) usb_putc(*s++); }
static void usb_puthex(uint32_t v, unsigned digits) {
    while (digits--) usb_putc("0123456789ABCDEF"[v >> (4 * digits) & 15u]);
}

/* 1 and the computer's last command in *c (wValue | wIndex << 16), or 0. */
static int usb_command(uint32_t *c) {
    if (!usb_cmd_new) return 0;
    usb_cmd_new = 0;
    *c = usb_cmd;
    return 1;
}

static void usb_xfer(unsigned in, const void *p, unsigned n) {
    struct usb_td *td = &usb_td[in];
    td->next = 1;
    td->token = n << 16 | 0x80u;
    td->page[0] = (uint32_t)p;
    for (unsigned i = 1; i < 5; ++i) td->page[i] = ((uint32_t)p & ~0xFFFu) + 0x1000u * i;
    usb_qh[in].td.next = (uint32_t)td;
    usb_qh[in].td.token = 0;
    USB_PRIME = in ? 1u << 16 : 1u;
}

static void usb_send(const void *p, unsigned n, unsigned asked) {      /* data in, then the status */
    usb_xfer(1, p, n < asked ? n : asked);
    usb_xfer(0, 0, 0);
}

static void usb_setup(const uint8_t *s) {
    unsigned type = s[0], req = s[1], value = s[2] | s[3] << 8u, len = s[6] | s[7] << 8u;
    if (type == 0x80 && req == 6 && s[3] == 1) { usb_send(usb_device, sizeof usb_device, len); return; }
    if (type == 0x80 && req == 6 && s[3] == 2) { usb_send(usb_config, sizeof usb_config, len); return; }
    if ((type & 0x9F) == 0x80 && req == 0) { usb_reply[0] = usb_reply[1] = 0; usb_send(usb_reply, 2, len); return; }
    if (type == 0x80 && req == 8) { usb_reply[0] = 1; usb_send(usb_reply, 1, len); return; }
    if (type == 0x00 && req == 5) { USB_DEVICEADDR = value << 25 | 1u << 24; usb_xfer(1, 0, 0); return; }
    if (type == 0x00 && req == 9) { usb_xfer(1, 0, 0); return; }
    if (type == 0x01 && req == 11) { usb_xfer(1, 0, 0); return; }
    if (type == 0xC0 && req == 1) {
        unsigned n = 0;
        while (n < 63 && n < len && usb_tail != usb_head) {
            usb_reply[n++] = usb_ring[usb_tail];
            usb_tail = (usb_tail + 1) % sizeof usb_ring;
        }
        usb_send(usb_reply, n, len);
        return;
    }
    if (type == 0x40 && req == 2) {
        usb_cmd = value | (uint32_t)(s[4] | s[5] << 8u) << 16;
        usb_cmd_new = 1;
        usb_xfer(1, 0, 0);
        return;
    }
    USB_CTRL0 |= 1u << 16 | 1u;                 /* anything else: refuse */
}

static void usb_poll(void) {
    uint32_t sts = USB_STS;
    USB_STS = sts;
    if (sts & 1u << 6) {                        /* bus reset */
        USB_SETUPSTAT = USB_SETUPSTAT;
        USB_COMPLETE = USB_COMPLETE;
        while (USB_PRIME) { }
        USB_FLUSH = 0xFFFFFFFFu;
        USB_DEVICEADDR = 0;
    }
    USB_COMPLETE = USB_COMPLETE;
    if (USB_SETUPSTAT & 1u) {
        uint32_t s[2];
        USB_SETUPSTAT = 1;
        do {
            USB_CMD |= 1u << 13;
            s[0] = usb_qh[0].setup[0];
            s[1] = usb_qh[0].setup[1];
        } while (!(USB_CMD & 1u << 13));
        USB_CMD &= ~(1u << 13);
        USB_FLUSH = 1u << 16 | 1u;
        while (USB_FLUSH) { }
        usb_setup((const uint8_t *)s);
    }
}

static void usb_start(void) {
    REG32(CREG_BASE + 0x004) &= ~(1u << 5);     /* CREG0: transceiver on */
    USB_CMD = 2;                                /* reset the controller: the ROM's device goes away */
    while (USB_CMD & 2u) { }
    USB_MODE = 2u | 1u << 3;                    /* device, no setup lock-out */
    USB_PORTSC1 |= 1u << 24;                    /* full speed only */
    for (unsigned i = 0; i < 12; ++i) { usb_qh[i].cap = 0; usb_qh[i].td.next = 1; usb_qh[i].td.token = 0; }
    usb_qh[0].cap = 64u << 16 | 1u << 15;       /* 64-byte packets; interrupt on setup */
    usb_qh[1].cap = 64u << 16;
    USB_LISTADDR = (uint32_t)usb_qh;
    USB_STS = 0xFFFFFFFFu;
    USB_CMD = 1;                                /* run: attaches */
}

#endif
