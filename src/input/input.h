/* input.h - HX Stomp XL control model + live monitor (a guess from the XL
 * firmware's strings: eight footswitches. Not what the HX Stomp has, and no
 * control has been read on a unit yet.)
 * Original note: control model + live monitor (confirm switches/knobs/buttons).
 *
 * Control set (from stock factory-test strings): 8 footswitches, mode/page
 * buttons, rotary encoder knob(s) with push, expression pedals + volume pot.
 * The physical READ BACKEND (direct GPIO vs 74HC165 shift register over SSP vs
 * SGPIO) is provided by input_hw.c, filled in from firmware reverse-engineering.
 * Everything above the backend (debounce, encoder decode, monitor UI) is tested
 * host-side and hardware-independent. */
#ifndef HX_INPUT_H
#define HX_INPUT_H
#include <stdint.h>
#include "encoder.h"
#include "switch.h"

#define N_FOOTSW   8
#define N_BUTTON   4     /* mode / page / menu / tap (exact set TBD) */
#define N_ENCODER  1     /* main value encoder (XL) + push             */
#define N_PEDAL    3     /* EXP 1/2/3 (analog)                          */

typedef struct {
    debouncer fsw[N_FOOTSW];
    debouncer btn[N_BUTTON];
    debouncer enc_push[N_ENCODER];
    encoder   enc[N_ENCODER];
    uint16_t  pedal[N_PEDAL];      /* 0..4095 ADC                        */
} input_state;

/* Backend (input_hw.c): read the raw hardware into flat arrays. Return 0 ok. */
int  input_hw_init(void);
void input_hw_read(uint8_t fsw_raw[N_FOOTSW], uint8_t btn_raw[N_BUTTON],
                   uint8_t enc_ab[N_ENCODER], uint8_t enc_push_raw[N_ENCODER],
                   uint16_t pedal[N_PEDAL]);

void input_init(input_state *s);
/* Poll once (call at ~1 kHz). Updates debouncers/encoders. */
void input_poll(input_state *s);

/* Draw a live monitor of every control on the LCD (green=pressed, encoder count,
 * pedal bars). Returns nothing; call each frame after input_poll. */
void input_monitor_draw(const input_state *s);

#endif
