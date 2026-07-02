/*
 * WPA2-PSK real PMK derivation.
 *
 * PMK = PBKDF2-HMAC-SHA256(passphrase, ssid, 4096 iterations, 32 bytes)
 * per IEEE 802.11-2020 §12.4.2 and RFC 2898 §5.2.
 *
 * PTK derivation uses PRF-SHA256 per IEEE 802.11 §12.7.1.2.
 */
#include "wpa.h"
#include "crypto.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"

#define SHA256_LEN 32
#define HMAC_BLOCK  64

/* ── HMAC-SHA256 ───────────────────────────────────────────────────────── */
static void hmac_sha256(const uint8_t* key, uint32_t klen,
                        const uint8_t* msg, uint32_t mlen,
                        uint8_t* out) {
    uint8_t tmp_key[SHA256_LEN];

    /* If key > block size, hash it first */
    if (klen > HMAC_BLOCK) {
        sha256_hash(key, klen, tmp_key);
        key  = tmp_key;
        klen = SHA256_LEN;
    }

    /* Build ipad and opad */
    uint8_t ipad[HMAC_BLOCK], opad[HMAC_BLOCK];
    for (int i = 0; i < HMAC_BLOCK; i++) {
        uint8_t kb = (i < (int)klen) ? key[i] : 0;
        ipad[i] = kb ^ 0x36;
        opad[i] = kb ^ 0x5C;
    }

    /* inner = SHA256(ipad || msg) */
    uint32_t inner_len = HMAC_BLOCK + mlen;
    uint8_t* inner_buf = (uint8_t*)kmalloc(inner_len);
    if (!inner_buf) return;
    for (int i = 0; i < HMAC_BLOCK; i++) inner_buf[i] = ipad[i];
    for (uint32_t i = 0; i < mlen; i++) inner_buf[HMAC_BLOCK + i] = msg[i];
    uint8_t inner_hash[SHA256_LEN];
    sha256_hash(inner_buf, inner_len, inner_hash);
    kfree(inner_buf);

    /* outer = SHA256(opad || inner_hash) */
    uint8_t outer_buf[HMAC_BLOCK + SHA256_LEN];
    for (int i = 0; i < HMAC_BLOCK; i++) outer_buf[i] = opad[i];
    for (int i = 0; i < SHA256_LEN; i++) outer_buf[HMAC_BLOCK + i] = inner_hash[i];
    sha256_hash(outer_buf, HMAC_BLOCK + SHA256_LEN, out);
}

/* ── PBKDF2-HMAC-SHA256 (RFC 2898 §5.2) ──────────────────────────────── */
/* Derives `dklen` bytes into `dk`. For WPA2, dklen=32, c=4096. */
static void pbkdf2_hmac_sha256(
    const uint8_t* pass,   uint32_t plen,
    const uint8_t* salt,   uint32_t slen,
    uint32_t        c,
    uint8_t*        dk,    uint32_t dklen)
{
    uint32_t blk_count = (dklen + SHA256_LEN - 1) / SHA256_LEN;
    uint32_t out_off   = 0;

    /* Temporary buffer for salt || block-counter (big-endian uint32) */
    uint8_t* salt_blk = (uint8_t*)kmalloc(slen + 4);
    if (!salt_blk) return;
    for (uint32_t i = 0; i < slen; i++) salt_blk[i] = salt[i];

    for (uint32_t blk = 1; blk <= blk_count; blk++) {
        /* PRF input = salt || INT(blk) big-endian */
        salt_blk[slen + 0] = (uint8_t)(blk >> 24);
        salt_blk[slen + 1] = (uint8_t)(blk >> 16);
        salt_blk[slen + 2] = (uint8_t)(blk >>  8);
        salt_blk[slen + 3] = (uint8_t)(blk);

        uint8_t U[SHA256_LEN], T[SHA256_LEN];
        hmac_sha256(pass, plen, salt_blk, slen + 4, U);
        for (int j = 0; j < SHA256_LEN; j++) T[j] = U[j];

        for (uint32_t i = 1; i < c; i++) {
            hmac_sha256(pass, plen, U, SHA256_LEN, U);
            for (int j = 0; j < SHA256_LEN; j++) T[j] ^= U[j];
        }

        uint32_t copy = dklen - out_off;
        if (copy > SHA256_LEN) copy = SHA256_LEN;
        for (uint32_t j = 0; j < copy; j++) dk[out_off + j] = T[j];
        out_off += copy;
    }
    kfree(salt_blk);
}

