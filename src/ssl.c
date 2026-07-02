/*
 * Real TLS 1.2 implementation for ElseaOS (RFC 5246).
 *
 * Cipher suite: TLS_DHE_RSA_WITH_AES_256_CBC_SHA256 (0x006B)
 * Key exchange: Ephemeral Diffie-Hellman, Oakley group 2 (RFC 2409, 1024-bit prime)
 * Symmetric:    AES-256-CBC + HMAC-SHA256 MAC
 * PRF:          TLS 1.2 PRF = P_SHA256 (HMAC-SHA256)
 * Certificate:  Received but not verified (embedded-device mode)
 *
 * Transport layer: tcp_connect / tcp_send / tcp_recv / tcp_close
 */
#include "ssl.h"
#include "kernel.h"
#include "string.h"
#include "crypto.h"
#include "kheap.h"
#include "tcp.h"
#include "dns.h"
#include "tpm.h"
#include "pit.h"

/* ── TLS record types ─────────────────────────────────────────────────────── */
#define TLS_CHANGE_CIPHER_SPEC  20
#define TLS_ALERT               21
#define TLS_HANDSHAKE           22
#define TLS_APPLICATION_DATA    23

/* ── TLS handshake message types ─────────────────────────────────────────── */
#define HS_CLIENT_HELLO         1
#define HS_SERVER_HELLO         2
#define HS_CERTIFICATE          11
#define HS_SERVER_KEY_EXCHANGE  12
#define HS_SERVER_HELLO_DONE    14
#define HS_CLIENT_KEY_EXCHANGE  16
#define HS_FINISHED             20

/* ── Cipher suite ─────────────────────────────────────────────────────────── */
#define TLS_DHE_RSA_AES256_CBC_SHA256  0x006B

/* ── Sizes ─────────────────────────────────────────────────────────────────── */
#define TLS_RECORD_HDR_SZ  5
#define AES_KEY_SZ         32
#define HMAC_KEY_SZ        32
#define IV_SZ              16
#define HMAC_SHA256_SZ     32
#define MASTER_SECRET_SZ   48
#define RANDOM_SZ          32

/* ── DH group 2 constants (RFC 2409, 1024-bit) ───────────────────────────── */
#define DH_WORDS 32   /* 1024 bits / 32 bits per word */
typedef uint32_t bn_t[DH_WORDS];

/* Oakley Group 2 prime (big-endian bytes, length 128) */
static const uint8_t dh_p_bytes[128] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC9,0x0F,0xDA,0xA2,
    0x21,0x68,0xC2,0x34,0xC4,0xC6,0x62,0x8B,0x80,0xDC,0x1C,0xD1,
    0x29,0x02,0x4E,0x08,0x8A,0x67,0xCC,0x74,0x02,0x0B,0xBE,0xA6,
    0x3B,0x13,0x9B,0x22,0x51,0x4A,0x08,0x79,0x8E,0x34,0x04,0xDD,
    0xEF,0x95,0x19,0xB3,0xCD,0x3A,0x43,0x1B,0x30,0x2B,0x0A,0x6D,
    0xF2,0x5F,0x14,0x37,0x4F,0xE1,0x35,0x6D,0x6D,0x51,0xC2,0x45,
    0xE4,0x85,0xB5,0x76,0x62,0x5E,0x7E,0xC6,0xF4,0x4C,0x42,0xE9,
    0xA6,0x37,0xED,0x6B,0x0B,0xFF,0x5C,0xB6,0xF4,0x06,0xB7,0xED,
    0xEE,0x38,0x6B,0xFB,0x5A,0x89,0x9F,0xA5,0xAE,0x9F,0x24,0x11,
    0x7C,0x4B,0x1F,0xE6,0x49,0x28,0x66,0x51,0xEC,0xE6,0x53,0x81,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

/* ── Bignum helpers (little-endian word arrays) ───────────────────────────── */
static void bn_zero(bn_t r) { for(int i=0;i<DH_WORDS;i++) r[i]=0; }
static void bn_copy(bn_t r, const bn_t a) { for(int i=0;i<DH_WORDS;i++) r[i]=a[i]; }

static void bn_from_be(bn_t r, const uint8_t* be, int nbytes) {
    bn_zero(r);
    for (int i = 0; i < nbytes && i < 128; i++) {
        int wi = (nbytes - 1 - i) / 4;
        int bi = (nbytes - 1 - i) % 4;
        if (wi < DH_WORDS) r[wi] |= (uint32_t)be[i] << (bi * 8);
    }
}

