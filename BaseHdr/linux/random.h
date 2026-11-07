#ifndef __LINUX_RANDOM_H__
#define __LINUX_RANDOM_H__

/*
 * DCL <linux/random.h> -- the /dev/random + /dev/urandom fops mem.c's
 * devlist[] points at. mainline keeps them in drivers/char/random.c on top
 * of its own ChaCha20 pool; DCL binds them straight to Xeneva's hardware
 * RNG (hwrng_read_bytes, already wired to virtio_rng.ko by the kmod shim),
 * so the bytes are real entropy rather than a second, weaker PRNG.
 *
 * Declared here exactly as mainline does, consumed by DCL/linux_mm_shim.c.
 */

#include <linux/fs.h>

extern const struct file_operations random_fops;
extern const struct file_operations urandom_fops;

void get_random_bytes(void* buf, unsigned long nbytes);

/* Mainline credits input timing through its CRNG.  Xeneva has a hardware
 * RNG, so the hook exists for the input core but contributes nothing. */
void add_input_randomness(unsigned int type, unsigned int code, int value);

#endif /* __LINUX_RANDOM_H__ */
