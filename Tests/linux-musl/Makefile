# Static aarch64 guests for the Linux ELF path.
# musl.elf is linked with musl libc. rust-hello is a no_std ELF.

MUSL_CC := $(shell command -v aarch64-linux-musl-gcc 2>/dev/null)
ZIG ?= zig
RESOURCES := ../../Resources/resources

.PHONY: all clean rust musl
all: musl rust

ifeq ($(MUSL_CC),)
musl.elf: clib.c
	$(ZIG) cc -target aarch64-linux-musl -static -O2 -o $@ clib.c
else
musl.elf: clib.c
	$(MUSL_CC) -static -O2 -o $@ clib.c
endif

musl: musl.elf
	cp -f musl.elf $(RESOURCES)/musl.elf

rust:
	cd rust-hello && RUSTC_BOOTSTRAP=1 cargo -Zjson-target-spec -Zbuild-std=core build --release --target ../aarch64-xeneva.json
	cp -f rust-hello/target/aarch64-xeneva/release/rust-hello $(RESOURCES)/hello.elf

clean:
	rm -f musl.elf
	cd rust-hello && cargo clean
