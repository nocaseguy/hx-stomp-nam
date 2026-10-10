#include "i2c.h"
#include "lpc4350.h"
#define CONSET 0x00
#define STAT   0x04
#define DAT    0x08
#define SCLH   0x10
#define SCLL   0x14
#define CONCLR 0x18
#define AA (1<<2)
#define SI (1<<3)
#define STO (1<<4)
#define STA (1<<5)
#define I2EN (1<<6)
static void wait(uint32_t b){ while(!(REG32(b+CONSET)&SI)){} }
void i2c_init(uint32_t base, uint32_t pclk, uint32_t hz){
    uint32_t half = pclk/(2u*hz);
    REG32(base+SCLH)=half; REG32(base+SCLL)=half;
    REG32(base+CONCLR)=0x6C; REG32(base+CONSET)=I2EN;
}
static int start(uint32_t b){ REG32(b+CONSET)=STA; wait(b); REG32(b+CONCLR)=STA; return REG32(b+STAT); }
int i2c_write(uint32_t base, uint8_t a7, const uint8_t*d, uint32_t n){
    int st=start(base); if(st!=0x08 && st!=0x10) return -1;
    REG32(base+DAT)=(a7<<1)|0; REG32(base+CONCLR)=SI; wait(base);
    if(REG32(base+STAT)!=0x18){ REG32(base+CONSET)=STO; REG32(base+CONCLR)=SI; return -2; }
    for(uint32_t i=0;i<n;i++){ REG32(base+DAT)=d[i]; REG32(base+CONCLR)=SI; wait(base);
        if(REG32(base+STAT)!=0x28){ REG32(base+CONSET)=STO; REG32(base+CONCLR)=SI; return -3; } }
    REG32(base+CONSET)=STO; REG32(base+CONCLR)=SI; return 0;
}
int i2c_probe(uint32_t base, uint8_t a7){
    int st=start(base); if(st!=0x08 && st!=0x10) return -1;
    REG32(base+DAT)=(a7<<1)|0; REG32(base+CONCLR)=SI; wait(base);
    int ok = (REG32(base+STAT)==0x18) ? 0 : -1;
    REG32(base+CONSET)=STO; REG32(base+CONCLR)=SI; return ok;
}