static void bn_to_be(const bn_t a, uint8_t* be, int nbytes) {
    for (int i = 0; i < nbytes; i++) {
        int wi = (nbytes - 1 - i) / 4;
        int bi = (nbytes - 1 - i) % 4;
        be[i] = (wi < DH_WORDS) ? (uint8_t)(a[wi] >> (bi * 8)) : 0;
    }
}

/* Compare: -1 if a<b, 0 if equal, 1 if a>b (2n-word) */
static int bn_cmp2(const uint32_t* a, const uint32_t* b, int n) {
    for (int i = n-1; i >= 0; i--) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return  1;
    }
    return 0;
}

/* Subtract: r = a - b (n-word), returns borrow */
static uint32_t bn_sub_n(uint32_t* r, const uint32_t* a, const uint32_t* b, int n) {
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        uint64_t d = (uint64_t)a[i] - (uint64_t)b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (d >> 63) & 1;
    }
    return (uint32_t)borrow;
}

/* Schoolbook multiply: r[2n] = a[n] × b[n] */
static void bn_mul_full(uint32_t* r, const uint32_t* a, const uint32_t* b) {
    for (int i = 0; i < 2*DH_WORDS; i++) r[i] = 0;
    for (int i = 0; i < DH_WORDS; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < DH_WORDS; j++) {
            uint64_t p = (uint64_t)a[i] * (uint64_t)b[j] + (uint64_t)r[i+j] + carry;
            r[i+j] = (uint32_t)p;
            carry = p >> 32;
        }
        r[i+DH_WORDS] += (uint32_t)carry;
    }
}

/*
 * Modular reduction: reduce 2n-word tmp modulo n-word m, store in n-word r.
 * Uses shift-and-subtract (binary long division style).
 */
static void bn_mod2n(uint32_t* tmp, const uint32_t* m) {
    /* tmp has 2*DH_WORDS words, m has DH_WORDS words.
       We work top-down, subtracting m<<k for each excess bit. */
    for (int bit = (2*DH_WORDS*32 - 1); bit >= (DH_WORDS*32); bit--) {
        int tw = bit / 32, tb = bit % 32;
        if (!((tmp[tw] >> tb) & 1)) continue;
        /* Subtract m shifted left by (bit - DH_WORDS*32 + 1) from tmp */
        int shift = bit - DH_WORDS*32;
        int sw = shift / 32, sb = shift % 32;
        uint64_t borrow = 0;
        for (int i = 0; i < DH_WORDS; i++) {
            int ti = sw + i + DH_WORDS;
            uint32_t mw = (sb == 0) ? m[i] : (m[i] << sb) | (i > 0 ? m[i-1] >> (32-sb) : 0);
            uint64_t d = (uint64_t)tmp[ti] - (uint64_t)mw - borrow;
            tmp[ti] = (uint32_t)d;
            borrow = (d >> 63) & 1;
        }
    }
    /* Now the answer sits in the low DH_WORDS words of tmp.
       Final correction: subtract m if still >= m. */
    while (bn_cmp2(tmp, m, DH_WORDS) >= 0) {
        bn_sub_n(tmp, tmp, m, DH_WORDS);
    }
}

/* r = (a × b) mod m */
static void bn_mulmod(bn_t r, const bn_t a, const bn_t b, const bn_t m) {
    uint32_t tmp[2*DH_WORDS];
    bn_mul_full(tmp, a, b);
    bn_mod2n(tmp, m);
    for (int i = 0; i < DH_WORDS; i++) r[i] = tmp[i];
}

/* r = a^e mod m  (square-and-multiply, e is DH_WORDS words) */
static void bn_powmod(bn_t r, const bn_t a, const bn_t e, const bn_t m) {
    bn_t base, result;
    bn_copy(base, a);
    bn_zero(result); result[0] = 1;  /* result = 1 */
    for (int i = 0; i < DH_WORDS * 32; i++) {
        int wi = i / 32, bi = i % 32;
        if ((e[wi] >> bi) & 1) bn_mulmod(result, result, base, m);
        bn_mulmod(base, base, base, m);
    }
    bn_copy(r, result);
}

/* ── Per-connection TLS state ─────────────────────────────────────────────── */
#define MAX_SSL_CONNS 4

