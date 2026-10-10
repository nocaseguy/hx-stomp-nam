#include "adc.h"
#include "lpc4350.h"
#define ADC_CR   0x00
#define ADC_GDR  0x04
void adc_init(uint32_t base, uint32_t pclk){
    uint32_t clkdiv = (pclk/4500000u); if (clkdiv) clkdiv--;   /* <=4.5 MHz ADC clk */
    REG32(base+ADC_CR) = (clkdiv<<8) | (1u<<21);               /* PDN=1, no channel */
}
uint16_t adc_read(uint32_t base, int ch){
    uint32_t cr = REG32(base+ADC_CR) & ~0xFFu;
    REG32(base+ADC_CR) = cr | (1u<<ch) | (1u<<24);            /* select ch, START=1 */
    uint32_t v;
    do { v = REG32(base+ADC_GDR); } while (!(v & (1u<<31)));  /* wait DONE */
    REG32(base+ADC_CR) &= ~(7u<<24);                          /* stop */
    return (uint16_t)((v>>6) & 0x3FF) << 2;                   /* 10-bit -> 0..4095 */
}
