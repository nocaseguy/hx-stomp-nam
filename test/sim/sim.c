/* sim.c - run the REAL firmware DSP + display/input code on the host.
 * Produces: dry.wav, wet.wav (audio through the EQ engine) and monitor.ppm
 * (the on-device 480x320 control monitor rendered to an image). */
#include "../../src/dsp/engine.h"
#include "../../src/input/input.h"
#include "../../src/display.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ---- sim hardware backend for input.c (scripted controls) ---- */
static uint8_t g_fsw[N_FOOTSW], g_btn[N_BUTTON], g_ab[N_ENCODER], g_push[N_ENCODER];
static uint16_t g_ped[N_PEDAL];
int input_hw_init(void){ return 0; }
void input_hw_read(uint8_t f[N_FOOTSW],uint8_t b[N_BUTTON],uint8_t ab[N_ENCODER],
                   uint8_t p[N_ENCODER],uint16_t ped[N_PEDAL]){
    memcpy(f,g_fsw,sizeof g_fsw); memcpy(b,g_btn,sizeof g_btn);
    memcpy(ab,g_ab,sizeof g_ab); memcpy(p,g_push,sizeof g_push);
    memcpy(ped,g_ped,sizeof g_ped);
}

#define FS 48000
#define N  (FS*2)

static void wav16(const char*fn, const int32_t*lr, int frames){
    FILE*f=fopen(fn,"wb"); int br=FS*4, bytes=frames*4;
    fputs("RIFF",f); int c=36+bytes; fwrite(&c,4,1,f); fputs("WAVE",f);
    fputs("fmt ",f); c=16; fwrite(&c,4,1,f); short s=1; fwrite(&s,2,1,f);
    s=2; fwrite(&s,2,1,f); c=FS; fwrite(&c,4,1,f); c=br; fwrite(&c,4,1,f);
    s=4; fwrite(&s,2,1,f); s=16; fwrite(&s,2,1,f);
    fputs("data",f); fwrite(&bytes,4,1,f);
    for(int i=0;i<frames;i++){ short l=lr[2*i]>>8, r=lr[2*i+1]>>8; fwrite(&l,2,1,f); fwrite(&r,2,1,f); }
    fclose(f);
}
static void ppm(const char*fn){
    uint16_t*fb=display_fb(); FILE*f=fopen(fn,"wb");
    fprintf(f,"P6\n%d %d\n255\n",LCD_W,LCD_H);
    for(int i=0;i<LCD_W*LCD_H;i++){ uint16_t p=fb[i];
        unsigned char c[3]={ (p>>11&0x1f)<<3, (p>>5&0x3f)<<2, (p&0x1f)<<3 };
        fwrite(c,1,3,f);}
    fclose(f);
}

int main(void){
    /* ---- 1. AUDIO: a decaying E-major chord through the EQ engine ---- */
    static int32_t dry[2*N], wet[2*N];
    const double freqs[]={82.41,123.47,164.81,207.65,246.94,329.63};
    for(int i=0;i<N;i++){
        double t=(double)i/FS, env=exp(-t*1.5), s=0;
        for(int k=0;k<6;k++) s+=sin(2*M_PI*freqs[k]*t)/6.0;
        int32_t v=(int32_t)(s*env*7000000.0);
        dry[2*i]=dry[2*i+1]=v; wet[2*i]=wet[2*i+1]=v;
    }
    audio_engine e; engine_init(&e,FS); engine_set_gain_db(&e,0.0f);
    engine_process_i32(&e,wet,N);
    wav16("dry.wav",dry,N); wav16("wet.wav",wet,N);
    /* measure the EQ effect: RMS ratio in a few bands via naive goertzel-ish */
    printf("[audio] wrote dry.wav + wet.wav (%d frames, 48kHz stereo)\n",N);

    /* ---- 2. DISPLAY: render the control monitor with scripted controls ---- */
    input_state in; input_init(&in);
    g_fsw[0]=g_fsw[2]=g_fsw[5]=1;     /* FS 1,3,6 pressed */
    g_btn[1]=1;                        /* a button lit */
    g_ped[0]=3000; g_ped[1]=1500; g_ped[2]=600;
    /* turn the encoder ~ a third of a turn CW */
    const uint8_t seq[4]={0b00,0b01,0b11,0b10};
    for(int step=0; step<40; step++){ g_ab[0]=seq[step&3]; input_poll(&in); }
    for(int k=0;k<8;k++) input_poll(&in);   /* settle debouncers on held switches */
    input_monitor_draw(&in);
    ppm("monitor.ppm");
    printf("[display] encoder count=%d, FS pressed:", in.enc[0].count);
    for(int i=0;i<N_FOOTSW;i++) if(sw_state(&in.fsw[i])) printf(" %d",i+1);
    printf("\n[display] wrote monitor.ppm (%dx%d)\n",LCD_W,LCD_H);
    return 0;
}
