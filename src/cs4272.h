/* cs4272.h - Cirrus Logic CS4272 stereo audio codec register map.
 * Per the HX Stomp teardown, the audio codec is a CS4272. Control is I2C
 * (7-bit addr 0x10 with AD0=0, 0x11 with AD0=1). Registers are public. */
#ifndef HX_CS4272_H
#define HX_CS4272_H
#define CS4272_I2C_ADDR   0x10
#define CS4272_REG_MODECTRL1  0x01
#define CS4272_REG_DACCTRL    0x02
#define CS4272_REG_DACVOL     0x03
#define CS4272_REG_DACCHA     0x04
#define CS4272_REG_DACCHB     0x05
#define CS4272_REG_ADCCTRL    0x06
#define CS4272_REG_MODECTRL2  0x07
#define CS4272_REG_CHIPID     0x08   /* [7:4]=part id (0x0), [3:0]=rev */
#endif