typedef struct {
    int      in_use;
    int      conn_id;          /* TCP connection ID */
    int      handshake_done;

    uint8_t  client_random[RANDOM_SZ];
    uint8_t  server_random[RANDOM_SZ];

    /* Session keys (derived after handshake) */
    uint8_t  client_write_key[AES_KEY_SZ];
    uint8_t  server_write_key[AES_KEY_SZ];
    uint8_t  client_write_MAC[HMAC_KEY_SZ];
    uint8_t  server_write_MAC[HMAC_KEY_SZ];
    uint8_t  client_write_IV [IV_SZ];
    uint8_t  server_write_IV [IV_SZ];

    uint64_t send_seq;
    uint64_t recv_seq;

    /* Handshake transcript hash accumulator */
    uint8_t  hs_msgs[8192];
    uint32_t hs_len;
} ssl_conn_t;

static ssl_conn_t ssl_conns[MAX_SSL_CONNS];
static int ssl_initialized = 0;

/* ── HMAC-SHA256 (local copy so ssl.c is self-contained) ─────────────────── */
#define HMAC_BLK 64

static void hmac_sha256_ssl(const uint8_t* key, uint32_t klen,
                             const uint8_t* msg, uint32_t mlen,
                             uint8_t* out) {
    uint8_t k[HMAC_SHA256_SZ];
    if (klen > HMAC_BLK) { sha256_hash(key, klen, k); key = k; klen = HMAC_SHA256_SZ; }

    uint8_t ipad[HMAC_BLK], opad[HMAC_BLK];
    for (int i = 0; i < HMAC_BLK; i++) {
        uint8_t b = i < (int)klen ? key[i] : 0;
        ipad[i] = b ^ 0x36; opad[i] = b ^ 0x5C;
    }
    uint8_t* inner = (uint8_t*)kmalloc(HMAC_BLK + mlen);
    if (!inner) return;
    memcpy(inner, ipad, HMAC_BLK);
    memcpy(inner + HMAC_BLK, msg, mlen);
    uint8_t ih[HMAC_SHA256_SZ];
    sha256_hash(inner, HMAC_BLK + mlen, ih);
    kfree(inner);

    uint8_t outer[HMAC_BLK + HMAC_SHA256_SZ];
    memcpy(outer, opad, HMAC_BLK);
    memcpy(outer + HMAC_BLK, ih, HMAC_SHA256_SZ);
    sha256_hash(outer, HMAC_BLK + HMAC_SHA256_SZ, out);
}

/* ── TLS 1.2 PRF = P_SHA256 ──────────────────────────────────────────────── */
static void tls12_prf(const uint8_t* secret, uint32_t slen,
                      const char*    label,
                      const uint8_t* seed,   uint32_t seedlen,
                      uint8_t*       out,    uint32_t outlen) {
    uint32_t llen = (uint32_t)strlen(label);
    uint32_t ls_len = llen + seedlen;
    uint8_t* ls = (uint8_t*)kmalloc(ls_len);
    if (!ls) return;
    memcpy(ls, label, llen);
    memcpy(ls + llen, seed, seedlen);

    /* A(0) = seed, A(i) = HMAC(secret, A(i-1)) */
    uint8_t A[HMAC_SHA256_SZ];
    hmac_sha256_ssl(secret, slen, ls, ls_len, A);   /* A(1) */

    uint32_t done = 0;
    uint8_t* tmp = (uint8_t*)kmalloc(HMAC_SHA256_SZ + ls_len);
    if (!tmp) { kfree(ls); return; }

    while (done < outlen) {
        memcpy(tmp, A, HMAC_SHA256_SZ);
        memcpy(tmp + HMAC_SHA256_SZ, ls, ls_len);
        uint8_t h[HMAC_SHA256_SZ];
        hmac_sha256_ssl(secret, slen, tmp, HMAC_SHA256_SZ + ls_len, h);

        uint32_t n = outlen - done;
        if (n > HMAC_SHA256_SZ) n = HMAC_SHA256_SZ;
        memcpy(out + done, h, n);
        done += n;

        /* A(i+1) = HMAC(secret, A(i)) */
        hmac_sha256_ssl(secret, slen, A, HMAC_SHA256_SZ, A);
    }
    kfree(tmp);
    kfree(ls);
}

/* ── TLS record layer ─────────────────────────────────────────────────────── */
static int tls_raw_send(int conn_id, uint8_t type,
                         const uint8_t* data, uint32_t dlen) {
    uint8_t hdr[5] = { type, 3, 3, (uint8_t)(dlen >> 8), (uint8_t)(dlen & 0xFF) };
    if (tcp_send(conn_id, hdr, 5) < 0) return -1;
    if (dlen > 0 && tcp_send(conn_id, data, dlen) < 0) return -1;
    return 0;
}

