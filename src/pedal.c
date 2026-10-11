/* pedal.c - the pedal as far as it goes: a fixed chain of three blocks with the
 * NAM model as the amp, worked from the front panel.
 *
 *     in -> AMP -> EQ -> DLY -> volume -> out
 *
 * Boots build/sharc_nam_live.h on the SHARC (src/sharc/nam_live.asm, which
 * does the audio), takes the jacks out of analog bypass, and from then on
 * turns the controls into that program's parameters. The controls are not
 * wired to the LPC4350: a controller on the footswitch board reports them
 * over I2C (see the enums below and HARDWARE.md).
 *   PAGE < >, upper knob     choose a block (so does touching its footswitch)
 *   ACTION, upper knob press the chosen block on or off
 *   three knobs under the screen   the chosen block's values, shown above them
 *   footswitch 1, 2, 3       AMP, EQ, DLY on or off; ring bright or dim
 *   volume knob              output level
 * The screen has the chain as three boxes (green: on, grey: off, yellow frame:
 * chosen) and under it the chosen block's values; the top row has how much
 * of the SHARC's time the chain takes and how much of this processor's time
 * goes on the controls and the screen, per cent, and the volume.
 *
 * Every controller message also goes to the computer over USB as a line of
 * text (src/usb_mon.h, tools/usbmon.py). Commands from there:
 *   D           back to the DFU loader
 *   S           ask the SHARC for its frame and print it (the sound stops for
 *               a moment): magic, most words it was behind << 16 | ring
 *               distance, gain, model, level
 *   S,a,b,c     the same with the word at SHARC address a | b << 8 | c << 16
 *               in place of the level
 *   U           print the SHARC's load, parts in 0x40000, and this processor's,
 *               per cent, in hex
 *   C,k,c,v     act as if the controller had sent kind k, control c, value v
 * PEDAL_SECONDS, if not 0, sends the image back to the DFU loader after that
 * long (for unattended tests); the image `make pedal` builds stays up. */
#define SMOKE_SHARC_NO_MAIN
#include <math.h>
#include "smoke_sharc.c"
#include "../build/sharc_nam_live.h"
#include "smoke_text.h"
#include "smoke_frames.h"
#include "audio_pins.h"
#include "i2c_poll.h"
#include "dfu.h"
#include "usb_mon.h"
#include "panel.h"

#ifndef PEDAL_SECONDS
#define PEDAL_SECONDS 0
#endif

/* nam_live.asm's parameters. */
enum { P_GAIN, P_MODEL, P_LEVEL, P_SHOW, P_BAND = 8, P_DTIME = 23, P_DFEED, P_DMIX, P_BAND_USE };

static const int8_t fs_block[3] = { B_AMP, B_EQ, B_DELAY };
static const uint8_t fs_colour[3][3] = { {100, 0, 0}, {0, 100, 0}, {0, 30, 100} };

static void put(unsigned par, float f) { union { float f; uint32_t u; } v = { f }; sharc_message(par, v.u); }
static float db(float d) { return powf(10.0f, d / 20.0f); }

/* The volume knob and the amp's LEVEL make one number for the SHARC. An A2
 * model's output stays some 18 dB under full scale however hard it is driven
 * (measured on the full model with sines), so LEVEL starts above 0 dB. */
static void level_out(void) {
    const struct block *k = &chain[B_AMP];
    put(P_LEVEL, out_level * (k->on ? db((float)k->val[1].v / 10.0f) : 1.0f));
}

/* One equaliser band for nam_live.asm, from the Audio EQ Cookbook's formulas
 * at 48 kHz: kind 0 low shelf, 1 peak (Q 0.7), 2 high shelf. */
static void band(unsigned n, unsigned kind, float hz, float gain_db) {
    float A = powf(10.0f, gain_db / 40.0f), w = 6.2831853f * hz / 48000.0f, c = cosf(w), s = sinf(w);
    float b0, b1, b2, a0, a1, a2;
    if (kind == 1) {
        float al = s / (2.0f * 0.7f);
        b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A;
        a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A;
    } else {
        float k = kind ? -1.0f : 1.0f;          /* the two shelves differ in these signs */
        float r = 2.0f * sqrtf(A) * s * 0.70710678f;
        b0 = A * ((A + 1) - k * (A - 1) * c + r);
        b1 = k * 2 * A * ((A - 1) - k * (A + 1) * c);
        b2 = A * ((A + 1) - k * (A - 1) * c - r);
        a0 = (A + 1) + k * (A - 1) * c + r;
        a1 = -k * 2 * ((A - 1) + k * (A + 1) * c);
        a2 = (A + 1) + k * (A - 1) * c - r;
    }
    unsigned p = P_BAND + 5 * n;
    put(p, b0 / a0); put(p + 1, b1 / a0); put(p + 2, b2 / a0); put(p + 3, -a1 / a0); put(p + 4, -a2 / a0);
    sharc_message(P_BAND_USE, n);               /* all five at once: half a band can be unstable */
}

