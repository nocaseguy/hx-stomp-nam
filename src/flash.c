#include "flash.h"
#include "lpc4350.h"
#include <string.h>

/* SPIFI peripheral registers (0x40003000). */
#define SPIFI_CTRL  0x000
#define SPIFI_CMD   0x004
#define SPIFI_ADDR  0x008
#define SPIFI_IDATA 0x00C
#define SPIFI_CLIMIT 0x010
#define SPIFI_DATA  0x014
#define SPIFI_MCMD  0x018
#define SPIFI_STAT  0x01C

/* JEDEC capacity byte -> size. e.g. 0x18=16MB,0x19=32MB,0x1A=64MB,0x1B=128MB. */
uint32_t flash_size_from_jedec(uint32_t id){
    uint8_t cap = id & 0xFF;
    if (cap >= 0x10 && cap <= 0x20) return 1u << cap;   /* 2^cap bytes */
    return 0;
}

/* Read via the memory-mapped XIP window - the boot ROM already set this up. */
void flash_read(uint32_t offset, void *dst, uint32_t len){
    memcpy(dst, (const void *)(FLASH_XIP_BASE + offset), len);
}

/* NOTE: The stock bootloader owns SPIFI setup and its JEDEC-ID/erase/program
 * routines (near the MX25L/S25FL/W25Q tables). The command-mode sequences below
 * are the standard SPIFI flow; VERIFY the exact command opcodes against the
 * detected part before trusting a restore on hardware. Read/backup is exact. */
uint32_t flash_read_jedec(void){
    /* Requires SPIFI in command mode (call after leaving XIP). Returns 0 if the
     * peripheral is still in memory-mode; use a hardware programmer if unsure. */
    volatile uint32_t *stat = &REG32(SPIFI_BASE+SPIFI_STAT);
    if (*stat & (1u<<1)) return 0;    /* MCINIT: still memory-mapped */
    REG32(SPIFI_BASE+SPIFI_CMD) =
        (0x9Fu<<24) | (0u<<21) | (1u<<19) | (3u<<0);   /* opcode 0x9F, read 3B */
    uint32_t id = 0;
    for (int i=0;i<3;i++) id = (id<<8) | (REG32(SPIFI_BASE+SPIFI_DATA) & 0xFF);
    return id;
}

int flash_erase_sector(uint32_t offset){
    if (offset < FLASH_APP_OFFSET) return -1;   /* protect bootloader */
    /* command-mode: WREN(0x06) then SECTOR_ERASE(0x20) @offset; poll WIP */
    /* left as a structured stub - validate opcodes per detected part */
    (void)offset; return -2;                    /* not enabled until HW-verified */
}
int flash_program(uint32_t offset, const void *src, uint32_t len){
    if (offset < FLASH_APP_OFFSET) return -1;   /* protect bootloader */
    (void)src; (void)len; return -2;            /* not enabled until HW-verified */
}

/* Stream a flash region out over UART as Intel HEX (reassemble on the host with
 * tools/unhex.py). Safe: XIP read only. Full-flash dumps are slow over UART
 * (prefer an external SPI programmer for 8-64 MB); handy for verification. */
#include "uart.h"
static void u8(uint32_t base, uint8_t b){
    const char *h="0123456789ABCDEF";
    uart_putc(base,h[b>>4]); uart_putc(base,h[b&0xF]);
}
void flash_dump_ihex(uint32_t uart_base, uint32_t offset, uint32_t len){
    uint32_t hi = 0xFFFFFFFF;
    for (uint32_t a=0; a<len; a+=16){
        uint32_t addr = offset + a;
        if ((addr>>16) != hi){ hi = addr>>16;                 /* extended addr rec */
            uart_puts(uart_base,":02000004");
            uint8_t s=0x02+0x04+(hi>>8)+(hi&0xFF);
            u8(uart_base,hi>>8); u8(uart_base,hi&0xFF); u8(uart_base,(uint8_t)(-s)); uart_putc(uart_base,'\n');
        }
        uint8_t n = (len-a>=16)?16:(uint8_t)(len-a);
        uint8_t sum = n + (addr>>8 & 0xFF) + (addr & 0xFF);
        uart_putc(uart_base,':'); u8(uart_base,n); u8(uart_base,addr>>8); u8(uart_base,addr); u8(uart_base,0);
        for (uint8_t i=0;i<n;i++){ uint8_t b=*(volatile uint8_t*)(FLASH_XIP_BASE+addr+i); u8(uart_base,b); sum+=b; }
        u8(uart_base,(uint8_t)(-sum)); uart_putc(uart_base,'\n');
    }
    uart_puts(uart_base,":00000001FF\n");                     /* EOF */
}