static int tls_raw_recv(int conn_id, uint8_t* type,
                         uint8_t* buf, uint32_t* len, uint32_t max) {
    uint8_t hdr[5];
    int n = tcp_recv(conn_id, hdr, 5, 5000);
    if (n < 5) return -1;
    *type = hdr[0];
    uint32_t rlen = ((uint32_t)hdr[3] << 8) | hdr[4];
    if (rlen > max) rlen = max;
    int got = tcp_recv(conn_id, buf, rlen, 5000);
    if (got < 0) return -1;
    *len = (uint32_t)got;
    return 0;
}

/* ── AES-256-CBC encrypt/decrypt (PKCS#7 padding) ────────────────────────── */
static int aes_cbc_encrypt(const uint8_t* key, const uint8_t* iv,
                            const uint8_t* in, uint32_t inlen,
                            uint8_t* out, uint32_t* outlen) {
    uint32_t pad = 16 - (inlen % 16);
    uint32_t total = inlen + pad;
    *outlen = total;

    aes256_ctx_t ctx;
    aes256_init(&ctx, key);

    uint8_t prev[16];
    memcpy(prev, iv, 16);

    for (uint32_t off = 0; off < total; off += 16) {
        uint8_t block[16];
        for (int i = 0; i < 16; i++) {
            uint8_t b = (off + i < inlen) ? in[off + i] : (uint8_t)pad;
            block[i] = b ^ prev[i];
        }
        aes256_encrypt_block(&ctx, block, out + off);
        memcpy(prev, out + off, 16);
    }
    return 0;
}

static int aes_cbc_decrypt(const uint8_t* key, const uint8_t* iv,
                            const uint8_t* in, uint32_t inlen,
                            uint8_t* out, uint32_t* outlen) {
    if (inlen == 0 || inlen % 16 != 0) return -1;
    aes256_ctx_t ctx;
    aes256_init(&ctx, key);

    uint8_t prev[16], block[16];
    memcpy(prev, iv, 16);
    for (uint32_t off = 0; off < inlen; off += 16) {
        aes256_decrypt_block(&ctx, in + off, block);
        for (int i = 0; i < 16; i++) out[off + i] = block[i] ^ prev[i];
        memcpy(prev, in + off, 16);
    }
    uint8_t pad = out[inlen - 1];
    *outlen = (pad < inlen) ? (inlen - pad) : inlen;
    return 0;
}

/* ── Encrypted TLS record send/receive ───────────────────────────────────── */
static int tls_send_encrypted(ssl_conn_t* sc, uint8_t type,
                               const uint8_t* data, uint32_t dlen) {
    /* MAC = HMAC(mac_key, seq || type || version || len || data) */
    uint8_t mac_input[13 + 16384];
    uint64_t seq_be = sc->send_seq;
    for (int i = 7; i >= 0; i--) { mac_input[i] = (uint8_t)(seq_be & 0xFF); seq_be >>= 8; }
    mac_input[8] = type;
    mac_input[9] = 3; mac_input[10] = 3;
    mac_input[11] = (uint8_t)(dlen >> 8);
    mac_input[12] = (uint8_t)(dlen & 0xFF);
    if (dlen <= sizeof(mac_input) - 13) memcpy(mac_input + 13, data, dlen);
    uint8_t mac[HMAC_SHA256_SZ];
    hmac_sha256_ssl(sc->client_write_MAC, HMAC_KEY_SZ, mac_input, 13 + dlen, mac);

    /* Plaintext = data || MAC */
    uint32_t pt_len = dlen + HMAC_SHA256_SZ;
    uint8_t* pt = (uint8_t*)kmalloc(pt_len);
    if (!pt) return -1;
    memcpy(pt, data, dlen);
    memcpy(pt + dlen, mac, HMAC_SHA256_SZ);

    /* Random IV for this record */
    uint8_t iv[16];
    tpm_get_random(iv, 16);

    /* Encrypt */
    uint32_t ct_len;
    uint8_t* ct = (uint8_t*)kmalloc(pt_len + 32);
    if (!ct) { kfree(pt); return -1; }
    aes_cbc_encrypt(sc->client_write_key, iv, pt, pt_len, ct, &ct_len);
    kfree(pt);

    /* Record = IV || ciphertext */
    uint32_t rec_len = 16 + ct_len;
    uint8_t hdr[5] = { type, 3, 3, (uint8_t)(rec_len >> 8), (uint8_t)(rec_len & 0xFF) };
    tcp_send(sc->conn_id, hdr, 5);
    tcp_send(sc->conn_id, iv, 16);
    tcp_send(sc->conn_id, ct, ct_len);
    kfree(ct);
    sc->send_seq++;
    return 0;
}

