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

#ifndef __WIFI_CRYPTO_H__
#define __WIFI_CRYPTO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_sha1(const uint8_t* msg, int len, uint8_t out[20]);
void wifi_hmac_sha1(const uint8_t* key, int key_len, const uint8_t* msg, int msg_len, uint8_t out[20]);
void wifi_pbkdf2_sha1(const uint8_t* pass, int pass_len, const uint8_t* salt, int salt_len,
					  int rounds, uint8_t* out, int out_len);
void wifi_sha1_prf(const uint8_t* key, int key_len, const char* label,
				   const uint8_t* data, int data_len, uint8_t* out, int out_len);
void wifi_aes_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
int wifi_aes_wrap(const uint8_t kek[16], const uint8_t* plain, int plain_len, uint8_t* out);
int wifi_aes_unwrap(const uint8_t kek[16], const uint8_t* cipher, int cipher_len, uint8_t* out);
int wifi_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[13],
					 const uint8_t* aad, int aad_len,
					 const uint8_t* pt, int pt_len, uint8_t* ct_and_mic);
int wifi_ccm_decrypt(const uint8_t key[16], const uint8_t nonce[13],
					 const uint8_t* aad, int aad_len,
					 const uint8_t* ct_and_mic, int ct_len, uint8_t* pt);

#ifdef __cplusplus
}
#endif

#endif
