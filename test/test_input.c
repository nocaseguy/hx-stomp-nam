#include "../src/input/encoder.h"
#include "../src/input/switch.h"
#include <stdio.h>
int main(void){
    int fail=0;
    /* Encoder: one full CW detent = A/B sequence 00->01->11->10->00 gives +4 steps
       (4 transitions). Full-step decoders count every valid edge. */
    encoder e; encoder_reset(&e);
    const uint8_t cw[]={0b00,0b01,0b11,0b10,0b00};
    for (int i=1;i<5;i++) encoder_update(&e,cw[i]);
    if (e.count!=4){ printf("FAIL CW count=%d want 4\n",e.count); fail=1; }
    else printf("ok   encoder CW = +4 edges\n");
    /* CCW: reverse sequence */
    encoder_reset(&e);
    const uint8_t ccw[]={0b00,0b10,0b11,0b01,0b00};
    for (int i=1;i<5;i++) encoder_update(&e,ccw[i]);
    if (e.count!=-4){ printf("FAIL CCW count=%d want -4\n",e.count); fail=1; }
    else printf("ok   encoder CCW = -4 edges\n");
    /* Bounce/invalid double-step yields 0 (no phantom counts) */
    encoder_reset(&e);
    encoder_update(&e,0b00); int d=encoder_update(&e,0b11); /* illegal jump */
    if (d!=0){ printf("FAIL illegal step counted %d\n",d); fail=1; }
    else printf("ok   encoder rejects illegal double-step\n");

    /* Debouncer: needs THRESH consecutive-ish highs to assert; bounces ignored */
    debouncer sw; sw_init(&sw,4);
    sw_event ev=SW_NONE;
    uint8_t bounce[]={1,0,1,0,1};          /* chattering, never stable */
    for (int i=0;i<5;i++){ ev=sw_update(&sw,bounce[i]); if(ev==SW_PRESSED){printf("FAIL bounce caused press\n");fail=1;} }
    printf("ok   debouncer ignores chatter\n");
    for (int i=0;i<6;i++) ev=sw_update(&sw,1);  /* held down */
    if (ev!=SW_PRESSED && sw_state(&sw)!=1){ printf("FAIL steady press not detected\n"); fail=1; }
    else printf("ok   debouncer detects steady press\n");
    for (int i=0;i<6;i++) ev=sw_update(&sw,0);  /* released */
    if (sw_state(&sw)!=0){ printf("FAIL release not detected\n"); fail=1; }
    else printf("ok   debouncer detects release\n");

    printf(fail?"\nINPUT TEST FAILURES\n":"\nALL INPUT TESTS PASS\n");
    return fail;
}
