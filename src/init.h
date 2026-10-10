#ifndef HX_INIT_H
#define HX_INIT_H
#include <stdint.h>
uint32_t clock_init(void);   /* -> resulting M4 core clock in Hz (204 MHz)    */
int      sdram_init(void);   /* 0 on completion; run post_sdram to validate    */
#endif
