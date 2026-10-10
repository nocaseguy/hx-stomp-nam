#include "switch.h"
sw_event sw_update(debouncer *d, uint8_t raw){
    if (raw){ if (d->cnt < d->thresh) d->cnt++; }
    else    { if (d->cnt > 0)         d->cnt--; }
    if (!d->stable && d->cnt >= d->thresh){ d->stable = 1; return SW_PRESSED; }
    if ( d->stable && d->cnt == 0){         d->stable = 0; return SW_RELEASED; }
    return SW_NONE;
}
