# XenevaOS Namdapha Makefile (GCC / LLVM Build)

# Default toolchain is GCC
TOOLCHAIN ?= gcc

BLEED ?= 0
ifeq ($(BLEED),1)
    BLEED_FLAGS = -D__XENEVA_BLEED__
endif

# NOTE: never define __XENEVA_UNIKERNEL__ for the standalone build. It only
# enables the NamdaphaThread wrapper, whose NamdaphaMain callee exists solely
# inside deodxr.exe (DeodhaiXR compiles these same sources with -include
# unikernel_syms.h, which renames main). Defining it here breaks the link
# with undefined NamdaphaMain -- and adding the -include instead would break
# it the other way (crt needs a real main). Unused here by design. --axiss
UNIKERNEL ?= 0

TARGET = nmdapha.elf
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr -I../../Libs/Chitralekha -I../../Ports/freetype2/include

CPP_SRCS = NamdaphaButton.cpp _gettime.cpp gomenu.cpp namdapha.cpp nmGoButton.cpp section.cpp
S_SRCS = crt_arm64.s

OBJDIR = obj
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))
OBJS = $(CPP_OBJS) $(S_OBJS)

ifeq ($(TOOLCHAIN), llvm)
    # LLVM / Clang configuration
    CXX = clang++
    CC  = clang
    LD  = clang++

    LLVM_TARGET = -target aarch64-unknown-windows

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT -DCOLOR_RGBA -DNAMDAPHA_HORIZONTAL $(BLEED_FLAGS) \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               $(INCLUDES)

    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native -Wl,-filealign:0x1000
else
    # GCC configuration
    CXX = $(CROSS_COMPILE)g++
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fPIE -fno-exceptions -fno-rtti \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT -DCOLOR_RGBA -DNAMDAPHA_HORIZONTAL $(BLEED_FLAGS) \
               $(INCLUDES)
endif

# Triggered when you run "make llvm"
.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: nmdapha.exe

ifeq ($(TOOLCHAIN), llvm)
nmdapha.exe: $(OBJS)
	@echo "Linking nmdapha.exe (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
else
nmdapha.exe: $(TARGET)
	@echo "Converting ELF to PE32+ (nmdapha.exe)..."
	$(OBJCOPY) -O pei-aarch64-little $(TARGET) nmdapha.exe

$(TARGET): $(OBJS)
	@echo "Linking nmdapha.elf..."
	$(LD) -nostdlib -T linker.ld $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
endif

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) nmdapha.exe $(TARGET)
