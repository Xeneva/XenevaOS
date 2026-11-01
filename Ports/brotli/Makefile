# Brotli 1.1.0 decoder for XenevaOS. Only c/common + c/dec are vendored; the
# encoder and CLI are not part of the image.

TOOLCHAIN ?= llvm
TARGET = libbrotlidec.a
ROOT = .
OBJDIR = obj
XE_INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
BROTLI_INCLUDES = -I$(ROOT)/port -I$(ROOT)/c/include -I$(ROOT)/c/common -I$(ROOT)/c/dec

COMMON_SRCS = $(wildcard $(ROOT)/c/common/*.c)
DEC_SRCS = $(wildcard $(ROOT)/c/dec/*.c)
OBJS = $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(COMMON_SRCS) $(DEC_SRCS))

COMMON = -ffreestanding -fno-stack-protector -fno-stack-check \
         -fno-strict-aliasing -O2 -std=c99 \
         -DARCH_ARM64 -D__STDC_LIMIT_MACROS \
         -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
         $(BROTLI_INCLUDES) $(XE_INCLUDES)

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
