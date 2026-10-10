/* switch.h - integrator debouncer for footswitches / buttons.
 * Poll at a fixed rate; a raw level must persist N polls to change the output.
 * Reports stable state plus press/release edge events. */
#ifndef HX_SWITCH_H
#define HX_SWITCH_H
#include <stdint.h>
typedef struct { uint8_t cnt, stable, thresh; } debouncer;
static inline void sw_init(debouncer *d, uint8_t thresh){ d->cnt=0; d->stable=0; d->thresh=thresh; }
typedef enum { SW_NONE=0, SW_PRESSED=1, SW_RELEASED=2 } sw_event;
/* raw = 1 when pressed (active). Returns an edge event when stable state flips. */
sw_event sw_update(debouncer *d, uint8_t raw);
static inline uint8_t sw_state(const debouncer *d){ return d->stable; }
#endif
