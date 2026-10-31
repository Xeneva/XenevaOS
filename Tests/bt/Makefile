CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -I../../KernelAA64/Bt

SRCS = main.c \
	../../KernelAA64/Bt/crypto.c \
	../../KernelAA64/Bt/h4.c \
	../../KernelAA64/Bt/parse.c \
	../../KernelAA64/Bt/lc3.c

.PHONY: all clean
all: bt_test

bt_test: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f bt_test
