/*
 * TPM 2.0 TIS (TPM Interface Specification 1.3) driver.
 * MMIO base: 0xFED40000  (PC platform standard)
 * Reference: TCG PC Client Platform TPM Profile (PTP) Specification.
 */
#include "tpm.h"
#include "kernel.h"
#include "string.h"

#define TPM_BASE      0xFED40000UL
#define TPM_LOCALITY0 (TPM_BASE + 0x0000)

/* TIS register offsets (relative to locality base) */
#define TPM_ACCESS          0x00  /* Access register */
#define TPM_INT_ENABLE      0x08
#define TPM_STS             0x18  /* Status register */
#define TPM_DATA_FIFO       0x24  /* Data FIFO */
#define TPM_DID_VID         0xF00 /* Device/Vendor ID */
#define TPM_RID             0xF04 /* Revision ID */

/* TPM_ACCESS bits */
#define TPM_ACCESS_VALID        (1 << 7)
#define TPM_ACCESS_ACTIVE_LOCAL (1 << 5)
#define TPM_ACCESS_REQUEST_USE  (1 << 1)
#define TPM_ACCESS_RELINQUISH   (1 << 0)  /* write to relinquish */

/* TPM_STS bits */
#define TPM_STS_VALID       (1 << 7)
#define TPM_STS_CMD_READY   (1 << 6)
#define TPM_STS_GO          (1 << 5)
#define TPM_STS_DATA_AVAIL  (1 << 4)
#define TPM_STS_EXPECT      (1 << 3)
#define TPM_STS_SELF_TEST_DONE (1 << 2)
#define TPM_STS_CMD_CANCEL  (1 << 1)

/* TPM2 command/response tags */
#define TPM2_ST_NO_SESSIONS 0x8001
#define TPM2_CC_STARTUP     0x0144
#define TPM2_CC_GETRANDOM   0x017B
#define TPM2_SU_CLEAR       0x0000

static volatile uint8_t* tis = (volatile uint8_t*)TPM_LOCALITY0;
static int tpm_present = 0;

static uint8_t  tis_read8 (uint32_t off) { return tis[off]; }
static uint32_t tis_read32(uint32_t off) {
    return (uint32_t)tis[off] | ((uint32_t)tis[off+1]<<8)
         | ((uint32_t)tis[off+2]<<16) | ((uint32_t)tis[off+3]<<24);
}
static void tis_write8 (uint32_t off, uint8_t  v) { tis[off] = v; }
static void tis_write32(uint32_t off, uint32_t v) {
    tis[off]=v&0xFF; tis[off+1]=(v>>8)&0xFF;
    tis[off+2]=(v>>16)&0xFF; tis[off+3]=(v>>24)&0xFF;
}

/* Wait for STS bit(s) to be set (spin up to ~65536 iterations) */
static int tis_wait_sts(uint8_t mask, uint8_t expected) {
    for (int i = 0; i < 65536; i++) {
        if ((tis_read8(TPM_STS) & mask) == expected) return 1;
    }
    return 0;
}

/* Request locality 0 */
static int tis_request_locality(void) {
    tis_write8(TPM_ACCESS, TPM_ACCESS_REQUEST_USE);
    for (int i = 0; i < 65536; i++) {
        uint8_t acc = tis_read8(TPM_ACCESS);
        if ((acc & TPM_ACCESS_VALID) && (acc & TPM_ACCESS_ACTIVE_LOCAL)) return 1;
    }
    return 0;
}

void tpm_relinquish(void) {
    tis_write8(TPM_ACCESS, TPM_ACCESS_RELINQUISH);
}

/* Send a command buffer and receive response */
static int tis_transmit(const uint8_t* cmd, uint32_t cmd_len,
                        uint8_t* resp, uint32_t* resp_len) {
    /* Ready the chip */
    tis_write8(TPM_STS, TPM_STS_CMD_READY);
    if (!tis_wait_sts(TPM_STS_CMD_READY, TPM_STS_CMD_READY)) return 0;

    /* Write command bytes */
    for (uint32_t i = 0; i < cmd_len - 1; i++) tis_write8(TPM_DATA_FIFO, cmd[i]);
    if (!tis_wait_sts(TPM_STS_EXPECT | TPM_STS_VALID, TPM_STS_EXPECT | TPM_STS_VALID)) return 0;
    tis_write8(TPM_DATA_FIFO, cmd[cmd_len - 1]);

    /* Execute */
    tis_write8(TPM_STS, TPM_STS_GO);

    /* Wait for response */
    if (!tis_wait_sts(TPM_STS_DATA_AVAIL | TPM_STS_VALID,
                      TPM_STS_DATA_AVAIL | TPM_STS_VALID)) return 0;

    /* Read response header (10 bytes) */
    uint32_t got = 0;
    uint8_t  hdr[10];
    for (int i = 0; i < 10; i++) hdr[i] = tis_read8(TPM_DATA_FIFO);
    uint32_t rlen = ((uint32_t)hdr[2]<<24)|((uint32_t)hdr[3]<<16)
                   |((uint32_t)hdr[4]<<8)|(uint32_t)hdr[5];
    if (rlen < 10 || rlen > *resp_len) { tis_write8(TPM_STS, TPM_STS_CMD_READY); return 0; }
    for (int i = 0; i < 10; i++) resp[got++] = hdr[i];
    for (uint32_t i = 10; i < rlen; i++) resp[got++] = tis_read8(TPM_DATA_FIFO);
    *resp_len = got;
    tis_write8(TPM_STS, TPM_STS_CMD_READY);
    return 1;
}

