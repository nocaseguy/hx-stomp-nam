#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
/* Push white noise through the real engine, measure gain per band = EQ curve. */
int main(void){
    audio_engine e; engine_init(&e,48000.0f); engine_set_gain_db(&e,0.0f);
    enum{N=1<<18}; static int32_t buf[2*N];
    unsigned seed=12345;
    for(int i=0;i<N;i++){ seed=seed*1103515245+12345; int32_t v=((int)(seed>>9)&0x3fffff)-0x200000; buf[2*i]=buf[2*i+1]=v; }
    static int32_t dry[2*N]; for(int i=0;i<2*N;i++) dry[i]=buf[i];
    engine_process_i32(&e,buf,N);
    double freqs[]={40,60,80,120,200,400,800,1500,3000,6000,12000};
    printf("  freq   gain(dB)   [HPF80 + -3dB@400 + +4dB@3k]\n");
    for(unsigned b=0;b<sizeof(freqs)/sizeof(freqs[0]);b++){
        double f=freqs[b], w=2*M_PI*f/48000, c=2*cos(w);
        double a1=0,a2=0,d1=0,d2=0;
        for(int i=0;i<N;i++){ double s;
            s=buf[2*i]+c*a1-a2; a2=a1; a1=s;
            s=dry[2*i]+c*d1-d2; d2=d1; d1=s; }
        double wm=sqrt(a1*a1+a2*a2-c*a1*a2), dm=sqrt(d1*d1+d2*d2-c*d1*d2);
        double g=20*log10((wm+1e-9)/(dm+1e-9));
        printf("%6.0f   %+6.1f   ",f,g);
        int bar=(int)(g+30); for(int k=0;k<bar;k++) putchar('#'); putchar('\n');
    }
    return 0;
}
