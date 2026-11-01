# zlib 1.3.1 for XenevaOS. The gz* file API is not vendored: the HTTP client
# only needs inflate/deflate, so the library is built with Z_SOLO.

TOOLCHAIN ?= llvm
TARGET = libz.a
ROOT = .
OBJDIR = obj
XE_INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
Z_INCLUDES = -I$(ROOT)

SRCS = adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c \
       inftrees.c trees.c uncompr.c zutil.c
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

COMMON = -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-strict-aliasing -O2 -std=c99 \
         -DZ_SOLO -DARCH_ARM64 -D__STDC_LIMIT_MACROS \
         -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
         $(Z_INCLUDES) $(XE_INCLUDES)

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

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

clean:
	rm -rf $(OBJDIR) $(TARGET)
