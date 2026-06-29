#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

#define AES_BLOCK_SIZE 16
#define SHA256_HASH_SIZE 32

/* Simple AES-256 Context Stub */
typedef struct {
    uint32_t round_keys[60];
} aes256_ctx_t;

void crypto_init(void);
void aes256_init(aes256_ctx_t* ctx, const uint8_t* key);
void aes256_encrypt_block(aes256_ctx_t* ctx, const uint8_t* in, uint8_t* out);
void aes256_decrypt_block(aes256_ctx_t* ctx, const uint8_t* in, uint8_t* out);

void sha256_hash(const uint8_t* data, uint32_t len, uint8_t* hash_out);

#endif
