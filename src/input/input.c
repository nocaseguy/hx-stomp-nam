#include "input.h"
#include "../display.h"

void input_init(input_state *s){
    for (int i=0;i<N_FOOTSW;i++)  sw_init(&s->fsw[i],4);
    for (int i=0;i<N_BUTTON;i++)  sw_init(&s->btn[i],4);
    for (int i=0;i<N_ENCODER;i++){ sw_init(&s->enc_push[i],4); encoder_reset(&s->enc[i]); }
    for (int i=0;i<N_PEDAL;i++)   s->pedal[i]=0;
}

void input_poll(input_state *s){
    uint8_t fsw[N_FOOTSW], btn[N_BUTTON], ab[N_ENCODER], push[N_ENCODER];
    uint16_t ped[N_PEDAL];
    input_hw_read(fsw, btn, ab, push, ped);
    for (int i=0;i<N_FOOTSW;i++)  sw_update(&s->fsw[i], fsw[i]);
    for (int i=0;i<N_BUTTON;i++)  sw_update(&s->btn[i], btn[i]);
    for (int i=0;i<N_ENCODER;i++){ encoder_update(&s->enc[i], ab[i]); sw_update(&s->enc_push[i], push[i]); }
    for (int i=0;i<N_PEDAL;i++)   s->pedal[i]=ped[i];
}

/* Live monitor: a row of 8 footswitch cells, button cells, encoder count as a
 * moving bar, and pedal level bars. Uses only display_fill_rect (no font). */
void input_monitor_draw(const input_state *s){
    display_clear(RGB565(0,0,30));
    /* footswitches: two rows of 4 (matches XL 8-switch layout) */
    for (int i=0;i<N_FOOTSW;i++){
        int col=i%4, row=i/4;
        uint16_t c = sw_state(&s->fsw[i]) ? RGB565(0,220,0) : RGB565(40,40,60);
        display_fill_rect(8+col*78, 24+row*52, 68, 40, c);
    }
    /* buttons row */
    for (int i=0;i<N_BUTTON;i++){
        uint16_t c = sw_state(&s->btn[i]) ? RGB565(220,180,0) : RGB565(40,40,60);
        display_fill_rect(8+i*78, 132, 68, 24, c);
    }
    /* encoder: count mapped to a horizontal bar; push lights the frame */
    int ecount = s->enc[0].count;
    int bar = LCD_W/2 + (ecount % 140);
    if (bar<8) bar=8;
    if (bar>LCD_W-16) bar=LCD_W-16;
    display_fill_rect(8, 166, LCD_W-16, 14, RGB565(30,30,50));
    display_fill_rect(bar, 166, 8, 14,
        sw_state(&s->enc_push[0])?RGB565(0,200,255):RGB565(200,200,255));
    /* pedals: vertical level bars */
    for (int i=0;i<N_PEDAL;i++){
        int h = (s->pedal[i]*48)/4095;
        display_fill_rect(8+i*30, 236-h, 22, h, RGB565(255,120,0));
    }
}
