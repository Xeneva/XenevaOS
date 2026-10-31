# Mbed TLS 3.6 client for XenevaOS. Upstream Apache-2.0 files stay in
# include/ and library/. This Makefile only adds the Xeneva port.

TOOLCHAIN ?= llvm
TARGET = libmbedtls.a

ROOT = .
OBJDIR = obj
XE_INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr
MBEDTLS_INCLUDES = -I$(ROOT)/include -I$(ROOT)/library -I$(ROOT)/port \
                   -I../zlib -I../brotli/c/include
CONFIG = -DMBEDTLS_CONFIG_FILE=\"xeneva_mbedtls_config.h\" \
         -DMBEDTLS_PSA_CRYPTO_CONFIG_FILE=\"xeneva_psa_config.h\"

LIB_SRCS = $(wildcard $(ROOT)/library/*.c)
PORT_C = $(ROOT)/port/entropy_hardware.c $(ROOT)/port/time_alt.c
PORT_CPP = $(ROOT)/port/https.cpp
GEN_C = $(OBJDIR)/cacert.c

LIB_OBJS = $(patsubst $(ROOT)/%.c,$(OBJDIR)/%.o,$(LIB_SRCS))
PORT_C_OBJS = $(OBJDIR)/port/entropy_hardware.o $(OBJDIR)/port/time_alt.o
PORT_CPP_OBJS = $(OBJDIR)/port/https.o
GEN_OBJS = $(OBJDIR)/cacert.o
OBJS = $(LIB_OBJS) $(PORT_C_OBJS) $(PORT_CPP_OBJS) $(GEN_OBJS)

LLVM_TARGET = -target aarch64-unknown-windows
COMMON = $(LLVM_TARGET) -ffreestanding -fno-stack-protector -fno-stack-check \
         -O2 -DARCH_ARM64 -D__STDC_LIMIT_MACROS -U_WIN32 -U_WIN64 -U__WIN32__ \
         -U_MSC_VER $(CONFIG) $(MBEDTLS_INCLUDES)

ifeq ($(TOOLCHAIN), llvm)
    CC = clang
    CXX = clang++
    AR = llvm-ar
    CFLAGS = -Wall $(COMMON) -Wno-unused-function -Wno-unused-variable \
             -Wno-unused-but-set-variable -w
    CXXFLAGS = -Wall -Wextra -fno-exceptions -fno-rtti $(COMMON) $(XE_INCLUDES) \
               -Wno-unused-function -Wno-unused-variable
endif

.PHONY: all clean llvm host-selftest
all: $(TARGET)
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/cacert.c: $(ROOT)/certs/cacert.pem $(ROOT)/port/pem_to_c.py
	@mkdir -p $(OBJDIR)
	python3 $(ROOT)/port/pem_to_c.py $(ROOT)/certs/cacert.pem $(OBJDIR)/cacert.c

# nolibc/ shadows XEClib headers that the vendored C cannot include.
# https.cpp is compiled separately and still sees the real socket header.
$(OBJDIR)/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT)/port/nolibc $(XE_INCLUDES) -c $< -o $@

$(OBJDIR)/cacert.o: $(OBJDIR)/cacert.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/port/https.o: $(ROOT)/port/https.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)

host-selftest:
	mkdir -p /tmp/mbedtls-host
	gcc -O2 -DMBEDTLS_SELF_TEST $(CONFIG) -I$(ROOT)/include -I$(ROOT)/library -I$(ROOT)/port \
		$(ROOT)/library/*.c $(ROOT)/port/entropy_hardware.c $(ROOT)/port/host_selftest.c \
		-o /tmp/mbedtls-host/selftest
	/tmp/mbedtls-host/selftest

clean:
	rm -rf $(OBJDIR) $(TARGET)