/* A block's state to the SHARC. */
static void apply(unsigned b) {
    const struct block *k = &chain[b];
    switch (b) {
    case B_AMP:
        put(P_GAIN, k->on ? db((float)k->val[0].v / 10.0f) : 1.0f);
        sharc_message(P_MODEL, k->on);
        level_out();
        break;
    case B_EQ:
        band(0, 0, 120.0f, k->on ? (float)k->val[0].v / 10.0f : 0.0f);
        band(1, 1, 700.0f, k->on ? (float)k->val[1].v / 10.0f : 0.0f);
        band(2, 2, 3500.0f, k->on ? (float)k->val[2].v / 10.0f : 0.0f);
        break;
    case B_DELAY:
        sharc_message(P_DTIME, (uint32_t)k->val[0].v * 48u);
        put(P_DFEED, (float)k->val[1].v / 100.0f);
        put(P_DMIX, k->on ? (float)k->val[2].v / 100.0f : 0.0f);
        break;
    }
}

/* ---- screen: palette 0 black, 1 white, 2 green, 3 red, 4 yellow, 5 grey ---- */
static void draw_char(unsigned x, unsigned y, char ch, unsigned colour) {
    uint16_t g = text_glyph(ch);
    for (unsigned r = 0; r < 5; ++r)
        for (unsigned c = 0; c < 3; ++c) {
            unsigned on = g >> (14 - (r * 3 + c)) & 1u;
            for (unsigned i = 0; i < 16; ++i) text_px(x + c * 4 + (i & 3), y + r * 4 + (i >> 2), on ? colour : 0);
        }
}
static void draw_str(unsigned x, unsigned y, const char *s, unsigned colour) {
    for (; *s; ++s, x += 16) draw_char(x, y, *s, colour);
}
static void box(unsigned x0, unsigned y0, unsigned x1, unsigned y1, unsigned colour) {
    for (unsigned t = 0; t < 3; ++t) {
        for (unsigned x = x0; x <= x1; ++x) { text_px(x, y0 + t, colour); text_px(x, y1 - t, colour); }
        for (unsigned y = y0; y <= y1; ++y) { text_px(x0 + t, y, colour); text_px(x1 - t, y, colour); }
    }
}
static unsigned len(const char *s) { unsigned n = 0; while (s[n]) ++n; return n; }

/* A value as text, right-aligned in six characters. */
static void number(char out[7], int v, int tenths) {
    char t[8];
    unsigned n = 0, neg = v < 0, u = (unsigned)(neg ? -v : v);
    if (tenths) { t[n++] = (char)('0' + u % 10); t[n++] = '.'; u /= 10; }
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) t[n++] = '-';
    for (unsigned i = 0; i < 6; ++i) out[i] = i < 6 - n ? ' ' : t[5 - i];
    out[6] = 0;
}

static void draw_chain(void) {
    for (unsigned b = 0; b < BLOCKS; ++b) {
        unsigned x0 = b * 107 + 18, w = len(chain[b].name) * 16 - 4;
        box(x0, 34, x0 + 69, 78, b == chosen ? 4 : chain[b].on ? 2 : 5);
        draw_str(x0 + (70 - w) / 2, 46, chain[b].name, chain[b].on ? 2 : 5);
        if (b + 1 < BLOCKS)
            for (unsigned x = x0 + 70; x < x0 + 107; ++x) { text_px(x, 55, 1); text_px(x, 56, 1); }
    }
}

static void draw_values(void) {
    const struct block *k = &chain[chosen];
    char t[7];
    draw_str(0, 108, "    ", 1);
    draw_str(0, 108, k->name, 4);
    draw_str(80, 108, k->on ? "ON " : "OFF", k->on ? 2 : 5);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned x = 4 + i * 108;
        draw_str(x, 156, "      ", 1);
        draw_str(x, 192, "      ", 1);
        if (i >= k->values) continue;
        draw_str(x, 156, k->val[i].name, 1);
        number(t, k->val[i].v, k->val[i].tenths);
        draw_str(x, 192, t, 4);
    }
}

