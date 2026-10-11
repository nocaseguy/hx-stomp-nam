/* stomp.c - the pedal with its screen in full colour and the UI of src/ui:
 * pedal.c's chain and hardware, the Sonulab pedal's look and manners.
 *
 *     in -> 1 AMP -> 2 EQ -> 3 DLY -> volume -> out
 *
 * What the controls do is at the top of src/ui/pages.h; how it looks, in
 * src/ui/draw.h. The pictures can be had without the unit:
 * `make -C test/sim pedal`.
 *
 * This file is the part that needs the unit. It sets up the SDRAM
 * (sdram.h), moves the screen to 16 bits a pixel there (screen16.h), boots
 * the SHARC as pedal.c does, and then goes round: hear the panel and the
 * computer, act, draw a frame, show it. The panel and the computer are also
 * heard between the rows of a frame, so neither waits for one.
 *
 * If the SDRAM does not pass its test, this image is pedal.c: the same chain
 * on the 16-colour screen that needs no SDRAM. That screen is how to tell.
 *
 * Over USB (src/usb_mon.h, tools/usbmon.py), pedal.c's commands
 *   D           back to the DFU loader
 *   S, S,a,b,c  the SHARC's frame (the sound stops for a moment)
 *   C,k,c,v     act as if the controller had sent kind k, control c, value v
 * and
 *   U           print the SHARC's load, this processor's and the frame rate
 *   F           send the screen: `tools/usbmon.py --shot file.png`
 *
 * On the unit: the SHARC comes up, the panel's messages arrive, the pages
 * come back over USB as the simulator draws them, about 20 frames a second
 * with this processor busy all the time it draws the scene. */
#define main pedal_main                         /* pedal.c whole, its main under another name */
#include "pedal.c"
#undef main
#include "sdram.h"

#define SCREEN_SCAN0 (SDRAM_BASE + 0x000000u)   /* two for the controller, turn about */
#define SCREEN_SCAN1 (SDRAM_BASE + 0x040000u)
#define FB_BACK ((uint16_t *)(SDRAM_BASE + 0x080000u))     /* the one the UI draws on */
#define SCREEN_MEMORY 0x0C0000u                 /* all three: what the memory test covers */

static void listen(void);
#define UI_YIELD() listen()
#define SCREEN_YIELD() listen()
#include "screen16.h"
#include "ui/fb.h"
#include "ui/draw.h"
#include "ui/scene.h"
#include "ui/pages.h"

static struct ui ui;

/* ---- what the UI asks of the hardware ---- */
static uint8_t rings_due;
static void hw_apply(unsigned b) { apply(b); }
static void hw_level(void) { level_out(); }
static void hw_rings(void) { rings_due = 7; }
static void hw_loader(void) { dfu_reboot(); }

/* One ring a call: the controller drops messages that come close together.
 * A block's ring is in its colour, bright when the block is on, a glimmer
 * when it is off; the menu can dim them all or put them out. */
static void rings_send(void) {
    static const uint8_t on_div[RINGS_N] = { 1, 4, 0 }, off_div[RINGS_N] = { 12, 30, 0 };
    for (unsigned sw = 0; sw < 3; ++sw) {
        if (!(rings_due >> sw & 1u)) continue;
        unsigned d = chain[sw].on ? on_div[ui.rings] : off_div[ui.rings];
        const uint8_t *c = block_ring[sw];
        if (light(0x2A, sw, d ? c[0] / d : 0, d ? c[1] / d : 0, d ? c[2] / d : 0) >= 8) rings_due &= (uint8_t)~(1u << sw);
        return;
    }
}

/* ---- the panel and the computer, heard at any moment ---- */
static struct { uint8_t kind, id, value; } heard[32];      /* panel messages not yet acted on */
static unsigned heard_in, heard_out;
static uint32_t command;                        /* the computer's last, or 0 */

static void hear(unsigned kind, unsigned id, unsigned value) {
    unsigned next = (heard_in + 1) % 32;
    if (next == heard_out) return;              /* full: drop */
    heard[heard_in].kind = (uint8_t)kind; heard[heard_in].id = (uint8_t)id; heard[heard_in].value = (uint8_t)value;
    heard_in = next;
}

static void listen(void) {
    static uint8_t buf[16];
    static unsigned fill;
    uint32_t c;
    screen_tick();
    usb_poll();
    if (usb_command(&c)) {
        if ((c & 0xFF) == 'C') hear(c >> 8 & 0xFF, c >> 16 & 0xFF, c >> 24);
        else command = c;                       /* acted on in main: D must be, see dfu.c */
    }
    unsigned n = i2c_listen(0, buf, 16, &fill);
    if (n >= 6 && buf[3] == 0) hear(buf[2], buf[4], buf[5]);
}

/* ---- the screen to the computer: "shot 320 240\n", then runs of one colour,
 * each a count less one and the colour's two bytes, low first ---- */
static int shot_at = -1;                        /* the next pixel to send, or -1 */

static unsigned usb_room(void) { return (usb_tail + sizeof usb_ring - usb_head - 1) % sizeof usb_ring; }

