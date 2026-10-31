# XenevaOS NetSurf Makefile (LLVM Build, AArch64)

# Bootstrap browser frontend for XenevaOS: Chitralekha GUI + XEClib sockets,
# HTTP/1.0 fetch with crude HTML-to-text rendering. Follows the Doomgeneric
# LLVM pattern (PE32+, entry _aumain, base 0x600000000).
#
# This is the staging frontend, not upstream NetSurf core yet: real NetSurf
# (libcss/libdom/libparserutils + TLS) lands once the musl shim + mbedTLS
# ports exist. See README.md.
#
# Like doom.exe, the built binary is a local artifact: *.exe is git-ignored
# and Scripts/Linux/build_and_run_qemu.sh packs/excludes it via --no-netsurf.

TOOLCHAIN ?= llvm

TARGET = netsurf.exe
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I. -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr -I../../Libs/Chitralekha -I../../Ports/freetype2/include \
           -I../../Ports/CssLibs/libcss/include -I../../Ports/CssLibs/libparserutils/include -I../../Ports/CssLibs/libwapcaplet/include \
           -I../../Ports/CssLibs/libdom/include -I../../Ports/CssLibs/libhubbub/include \
           -I../../Ports/mbedtls/port -I../../Ports/quickjs/include -Iminiz -DMINIZ_EXPORT=

CPP_SRCS = main.cpp
C_SRCS = css.c dom.c layout.c js.c pngdec.c svgdec.c miniz/miniz_tinfl.c
S_SRCS =

OBJDIR = obj
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
C_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))
CRT_OBJ = $(OBJDIR)/crt0_arm64.o
OBJS = $(CPP_OBJS) $(C_OBJS) $(S_OBJS) $(CRT_OBJ)

COMMON_DEFS = -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -D__bool_true_false_are_defined

ifeq ($(TOOLCHAIN), llvm)
    CXX = clang++
    CC  = clang
    LD  = clang++
    LLVM_TARGET = -target aarch64-unknown-windows
    CXXFLAGS = -Wall -Wextra -D_FORTIFY=0 -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               $(COMMON_DEFS) \
               -U_WIN32 \
               -include stdbool.h \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
               -Wno-unused-parameter \
               -Wno-macro-redefined \
               $(INCLUDES)
    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native -Wl,-filealign:0x1000
endif

CSSLIB = ../../Ports/CssLibs/libcssport.a
QJSLIB = ../../Ports/quickjs/libquickjs.a
MBEDLIB = ../../Ports/mbedtls/libmbedtls.a
ZLIB = ../../Ports/zlib/libz.a
BROTLILIB = ../../Ports/brotli/libbrotlidec.a

# Sources under Ports/. A newer file rebuilds that archive from this makefile.
CSS_INPUTS := $(shell find ../../Ports/CssLibs \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) ! -path '*/obj/*')
QJS_INPUTS := $(shell find ../../Ports/quickjs \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) ! -path '*/obj/*') \
	../../Ports/quickjs/port/libm.c ../../Ports/quickjs/port/int128.o
MBED_INPUTS := $(shell find ../../Ports/mbedtls \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.pem' -o -name 'Makefile' \) ! -path '*/obj/*')

.PHONY: all clean llvm

all: $(TARGET)

# Ports archives this binary links. One `make` here builds any that are missing
# or older than their sources.
$(CSSLIB): $(CSS_INPUTS)
	$(MAKE) -C ../../Ports/CssLibs llvm

$(QJSLIB): $(QJS_INPUTS)
	$(MAKE) -C ../../Ports/quickjs llvm

$(MBEDLIB): $(MBED_INPUTS)
	$(MAKE) -C ../../Ports/mbedtls llvm

$(ZLIB):
	$(MAKE) -C ../../Ports/zlib llvm

$(BROTLILIB):
	$(MAKE) -C ../../Ports/brotli llvm

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

$(CRT_OBJ): ../../Process/Init/crt0arm64.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

$(TARGET): $(OBJS) $(CSSLIB) $(QJSLIB) $(MBEDLIB) $(ZLIB) $(BROTLILIB)
	@echo "Linking $@ (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) $(CSSLIB) $(QJSLIB) ../../Libs/Chitralekha/libChitralekha.a \
		$(MBEDLIB) $(ZLIB) $(BROTLILIB) ../../Libs/XEClib/libXEClib.a -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