/* How much of the SHARC is in use: nam_live.asm holds its report line high
 * while it works on a sample. Parts in 2^bits, from that many looks at the
 * line. */
static unsigned dsp_load(unsigned bits) {
    unsigned high = 0;
    for (unsigned i = 0; i < 1u << bits; ++i) high += level();
    return high;
}

/* How much of this processor is in use. The main loop only looks whether
 * something has come in; the time it spends acting on what did, and drawing,
 * is counted in `busy` (timer 0 counts, see WORK below) and compared with the
 * time gone by. */
#define T0_TCR REG32(0x40084004u)
#define T0_TC  REG32(0x40084008u)
static uint32_t busy;
static int arm_load = -1;                       /* per cent, as last drawn */
#define WORK(x) do { uint32_t t_ = T0_TC; x; busy += T0_TC - t_; } while (0)

static void draw_load(void) {
    static unsigned dsp;                        /* sixteenths of a per cent, smoothed */
    static uint32_t then;
    char t[7];
    unsigned now = dsp_load(13) * 1600u >> 13;
    dsp = dsp ? dsp + ((int)now - (int)dsp) / 4 : now;
    number(t, (int)((dsp + 8) / 16), 0);
    draw_str(0, 0, "DSP", 1);
    draw_str(48, 0, t + 3, dsp > 97 * 16 ? 3 : 4);
    uint32_t tc = T0_TC, gone = tc - then;
    draw_str(112, 0, "ARM", 1);
    if (gone >> 8) {
        arm_load = (int)((busy >> 8) * 100u / (gone >> 8));
        number(t, arm_load, 0);
        draw_str(160, 0, t + 3, 4);
    } else draw_str(160, 0, " --", 5);          /* the timer is not running */
    then = tc;
    busy = 0;
}

static void draw_volume(void) {
    char t[7];
    number(t, volume, 0);
    if (volume < 0) draw_str(272, 0, " --", 5); else draw_str(272, 0, t + 3, 4);
    draw_str(224, 0, "VOL", 1);
}

/* ---- footswitch rings ---- */
/* A footswitch light, as the stock application asks for it: its message is
 * {0, switch 0-3 (0-8 on an XL), 0, red, green, blue 0-100}, sent as length + 2, the inverted
 * sum of the message, then the message. Returns what i2c_write does. */
static int light(unsigned addr7, unsigned sw, unsigned r, unsigned g, unsigned b) {
    uint8_t m[8] = { 8, 0, 0, (uint8_t)sw, 0, (uint8_t)r, (uint8_t)g, (uint8_t)b };
    unsigned sum = 0;
    for (unsigned i = 2; i < 8; ++i) sum += m[i];
    m[1] = (uint8_t)~sum;
    return i2c_write(0, addr7, m, 8);
}

/* The rings are sent one a pass of the loop: the controller drops messages
 * that come close together. */
static uint8_t ring_due;
static void rings(unsigned b) {
    for (unsigned sw = 0; sw < 3; ++sw) if ((unsigned)fs_block[sw] == b) ring_due |= 1u << sw;
}
static void ring_send(void) {
    for (unsigned sw = 0; sw < 3; ++sw) {
        if (!(ring_due >> sw & 1u)) continue;
        unsigned d = chain[fs_block[sw]].on ? 1 : 12;       /* off: a glimmer of the same colour */
        if (light(0x2A, sw, fs_colour[sw][0] / d, fs_colour[sw][1] / d, fs_colour[sw][2] / d) >= 8)
            ring_due &= ~(1u << sw);
        return;
    }
}

/* ---- controls ---- */
static void choose(unsigned b) {
    if (b == chosen) return;
    chosen = b;
    draw_chain();
    draw_values();
}

static void toggle(unsigned b) {
    chain[b].on = !chain[b].on;
    apply(b);
    rings(b);
    draw_chain();
    if (b == chosen) draw_values();
}

