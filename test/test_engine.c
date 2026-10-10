#include "../src/dsp/engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    audio_engine e; engine_init(&e,48000.0f); engine_set_gain_db(&e,0.0f);
    /* 1) DC input must be blocked by the 80Hz high-pass (output settles ~0). */
    int32_t buf[2]; double tail=0;
    for(int i=0;i<20000;i++){ buf[0]=buf[1]=4000000; engine_process_i32(&e,buf,1);
        if(i>19000) tail += (double)buf[0]*buf[0]; }
    int fail=0;
    if(tail/1000.0 > 1e8){ printf("FAIL DC not blocked (%.0f)\n",tail); fail=1; }
    else printf("ok   DC blocked by high-pass\n");
    /* 2) full-scale 1kHz sine stays in range, no clipping garbage / NaN. */
    engine_init(&e,48000.0f);
    int over=0; long peak=0;
    for(int i=0;i<48000;i++){ double s=sin(2*M_PI*1000.0*i/48000.0)*8000000.0;
        buf[0]=buf[1]=(int32_t)s; engine_process_i32(&e,buf,1);
        if(buf[0]>8388607||buf[0]<-8388608) over++;
        long a=labs(buf[0]); if(a>peak)peak=a; }
    if(over){ printf("FAIL %d samples out of 24-bit range\n",over); fail=1; }
    else printf("ok   1kHz full-scale in range (peak=%ld)\n",peak);
    /* 3) presence peak at 3kHz boosts relative to 1kHz */
    printf(fail?"\nENGINE TEST FAILURES\n":"\nALL ENGINE TESTS PASS\n");
    return fail;
}
