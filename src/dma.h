#pragma once
#include <stdint.h>

void isa_dma_start(uint8_t channel, uint32_t phys_addr, uint32_t size, uint8_t mode);