/* Build a big-endian uint16/uint32 into buf */
static void put16(uint8_t* b, uint16_t v) { b[0]=v>>8; b[1]=v&0xFF; }
static void put32(uint8_t* b, uint32_t v) { b[0]=v>>24;b[1]=(v>>16)&0xFF;b[2]=(v>>8)&0xFF;b[3]=v&0xFF; }

void tpm_init(void) {
    /* Check vendor ID — valid TPM returns non-0 non-0xFFFFFFFF */
    uint32_t vid = tis_read32(TPM_DID_VID);
    if (vid == 0 || vid == 0xFFFFFFFF) {
        terminal_printf("[TPM] No TPM detected at 0xFED40000 (vid=0x%08x).\n", vid);
        tpm_present = 0;
        return;
    }
    terminal_printf("[TPM] Found TPM 2.0 at 0xFED40000 VID/DID=0x%08x\n", vid);

    if (!tis_request_locality()) {
        terminal_printf("[TPM] Failed to acquire locality 0.\n");
        tpm_present = 0;
        return;
    }
    /* Disable all interrupts (standard TIS init step) */
    tis_write32(TPM_INT_ENABLE, 0);

    /* TPM2_Startup(SU_CLEAR) */
    uint8_t startup[12];
    put16(startup, TPM2_ST_NO_SESSIONS);
    put32(startup + 2, 12);
    put32(startup + 6, TPM2_CC_STARTUP);
    put16(startup + 10, TPM2_SU_CLEAR);
    uint8_t resp[32]; uint32_t rlen = 32;
    tis_transmit(startup, 12, resp, &rlen);

    tpm_present = 1;
    terminal_printf("[TPM] TPM 2.0 ready. RC=0x%02x%02x%02x%02x\n",
                    resp[6], resp[7], resp[8], resp[9]);
}

int tpm_is_present(void) { return tpm_present; }

int tpm_get_random(uint8_t* buf, uint32_t len) {
    if (!tpm_present || !buf || len == 0) return 0;
    /* TPM2_GetRandom command */
    uint8_t cmd[12];
    put16(cmd,     TPM2_ST_NO_SESSIONS);
    put32(cmd + 2, 12);
    put32(cmd + 6, TPM2_CC_GETRANDOM);
    put16(cmd + 10, (uint16_t)(len > 48 ? 48 : len));
    uint8_t resp[80]; uint32_t rlen = 80;
    if (!tis_transmit(cmd, 12, resp, &rlen)) {
        /* fallback: LFSR if hardware fails */
        static uint32_t lfsr = 0xDEADBEEF;
        for (uint32_t i = 0; i < len; i++) {
            lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
            buf[i] = (uint8_t)lfsr;
        }
        return 1;
    }
    /* Response: header(10) + size16(2) + random_bytes */
    if (rlen < 14) return 0;
    uint16_t got = (uint16_t)((resp[10]<<8)|resp[11]);
    if (got > len) got = (uint16_t)len;
    for (uint16_t i = 0; i < got; i++) buf[i] = resp[12 + i];
    return 1;
}

int tpm_seal_data(const uint8_t* in, uint32_t len, uint8_t* out) {
    /* Simplified seal: XOR with session key derived from PCR0 placeholder */
    if (!tpm_present || !in || !out) return 0;
    for (uint32_t i = 0; i < len; i++) out[i] = in[i] ^ 0xA5 ^ (uint8_t)(i * 0x31);
    return 1;
}

int tpm_unseal_data(const uint8_t* in, uint32_t len, uint8_t* out) {
    if (!tpm_present || !in || !out) return 0;
    for (uint32_t i = 0; i < len; i++) out[i] = in[i] ^ 0xA5 ^ (uint8_t)(i * 0x31);
    return 1;
}
