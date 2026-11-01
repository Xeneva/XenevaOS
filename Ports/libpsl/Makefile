# libpsl 0.23.3 for XenevaOS.
#
# Built with the public-suffix data compiled in (ENABLE_BUILTIN) and
# without runtime IDNA support.  psl.c then uses its own punycode
# conversion, so no iconv or libidn2 dependency is needed.

TOOLCHAIN ?= llvm
TARGET = libpsl.a
ROOT = .
OBJDIR = obj
XE_INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
PSL_INCLUDES = -I$(ROOT)/port -I$(ROOT)/include -I$(ROOT)/src

SRCS = $(wildcard $(ROOT)/src/*.c) $(wildcard $(ROOT)/port/*.c)
OBJS = $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(SRCS))

COMMON = -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-strict-aliasing -O2 -std=c99 \
         -DARCH_ARM64 -D__STDC_LIMIT_MACROS -DHAVE_CONFIG_H -DBUILDING_PSL \
         -U_WIN32 -U_MSC_VER \
         -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
         -Wno-incompatible-pointer-types-discards-qualifiers \
         $(PSL_INCLUDES) $(XE_INCLUDES)

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