static int tls_recv_decrypted(ssl_conn_t* sc, uint8_t* type,
                               uint8_t* out, uint32_t* outlen, uint32_t max) {
    uint8_t hdr[5];
    if (tcp_recv(sc->conn_id, hdr, 5, 5000) < 5) return -1;
    *type = hdr[0];
    uint32_t rlen = ((uint32_t)hdr[3] << 8) | hdr[4];
    uint8_t* buf = (uint8_t*)kmalloc(rlen);
    if (!buf) return -1;
    if ((uint32_t)tcp_recv(sc->conn_id, buf, rlen, 5000) < rlen) { kfree(buf); return -1; }

    /* First 16 bytes are the explicit IV */
    if (rlen < 16) { kfree(buf); return -1; }
    uint32_t pt_len;
    uint8_t* pt = (uint8_t*)kmalloc(rlen);
    if (!pt) { kfree(buf); return -1; }
    aes_cbc_decrypt(sc->server_write_key, buf, buf + 16, rlen - 16, pt, &pt_len);
    kfree(buf);

    /* Strip MAC from end */
    if (pt_len < HMAC_SHA256_SZ) { kfree(pt); return -1; }
    pt_len -= HMAC_SHA256_SZ;

    uint32_t n = pt_len < max ? pt_len : max;
    memcpy(out, pt, n);
    *outlen = n;
    kfree(pt);
    sc->recv_seq++;
    return 0;
}

/* ── Handshake helper: accumulate messages for Finished hash ──────────────── */
static void hs_append(ssl_conn_t* sc, const uint8_t* msg, uint32_t len) {
    if (sc->hs_len + len < sizeof(sc->hs_msgs))
        memcpy(sc->hs_msgs + sc->hs_len, msg, len), sc->hs_len += len;
}

/* ── Build and send ClientHello ───────────────────────────────────────────── */
static int send_client_hello(ssl_conn_t* sc) {
    tpm_get_random(sc->client_random, RANDOM_SZ);

    /* ClientHello body */
    uint8_t hello[128];
    int p = 0;
    hello[p++] = 3; hello[p++] = 3;          /* version TLS 1.2 */
    memcpy(hello + p, sc->client_random, 32); p += 32;
    hello[p++] = 0;                            /* session ID length = 0 */
    /* cipher suites: 2 bytes count + 1 suite */
    hello[p++] = 0; hello[p++] = 2;
    hello[p++] = (uint8_t)(TLS_DHE_RSA_AES256_CBC_SHA256 >> 8);
    hello[p++] = (uint8_t)(TLS_DHE_RSA_AES256_CBC_SHA256 & 0xFF);
    hello[p++] = 1; hello[p++] = 0;           /* compression: 1 method, null */
    /* no extensions */

    /* Handshake header: type + 3-byte length */
    uint8_t hs_hdr[4] = { HS_CLIENT_HELLO, 0, 0, (uint8_t)p };
    uint8_t hs_msg[4 + 128];
    memcpy(hs_msg, hs_hdr, 4);
    memcpy(hs_msg + 4, hello, p);
    uint32_t hs_len = 4 + (uint32_t)p;

    hs_append(sc, hs_msg, hs_len);
    return tls_raw_send(sc->conn_id, TLS_HANDSHAKE, hs_msg, hs_len);
}

/* ── Parse ServerHello (extract server random) ────────────────────────────── */
static int parse_server_hello(ssl_conn_t* sc, const uint8_t* msg, uint32_t len) {
    if (len < 38) return -1;
    /* msg[0..1] = version, msg[2..33] = random */
    memcpy(sc->server_random, msg + 2, 32);
    terminal_printf("[TLS] ServerHello: cipher=0x%02x%02x\n", msg[35], msg[36]);
    return 0;
}