static void shot_send(void) {
    if (shot_at == 0 && usb_room() > 16) usb_puts("shot 320 240\n");
    while (shot_at >= 0 && usb_room() >= 3) {
        uint16_t c = FB_BACK[shot_at];
        unsigned run = 1;
        while (run < 256 && shot_at + (int)run < FB_W * FB_H && FB_BACK[shot_at + (int)run] == c) ++run;
        usb_putc((char)(run - 1)); usb_putc((char)(c & 0xFF)); usb_putc((char)(c >> 8));
        shot_at += (int)run;
        if (shot_at >= FB_W * FB_H) shot_at = -1;
    }
}

/* How much of the SHARC is in use, per cent: nam_live.asm holds its report
 * line high while it works on a sample. */
static int dsp_per_cent(void) {
    unsigned high = 0;
    for (unsigned i = 0; i < 8192; ++i) high += level();
    return (int)((high * 100 + 4096) / 8192);
}

/* A fault is told to the computer, and D still goes back to the loader. The
 * vector table is in RAM with the image (startup.c), so it can point here. */
extern void (*const g_vectors[])(void);
static void fault(void) {
    uint32_t c;
    usb_puts("fault: cfsr "); usb_puthex(REG32(0xE000ED28u), 8);
    usb_puts(" hfsr "); usb_puthex(REG32(0xE000ED2Cu), 8);
    usb_puts(" bfar "); usb_puthex(REG32(0xE000ED38u), 8); usb_putc('\n');
    for (;;) { usb_poll(); if (usb_command(&c) && (c & 0xFF) == 'D') dfu_reboot(); }
}

int main(void) {
    uint32_t *v = (uint32_t *)(uintptr_t)g_vectors;
    for (unsigned i = 2; i < 7; ++i) v[i] = (uint32_t)fault | 1u;
    REG32(0xE000ED08u) = (uint32_t)v;           /* VTOR */
    lcd_start();
    if (!sdram_start() || !sdram_test(SDRAM_BASE, SCREEN_MEMORY)) return pedal_main();

    bands(0, 16, 0);
    audio_pins(0);
    link_init();
    sharc_boot(sharc_nam_live, sizeof sharc_nam_live / sizeof sharc_nam_live[0]);
    uint32_t w[5];
    int up = 0;
    for (unsigned n = 0; n < 4 && !up; ++n) up = frame_in(w, 5) && w[0] == SHARC_NAM_LIVE_MAGIC;
    spin(2500 * MS);                            /* past the frames it sends before it listens */
    for (unsigned b = 0; b < BLOCKS; ++b) apply(b);

    i2c_init(0, 0x80);
    i2c_init(1, 0x54);
    usb_start();
    usb_puts(up ? "stomp: dsp up\n" : "stomp: no dsp\n");
    ui_init(&ui, up);
    rings_due = 7;
    screen_start();

    uint32_t drawn_at = 0, ring_at = 0, second_at = 0, busy = 0, frames = 0;
    for (;;) {
        listen();
        while (heard_out != heard_in) {
            ui.now = screen_ms();
            ui_control(&ui, heard[heard_out].kind, heard[heard_out].id, heard[heard_out].value);
            if (shot_at < 0) {                  /* to the computer too, but not into a picture on its way */
                usb_puts("c "); usb_puthex(heard[heard_out].kind, 2); usb_putc(' ');
                usb_puthex(heard[heard_out].id, 2); usb_putc(' '); usb_puthex(heard[heard_out].value, 2); usb_putc('\n');
            }
            heard_out = (heard_out + 1) % 32;
        }
        if (command) {
            uint32_t c = command;
            command = 0;
            switch (c & 0xFF) {
            case 'D': dfu_reboot();
            case 'S': report(c >> 8); break;
            case 'U': usb_puts("dsp "); usb_puthex((uint32_t)ui.dsp_load, 2); usb_puts(" arm "); usb_puthex((uint32_t)ui.arm_load, 2);
                      usb_puts(" fps "); usb_puthex((uint32_t)ui.fps, 2); usb_putc('\n'); break;
            case 'F': shot_at = 0; break;
            }
        }
        if (shot_at >= 0) { shot_send(); continue; }    /* the picture stands still while it is sent */

        uint32_t f = screen_frames;
        if (f - ring_at >= 16) { ring_at = f; rings_send(); }       /* a quarter of a second apart */
        if (f - second_at >= 65) {                                  /* once a second: the loads */
            static int dsp;                                         /* sixteenths of a per cent, smoothed */
            int now = dsp_per_cent() * 16;
            dsp = dsp ? dsp + (now - dsp) / 4 : now;
            ui.dsp_load = up ? (dsp + 8) / 16 : -1;
            ui.arm_load = (int)(busy * 100 / (f - second_at));
            ui.fps = (int)(frames * 65 / (f - second_at));
            second_at = f; busy = 0; frames = 0;
        }
        if (f - drawn_at < 3) continue;                             /* 22 frames a second at most */
        drawn_at = f;
        ui_draw(&ui, screen_ms());
        screen_flip();
        busy += screen_frames - f;                                  /* in the panel's frames: coarse, and enough */
        ++frames;
    }
}
