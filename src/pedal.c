/* pedal.c - the pedal as far as it goes: the NAM model in the audio path and a
 * screen that shows what the controls send.
 *
 * Boots build/sharc_nam_live.h on the SHARC (src/sharc/nam_live.asm), takes
 * the jacks out of analog bypass, sets the model's parameters over SPI, and
 * then watches everything a control could arrive on. The knobs, switches and
 * footswitches are not wired to the LPC4350 directly: the stock application
 * talks to a controller on the footswitch board over I2C, and what that
 * controller says is not decoded yet. So the screen is a monitor for now:
 *   row 0   NAM, ON or BYP, and seconds running
 *   row 1   input gain and output level, hundredths
 *   row 2   I2C: who answered when the buses were scanned at start
 *           (bus 0 addresses, then bus 1; -- = nobody)
 *   row 3   messages received as a slave on bus 0: count, length of the last
 *   row 4-5 its first sixteen bytes
 *   row 6   the same for bus 1: count, length
 *   row 7-8 its bytes
 *   row 9   the seven input pins the stock application reads directly (jack
 *           switches, by the look of it), then ADC0 channels 3 and 5 (the
 *           two it reads: expression pedals)
 * Turn, press or plug something and see which numbers move.
 * PEDAL_SECONDS, if not 0, sends the image back to the DFU loader after that
 * long (for unattended tests); the image `make pedal` builds stays up. */
#define SMOKE_SHARC_NO_MAIN
#include "smoke_sharc.c"
#include "../build/sharc_nam_live.h"
#include "smoke_text.h"
#include "smoke_frames.h"
#include "audio_pins.h"
#include "i2c_poll.h"
#include "dfu.h"

#ifndef PEDAL_SECONDS
#define PEDAL_SECONDS 0
#endif

enum { P_GAIN, P_MODEL, P_LEVEL, P_SHOW };

static uint32_t fbits(float f) { union { float f; uint32_t u; } v = { f }; return v.u; }

static struct { float gain, level; unsigned model; } par = { 1.0f, 0.25f, 1 };

static void send_params(void) {
    sharc_message(P_GAIN, fbits(par.gain));
    sharc_message(P_MODEL, par.model);
    sharc_message(P_LEVEL, fbits(par.level));
}

static void dec2(unsigned col, unsigned row, float v, unsigned colour) {     /* n.nn */
    unsigned h = (unsigned)(v * 100.0f + 0.5f);
    text_char(col, row, (char)('0' + h / 100 % 10), colour);
    text_char(col + 1, row, '.', colour);
    text_char(col + 2, row, (char)('0' + h / 10 % 10), colour);
    text_char(col + 3, row, (char)('0' + h % 10), colour);
}

static void show_params(void) {
    text_str(0, 0, "NAM", 1);
    text_str(4, 0, par.model ? "ON " : "BYP", par.model ? 2 : 4);
    text_str(0, 1, "GAIN", 1);
    dec2(5, 1, par.gain, 4);
    text_str(10, 1, "LEVEL", 1);
    dec2(16, 1, par.level, 4);
}

/* The seven inputs the stock application reads directly, one bit each. */
static unsigned inputs(void) {
    static const struct { uint8_t sp, spin, mode, port, pin; } in[7] = {
        {13, 12, 0x44, 6, 26}, {6, 6, 0x40, 0, 5}, {1, 17, 0x40, 0, 12}, {1, 18, 0x40, 0, 13},
        {8, 0, 0x40, 4, 0},    {8, 1, 0x44, 4, 1}, {2, 5, 0x44, 5, 5},
    };
    unsigned v = 0;
    for (unsigned i = 0; i < 7; ++i) {
        SCU_SFS(in[i].sp, in[i].spin) = in[i].mode;
        v |= (GPIO_PIN(in[i].port) >> in[i].pin & 1u) << i;
    }
    return v;
}

#define ADC0_CR     REG32(0x400E3000u)
#define ADC0_DR(n)  REG32(0x400E3010u + 4u * (n))

static void scan(unsigned bus, unsigned col) {      /* up to two answering addresses */
    unsigned n = 0;
    for (unsigned a = 8; a < 0x78 && n < 2; ++a)
        if (i2c_probe(bus, a)) text_hex(col + 3 * n++, 2, a, 2, 2);
    for (; n < 2; ++n) text_str(col + 3 * n, 2, "--", 3);
}

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
    send_params();

    text_start();
    show_params();
    if (!up) text_str(8, 0, "NO DSP", 3);
    i2c_init(0, 0x80);
    i2c_init(1, 0x54);
    ADC0_CR = 0xFFu | 2u << 8 | 1u << 16 | 1u << 21;        /* all channels, 4 MHz, burst, on */
    text_str(0, 2, "I2C", 1);
    scan(0, 4);
    scan(1, 12);
    for (unsigned sw = 0; sw < 4; ++sw) {     /* light the footswitches, whichever address is theirs */
        if (light(0x2A, sw, sw & 1 ? 100 : 0, sw & 2 ? 100 : 0, sw & 4 ? 0 : 100) < 0)
            light(0x15, sw, sw & 1 ? 100 : 0, sw & 2 ? 100 : 0, sw & 4 ? 0 : 100);
        spin(5 * MS);
    }

    static uint8_t buf[2][16];
    unsigned fill[2] = {0, 0}, count[2] = {0, 0};
    for (unsigned tick = 0;; ++tick) {
        for (unsigned bus = 0; bus < 2; ++bus) {
            unsigned len = i2c_listen(bus, buf[bus], 16, &fill[bus]);
            if (!len) continue;
            unsigned row = 3 + 3 * bus;
            text_hex(0, row, ++count[bus], 4, 2);
            text_hex(5, row, len, 2, 1);
            for (unsigned i = 0; i < 16; ++i)
                text_hex(i % 8 * 2 + i % 8 / 4, row + 1 + i / 8, i < len ? buf[bus][i] : 0, 2, i < len ? 1 : 0);
        }
        if (tick % 20000) continue;           /* a few times a second */
        text_hex(0, 9, inputs(), 2, 4);
        text_hex(4, 9, ADC0_DR(3) >> 6 & 0x3FF, 3, 1);
        text_hex(8, 9, ADC0_DR(5) >> 6 & 0x3FF, 3, 1);
        unsigned secs = tick / 140000;        /* roughly */
        text_hex(16, 0, secs, 4, 1);
#if PEDAL_SECONDS
        if (secs >= PEDAL_SECONDS) dfu_reboot();
#endif
    }
}
