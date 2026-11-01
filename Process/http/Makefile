# LLVM curl.exe — HTTP/1.1 GET/HEAD client for --term.

TOOLCHAIN ?= gcc
TARGET = curl.exe
INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr -I../../Ports/mbedtls/port
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

MBEDLIB = ../../Ports/mbedtls/libmbedtls.a
ZLIB = ../../Ports/zlib/libz.a
BROTLILIB = ../../Ports/brotli/libbrotlidec.a

.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: $(TARGET)

$(MBEDLIB):
	$(MAKE) -C ../../Ports/mbedtls llvm

$(ZLIB):
	$(MAKE) -C ../../Ports/zlib llvm

$(BROTLILIB):
	$(MAKE) -C ../../Ports/brotli llvm

ifeq ($(TOOLCHAIN), llvm)
$(TARGET): $(OBJS) $(MBEDLIB) $(ZLIB) $(BROTLILIB)
	$(LD) $(LDFLAGS) $(OBJS) $(MBEDLIB) $(ZLIB) $(BROTLILIB) \
		../../Libs/XEClib/libXEClib.a -o $@
endif

$(OBJDIR)/main.o: main.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/crt0_arm64.o: ../../Process/Init/crt0arm64.s
	@mkdir -p $(OBJDIR)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
