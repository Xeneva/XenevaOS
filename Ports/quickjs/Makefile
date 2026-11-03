# XenevaOS QuickJS port Makefile (LLVM/AArch64)
#
# Builds quickjs.c + siblings (vendored under src/) into libquickjs.a with the
# same LLVM flags as the other PE/COFF ports (CssLibs, mbedtls). Port glue:
#  - port/port.c (built here) replaces the original hand-built os.o with
#    compiled equivalents: _msize via dlmalloc, clock_gettime, a full UTC
#    gmtime/localtime and no-op pthread_cond_* (single JS thread).
#  - port/libm.c is the double-precision gap (acos..trunc). The makefile
#    compiles it, so deleting the object does not lose the functions.
#  - 128-bit division helpers now come from Ports/compiler-rt; the old
#    checked-in port/int128.o workaround is gone.
# Port shims for libc headers quickjs.c needs (fenv, math, time, pthread)
# live in port/*.h and shadow the XEClib ones for this library only.

LLVM_TARGET = -target aarch64-unknown-windows
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -Iinclude -Iport -Isrc

COMMON_DEFS = -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC \
              -D__STDC_LIMIT_MACROS -D__bool_true_false_are_defined \
              -DCONFIG_VERSION=\"2026-06-04\" \
              -Dalloca=__builtin_alloca

INCLUDES += -I../../Libs/XEClib/includes -I../../BaseHdr

WARN_FLAGS = -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
             -Wno-unused-parameter -Wno-macro-redefined -Wno-error=incompatible-pointer-types \
             -Wno-error=int-conversion -Wno-implicit-function-declaration -Wno-c++11-narrowing

CFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
         -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) $(COMMON_DEFS) -U_WIN32 \
         -include stdbool.h $(WARN_FLAGS) $(INCLUDES)

# Debug: pass JQ_TRACE=1 for atom-table / heap tracing via _KePrint
JQ_TRACE ?= 0
ifneq ($(JQ_TRACE),0)
CFLAGS += -DJQ_TRACE
endif

AR = llvm-ar
RANLIB = llvm-ranlib

SRCS = src/quickjs.c src/cutils.c src/dtoa.c src/libregexp.c src/libunicode.c port/port.c port/libm.c
OBJS = obj/quickjs.o obj/cutils.o obj/dtoa.o obj/libregexp.o obj/libunicode.o obj/port.o obj/libm.o

TARGET = libquickjs.a

.PHONY: all clean llvm gcc

all: llvm

llvm: $(TARGET)

obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -Wno-unused-command-line-argument -c $< -o $@

obj/port.o: port/port.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -Wno-unused-command-line-argument -c $< -o $@

obj/libm.o: port/libm.c
	@mkdir -p $(dir $@)
	clang $(CFLAGS) -Wno-unused-command-line-argument -c $< -o $@

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	@echo "linked $@" && llvm-nm $@ | head -0

clean:
	rm -rf obj $(TARGET)
