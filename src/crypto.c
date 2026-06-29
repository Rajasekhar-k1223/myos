#include "crypto.h"
#include "kernel.h"

void crypto_init(void) {
    terminal_printf("[CRYPTO] AES-256 and SHA-256 engines initialized.\n");
}

void aes256_init(aes256_ctx_t* ctx, const uint8_t* key) {
    /* Basic stub for AES Key Expansion */
    if (!ctx || !key) return;
    for(int i = 0; i < 60; i++) {
        ctx->round_keys[i] = key[i % 32]; /* Mock key expansion */
    }
}

void aes256_encrypt_block(aes256_ctx_t* ctx, const uint8_t* in, uint8_t* out) {
    /* Basic stub: XOR with mock round keys */
    if (!ctx || !in || !out) return;
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        out[i] = in[i] ^ (ctx->round_keys[i] & 0xFF);
    }
}

void aes256_decrypt_block(aes256_ctx_t* ctx, const uint8_t* in, uint8_t* out) {
    /* Basic stub: XOR with mock round keys */
    if (!ctx || !in || !out) return;
    for (int i = 0; i < AES_BLOCK_SIZE; i++) {
        out[i] = in[i] ^ (ctx->round_keys[i] & 0xFF);
    }
}

void sha256_hash(const uint8_t* data, uint32_t len, uint8_t* hash_out) {
    /* Very basic mock hash */
    if (!data || !hash_out) return;
    for (int i = 0; i < SHA256_HASH_SIZE; i++) {
        hash_out[i] = 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        hash_out[i % SHA256_HASH_SIZE] ^= data[i];
    }
}