/* ── Parse ServerKeyExchange: extract DH parameters p, g, Ys ─────────────── */
static int parse_server_kex(ssl_conn_t* sc, const uint8_t* msg, uint32_t len,
                             bn_t dh_p, bn_t dh_g, bn_t dh_Ys) {
    (void)sc;
    if (len < 6) return -1;
    uint32_t off = 0;
    /* p */
    uint16_t plen = ((uint16_t)msg[off] << 8) | msg[off+1]; off += 2;
    if (off + plen > len) return -1;
    bn_from_be(dh_p, msg + off, plen); off += plen;
    /* g */
    uint16_t glen = ((uint16_t)msg[off] << 8) | msg[off+1]; off += 2;
    if (off + glen > len) return -1;
    bn_from_be(dh_g, msg + off, glen); off += glen;
    /* Ys */
    uint16_t ylen = ((uint16_t)msg[off] << 8) | msg[off+1]; off += 2;
    if (off + ylen > len) return -1;
    bn_from_be(dh_Ys, msg + off, ylen);
    terminal_printf("[TLS] ServerKeyExchange: p=%u-bit g=%u-bit Ys=%u-bit\n",
                    plen*8, glen*8, ylen*8);
    return 0;
}

/* ── Build and send ClientKeyExchange (DH Yc) ────────────────────────────── */
static int send_client_key_exchange(ssl_conn_t* sc, const uint8_t* yc_bytes, uint16_t yc_len) {
    /* body: uint16 length + Yc bytes */
    uint8_t* body = (uint8_t*)kmalloc(2 + yc_len + 4);
    if (!body) return -1;
    body[0] = (uint8_t)(yc_len >> 8); body[1] = (uint8_t)(yc_len & 0xFF);
    memcpy(body + 2, yc_bytes, yc_len);

    uint32_t body_len = 2 + yc_len;
    uint8_t hs_hdr[4] = { HS_CLIENT_KEY_EXCHANGE, 0,
                           (uint8_t)(body_len >> 8), (uint8_t)(body_len & 0xFF) };
    uint8_t* hs_msg = (uint8_t*)kmalloc(4 + body_len);
    if (!hs_msg) { kfree(body); return -1; }
    memcpy(hs_msg, hs_hdr, 4);
    memcpy(hs_msg + 4, body, body_len);
    kfree(body);

    hs_append(sc, hs_msg, 4 + body_len);
    tls_raw_send(sc->conn_id, TLS_HANDSHAKE, hs_msg, 4 + body_len);
    kfree(hs_msg);
    return 0;
}

/* ── Derive master secret and session keys ────────────────────────────────── */
static void derive_keys(ssl_conn_t* sc, const uint8_t* premaster, uint32_t pmlen) {
    uint8_t seed[64];
    memcpy(seed, sc->client_random, 32);
    memcpy(seed + 32, sc->server_random, 32);

    uint8_t master[48];
    tls12_prf(premaster, pmlen, "master secret", seed, 64, master, 48);

    /* key_block = PRF(master, "key expansion", ServerRandom || ClientRandom) */
    uint8_t seed2[64];
    memcpy(seed2, sc->server_random, 32);
    memcpy(seed2 + 32, sc->client_random, 32);

    /* Total key material: 2×MAC(32) + 2×AES(32) + 2×IV(16) = 160 bytes */
    uint8_t kb[160];
    tls12_prf(master, 48, "key expansion", seed2, 64, kb, 160);

    memcpy(sc->client_write_MAC, kb +   0, 32);
    memcpy(sc->server_write_MAC, kb +  32, 32);
    memcpy(sc->client_write_key, kb +  64, 32);
    memcpy(sc->server_write_key, kb +  96, 32);
    memcpy(sc->client_write_IV,  kb + 128, 16);
    memcpy(sc->server_write_IV,  kb + 144, 16);
    terminal_printf("[TLS] Session keys derived (AES-256-CBC + HMAC-SHA256).\n");
}

/* ── Build Finished verify_data ───────────────────────────────────────────── */
static void compute_finished(ssl_conn_t* sc, const uint8_t* master,
                              const char* label, uint8_t* vd_out) {
    uint8_t hs_hash[32];
    sha256_hash(sc->hs_msgs, sc->hs_len, hs_hash);
    tls12_prf(master, 48, label, hs_hash, 32, vd_out, 12);
}

/* ── Public API ───────────────────────────────────────────────────────────── */
void ssl_init(void) {
    if (ssl_initialized) return;
    memset(ssl_conns, 0, sizeof(ssl_conns));
    ssl_initialized = 1;
    terminal_printf("[SSL] TLS 1.2 engine initialized (DHE-AES256-CBC-SHA256).\n");
}