/* ── PRF-SHA256 (IEEE 802.11 §12.7.1.2) for PTK derivation ───────────── */
/* PRF(key, A, B, len_bits) = HMAC-SHA256(key, A || 0x00 || B || counter) iterated */
static void prf_sha256(const uint8_t* key, uint32_t klen,
                       const char*    label,
                       const uint8_t* data,  uint32_t dlen,
                       uint8_t*       out,   uint32_t out_bytes) {
    uint32_t llen = (uint32_t)strlen(label);
    uint32_t buf_len = llen + 1 + dlen + 1; /* label + 0x00 + data + counter */
    uint8_t* buf = (uint8_t*)kmalloc(buf_len);
    if (!buf) return;
    for (uint32_t i = 0; i < llen; i++) buf[i] = (uint8_t)label[i];
    buf[llen] = 0x00;
    for (uint32_t i = 0; i < dlen; i++) buf[llen + 1 + i] = data[i];

    uint32_t written = 0;
    uint8_t  counter = 0;
    while (written < out_bytes) {
        buf[llen + 1 + dlen] = counter++;
        uint8_t h[SHA256_LEN];
        hmac_sha256(key, klen, buf, buf_len, h);
        uint32_t copy = out_bytes - written;
        if (copy > SHA256_LEN) copy = SHA256_LEN;
        for (uint32_t i = 0; i < copy; i++) out[written + i] = h[i];
        written += copy;
    }
    kfree(buf);
}

/* ── Public API ───────────────────────────────────────────────────────── */
int wpa_authenticate(const char* psk) {
    return wpa_derive_pmk(psk, "ElseaOS_SSID", NULL, 0);
}

int wpa_derive_pmk(const char* passphrase, const char* ssid,
                   uint8_t* pmk_out, int store) {
    static uint8_t pmk[SHA256_LEN];
    uint32_t plen = (uint32_t)strlen(passphrase);
    uint32_t slen = (uint32_t)strlen(ssid);

    terminal_printf("[WPA] PBKDF2-HMAC-SHA256: %u iters for SSID '%s' ...\n", 4096, ssid);
    pbkdf2_hmac_sha256(
        (const uint8_t*)passphrase, plen,
        (const uint8_t*)ssid,       slen,
        4096,
        pmk, SHA256_LEN);
    terminal_printf("[WPA] PMK derived: %02x%02x%02x%02x...\n",
                    pmk[0], pmk[1], pmk[2], pmk[3]);

    if (pmk_out && store)
        for (int i = 0; i < SHA256_LEN; i++) pmk_out[i] = pmk[i];
    return 1;
}

void wpa_derive_ptk(const uint8_t* pmk,
                    const uint8_t* aa, const uint8_t* spa,
                    const uint8_t* anonce, const uint8_t* snonce,
                    uint8_t* ptk_out, uint32_t ptk_len) {
    /* PTK = PRF-SHA256(PMK, "Pairwise key expansion",
       Min(AA,SPA)||Max(AA,SPA)||Min(ANonce,SNonce)||Max(ANonce,SNonce)) */
    uint8_t data[6 + 6 + 32 + 32];
    /* Min/Max of AA and SPA */
    const uint8_t *lo_mac = aa, *hi_mac = spa;
    if (memcmp(aa, spa, 6) > 0) { lo_mac = spa; hi_mac = aa; }
    for (int i = 0; i < 6; i++)  data[i]     = lo_mac[i];
    for (int i = 0; i < 6; i++)  data[6+i]   = hi_mac[i];
    /* Min/Max of nonces */
    const uint8_t *lo_n = anonce, *hi_n = snonce;
    if (memcmp(anonce, snonce, 32) > 0) { lo_n = snonce; hi_n = anonce; }
    for (int i = 0; i < 32; i++) data[12+i]  = lo_n[i];
    for (int i = 0; i < 32; i++) data[44+i]  = hi_n[i];

    prf_sha256(pmk, SHA256_LEN,
               "Pairwise key expansion",
               data, sizeof(data),
               ptk_out, ptk_len);
    terminal_printf("[WPA] PTK derived: %02x%02x%02x%02x...\n",
                    ptk_out[0], ptk_out[1], ptk_out[2], ptk_out[3]);
}
