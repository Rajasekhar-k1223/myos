#pragma once
#include <stdint.h>

int ata_init(void);
void ata_read_sector(uint32_t lba, uint8_t* buffer);
void ata_write_sector(uint32_t lba, uint8_t* buffer);