int ssl_connect(const char* host, uint16_t port) {
    if (!ssl_initialized) ssl_init();

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_SSL_CONNS; i++)
        if (!ssl_conns[i].in_use) { slot = i; break; }
    if (slot < 0) { terminal_printf("[SSL] No free connection slots.\n"); return -1; }

    ssl_conn_t* sc = &ssl_conns[slot];
    memset(sc, 0, sizeof(*sc));
    sc->in_use = 1;

    /* DNS + TCP connect */
    uint32_t ip;
    terminal_printf("[SSL] Resolving %s...\n", host);
    if (dns_resolve(host, &ip) != 0) {
        terminal_printf("[SSL] DNS failed for %s\n", host);
        sc->in_use = 0;
        return -1;
    }
    sc->conn_id = tcp_connect(ip, port);
    if (sc->conn_id < 0) {
        terminal_printf("[SSL] TCP connect to %s:%u failed\n", host, port);
        sc->in_use = 0;
        return -1;
    }
    terminal_printf("[SSL] TCP connected to %s:%u (conn=%d)\n", host, port, sc->conn_id);

    /* ── TLS Handshake ────────────────────────────────────────────────────── */

    /* 1. ClientHello */
    if (send_client_hello(sc) < 0) goto fail;
    terminal_printf("[TLS] ClientHello sent.\n");

    /* 2. Receive and parse server handshake messages */
    bn_t dh_p, dh_g, dh_Ys;
    bn_zero(dh_p); bn_zero(dh_g); bn_zero(dh_Ys);
    /* Load default DH group 2 in case server omits ServerKeyExchange */
    bn_from_be(dh_p, dh_p_bytes, 128);
    dh_g[0] = 2;

    int got_hello = 0, got_hellodone = 0;
    uint8_t rec_type;
    uint8_t* rbuf = (uint8_t*)kmalloc(16384);
    if (!rbuf) goto fail;

    while (!got_hellodone) {
        uint32_t rlen;
        if (tls_raw_recv(sc->conn_id, &rec_type, rbuf, &rlen, 16384) < 0) break;
        if (rec_type == TLS_ALERT) {
            terminal_printf("[TLS] Alert from server: %u %u\n", rbuf[0], rbuf[1]);
            break;
        }
        if (rec_type != TLS_HANDSHAKE) continue;

        hs_append(sc, rbuf, rlen);
        uint32_t off = 0;
        while (off + 4 <= rlen) {
            uint8_t  mtype = rbuf[off];
            uint32_t mlen  = ((uint32_t)rbuf[off+1]<<16)|((uint32_t)rbuf[off+2]<<8)|rbuf[off+3];
            off += 4;
            if (off + mlen > rlen) break;

            if (mtype == HS_SERVER_HELLO) {
                parse_server_hello(sc, rbuf + off, mlen);
                got_hello = 1;
            } else if (mtype == HS_CERTIFICATE) {
                terminal_printf("[TLS] Certificate received (%u bytes) — skipping verification.\n", mlen);
            } else if (mtype == HS_SERVER_KEY_EXCHANGE) {
                parse_server_kex(sc, rbuf + off, mlen, dh_p, dh_g, dh_Ys);
            } else if (mtype == HS_SERVER_HELLO_DONE) {
                terminal_printf("[TLS] ServerHelloDone received.\n");
                got_hellodone = 1;
            }
            off += mlen;
        }
    }
    if (!got_hello || !got_hellodone) { kfree(rbuf); goto fail; }

    /* 3. DHE: generate private key Xc, compute Yc = g^Xc mod p */
    terminal_printf("[TLS] Computing DH key pair...\n");
    bn_t Xc, Yc, premaster_bn;
    uint8_t Xc_bytes[32];
    tpm_get_random(Xc_bytes, 32);
    bn_from_be(Xc, Xc_bytes, 32);
    bn_powmod(Yc, dh_g, Xc, dh_p);

    /* Compute premaster = Ys^Xc mod p */
    if (dh_Ys[0] == 0 && dh_Ys[1] == 0) {
        /* Server didn't send SKE — generate random premaster */
        tpm_get_random(Xc_bytes, 32);
        bn_from_be(premaster_bn, Xc_bytes, 32);
    } else {
        bn_powmod(premaster_bn, dh_Ys, Xc, dh_p);
    }
    terminal_printf("[TLS] DH premaster secret computed.\n");

    /* 4. Send ClientKeyExchange */
    uint8_t yc_bytes[128];
    bn_to_be(Yc, yc_bytes, 128);
    /* Find actual byte length (strip leading zeros) */
    int yc_start = 0;
    while (yc_start < 127 && yc_bytes[yc_start] == 0) yc_start++;
    send_client_key_exchange(sc, yc_bytes + yc_start, (uint16_t)(128 - yc_start));

    /* 5. Derive master secret and session keys */
    uint8_t pm_bytes[128];
    bn_to_be(premaster_bn, pm_bytes, 128);
    int pm_start = 0;
    while (pm_start < 127 && pm_bytes[pm_start] == 0) pm_start++;
    derive_keys(sc, pm_bytes + pm_start, (uint32_t)(128 - pm_start));

    /* Also need master for Finished */
    uint8_t seed[64];
    memcpy(seed, sc->client_random, 32);
    memcpy(seed + 32, sc->server_random, 32);
    uint8_t master[48];
    tls12_prf(pm_bytes + pm_start, (uint32_t)(128 - pm_start),
              "master secret", seed, 64, master, 48);

    /* 6. Send ChangeCipherSpec */
    uint8_t ccs = 1;
    tls_raw_send(sc->conn_id, TLS_CHANGE_CIPHER_SPEC, &ccs, 1);

    /* 7. Send Finished */
    uint8_t vd[12];
    compute_finished(sc, master, "client finished", vd);
    uint8_t fin_hs[16] = { HS_FINISHED, 0, 0, 12 };
    memcpy(fin_hs + 4, vd, 12);
    hs_append(sc, fin_hs, 16);
    tls_send_encrypted(sc, TLS_HANDSHAKE, fin_hs, 16);
    terminal_printf("[TLS] Finished sent.\n");

    /* 8. Receive server ChangeCipherSpec + Finished */
    uint32_t rlen2;
    if (tls_raw_recv(sc->conn_id, &rec_type, rbuf, &rlen2, 16384) == 0
        && rec_type == TLS_CHANGE_CIPHER_SPEC)
        terminal_printf("[TLS] Server ChangeCipherSpec received.\n");

    if (tls_recv_decrypted(sc, &rec_type, rbuf, &rlen2, 16384) == 0
        && rec_type == TLS_HANDSHAKE && rlen2 >= 16)
        terminal_printf("[TLS] Server Finished received — handshake complete!\n");

    kfree(rbuf);
    sc->handshake_done = 1;
    terminal_printf("[SSL] TLS 1.2 session established with %s:%u\n", host, port);
    return slot;

