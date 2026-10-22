# Entry from a patched call site (`jal mod_entry` where the game had `jal 0x00345740`): run
# mod_frame(), then continue into 0x00345740 with the caller's ra, so it returns where the game
# expects. The EE's registers are 128 bits and the game may keep 64/128-bit values in any of
# them, so everything is saved whole with sq/lq (EE-only, written as .word: the assembler doesn't
# know them), and the FPU registers with swc1/lwc1. mod_frame is ordinary C and may clobber any
# caller-saved register and the low halves of the rest.
        .set noreorder
        .set noat
        .section .text.entry, "ax"
        .globl mod_entry
        .ent mod_entry
mod_entry:
        addiu   $sp, $sp, -0x2A0
        .irp r, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,30,31
        .word   0x7C000000 | (29 << 21) | (\r << 16) | (\r * 16)       # sq $r, r*16($sp)
        .endr
        mfhi    $8
        sw      $8, 0x200($sp)
        mflo    $8
        sw      $8, 0x204($sp)
        .irp f, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        swc1    $f\f, (0x210 + \f * 4)($sp)
        .endr
        cfc1    $8, $31
        sw      $8, 0x290($sp)
        move    $4, $31                 # a0 = the call site + 8 (before jal replaces $31)
        jal     mod_frame
        nop
        lw      $8, 0x290($sp)
        ctc1    $8, $31
        .irp f, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        lwc1    $f\f, (0x210 + \f * 4)($sp)
        .endr
        lw      $8, 0x200($sp)
        mthi    $8
        lw      $8, 0x204($sp)
        mtlo    $8
        .irp r, 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,30,31
        .word   0x78000000 | (29 << 21) | (\r << 16) | (\r * 16)       # lq $r, r*16($sp)
        .endr
        j       0x00345740
        addiu   $sp, $sp, 0x2A0
        .end mod_entry
