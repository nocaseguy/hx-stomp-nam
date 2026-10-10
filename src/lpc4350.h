/* lpc4350.h - minimal register map for the NXP LPC4350 (Cortex-M4 core).
 * Only what the HX Stomp bring-up/POST needs. Addresses from NXP UM10503 and
 * cross-checked against the base addresses referenced in the stock firmware. */
#ifndef LPC4350_H
#define LPC4350_H
#include <stdint.h>
#define REG32(a) (*(volatile uint32_t *)(a))

/* ---- Clock / reset ---- */
#define CGU_BASE      0x40050000u
#define CCU1_BASE     0x40051000u
#define CCU2_BASE     0x40052000u
#define RGU_BASE      0x40053000u
#define CREG_BASE     0x40043000u
#define CGU_XTAL_OSC_CTRL   REG32(CGU_BASE+0x018)
#define CGU_PLL1_STAT       REG32(CGU_BASE+0x040)
#define CGU_PLL1_CTRL       REG32(CGU_BASE+0x044)
#define CGU_BASE_M4_CLK     REG32(CGU_BASE+0x06C)
#define CGU_BASE_APB1_CLK   REG32(CGU_BASE+0x080)
#define CGU_BASE_APB3_CLK   REG32(CGU_BASE+0x084)
#define CGU_BASE_SSP0_CLK   REG32(CGU_BASE+0x094)
#define CGU_BASE_SSP1_CLK   REG32(CGU_BASE+0x098)
#define CGU_BASE_UART0_CLK  REG32(CGU_BASE+0x09C)   /* USART0 */
#define CGU_BASE_UART3_CLK  REG32(CGU_BASE+0x0A8)

/* ---- Pin mux (SCU) ---- */
#define SCU_BASE      0x40086000u
#define SCU_SFS(port,pin)   REG32(SCU_BASE + 0x80u*(port) + 0x4u*(pin))
#define SCU_SFSI2C0         REG32(SCU_BASE+0xC0C)     /* I2C0 open-drain pads */
#define SCU_FUNC(f)   ((f)&0x7)
#define SCU_EPD       (1u<<3)   /* pull-down enable */
#define SCU_EPUN      (1u<<4)   /* pull-up disable  */
#define SCU_EHS       (1u<<5)   /* high-speed slew  */
#define SCU_EZI       (1u<<6)   /* input buffer enable */
#define SCU_ZIF       (1u<<7)   /* input glitch filter disable */

/* ---- GPIO ---- */
#define GPIO_BASE     0x400F4000u
#define GPIO_B(port,pin)  (*(volatile uint8_t *)(GPIO_BASE + 0x20u*(port) + (pin)))
#define GPIO_DIR(port)    REG32(GPIO_BASE+0x2000u + 0x4u*(port))
#define GPIO_PIN(port)    REG32(GPIO_BASE+0x2100u + 0x4u*(port))
#define GPIO_SET(port)    REG32(GPIO_BASE+0x2200u + 0x4u*(port))
#define GPIO_CLR(port)    REG32(GPIO_BASE+0x2280u + 0x4u*(port))

/* ---- Peripherals confirmed referenced by the stock firmware ---- */
#define EMC_BASE      0x40005000u    /* external SDRAM controller */
#define SPIFI_BASE    0x40003000u    /* QSPI NOR (XIP @0x14000000) */
#define LCD_BASE      0x40008000u    /* LPC LCD controller (display) */
#define USB0_BASE     0x40006000u
#define I2S0_BASE     0x400A2000u    /* audio link */
#define I2C0_BASE     0x400A1000u
#define I2C1_BASE     0x400E0000u    /* codec / EEPROM control bus */
#define SSP0_BASE     0x40083000u    /* SHARC boot + host link */
#define SSP1_BASE     0x400C5000u
#define USART0_BASE   0x40081000u
#define UART1_BASE    0x40082000u
#define USART2_BASE   0x400C1000u
#define USART3_BASE   0x400C2000u
#define ADC0_BASE     0x400E3000u
#define ADC1_BASE     0x400E4000u
#define SDRAM_BASE    0x28000000u

/* ---- Cortex-M SysTick ---- */
#define SYST_CSR      REG32(0xE000E010)
#define SYST_RVR      REG32(0xE000E014)
#define SYST_CVR      REG32(0xE000E018)

#endif