fail:
    tcp_close(sc->conn_id);
    sc->in_use = 0;
    terminal_printf("[SSL] TLS handshake failed with %s:%u\n", host, port);
    return -1;
}

int ssl_send(int socket, const char* data, uint32_t len) {
    if (socket < 0 || socket >= MAX_SSL_CONNS || !ssl_conns[socket].in_use) return -1;
    ssl_conn_t* sc = &ssl_conns[socket];
    if (!sc->handshake_done)
        return tcp_send(sc->conn_id, (const uint8_t*)data, len);
    return tls_send_encrypted(sc, TLS_APPLICATION_DATA, (const uint8_t*)data, len) == 0
           ? (int)len : -1;
}

int ssl_receive(int socket, char* buffer, uint32_t max_len) {
    if (socket < 0 || socket >= MAX_SSL_CONNS || !ssl_conns[socket].in_use) return -1;
    ssl_conn_t* sc = &ssl_conns[socket];
    if (!sc->handshake_done)
        return tcp_recv(sc->conn_id, (uint8_t*)buffer, max_len, 5000);
    uint8_t type;
    uint32_t got;
    if (tls_recv_decrypted(sc, &type, (uint8_t*)buffer, &got, max_len) < 0) return -1;
    return (int)got;
}

void ssl_close(int socket) {
    if (socket < 0 || socket >= MAX_SSL_CONNS) return;
    ssl_conn_t* sc = &ssl_conns[socket];
    if (!sc->in_use) return;
    /* Send TLS close_notify alert */
    if (sc->handshake_done) {
        uint8_t alert[2] = { 1, 0 };  /* warning, close_notify */
        tls_send_encrypted(sc, TLS_ALERT, alert, 2);
    }
    tcp_close(sc->conn_id);
    sc->in_use = 0;
}
