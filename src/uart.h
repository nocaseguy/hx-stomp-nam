#ifndef HX_UART_H
#define HX_UART_H
#include <stdint.h>
/* Polled 16550-style USART on LPC43xx. base = USARTn_BASE, pclk = its clock Hz. */
void uart_init(uint32_t base, uint32_t pclk, uint32_t baud);
void uart_putc(uint32_t base, char c);
void uart_puts(uint32_t base, const char *s);
void uart_puthex(uint32_t base, uint32_t v);
#endif
