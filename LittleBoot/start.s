.extern __bss_start
.extern __bss_size
.extern __image_size

.section .text.boot, "ax"
.global _start
/*
 * Linux arm64 Image header (Documentation/arch/arm64/booting.rst).
 * kernel8.img is a flat binary whose .bss (~8 MiB of pmmngr data) is not in
 * the file. image_size tells the loader (Pi firmware / QEMU) how much memory
 * LittleBoot really occupies, so the DTB and initramfs are not placed inside
 * .bss -- which start.s zeroes below. text_offset pins the load address to
 * 0x80000, the address linker.ld links LittleBoot at.
 */
_start:
   b _real_start                 // code0: branch over the header
   .word 0                       // code1
   .quad 0x80000                 // text_offset
   .quad __image_size            // image_size (file + .bss), little endian
   .quad 0x2                     // flags: LE, 4K pages, 2MB-aligned base near DRAM start
   .quad 0                       // res2
   .quad 0                       // res3
   .quad 0                       // res4
   .ascii "ARM\x64"              // magic
   .word 0                       // res5 (PE header offset, unused)

_real_start:
   /* x0 = DTB physical address (Linux boot protocol); left untouched until
    * LittleBootMain(dtb) */
   ldr x30, =stack_top
   mov x1,x4
   mov sp, x30
   ldr x5, =__bss_start
   ldr w6, =__bss_size
3:
   cbz w6, 4f
   str xzr, [x5], #8
   sub w6, w6, #1
   cbnz w6, 3b
4:
   bl LittleBootMain
hang:
   wfe
   b hang

.align 12
stack_bottom:
   .skip 16384
stack_top:
