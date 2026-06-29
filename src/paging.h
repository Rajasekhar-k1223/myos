#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

void paging_init(void);
void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void paging_switch_directory(uint32_t* dir);
void paging_register_tlb_handler(void);
uint32_t* paging_clone_directory(void);

void paging_page_fault_handler(uint32_t fault_addr, uint32_t error_code);
void cow_inc(uint32_t phys_addr);
void cow_dec(uint32_t phys_addr);
int  cow_count(uint32_t phys_addr);

#endif
