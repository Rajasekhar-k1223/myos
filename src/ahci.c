#include "ahci.h"
#include "pci.h"
#include "kernel.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "pit.h"

#define HBA_PxIS_TFES (1 << 30) // Task file error status

static HBA_MEM* abar = 0;

static void start_cmd(HBA_PORT* port) {
    // Wait until CR (bit15) is cleared
    int timeout = 1000000;
    while ((port->cmd & HBA_PxCMD_CR) && timeout > 0) timeout--;

    // Set FRE (bit4) and ST (bit0)
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static void stop_cmd(HBA_PORT* port) {
    // Clear ST (bit0)
    port->cmd &= ~HBA_PxCMD_ST;
    // Clear FRE (bit4)
    port->cmd &= ~HBA_PxCMD_FRE;

    // Wait until FR (bit14), CR (bit15) are cleared
    int timeout = 1000000;
    while (timeout > 0) {
        if ((port->cmd & HBA_PxCMD_FR) == 0 && (port->cmd & HBA_PxCMD_CR) == 0) break;
        timeout--;
    }
}

static void port_rebase(HBA_PORT* port, int portno) {
    (void)portno;
    terminal_printf("   - Rebase: stopping...\n");
    stop_cmd(port);

    terminal_printf("   - Rebase: allocating...\n");
    // Allocate frame for Command List (1KB) and FIS (256B)
    uint32_t base = (uint32_t)pmm_alloc_frame();
    memset((void*)base, 0, 4096);

    port->clb = base;
    port->clbu = 0;
    
    port->fb = base + 1024;
    port->fbu = 0;

    // Command table offset
    uint32_t ctb = base + 2048;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)port->clb;
    for (int i=0; i<32; i++) {
        cmdheader[i].prdtl = 8;
        cmdheader[i].ctba = ctb + (i * 256);
        cmdheader[i].ctbau = 0;
    }

    terminal_printf("   - Rebase: starting...\n");
    start_cmd(port);
    terminal_printf("   - Rebase: done!\n");
}

static int check_type(HBA_PORT* port) {
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DEV_PRESENT || ipm != HBA_PORT_IPM_ACTIVE) {
        return 0; // None
    }

    switch (port->sig) {
        case SATA_SIG_ATAPI: return 2;
        case SATA_SIG_SEMB: return 3;
        case SATA_SIG_PM: return 4;
        default: return 1; // SATA (AHCI)
    }
}

static int find_cmdslot(HBA_PORT* port) {
    // If not set in SACT and CI, the slot is free
    uint32_t slots = (port->sact | port->ci);
    for (int i=0; i<32; i++) {
        if ((slots & (1<<i)) == 0) {
            return i;
        }
    }
    return -1;
}

int ahci_read_sector(HBA_PORT* port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t* buf) {
    port->is = (uint32_t)-1; // Clear pending interrupt bits
    int spin = 0;
    int slot = find_cmdslot(port);
    if (slot == -1) return 0;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)port->clb;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D)/sizeof(uint32_t); // Command FIS size
    cmdheader->w = 0; // Read
    cmdheader->prdtl = (uint16_t)((count-1)>>4) + 1; // PRDT entries

    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)cmdheader->ctba;
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1)*sizeof(HBA_PRDT_ENTRY));

    int i = 0;
    for (i=0; i<cmdheader->prdtl - 1; i++) {
        cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
        cmdtbl->prdt_entry[i].dbc = 8*1024 - 1; // 8K bytes
        cmdtbl->prdt_entry[i].i = 0;
        buf += 4096;
        count -= 16;
    }
    cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
    cmdtbl->prdt_entry[i].dbc = count * 512 - 1;
    cmdtbl->prdt_entry[i].i = 1;

    // Setup command
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = 0x25; // READ DMA EXT

    cmdfis->lba0 = (uint8_t)startl;
    cmdfis->lba1 = (uint8_t)(startl >> 8);
    cmdfis->lba2 = (uint8_t)(startl >> 16);
    cmdfis->device = 1 << 6; // LBA mode

    cmdfis->lba3 = (uint8_t)(startl >> 24);
    cmdfis->lba4 = (uint8_t)starth;
    cmdfis->lba5 = (uint8_t)(starth >> 8);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
    }

    port->ci = 1 << slot; // Issue command

    // Wait for completion
    int read_timeout = 0x1000000;
    while (read_timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & HBA_PxIS_TFES) {
            terminal_printf("[AHCI] Read disk error\n");
            return 0;
        }
    }
    if (read_timeout <= 0) { terminal_printf("[AHCI] Read timeout\n"); return 0; }

    if (port->is & HBA_PxIS_TFES) {
        terminal_printf("[AHCI] Read disk error\n");
        return 0;
    }

    return 1;
}

static HBA_PORT* primary_port = 0;

