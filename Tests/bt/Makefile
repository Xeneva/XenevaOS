CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -I../../KernelAA64/Bt

SRCS = main.c \
	../../KernelAA64/Bt/crypto.c \
	../../KernelAA64/Bt/h4.c \
	../../KernelAA64/Bt/parse.c \
	../../KernelAA64/Bt/lc3.c

# OUT keeps a caller such as Tests/dcl_test_all.sh from rebuilding over the
# tracked bt_test binary in the working tree.
OUT ?= bt_test

.PHONY: all clean
all: $(OUT)

$(OUT): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUT) bt_test
