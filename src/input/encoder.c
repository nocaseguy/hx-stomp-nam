#include "encoder.h"
/* Transition table: index = (prev<<2)|cur (4-bit). +1 CW, -1 CCW, 0 invalid/none.
 * Valid Gray transitions only; the two "impossible" double-steps map to 0. */
static const int8_t T[16] = {
    0, +1, -1,  0,
   -1,  0,  0, +1,
   +1,  0,  0, -1,
    0, -1, +1,  0
};
int encoder_update(encoder *e, uint8_t ab){
    ab &= 3;
    int8_t d = T[((e->state & 3) << 2) | ab];
    e->state = ab;
    e->count += d;
    return d;
}
