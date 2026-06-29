#ifndef FDE_H
#define FDE_H

#include <stdint.h>
#include "crypto.h"

void fde_init(void);
int  fde_unlock_disk(const char* password);
int  fde_encrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data);
int  fde_decrypt_block(uint32_t block_lba, const uint8_t* in_data, uint8_t* out_data);

#endif
