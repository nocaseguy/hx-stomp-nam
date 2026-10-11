/* panel.h - the front panel's messages and the chain they work, for the
 * images that are a pedal (src/pedal.c, src/stomp.c) and for the UI on a
 * computer (test/sim/pedal_sim.c). */
#ifndef PANEL_H
#define PANEL_H
#include <stdint.h>

/* What the controller sends, measured on an HX Stomp: length 6, check byte,
 * then kind, 0, control, value.
 *   kind 0  a switch: value 1 down (or touched), 0 up
 *   kind 1  a knob turned: value is the steps, signed
 *   kind 2  the volume knob (control 0): value is its position, 0-100; sent
 *           only when it moves
 *   kind 3  seen on some runs only, value near 0x24; not known */
enum { K_SWITCH, K_KNOB, K_VOLUME };
enum {                                          /* controls of kind 0 */
    SW_KNOB1, SW_KNOB2, SW_KNOB3, SW_UPPER, SW_LOWER, SW_PAGE_L, SW_VIEW, SW_ACTION, SW_PAGE_R,
    SW_FS1, SW_FS2, SW_FS3, SW_TOUCH1, SW_TOUCH2, SW_TOUCH3
};
enum { KN_1, KN_2, KN_3, KN_UPPER, KN_LOWER };  /* controls of kind 1 */

/* The chain. A value is a whole number of `unit`s: tenths where that says 10. */
enum { B_AMP, B_EQ, B_DELAY, BLOCKS };
struct value { const char *name; int16_t v, min, max, step; uint8_t tenths; };
static struct block { const char *name; uint8_t on, values; struct value val[3]; } chain[BLOCKS] = {
    { "AMP", 1, 2, { { "GAIN", 0, -200, 200, 5, 1 }, { "LEVEL", 120, -200, 300, 5, 1 } } },    /* dB */
    { "EQ", 1, 3, { { "BASS", 0, -120, 120, 5, 1 }, { "MID", 0, -120, 120, 5, 1 }, { "TREBLE", 0, -120, 120, 5, 1 } } },
    { "DLY", 0, 3, { { "TIME", 250, 20, 330, 5, 0 }, { "FDBK", 30, 0, 90, 1, 0 }, { "MIX", 30, 0, 100, 1, 0 } } },
};
static unsigned chosen = B_AMP;
static int volume = -1;                         /* not known until the knob moves */
static float out_level = 0.25f;

#endif
