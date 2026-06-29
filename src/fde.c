#include "fde.h"
#include "tpm.h"
#include "kernel.h"
#include "string.h"

static int fde_unlocked = 0;
static aes256_ctx_t master_ctx;

void fde_init(void) {
    terminal_printf("[FDE] Full Disk Encryption module initialized.\n");
    fde_unlocked = 0;
}

int fde_unlock_disk(const char* password) {
    /* Stub: Hash password, combine with TPM sealed key, derive master AES key */
    uint8_t hash[32];
    sha256_hash((const uint8_t*)password, strlen(password), hash);
    
    uint8_t unsealed[32];
    if (tpm_is_present()) {
        tpm_unseal_data(hash, 32, unsealed);
        aes256_init(&master_ctx, unsealed);
    } else {
        aes256_init(&master_ctx, hash);
    }
    
    fde_unlocked = 1;
    terminal_printf("[FDE] Disk unlocked successfully.\n");
    return 1;
}

int fde_encrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data) {
    if (!fde_unlocked) return 0;
    /* AES-XTS mode stub over 512 bytes */
    for (int i = 0; i < 512; i += 16) {
        aes256_encrypt_block(&master_ctx, in_data + i, out_data + i);
    }
    return 1;
}

int fde_decrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data) {
    if (!fde_unlocked) return 0;
    /* AES-XTS mode stub over 512 bytes */
    for (int i = 0; i < 512; i += 16) {
        aes256_decrypt_block(&master_ctx, in_data + i, out_data + i);
    }
    return 1;
}
