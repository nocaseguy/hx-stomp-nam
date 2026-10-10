/* encoder.h - quadrature rotary-encoder decoder (for the HX Stomp knobs).
 * Table-driven state machine: feed the 2-bit A/B phase each poll, get -1/0/+1.
 * Works for GPIO-read or shift-register-read encoders; source is abstracted. */
#ifndef HX_ENCODER_H
#define HX_ENCODER_H
#include <stdint.h>
typedef struct { uint8_t state; int32_t count; } encoder;
static inline void encoder_reset(encoder *e){ e->state=0; e->count=0; }
/* ab = current 2-bit gray code (bit1=A, bit0=B). Returns step -1/0/+1 and
 * accumulates into count. Uses the standard full-step transition table. */
int encoder_update(encoder *e, uint8_t ab);
#endif
