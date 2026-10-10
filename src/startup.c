/* startup.c - minimal LPC4350 (Cortex-M4) startup, from scratch.
 * No vendor headers, no CMSIS: just the reset path and a vector table,
 * matching the layout the stock firmware uses (SP + reset at image start). */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int  main(void);
void Reset_Handler(void);
void Default_Handler(void) { for (;;) {} }

/* Vector table placed first in the image (see linker script .isr_vector). */
__attribute__((section(".isr_vector"), used))
void (* const g_vectors[])(void) = {
    (void (*)(void)) &_estack,   /* 0x00 initial stack pointer            */
    Reset_Handler,               /* 0x04 reset                           */
    Default_Handler,             /* NMI            */
    Default_Handler,             /* HardFault      */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    0, 0, 0,
    Default_Handler,             /* SVCall         */
    Default_Handler, 0,
    Default_Handler,             /* PendSV         */
    Default_Handler,             /* SysTick        */
};

void Reset_Handler(void) {
    /* The images are built for the FPU (hard-float): give code access to it
     * before anything can use it. Without this the first float instruction
     * faults (seen on the unit: pedal.c stopped at its first number). */
    *(volatile uint32_t *)0xE000ED88u |= 0xFu << 20;         /* CPACR: CP10, CP11 full access */
    /* copy .data from LMA to RAM */
    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata; ) *d++ = *s++;
    /* zero .bss */
    for (uint32_t *d = &_sbss; d < &_ebss; ) *d++ = 0;
    main();
    for (;;) {}
}
