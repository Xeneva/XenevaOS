/*
 * LittleBoot linker script -- Raspberry Pi 3B/3B+ (AArch64)
 *
 * The VideoCore firmware loads kernel8.img as a raw binary at 0x80000
 * (arm_64bit=1, no kernel_address override) and jumps to its first byte
 * at EL2 with the DTB pointer in x0. objcopy -O binary keeps everything
 * from the first loadable section on, so _start (.text.boot) must be first:
 * it begins with the 64-byte arm64 Image header (see start.s).
 *
 * Symbols consumed by the C/asm sources:
 *   phys        -- physical start of LittleBoot (littleboot.c memory map)
 *   end         -- physical end of LittleBoot, including .bss
 *   __bss_start -- start of .bss (start.s)
 *   __bss_size  -- size of .bss in 8-byte words (start.s clears 8 bytes/iter)
 */

OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    . = 0x80000;
    phys = .;

    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16) {
        *(.data .data.*)
    }

    /* .bss is NOBITS: it is not part of kernel8.img, which is why the
     * firmware must not place the initramfs right after the file
     * (see Scripts/Linux/build_rpi3.sh, initramfs address). */
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    . = ALIGN(4096);
    end = .;

    __bss_size = (__bss_end - __bss_start) >> 3;
    /* arm64 Image header image_size (start.s): whole footprint incl. .bss */
    __image_size = end - phys;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
