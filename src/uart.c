#include "uart.h"
#include "lpc4350.h"
#define RBR 0x00
#define THR 0x00
#define DLL 0x00
#define DLM 0x04
#define FCR 0x08
#define LCR 0x0C
#define LSR 0x14
void uart_init(uint32_t base, uint32_t pclk, uint32_t baud){
    uint32_t div = pclk / (16u*baud);
    REG32(base+LCR) = 0x83;                 /* DLAB=1, 8N1 */
    REG32(base+DLL) = div & 0xFF;
    REG32(base+DLM) = (div>>8) & 0xFF;
    REG32(base+LCR) = 0x03;                 /* DLAB=0, 8N1 */
    REG32(base+FCR) = 0x07;                 /* enable+reset FIFOs */
}
void uart_putc(uint32_t base, char c){
    while(!(REG32(base+LSR) & (1u<<5))){}   /* THRE */
    REG32(base+THR) = (uint8_t)c;
}
void uart_puts(uint32_t base, const char *s){ while(*s){ if(*s=='\n') uart_putc(base,'\r'); uart_putc(base,*s++); } }
void uart_puthex(uint32_t base, uint32_t v){
    uart_puts(base,"0x"); for(int i=28;i>=0;i-=4){ int d=(v>>i)&0xF; uart_putc(base, d<10?'0'+d:'a'+d-10); }
}
