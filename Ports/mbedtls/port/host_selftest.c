/* Host check for the trimmed Mbed TLS config. Not packed into the guest. */
#include "mbedtls/build_info.h"
#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/gcm.h"
#include "mbedtls/chacha20.h"
#include "mbedtls/poly1305.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/ecp.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include <stdio.h>

extern int mbedtls_sha256_self_test(int verbose);
extern int mbedtls_aes_self_test(int verbose);
extern int mbedtls_gcm_self_test(int verbose);
extern int mbedtls_chacha20_self_test(int verbose);
extern int mbedtls_poly1305_self_test(int verbose);
extern int mbedtls_chachapoly_self_test(int verbose);
extern int mbedtls_ecp_self_test(int verbose);
extern int mbedtls_rsa_self_test(int verbose);
extern int mbedtls_ctr_drbg_self_test(int verbose);
extern int mbedtls_entropy_self_test(int verbose);

static int run(const char* name, int (*fn)(int)) {
	int rc = fn(0);
	printf("%s: %s\n", name, rc ? "FAIL" : "ok");
	return rc;
}

int main(void) {
	int failed = 0;
	failed |= run("sha256", mbedtls_sha256_self_test);
	failed |= run("aes", mbedtls_aes_self_test);
	failed |= run("gcm", mbedtls_gcm_self_test);
	failed |= run("chacha20", mbedtls_chacha20_self_test);
	failed |= run("poly1305", mbedtls_poly1305_self_test);
	failed |= run("chachapoly", mbedtls_chachapoly_self_test);
	failed |= run("ecp", mbedtls_ecp_self_test);
	failed |= run("rsa", mbedtls_rsa_self_test);
	failed |= run("ctr_drbg", mbedtls_ctr_drbg_self_test);
	failed |= run("entropy", mbedtls_entropy_self_test);
	return failed ? 1 : 0;
}
