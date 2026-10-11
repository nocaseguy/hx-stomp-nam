/* pedal_sim.c - the pedal's UI (src/ui) on a computer: the same pages, fed
 * the messages the front panel's controller sends, drawn into pictures.
 *
 *   pedal_sim <dir> [script]
 * runs a script (a file, or the one built in below) and writes <dir>/<name>.ppm
 * for each of its `shot` lines. A script line is one of
 *   c <kind> <control> <value>   a message from the panel (src/panel.h)
 *   t <ms>                       time goes by
 *   dsp <0|1>                    start again with the DSP up or not
 *   load <dsp> <arm> <fps>       what the image would have measured
 *   shot <name>                  draw the screen and write it
 *   # ...                        a remark */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/panel.h"

static uint16_t fb_mem[320 * 240];
#define FB_BACK fb_mem
#include "../../src/ui/fb.h"
#include "../../src/ui/draw.h"
#include "../../src/ui/scene.h"
#include "../../src/ui/pages.h"

static void hw_apply(unsigned b) { printf("  apply %s\n", chain[b].name); }
static void hw_level(void) { printf("  level %.4f\n", out_level); }
static void hw_rings(void) { printf("  rings\n"); }
static void hw_loader(void) { printf("  loader\n"); }

static const char *const built_in =
    "load 96 14 20\n"
    "t 300\nshot stage\n"
    "# the volume knob, a foot on 3, then its press: DLY comes on\n"
    "c 2 0 62\nc 0 14 1\nc 0 11 1\nc 0 11 0\nc 0 14 0\nt 200\nshot stage_dly_on\n"
    "# its time turned up, the knob then held down\n"
    "c 1 0 12\nt 300\nc 0 0 1\nshot stage_turned_held\nc 0 0 0\nt 6000\n"
    "# the equaliser chosen with PAGE <, bass down, treble up; AMP off by foot\n"
    "c 0 5 1\nc 0 5 0\nc 1 0 250\nc 1 2 9\nc 0 9 1\nc 0 9 0\nt 2500\nshot stage_eq\n"
    "t 9000\nc 0 7 1\nc 0 7 0\nshot menu\n"
    "c 0 3 1\nc 0 3 0\nc 1 3 1\nc 0 3 1\nc 0 3 0\nshot menu_switched\n"
    "c 0 7 1\nc 0 7 0\nt 100\nshot stage_plain\nc 0 7 1\nc 0 7 0\n"
    "c 1 3 1\nc 0 2 1\nc 0 2 0\nt 500\nshot confirm_reset\n"
    "c 0 2 1\nc 0 2 0\nt 300\nshot stage_after_reset\n"
    "c 0 7 1\nc 0 7 0\nc 1 3 2\nc 0 3 1\nc 0 3 0\nshot system\n"
    "c 0 6 1\nc 0 6 0\nc 1 3 255\nc 0 3 1\nc 0 3 0\nshot help\n"
    "c 1 3 6\nshot help_further\n"
    "c 0 0 1\nc 0 0 0\nc 1 3 5\nc 0 2 1\nc 0 2 0\nt 400\nshot confirm_loader\n"
    "dsp 0\nt 200\nshot stage_no_dsp\n";

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: pedal_sim <dir> [script]\n"); return 2; }
    char *script = NULL;
    if (argc > 2) {
        FILE *f = fopen(argv[2], "rb");
        if (!f) { perror(argv[2]); return 1; }
        script = calloc(1, 1 << 20);
        if (fread(script, 1, (1 << 20) - 1, f) == 0) { fprintf(stderr, "%s: empty\n", argv[2]); return 1; }
        fclose(f);
    }
    static struct ui u;
    static const struct block chain0[BLOCKS];
    uint32_t now = 0;
    (void)chain0;
    ui_init(&u, 1);
    char line[128];
    for (const char *p = script ? script : built_in; *p; ) {
        size_t n = strcspn(p, "\n");
        snprintf(line, sizeof line, "%.*s", (int)n, p);
        p += n + (p[n] == '\n');
        unsigned a, b, c;
        char name[64];
        if (sscanf(line, "c %u %u %u", &a, &b, &c) == 3) { u.now = now; ui_control(&u, a, b, c); }
        else if (sscanf(line, "t %u", &a) == 1) now += a;
        else if (sscanf(line, "dsp %u", &a) == 1) { ui_init(&u, (int)a); u.msg_at = now; }
        else if (sscanf(line, "load %u %u %u", &a, &b, &c) == 3) { u.dsp_load = (int)a; u.arm_load = (int)b; u.fps = (int)c; }
        else if (sscanf(line, "shot %63s", name) == 1) {
            char path[512];
            ui_draw(&u, now);
            snprintf(path, sizeof path, "%s/%s.ppm", argv[1], name);
            FILE *f = fopen(path, "wb");
            if (!f) { perror(path); return 1; }
            fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
            for (int i = 0; i < FB_W * FB_H; ++i) {
                uint16_t v = fb_mem[i];
                unsigned char rgb[3] = { (unsigned char)((v >> 11) * 255 / 31), (unsigned char)((v >> 5 & 63) * 255 / 63), (unsigned char)((v & 31) * 255 / 31) };
                fwrite(rgb, 1, 3, f);
            }
            fclose(f);
            printf("%s\n", path);
        }
    }
    return 0;
}
