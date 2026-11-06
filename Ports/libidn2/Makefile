# libidn2 2.3.8 for XenevaOS.
#
# The library is built as a static archive with its bundled unistring
# subset.  XenevaOS has no iconv, so port/xe_port.c provides the small
# UTF-8-only locale conversion helpers used by the idn2_*_ul wrappers.

TOOLCHAIN ?= llvm
TARGET = libidn2.a
ROOT = .
OBJDIR = obj
XE_INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
IDN2_INCLUDES = -I$(ROOT)/port -I$(ROOT)/include -I$(ROOT)/lib -I$(ROOT)/unistring

SRCS = $(wildcard $(ROOT)/lib/*.c) \
       $(wildcard $(ROOT)/port/*.c) \
       $(wildcard $(ROOT)/unistring/*.c) \
       $(wildcard $(ROOT)/unistring/unistr/*.c) \
       $(wildcard $(ROOT)/unistring/unictype/*.c) \
       $(wildcard $(ROOT)/unistring/uninorm/*.c)
OBJS = $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(SRCS))

COMMON = -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-strict-aliasing -O2 -std=c99 \
         -DARCH_ARM64 -D__STDC_LIMIT_MACROS -DHAVE_CONFIG_H -DIDN2_BUILDING \
         -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
         -Wno-macro-redefined -Wno-incompatible-pointer-types-discards-qualifiers \
         $(IDN2_INCLUDES) $(XE_INCLUDES)

ifeq ($(TOOLCHAIN), llvm)
    CC = clang
    AR = llvm-ar
    CFLAGS = $(COMMON) -target aarch64-unknown-windows
else
    CC = aarch64-linux-gnu-gcc
    AR = aarch64-linux-gnu-ar
    CFLAGS = $(COMMON) -fPIE
endif

.PHONY: all clean llvm

all: $(TARGET)

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

clean:
	rm -rf $(OBJDIR) $(TARGET)
