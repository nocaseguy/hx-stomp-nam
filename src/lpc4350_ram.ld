/* Link for LPC4350 internal SRAM, where the DFU boot ROM loads the image.
 * Local SRAM: 0x10000000 (128K) + 0x10080000 (72K). Stack at top of bank0.
 * Note: the framebuffer and large buffers live in external SDRAM (0x28000000),
 * addressed directly by drivers, not via this link map. */
ENTRY(Reset_Handler)
MEMORY {
  SRAM0 (rwx) : ORIGIN = 0x10000000, LENGTH = 128K
  SRAM1 (rwx) : ORIGIN = 0x10080000, LENGTH = 72K
}
_estack = ORIGIN(SRAM0) + LENGTH(SRAM0);
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > SRAM0
  .text : { *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t) } > SRAM0
  .ARM.exidx : { *(.ARM.exidx*) } > SRAM0
  _sidata = LOADADDR(.data);
  .data : { _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > SRAM0 AT > SRAM0
  .bss  : { _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > SRAM0
  . = ALIGN(8); _heap = .;
}
