# LittleBoot -- flat AArch64 loader for Raspberry Pi 3B/3B+
#
# Produces kernel8.img, the raw binary the Pi firmware (start.elf) loads at
# 0x80000 when config.txt has arm_64bit=1. LittleBoot then reads
# EFI/XENEVA/xnkrnl.exe out of the FAT32 initramfs and jumps to KernelAA64.
#
#   make            build with $(TOOLCHAIN) (default: llvm)
#   make llvm       build with clang + ld.lld + llvm-objcopy
#   make gcc        build with aarch64-linux-gnu-{gcc,ld,objcopy}
#   make clean
#
# Override CROSS_COMPILE for a different GNU prefix (e.g. aarch64-none-elf-).

TOOLCHAIN ?= llvm

TARGET_ELF = littleboot.elf
TARGET_IMG = kernel8.img
TARGET_MAP = littleboot.map
LDSCRIPT   = linker.ld

C_SRCS   = littleboot.c ctype.c dtb.c physm.c ramdisk.c string.c uart.c vector.c vmmngr.c
ASM_SRCS = start.s vector.s

OBJDIR = obj
# Assembly objects get a .s.o suffix: vector.s and vector.c would both map
# to obj/vector.o otherwise. start.s.o goes first so _start lands at 0x80000
# (linker.ld also pins .text.boot).
ASM_OBJS = $(patsubst %.s,$(OBJDIR)/%.s.o,$(ASM_SRCS))
C_OBJS   = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
OBJS     = $(ASM_OBJS) $(C_OBJS)

# Freestanding, no FP/SIMD (CPACR_EL1.FPEN is not set before the kernel
# runs) and strict alignment (LittleBoot runs with the MMU off at first, so
# all memory is Device-nGnRnE and unaligned accesses fault).
COMMON_FLAGS = -ffreestanding -fno-builtin -nostdlib -fno-stack-protector \
               -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
               -fno-omit-frame-pointer -O2 -g -I.

# The sources are older-style C (implicit decls, pointer/int mixing in a
# few spots); keep those as warnings rather than hard errors.
WARN_FLAGS = -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
             -Wno-pointer-sign -Wno-incompatible-pointer-types \
             -Wno-int-conversion -Wno-implicit-function-declaration \
             -Wno-builtin-declaration-mismatch -Wno-incompatible-library-redeclaration

# make POISON=1: fill free RAM with junk before the kernel runs (testing
# only, catches code that relies on QEMU's zeroed memory)
ifeq ($(POISON),1)
    COMMON_FLAGS += -DLB_POISON_RAM
endif

.PHONY: all clean llvm gcc

all: $(TARGET_IMG)

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

gcc:
	@$(MAKE) TOOLCHAIN=gcc all

ifeq ($(TOOLCHAIN),llvm)
    CC      = clang
    LD      = ld.lld
    OBJCOPY = llvm-objcopy
    CFLAGS  = --target=aarch64-none-elf -mcpu=cortex-a53 $(COMMON_FLAGS) $(WARN_FLAGS) \
              -Wno-unknown-warning-option
    ASFLAGS = --target=aarch64-none-elf -mcpu=cortex-a53

    ifeq ($(shell command -v $(CC) 2>/dev/null),)
        $(error $(CC) not found on PATH)
    endif
    ifeq ($(shell command -v $(LD) 2>/dev/null),)
        $(error $(LD) not found on PATH. It ships with the clang/lld package)
    endif
else
    CROSS_COMPILE ?= aarch64-linux-gnu-
    CC      = $(CROSS_COMPILE)gcc
    LD      = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy
    CFLAGS  = -mcpu=cortex-a53 $(COMMON_FLAGS) $(WARN_FLAGS) -fno-asynchronous-unwind-tables
    ASFLAGS = -mcpu=cortex-a53
endif

LDFLAGS = -nostdlib -static -T $(LDSCRIPT) -Map=$(TARGET_MAP) --no-dynamic-linker

$(TARGET_IMG): $(TARGET_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "[LittleBoot] $@ ready ($$(stat -c %s $@) bytes)"

$(TARGET_ELF): $(OBJS) $(LDSCRIPT)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.s.o: %.s | $(OBJDIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OBJDIR):
	@mkdir -p $@

# headers are few and shared by nearly everything
$(OBJS): $(wildcard *.h) Makefile

clean:
	rm -rf $(OBJDIR) $(TARGET_ELF) $(TARGET_IMG) $(TARGET_MAP)
