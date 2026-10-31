/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022-2026, Manas Kamal Choudhury
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **/

#ifndef __BT_CRYPTO_H__
#define __BT_CRYPTO_H__

#include <stdint.h>
#include <stddef.h>

void bt_aes_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void bt_cmac(const uint8_t key[16], const uint8_t* msg, size_t len, uint8_t mac[16]);

/* SMP inputs are little-endian on the air (first octet is least significant). */
void bt_e(const uint8_t k[16], uint8_t r[16]);
void bt_c1(const uint8_t k[16], const uint8_t r[16], const uint8_t preq[7], const uint8_t pres[7],
		   uint8_t iat, const uint8_t ia[6], uint8_t rat, const uint8_t ra[6], uint8_t res[16]);
void bt_s1(const uint8_t k[16], const uint8_t r1[16], const uint8_t r2[16], uint8_t out[16]);
void bt_ah(const uint8_t irk[16], const uint8_t r[3], uint8_t out[3]);
void bt_f4(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], uint8_t z, uint8_t res[16]);
void bt_f5(const uint8_t w[32], const uint8_t n1[16], const uint8_t n2[16], const uint8_t a1[7],
		   const uint8_t a2[7], uint8_t mackey[16], uint8_t ltk[16]);
void bt_f6(const uint8_t w[16], const uint8_t n1[16], const uint8_t n2[16], const uint8_t r[16],
		   const uint8_t io_cap[3], const uint8_t a1[7], const uint8_t a2[7], uint8_t res[16]);
uint32_t bt_g2(const uint8_t u[32], const uint8_t v[32], const uint8_t x[16], const uint8_t y[16]);

/* Scalar and coordinates are big-endian (first octet is most significant). */
int bt_p256_mul_g(const uint8_t scalar[32], uint8_t x[32], uint8_t y[32]);
int bt_p256_dh(const uint8_t scalar[32], const uint8_t px[32], const uint8_t py[32], uint8_t outx[32]);

void bt_rev(const uint8_t* in, uint8_t* out, size_t n);

#endif
