/* flash.h - SPIFI QSPI NOR access for backup/restore of the HX Stomp flash.
 *
 * The LPC4350 boot ROM memory-maps the SPIFI flash at 0x14000000 (XIP), so the
 * entire flash can be READ as memory - this makes a full backup trivial and
 * safe. WRITE/ERASE use the SPIFI peripheral command interface. The stock
 * bootloader lives at the start of flash; NEVER erase/program the bootloader
 * region (see FLASH_APP_OFFSET) so the official HX Edit recovery always works. */
#ifndef HX_FLASH_H
#define HX_FLASH_H
#include <stdint.h>

#define FLASH_XIP_BASE   0x14000000u
#define FLASH_SIZE       0x04000000u     /* 64 MB S25FL512S-class (confirmed high) */
#define FLASH_SECTOR     0x40000u        /* 256 KiB erase sector (S25FL512S)      */
/* Keep the bootloader (entry 1, ~177 KiB) untouched. Round up generously. */
#define FLASH_APP_OFFSET 0x40000u       /* only ever write at/after this offset  */

uint32_t flash_read_jedec(void);        /* 0x??MMCC : manuf/mem/capacity code    */
uint32_t flash_size_from_jedec(uint32_t id); /* bytes, from capacity byte         */

/* Read via XIP (always safe). offset is from flash start. */
void     flash_read(uint32_t offset, void *dst, uint32_t len);

/* Erase one 4KiB sector / program bytes (command mode). Return 0 on success.
 * Refuses any offset < FLASH_APP_OFFSET to protect the bootloader. */
int      flash_erase_sector(uint32_t offset);
int      flash_program(uint32_t offset, const void *src, uint32_t len);
void     flash_dump_ihex(uint32_t uart_base, uint32_t offset, uint32_t len);

#endif
