#include "ata.h"
#include "io.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRV_HEAD    0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_TIMEOUT 0x100000

static int ata_wait_bsy(void) {
    int t = ATA_TIMEOUT;
    while ((inb(ATA_STATUS) & 0x80) && t > 0) t--;
    return t > 0 ? 0 : -1;
}

static int ata_wait_drq(void) {
    int t = ATA_TIMEOUT;
    while (!(inb(ATA_STATUS) & 0x08) && t > 0) t--;
    return t > 0 ? 0 : -1;
}

int ata_init() {
    outb(ATA_DRV_HEAD, 0xA0);
    outb(ATA_SECTOR_CNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_COMMAND, 0xEC);
    
    uint8_t status = inb(ATA_STATUS);
    if (status == 0) return 0;
    
    ata_wait_bsy();
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HI) != 0) return 0;
    
    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        inw(ATA_DATA);
    }
    
    return 1;
}

int ata_read_sector(uint32_t lba, uint8_t* buffer) {
    if (ata_wait_bsy()) return -1;
    outb(ATA_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, (uint8_t) lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, 0x20);
    if (ata_wait_bsy()) return -1;
    if (ata_wait_drq()) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t word = inw(ATA_DATA);
        buffer[i * 2] = (uint8_t)word;
        buffer[i * 2 + 1] = (uint8_t)(word >> 8);
    }
    return 0;
}

int ata_write_sector(uint32_t lba, uint8_t* buffer) {
    if (ata_wait_bsy()) return -1;
    outb(ATA_DRV_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, (uint8_t) lba);
    outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_COMMAND, 0x30);
    if (ata_wait_bsy()) return -1;
    if (ata_wait_drq()) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t word = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw(ATA_DATA, word);
    }
    outb(ATA_COMMAND, 0xE7); // Cache flush
    if (ata_wait_bsy()) return -1;
    return 0;
}
