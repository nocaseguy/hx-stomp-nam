#include "../src/dsp/biquad.h"
#include <math.h>
#include <stdio.h>
#include <complex.h>

/* Analytic magnitude of H(z) at frequency f (Hz) for given coeffs. */
static double magdb(const biquad_coeffs *c, double fs, double f) {
    double w = 2.0 * M_PI * f / fs;
    double complex z1 = cexp(-I*w), z2 = cexp(-2*I*w);
    double complex num = c->b0 + c->b1*z1 + c->b2*z2;
    double complex den = 1.0  + c->a1*z1 + c->a2*z2;
    return 20.0*log10(cabs(num/den));
}
static int approx(double a, double b, double tol, const char*m){
    if (fabs(a-b) > tol){ printf("FAIL %s: %.3f vs %.3f\n",m,a,b); return 1; }
    printf("ok   %-28s %.2f dB (want %.2f)\n",m,a,b); return 0;
}
int main(void){
    int fail=0; float fs=48000;
    biquad_coeffs c;
    biquad_lowpass(&c,fs,1000,0.7071f);
    fail+=approx(magdb(&c,fs,20),   0.0, 0.1,"LPF gain at 20Hz ~0dB");
    fail+=approx(magdb(&c,fs,1000),-3.0, 0.5,"LPF at fc ~-3dB");
    { double m=magdb(&c,fs,20000); fail+= (m<-40.0)?(printf("ok   LPF stopband %.1f dB (<-40)\n",m),0):(printf("FAIL LPF stopband %.1f\n",m),1); }
    biquad_highpass(&c,fs,1000,0.7071f);
    fail+=approx(magdb(&c,fs,20000),0.0,0.2,"HPF passband ~0dB");
    fail+=approx(magdb(&c,fs,1000),-3.0,0.5,"HPF at fc ~-3dB");
    biquad_peaking(&c,fs,1000,2.0f,6.0f);
    fail+=approx(magdb(&c,fs,1000),6.0,0.2,"Peak +6dB at fc");
    fail+=approx(magdb(&c,fs,50),0.0,0.3,"Peak flat at 50Hz");
    /* impulse-response stability: energy must decay */
    biquad_lowpass(&c,fs,500,0.7071f); biquad_state s; biquad_reset(&s);
    float e_early=0,e_late=0; 
    for(int i=0;i<4096;i++){ float x=(i==0)?1.0f:0.0f; float y=biquad_tick(&c,&s,x);
        if(i<64)e_early+=y*y; if(i>=2048)e_late+=y*y; }
    fail+= (e_late < 1e-9*e_early)?(printf("ok   stable IIR decays\n"),0):(printf("FAIL unstable\n"),1);
    printf(fail?"\n%d FAILURES\n":"\nALL BIQUAD TESTS PASS\n",fail);
    return fail;
}
