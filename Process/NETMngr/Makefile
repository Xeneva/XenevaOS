# LLVM udpecho.exe — localhost UDP self-test (weekly loopback check).

TOOLCHAIN ?= gcc
BUILD_MODE ?= static 

TARGET = netmngr.exe
INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
OBJDIR = obj
CPP_OBJS = $(OBJDIR)/main.o
S_OBJS = $(OBJDIR)/crt0_arm64.o

ifeq ($(BUILD_MODE), dynamic)
	OBJS = $(CPP_OBJS)
 LINK_LIBS = ../../Libs/XEClib/libXEClib.a ../../Libs/XECrt/xecrt.a 
else
	OBJS = $(CPP_OBJS) $(S_OBJS)
	LINK_LIBS = ../../Libs/XEClib/libXEClib.a 
endif

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
    # Entry must match the linked crt (Init/crt0arm64.s provides _aumain;
    # _xemain lives only in XECrt for dynamic builds). --axiss
    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native, -Wl,-filealign:0x1000
endif

.PHONY: all clean llvm dynamic
llvm:
	@$(MAKE) TOOLCHAIN=llvm all
dynamic:
	@$(MAKE) TOOLCHAIN=llvm BUILD_MODE=dynamic all

all: $(TARGET)

ifeq ($(TOOLCHAIN), llvm)
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(LINK_LIBS) -o $@
endif

$(OBJDIR)/main.o: main.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/crt0_arm64.o: ../../Process/Init/crt0arm64.s
	@mkdir -p $(OBJDIR)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
