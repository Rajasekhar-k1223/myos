/*
 * Full-Disk Encryption: AES-XTS (IEEE P1619) over 512-byte sectors.
 *
 * AES-XTS uses two independent AES-256 keys (k1 for data, k2 for tweak).
 * For sector i, tweak T = AES_k2(i as 16-byte little-endian), then each
 * 16-byte unit j is: C = AES_k1(P ^ T_j) ^ T_j, where T_j = GF(2^128)
 * multiply T by alpha^j.
 */
#include "fde.h"
#include "tpm.h"
#include "kernel.h"
#include "string.h"

/* The 64-byte master key is split: first 32 = data key (k1), next 32 = tweak key (k2) */
static int        fde_unlocked = 0;
static aes256_ctx_t ctx_k1;          /* data AES context */
static aes256_ctx_t ctx_k2;          /* tweak AES context */

/* GF(2^128) multiply by alpha (primitive polynomial x^128+x^7+x^2+x+1) */
static void gf128_mul_alpha(uint8_t* t) {
    uint8_t carry = 0;
    /* Shift left by 1 bit across 16 bytes (little-endian) */
    for (int i = 0; i < 16; i++) {
        uint8_t next_carry = (t[i] >> 7) & 1;
        t[i] = (uint8_t)((t[i] << 1) | carry);
        carry = next_carry;
    }
    if (carry) t[0] ^= 0x87;  /* feedback polynomial */
}

/* Build 16-byte tweak from LBA (little-endian 128-bit integer) */
static void lba_to_tweak_in(uint32_t lba, uint8_t* tweak_in) {
    tweak_in[0] = (uint8_t)(lba);
    tweak_in[1] = (uint8_t)(lba >> 8);
    tweak_in[2] = (uint8_t)(lba >> 16);
    tweak_in[3] = (uint8_t)(lba >> 24);
    for (int i = 4; i < 16; i++) tweak_in[i] = 0;
}

void fde_init(void) {
    fde_unlocked = 0;
    terminal_printf("[FDE] AES-XTS Full-Disk Encryption ready.\n");
}

int fde_unlock_disk(const char* password) {
    /* Derive 64-byte key material from password via SHA-256 twice */
    uint8_t key64[64];
    sha256_hash((const uint8_t*)password, (uint32_t)strlen(password), key64);
    /* Second hash for tweak key: SHA-256(SHA-256(password) || password) */
    uint8_t buf[64 + 128];
    for (int i = 0; i < 32; i++) buf[i] = key64[i];
    uint32_t plen = (uint32_t)strlen(password);
    for (uint32_t i = 0; i < plen && i < 128; i++) buf[32 + i] = (uint8_t)password[i];
    sha256_hash(buf, 32 + plen, key64 + 32);

    if (tpm_is_present()) {
        uint8_t sealed[64];
        tpm_unseal_data(key64, 64, sealed);
        aes256_init(&ctx_k1, sealed);
        aes256_init(&ctx_k2, sealed + 32);
    } else {
        aes256_init(&ctx_k1, key64);
        aes256_init(&ctx_k2, key64 + 32);
    }

    fde_unlocked = 1;
    terminal_printf("[FDE] Disk unlocked (AES-XTS, two 256-bit keys).\n");
    return 1;
}

/* Encrypt a 512-byte sector in AES-XTS mode */
int fde_encrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data) {
    if (!fde_unlocked) return 0;

    uint8_t tweak[16];
    lba_to_tweak_in(block_lba, tweak);
    aes256_encrypt_block(&ctx_k2, tweak, tweak);  /* T = AES_k2(LBA) */

    for (int i = 0; i < 512; i += 16) {
        uint8_t pp[16];
        for (int j = 0; j < 16; j++) pp[j] = in_data[i + j] ^ tweak[j]; /* pre-whitening */
        uint8_t cc[16];
        aes256_encrypt_block(&ctx_k1, pp, cc);
        for (int j = 0; j < 16; j++) out_data[i + j] = cc[j] ^ tweak[j]; /* post-whitening */
        gf128_mul_alpha(tweak);
    }
    return 1;
}

/* Decrypt a 512-byte sector in AES-XTS mode */
int fde_decrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data) {
    if (!fde_unlocked) return 0;

    uint8_t tweak[16];
    lba_to_tweak_in(block_lba, tweak);
    aes256_encrypt_block(&ctx_k2, tweak, tweak);

    for (int i = 0; i < 512; i += 16) {
        uint8_t cc[16];
        for (int j = 0; j < 16; j++) cc[j] = in_data[i + j] ^ tweak[j];
        uint8_t pp[16];
        aes256_decrypt_block(&ctx_k1, cc, pp);
        for (int j = 0; j < 16; j++) out_data[i + j] = pp[j] ^ tweak[j];
        gf128_mul_alpha(tweak);
    }
    return 1;
}