static void control(unsigned kind, unsigned id, unsigned value) {
    int steps = (int8_t)value;
    if (kind == K_VOLUME) {
        volume = value > 100 ? 100 : (int)value;
        float v = (float)volume / 100.0f;
        out_level = v * v;                          /* 0 is silent, 50 is the 0.25 it starts at */
        level_out();
        draw_volume();
    } else if (kind == K_KNOB && id <= KN_3) {
        if (id >= chain[chosen].values) return;
        struct value *p = &chain[chosen].val[id];
        int v = p->v + steps * p->step;
        v = v > p->max ? p->max : v < p->min ? p->min : v;
        if (v == p->v) return;
        p->v = (int16_t)v;
        apply(chosen);
        draw_values();
    } else if (kind == K_KNOB && id == KN_UPPER) {
        static int turned;                      /* four steps a block */
        turned += steps;
        if (turned >= 4 || turned <= -4) {
            choose((chosen + (turned > 0 ? 1 : BLOCKS - 1)) % BLOCKS);
            turned = 0;
        }
    } else if (kind == K_SWITCH && value) {
        switch (id) {
        case SW_PAGE_L: choose((chosen + BLOCKS - 1) % BLOCKS); break;
        case SW_PAGE_R: choose((chosen + 1) % BLOCKS); break;
        case SW_ACTION: case SW_UPPER: toggle(chosen); break;
        case SW_FS1: case SW_FS2: case SW_FS3: toggle((unsigned)fs_block[id - SW_FS1]); break;
        case SW_TOUCH1: case SW_TOUCH2: case SW_TOUCH3: choose((unsigned)fs_block[id - SW_TOUCH1]); break;
        }
    }
}

/* The SHARC's frame, to the computer. */
static void report(uint32_t address) {
    uint32_t w[5] = {0, 0, 0, 0, 0};
    GPIO_CLR(CS_PORT) = 1u << CS_PIN;           /* sharc_message() without the wait after it */
    spin(MS / 4);
    send(P_SHOW);
    send(address);
    spin(MS / 4);
    GPIO_SET(CS_PORT) = 1u << CS_PIN;
    int ok = frame_now(w, 5);
    spin(3 * MS);
    usb_puts(ok ? "sharc" : "sharc: no frame");
    for (unsigned i = 0; ok && i < 5; ++i) { usb_putc(' '); usb_puthex(w[i], 8); }
    usb_putc('\n');
}

int main(void) {
    lcd_start();
    bands(0, 16, 0);
    audio_pins(0);
    link_init();
    sharc_boot(sharc_nam_live, sizeof sharc_nam_live / sizeof sharc_nam_live[0]);
    uint32_t w[5];
    int up = 0;
    for (unsigned n = 0; n < 4 && !up; ++n) up = frame_in(w, 5) && w[0] == SHARC_NAM_LIVE_MAGIC;
    spin(2500 * MS);                          /* past the frames it sends before it listens */
    put(P_LEVEL, out_level);
    for (unsigned b = 0; b < BLOCKS; ++b) apply(b);

    text_start();
    LCD_PAL(2) = 0x03FFu | 0x2108u << 16;     /* yellow, and grey for what is off */
    draw_volume();
    draw_chain();
    draw_values();
    if (!up) draw_str(112, 0, "NO DSP", 3);
    i2c_init(0, 0x80);
    i2c_init(1, 0x54);
    (void)i2c_probe; (void)text_hex;
    ring_due = 7;
    T0_TCR = 1;
    usb_start();
    usb_puts(up ? "pedal: dsp up\n" : "pedal: no dsp\n");

    static uint8_t buf[2][16];
    unsigned fill[2] = {0, 0};
    for (unsigned tick = 0;; ++tick) {
        uint32_t c;
        usb_poll();
        if (usb_command(&c)) WORK(switch (c & 0xFF) {     /* from tools/usbmon.py --send */
        case 'D': dfu_reboot();
        case 'S': report(c >> 8); break;
        case 'U': usb_puts("load "); usb_puthex(dsp_load(18), 5);
                  usb_puts(" arm "); usb_puthex((uint32_t)arm_load, 2); usb_putc('\n'); break;
        case 'C': control(c >> 8 & 0xFF, c >> 16 & 0xFF, c >> 24); break;
        });
        for (unsigned bus = 0; bus < 2; ++bus) {
            unsigned n = i2c_listen(bus, buf[bus], 16, &fill[bus]);
            if (n < 6) continue;
            const uint8_t *m = buf[bus] + 2;
            usb_putc((char)('0' + bus));      /* to the computer: bus, then the bytes as they came */
            for (unsigned i = 0; i < n; ++i) { usb_putc(' '); usb_puthex(buf[bus][i], 2); }
            usb_putc('\n');
            if (bus == 0 && m[1] == 0) WORK(control(m[0], m[2], m[3]));
        }
        if (tick % 32000) continue;           /* a few times a second */
        WORK(ring_send());
        if (tick % 128000 == 0) WORK(draw_load());
#if PEDAL_SECONDS
        if (tick / 140000 >= PEDAL_SECONDS) dfu_reboot();
#endif
    }
}
