# LLVM dcltest.exe -- userspace end-to-end test for the DCL ports

TOOLCHAIN ?= gcc
TARGET = dcltest.exe
INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
OBJDIR = obj
CPP_OBJS = $(OBJDIR)/main.o
S_OBJS = $(OBJDIR)/crt0_arm64.o
OBJS = $(CPP_OBJS) $(S_OBJS)

ifeq ($(TOOLCHAIN), llvm)
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
endif

.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: $(TARGET)

ifeq ($(TOOLCHAIN), llvm)
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/XEClib/libXEClib.a -o $@
endif

$(OBJDIR)/main.o: main.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/crt0_arm64.o: ../../Process/Init/crt0arm64.s
	@mkdir -p $(OBJDIR)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
