#include "lpc4350.h"
/* Busy-wait millisecond delay using SysTick, assuming a known core clock. */
static uint32_t g_hz = 12000000u;
void delay_set_clock(uint32_t hz){ g_hz = hz; }
void delay_ms(uint32_t ms){
    SYST_RVR = (g_hz/1000u) - 1u; SYST_CVR = 0; SYST_CSR = 5; /* enable, core clk, no int */
    for (uint32_t i=0;i<ms;i++){ while(!(SYST_CSR & (1u<<16))){} }
    SYST_CSR = 0;
}
