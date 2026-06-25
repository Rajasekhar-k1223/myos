#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init(void);
void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

#endif
