#ifndef HX_ADC_H
#define HX_ADC_H
#include <stdint.h>
/* LPC43xx 10-bit SAR ADC (ADC0/ADC1), polled. base=ADC0_BASE/ADC1_BASE. */
void     adc_init(uint32_t base, uint32_t pclk);
uint16_t adc_read(uint32_t base, int ch);   /* returns 0..1023 (<<2 -> 0..4095) */
#endif
