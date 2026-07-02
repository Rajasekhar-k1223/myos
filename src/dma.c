#include "dma.h"

extern void outb(uint16_t port, uint8_t value);
extern uint8_t inb(uint16_t port);

/* 
 * 8237 ISA DMA Controller registers.
 * Master (8-bit) = 0x00 - 0x0F
 * Slave (16-bit) = 0xC0 - 0xDF
 */
void isa_dma_start(uint8_t channel, uint32_t phys_addr, uint32_t size, uint8_t mode) {
    if (channel > 7 || channel == 4) return; /* 4 is cascade */

    uint16_t mask_reg, mode_reg, clear_reg, base_addr_reg, count_reg, page_reg;
    uint8_t  dma_mode, page_val;
    uint32_t offset_val, count_val;

    if (channel < 4) { /* 8-bit (Master) */
        mask_reg      = 0x0A;
        mode_reg      = 0x0B;
        clear_reg     = 0x0C;
        base_addr_reg = 0x00 + (channel * 2);
        count_reg     = 0x01 + (channel * 2);
        
        offset_val = phys_addr;
        count_val  = size - 1;
        page_val   = (phys_addr >> 16) & 0xFF;
    } else { /* 16-bit (Slave) */
        mask_reg      = 0xD4;
        mode_reg      = 0xD6;
        clear_reg     = 0xD8;
        base_addr_reg = 0xC0 + ((channel - 4) * 4);
        count_reg     = 0xC2 + ((channel - 4) * 4);
        
        offset_val = (phys_addr >> 1) & 0xFFFF;
        count_val  = (size >> 1) - 1;
        page_val   = (phys_addr >> 16) & 0xFE;
    }

    /* Page registers */
    switch (channel) {
        case 0: page_reg = 0x87; break;
        case 1: page_reg = 0x83; break;
        case 2: page_reg = 0x81; break;
        case 3: page_reg = 0x82; break;
        case 5: page_reg = 0x8B; break;
        case 6: page_reg = 0x89; break;
        case 7: page_reg = 0x8A; break;
        default: return;
    }

    /* Mode: Demand mode + Address increment + Auto-initialize off + Read/Write */
    dma_mode = mode | (channel < 4 ? channel : (channel - 4));

    /* Disable DMA channel */
    outb(mask_reg, (channel < 4 ? channel : (channel - 4)) | 0x04);
    
    /* Set mode */
    outb(mode_reg, dma_mode);
    
    /* Clear flip-flop */
    outb(clear_reg, 0x00);
    
    /* Set offset address */
    outb(base_addr_reg, offset_val & 0xFF);
    outb(base_addr_reg, (offset_val >> 8) & 0xFF);
    
    /* Set page */
    outb(page_reg, page_val);
    
    /* Clear flip-flop again */
    outb(clear_reg, 0x00);
    
    /* Set count */
    outb(count_reg, count_val & 0xFF);
    outb(count_reg, (count_val >> 8) & 0xFF);
    
    /* Enable DMA channel */
    outb(mask_reg, (channel < 4 ? channel : (channel - 4)));
}
