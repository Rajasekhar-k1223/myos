#ifndef NVME_H
#define NVME_H

#include <stdint.h>

typedef struct nvme_bar0 {
    uint64_t cap;       // Controller Capabilities
    uint32_t vs;        // Version
    uint32_t intms;     // Interrupt Mask Set
    uint32_t intmc;     // Interrupt Mask Clear
    uint32_t cc;        // Controller Configuration
    uint32_t rsvd1;
    uint32_t csts;      // Controller Status
    uint32_t nssr;      // NVM Subsystem Reset
    uint32_t aqa;       // Admin Queue Attributes
    uint64_t asq;       // Admin Submission Queue Base Address
    uint64_t acq;       // Admin Completion Queue Base Address
} __attribute__((packed)) nvme_bar0_t;

void nvme_init(void);
int  nvme_read_sector(uint64_t lba, void* buf);
int  nvme_write_sector(uint64_t lba, const void* buf);

#endif