void ahci_init(void) {
    pci_device_t ahci_dev;
    
    // Class 0x01 (Mass Storage), Subclass 0x06 (SATA), Prog_IF 0x01 (AHCI)
    if (!pci_get_device_by_class(0x01, 0x06, 0x01, &ahci_dev)) {
        terminal_printf("[AHCI] No SATA AHCI Controller found.\n");
        return;
    }

    // BAR5 is the ABAR (AHCI Base Address Register)
    uint32_t bar5 = pci_read_config_32(ahci_dev.bus, ahci_dev.slot, ahci_dev.func, 0x24);
    abar = (HBA_MEM*)(bar5 & ~0xF);

    uint32_t phys_abar = (uint32_t)abar & ~0xFFF;
    paging_map_page(phys_abar, phys_abar, 0x1B);
    paging_map_page(phys_abar + 0x1000, phys_abar + 0x1000, 0x1B);
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax"); // Flush TLB

    terminal_printf("[AHCI] Found SATA Controller at PCI %d:%d:%d (ABAR: 0x%x)\n", ahci_dev.bus, ahci_dev.slot, ahci_dev.func, abar);

    // Enable Bus Mastering and MMIO
    pci_enable_bus_mastering(&ahci_dev);

    terminal_printf(" - Enabling AE...\n");
    abar->ghc |= (1 << 31);
    
    terminal_printf(" - Resetting controller...\n");
    abar->ghc |= (1 << 0);
    int timeout = 1000000;
    while ((abar->ghc & (1 << 0)) && timeout > 0) timeout--;
    
    if (timeout == 0) terminal_printf(" - Reset TIMEOUT!\n");
    else terminal_printf(" - Reset OK.\n");
    
    terminal_printf(" - Re-enabling AE...\n");
    abar->ghc |= (1 << 31);

    terminal_printf(" - Finding ports...\n");

    // Find and init implemented ports
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & 1) {
            int dt = check_type(&abar->ports[i]);
            if (dt == 1) {
                terminal_printf(" - Port %d: SATA Drive\n", i);
                port_rebase(&abar->ports[i], i);
                if (!primary_port) primary_port = &abar->ports[i];
            } else if (dt == 2) {
                terminal_printf(" - Port %d: SATAPI Drive (CD-ROM)\n", i);
                port_rebase(&abar->ports[i], i);
            }
        }
        pi >>= 1;
    }
}

int ahci_read(uint32_t lba, uint32_t count, void* buf) {
    if (!primary_port) return 0;
    return ahci_read_sector(primary_port, lba, 0, count, (uint16_t*)buf);
}

int ahci_write_sector(HBA_PORT* port, uint32_t startl, uint32_t starth, uint32_t count, uint16_t* buf) {
    port->is = (uint32_t)-1; // Clear pending interrupt bits
    int spin = 0;
    int slot = find_cmdslot(port);
    if (slot == -1) return 0;

    HBA_CMD_HEADER* cmdheader = (HBA_CMD_HEADER*)port->clb;
    cmdheader += slot;
    cmdheader->cfl = sizeof(FIS_REG_H2D)/sizeof(uint32_t); // Command FIS size
    cmdheader->w = 1; // Write, H2D
    cmdheader->prdtl = (uint16_t)((count-1)>>4) + 1; // PRDT entries

    HBA_CMD_TBL* cmdtbl = (HBA_CMD_TBL*)cmdheader->ctba;
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1)*sizeof(HBA_PRDT_ENTRY));

    int i = 0;
    for (i=0; i<cmdheader->prdtl - 1; i++) {
        cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
        cmdtbl->prdt_entry[i].dbc = 8*1024 - 1; // 8K bytes
        cmdtbl->prdt_entry[i].i = 0;
        buf += 4096;
        count -= 16;
    }
    cmdtbl->prdt_entry[i].dba = (uint32_t)buf;
    cmdtbl->prdt_entry[i].dbc = count * 512 - 1;
    cmdtbl->prdt_entry[i].i = 1;

    // Setup command
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = 0x35; // WRITE DMA EXT

    cmdfis->lba0 = (uint8_t)startl;
    cmdfis->lba1 = (uint8_t)(startl >> 8);
    cmdfis->lba2 = (uint8_t)(startl >> 16);
    cmdfis->device = 1 << 6; // LBA mode

    cmdfis->lba3 = (uint8_t)(startl >> 24);
    cmdfis->lba4 = (uint8_t)starth;
    cmdfis->lba5 = (uint8_t)(starth >> 8);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    while ((port->tfd & (0x80 | 0x08)) && spin < 1000000) {
        spin++;
    }

    port->ci = 1 << slot; // Issue command

    // Wait for completion
    int write_timeout = 0x1000000;
    while (write_timeout-- > 0) {
        if ((port->ci & (1 << slot)) == 0) break;
        if (port->is & HBA_PxIS_TFES) {
            terminal_printf("[AHCI] Write disk error\n");
            return 0;
        }
    }
    if (write_timeout <= 0) { terminal_printf("[AHCI] Write timeout\n"); return 0; }

    if (port->is & HBA_PxIS_TFES) {
        terminal_printf("[AHCI] Write disk error\n");
        return 0;
    }

    return 1;
}

int ahci_write(uint32_t lba, uint32_t count, void* buf) {
    if (!primary_port) return 0;
    return ahci_write_sector(primary_port, lba, 0, count, (uint16_t*)buf);
}
