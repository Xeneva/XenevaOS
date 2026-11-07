# XenevaOS ntpd Makefile (GCC / LLVM Build)

# Default toolchain is GCC
TOOLCHAIN ?= gcc

TARGET = ntpd.elf
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr

CPP_SRCS = main.cpp

OBJDIR = obj
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(OBJDIR)/crt0_arm64.o
OBJS = $(CPP_OBJS) $(S_OBJS)

ifeq ($(TOOLCHAIN), llvm)
    # LLVM / Clang configuration
    CXX = clang++
    CC  = clang
    LD  = clang++

    LLVM_TARGET = -target aarch64-unknown-windows

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               $(INCLUDES)

    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native
else
    # GCC configuration
    CXX = $(CROSS_COMPILE)g++
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fPIE -fno-exceptions -fno-rtti \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS \
               $(INCLUDES)
endif

# Triggered when you run "make llvm"
.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: ntpd.exe

ifeq ($(TOOLCHAIN), llvm)
ntpd.exe: $(OBJS)
	@echo "Linking ntpd.exe (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/XEClib/libXEClib.a -o $@
else
ntpd.exe: $(TARGET)
	@echo "Converting ELF to PE32+ (ntpd.exe)..."
	$(OBJCOPY) -O pei-aarch64-little $(TARGET) ntpd.exe

$(TARGET): $(OBJS)
	@echo "Linking ntpd.elf..."
	$(LD) -nostdlib -T linker.ld $(OBJS) ../../Libs/XEClib/libXEClib.a -o $@
endif

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/crt0_arm64.o: ../../Process/Init/crt0arm64.s
	@mkdir -p $(OBJDIR)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) ntpd.exe $(TARGET)
