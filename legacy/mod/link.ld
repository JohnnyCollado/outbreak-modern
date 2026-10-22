/* Code and constants at MOD_BASE (the patch writes them), state at MOD_STATE (never written by
   the patch: our code initialises it), the status block at MOD_STATUS (mod.h). */
ENTRY(mod_entry)
SECTIONS
{
    . = 0x000A0000;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) }
    _text_end = .;
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    _image_end = .;
    . = 0x000A8000;
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    _state_end = .;
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.comment) *(.pdr) *(.gnu.attributes) *(.mdebug.*) }
}
ASSERT(SIZEOF(.data) == 0, "initialised data would be rewritten every frame by the patch: use .bss and initialise it in code")
ASSERT(_image_end <= 0x000A8000, "code too big for 0xA0000-0xA8000")
ASSERT(_state_end <= 0x000AFF00, "state would overlap the status block")
