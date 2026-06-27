#include "png.h"
#include "font.h"
#include "string.h"
#include "kheap.h"
#include <stdint.h>

extern int sprintf(char* buf, const char* fmt, ...);

/* PNG file signature: 8 bytes */
static const uint8_t PNG_SIG[8] = {0x89,'P','N','G','\r','\n',0x1a,'\n'};

/* Big-endian 32-bit read from unaligned pointer */
static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

int png_get_info(const uint8_t* data, uint32_t len, png_info_t* info) {
    if (!data || !info || len < 33) return -1;

    /* Check signature */
    for (int i = 0; i < 8; i++)
        if (data[i] != PNG_SIG[i]) return -1;

    /* Walk chunks looking for IHDR (must be the first chunk at offset 8) */
    uint32_t offset = 8;
    while (offset + 12 <= len) {
        uint32_t chunk_len  = be32(data + offset);
        uint8_t  type0 = data[offset + 4];
        uint8_t  type1 = data[offset + 5];
        uint8_t  type2 = data[offset + 6];
        uint8_t  type3 = data[offset + 7];

        if (type0 == 'I' && type1 == 'H' && type2 == 'D' && type3 == 'R') {
            if (chunk_len < 13 || offset + 8 + 13 > len) return -1;
            const uint8_t* ihdr = data + offset + 8;
            info->width      = be32(ihdr);
            info->height     = be32(ihdr + 4);
            info->depth      = ihdr[8];
            info->color_type = ihdr[9];
            return 0;
        }

        /* Skip to next chunk: 4 (length) + 4 (type) + chunk_len + 4 (CRC) */
        offset += 12 + chunk_len;
    }
    return -1; /* IHDR not found */
}

/* ------------------------------------------------------------------
 * DEFLATE inflate (RFC 1951)
 * Supports: BTYPE=00 (stored), BTYPE=01 (fixed Huffman)
 * ------------------------------------------------------------------ */

/* Bit reader state */
typedef struct {
    const uint8_t* in;
    uint32_t       in_len;
    uint32_t       in_pos;
    uint32_t       bit_buf;
    int            bit_cnt;
} BitReader;

static void br_init(BitReader* br, const uint8_t* in, uint32_t in_len) {
    br->in     = in;
    br->in_len = in_len;
    br->in_pos = 0;
    br->bit_buf = 0;
    br->bit_cnt = 0;
}

/* Refill bit buffer with more bytes */
static void br_refill(BitReader* br) {
    while (br->bit_cnt <= 24 && br->in_pos < br->in_len) {
        br->bit_buf |= (uint32_t)br->in[br->in_pos++] << br->bit_cnt;
        br->bit_cnt += 8;
    }
}

/* Read 'n' bits (LSB first) */
static uint32_t br_bits(BitReader* br, int n) {
    if (n == 0) return 0;
    br_refill(br);
    uint32_t v = br->bit_buf & ((1u << n) - 1u);
    br->bit_buf >>= n;
    br->bit_cnt  -= n;
    return v;
}

/* Align to next byte boundary (discard partial byte bits) */
static void br_align(BitReader* br) {
    int discard = br->bit_cnt & 7;
    if (discard) {
        br->bit_buf >>= discard;
        br->bit_cnt  -= discard;
    }
}

/* Fixed Huffman decode for literal/length (RFC 1951 s3.2.6)
 * Returns symbol 0-287, or -1 on error */
static int fixed_litlen(BitReader* br) {
    /* Read 7 bits first */
    uint32_t code7 = br_bits(br, 7);

    /* 7-bit range 0x00-0x17 => symbols 256-279 */
    if (code7 <= 0x17) {
        return 256 + (int)code7;
    }

    /* Need one more bit */
    uint32_t code8 = code7 | (br_bits(br, 1) << 7);

    /* 8-bit range 0x30-0xBF => symbols 0-143 */
    if (code8 >= 0x30 && code8 <= 0xBF) {
        return (int)(code8 - 0x30);
    }

    /* 8-bit range 0xC0-0xC7 => symbols 280-287 */
    if (code8 >= 0xC0 && code8 <= 0xC7) {
        return 280 + (int)(code8 - 0xC0);
    }

    /* Need one more bit (9 total) */
    uint32_t code9 = code8 | (br_bits(br, 1) << 8);

    /* 9-bit range 0x190-0x1FF => symbols 144-255 */
    if (code9 >= 0x190 && code9 <= 0x1FF) {
        return 144 + (int)(code9 - 0x190);
    }

    return -1; /* error */
}

/* Length extra bits table (symbols 257-285) */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
    35,43,51,59, 67,83,99,115, 131,163,195,227, 258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
    3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};

/* Distance extra bits table (codes 0-29) */
static const uint16_t dist_base[30] = {
    1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193,
    257,385,513,769, 1025,1537,2049,3073,
    4097,6145,8193,12289, 16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6,
    7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
};

/* Fixed Huffman distance code: 5-bit MSB-first, value 0-29 */
static int fixed_dist(BitReader* br) {
    uint32_t code = 0;
    for (int i = 4; i >= 0; i--)
        code |= br_bits(br, 1) << i;
    if (code >= 30) return -1;
    return (int)code;
}

/* Returns number of decompressed bytes, or -1 on error */
static int png_inflate(const uint8_t* in, uint32_t in_len,
                       uint8_t* out, uint32_t out_max)
{
    BitReader br;
    br_init(&br, in, in_len);
    uint32_t out_pos = 0;
    int bfinal = 0;

    do {
        bfinal = (int)br_bits(&br, 1);
        int btype = (int)br_bits(&br, 2);

        if (btype == 0) {
            /* Stored / non-compressed block */
            br_align(&br);
            if (br.in_pos + 4 > br.in_len) return -1;
            /* Read LEN and NLEN from remaining bit buffer bytes */
            uint16_t len  = (uint16_t)br_bits(&br, 8) | ((uint16_t)br_bits(&br, 8) << 8);
            uint16_t nlen = (uint16_t)br_bits(&br, 8) | ((uint16_t)br_bits(&br, 8) << 8);
            (void)nlen; /* one's complement check skipped */
            if (out_pos + len > out_max) return -1;
            for (uint16_t i = 0; i < len; i++) {
                if (br.in_pos >= br.in_len && br.bit_cnt < 8) return -1;
                out[out_pos++] = (uint8_t)br_bits(&br, 8);
            }
        } else if (btype == 1) {
            /* Fixed Huffman codes */
            for (;;) {
                int sym = fixed_litlen(&br);
                if (sym < 0) return -1;
                if (sym < 256) {
                    /* Literal byte */
                    if (out_pos >= out_max) return -1;
                    out[out_pos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    /* End of block */
                    break;
                } else {
                    /* Length/distance back-reference */
                    int li = sym - 257;
                    if (li < 0 || li >= 29) return -1;
                    uint32_t length = len_base[li] + br_bits(&br, len_extra[li]);
                    int dc = fixed_dist(&br);
                    if (dc < 0 || dc >= 30) return -1;
                    uint32_t distance = dist_base[dc] + br_bits(&br, dist_extra[dc]);
                    if (distance > out_pos) return -1;
                    if (out_pos + length > out_max) return -1;
                    /* Copy (may overlap — byte by byte) */
                    uint32_t src = out_pos - distance;
                    for (uint32_t k = 0; k < length; k++)
                        out[out_pos++] = out[src++];
                }
            }
        } else {
            /* BTYPE=2 dynamic Huffman or BTYPE=3 reserved */
            return -1;
        }
    } while (!bfinal);

    return (int)out_pos;
}

/* ------------------------------------------------------------------
 * PNG filter reconstruction (per-scanline, RFC 2083)
 * ------------------------------------------------------------------ */

static int paeth_predictor(int a, int b, int c) {
    int p  = a + b - c;
    int pa = p - a; if (pa < 0) pa = -pa;
    int pb = p - b; if (pb < 0) pb = -pb;
    int pc = p - c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static void png_defilter(uint8_t* raw, const uint8_t* prior,
                         int filter_type, int bpp, int rowbytes)
{
    switch (filter_type) {
    case 0: /* None */
        break;
    case 1: /* Sub */
        for (int x = bpp; x < rowbytes; x++)
            raw[x] = (uint8_t)(raw[x] + raw[x - bpp]);
        break;
    case 2: /* Up */
        for (int x = 0; x < rowbytes; x++)
            raw[x] = (uint8_t)(raw[x] + prior[x]);
        break;
    case 3: /* Average */
        for (int x = 0; x < rowbytes; x++) {
            int a = (x >= bpp) ? raw[x - bpp] : 0;
            int b = prior[x];
            raw[x] = (uint8_t)(raw[x] + (a + b) / 2);
        }
        break;
    case 4: /* Paeth */
        for (int x = 0; x < rowbytes; x++) {
            int a = (x >= bpp) ? raw[x - bpp] : 0;
            int b = prior[x];
            int c = (x >= bpp) ? prior[x - bpp] : 0;
            raw[x] = (uint8_t)(raw[x] + paeth_predictor(a, b, c));
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------
 * Full PNG decode
 * ------------------------------------------------------------------ */

int png_decode(const uint8_t* data, uint32_t len,
               uint32_t* out_pixels, int out_w, int out_h)
{
    if (!data || !out_pixels || out_w <= 0 || out_h <= 0) return -1;

    /* Verify signature */
    if (len < 8) return -1;
    for (int i = 0; i < 8; i++)
        if (data[i] != PNG_SIG[i]) return -1;

    /* Parse IHDR */
    png_info_t info;
    if (png_get_info(data, len, &info) != 0) return -1;

    uint32_t img_w = info.width;
    uint32_t img_h = info.height;
    if (img_w == 0 || img_h == 0 || img_w > 4096 || img_h > 4096) return -1;
    if (info.depth != 8) return -1; /* Only 8-bit depth supported */

    int bpp; /* bytes per pixel in raw image */
    switch (info.color_type) {
    case 0: bpp = 1; break; /* Grayscale */
    case 2: bpp = 3; break; /* RGB */
    case 4: bpp = 2; break; /* Grayscale+Alpha */
    case 6: bpp = 4; break; /* RGBA */
    default: return -1;
    }

    /* Collect all IDAT chunk data into one buffer */
    uint32_t idat_cap = img_w * img_h * (uint32_t)bpp + img_h + 64;
    uint8_t* idat_buf = (uint8_t*)kmalloc(idat_cap);
    if (!idat_buf) return -1;
    uint32_t idat_len = 0;

    uint32_t offset = 8;
    while (offset + 12 <= len) {
        uint32_t chunk_len = be32(data + offset);
        uint8_t  t0 = data[offset + 4];
        uint8_t  t1 = data[offset + 5];
        uint8_t  t2 = data[offset + 6];
        uint8_t  t3 = data[offset + 7];

        if (t0 == 'I' && t1 == 'D' && t2 == 'A' && t3 == 'T') {
            if (offset + 8 + chunk_len <= len &&
                idat_len + chunk_len <= idat_cap) {
                memcpy(idat_buf + idat_len, data + offset + 8, chunk_len);
                idat_len += chunk_len;
            }
        }
        if (t0 == 'I' && t1 == 'E' && t2 == 'N' && t3 == 'D') break;
        if (chunk_len > len) break; /* safety */
        offset += 12 + chunk_len;
    }

    if (idat_len < 6) { kfree(idat_buf); return -1; }

    /* zlib wrapper: skip 2-byte header (CMF + FLG), ignore 4-byte Adler32 tail */
    const uint8_t* zlib_data     = idat_buf + 2;
    uint32_t       zlib_data_len = (idat_len > 6) ? idat_len - 6 : 0;
    if (zlib_data_len == 0) { kfree(idat_buf); return -1; }

    /* Allocate raw output: (rowbytes + 1 filter byte) * height */
    uint32_t raw_cap = ((uint32_t)img_w * (uint32_t)bpp + 1u) * img_h + 16u;
    uint8_t* raw_buf = (uint8_t*)kmalloc(raw_cap);
    if (!raw_buf) { kfree(idat_buf); return -1; }

    int raw_len = png_inflate(zlib_data, zlib_data_len, raw_buf, raw_cap);
    kfree(idat_buf);
    if (raw_len < 0) { kfree(raw_buf); return -1; }

    /* Allocate decoded ARGB pixels at native resolution */
    uint32_t* argb = (uint32_t*)kmalloc(img_w * img_h * 4u);
    if (!argb) { kfree(raw_buf); return -1; }

    int rowbytes = (int)img_w * bpp;
    uint8_t* prior_row = (uint8_t*)kmalloc((uint32_t)rowbytes);
    if (!prior_row) { kfree(raw_buf); kfree(argb); return -1; }
    memset(prior_row, 0, (uint32_t)rowbytes);

    const uint8_t* rp = raw_buf;
    int consumed = 0;
    for (uint32_t y = 0; y < img_h; y++) {
        if (consumed + 1 + rowbytes > raw_len) break;
        int filter_type = (int)*rp++;
        consumed++;

        /* Defilter in place */
        png_defilter((uint8_t*)rp, prior_row, filter_type, bpp, rowbytes);

        /* Convert to ARGB32 */
        for (uint32_t x = 0; x < img_w; x++) {
            const uint8_t* px = rp + x * (uint32_t)bpp;
            uint32_t v;
            switch (info.color_type) {
            case 0: /* Grayscale */
                v = 0xFF000000u | ((uint32_t)px[0] << 16) |
                    ((uint32_t)px[0] << 8) | px[0];
                break;
            case 2: /* RGB */
                v = 0xFF000000u | ((uint32_t)px[0] << 16) |
                    ((uint32_t)px[1] << 8) | px[2];
                break;
            case 4: /* Grayscale+Alpha */
                v = ((uint32_t)px[1] << 24) | ((uint32_t)px[0] << 16) |
                    ((uint32_t)px[0] << 8) | px[0];
                break;
            case 6: /* RGBA */
                v = ((uint32_t)px[3] << 24) | ((uint32_t)px[0] << 16) |
                    ((uint32_t)px[1] << 8) | px[2];
                break;
            default:
                v = 0xFF000000u;
                break;
            }
            argb[y * img_w + x] = v;
        }

        memcpy(prior_row, rp, (uint32_t)rowbytes);
        rp += rowbytes;
        consumed += rowbytes;
    }

    kfree(prior_row);
    kfree(raw_buf);

    /* Nearest-neighbour scale to out_w x out_h */
    for (int dy = 0; dy < out_h; dy++) {
        uint32_t sy = (uint32_t)dy * img_h / (uint32_t)out_h;
        if (sy >= img_h) sy = img_h - 1;
        for (int dx = 0; dx < out_w; dx++) {
            uint32_t sx = (uint32_t)dx * img_w / (uint32_t)out_w;
            if (sx >= img_w) sx = img_w - 1;
            out_pixels[dy * out_w + dx] = argb[sy * img_w + sx];
        }
    }

    kfree(argb);
    return 0;
}

/* ------------------------------------------------------------------
 * Placeholder renderer (kept for fallback when decode fails)
 * ------------------------------------------------------------------ */

static void draw_char_buf(uint32_t* buf, int buf_w, int buf_h,
                          int cx, int cy, char ch, uint32_t fg) {
    const unsigned char* glyph = font8x8[(unsigned char)ch];
    for (int row = 0; row < 8; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = cx + col;
                int py = cy + row;
                if (px >= 0 && px < buf_w && py >= 0 && py < buf_h)
                    buf[py * buf_w + px] = fg;
            }
        }
    }
}

static void draw_str_buf(uint32_t* buf, int buf_w, int buf_h,
                         int x, int y, const char* s, uint32_t fg) {
    while (*s) {
        draw_char_buf(buf, buf_w, buf_h, x, y, *s, fg);
        x += 8;
        s++;
    }
}

static uint32_t hash_str(const char* s) {
    uint32_t h = 0x811c9dc5u;
    while (*s) { h ^= (uint8_t)*s++; h *= 0x01000193u; }
    return h;
}

void png_render_placeholder(uint32_t* buf, int buf_w, int buf_h,
                            const png_info_t* info, const char* filename) {
    if (!buf || buf_w <= 0 || buf_h <= 0) return;

    uint32_t h = filename ? hash_str(filename) : 0xdeadbeef;
    uint32_t bg = 0xFF000000
                | (((h >> 16) & 0xFF) << 16)
                | (((h >>  8) & 0xFF) <<  8)
                | ( (h        & 0xFF));
    bg = ((bg & 0xFEFEFE) >> 1) | 0xFF111111;

    for (int i = 0; i < buf_w * buf_h; i++)
        buf[i] = bg;

    for (int x = 0; x < buf_w; x++) {
        buf[0 * buf_w + x] = 0xFFFFFF;
        buf[(buf_h - 1) * buf_w + x] = 0xFFFFFF;
    }
    for (int y = 0; y < buf_h; y++) {
        buf[y * buf_w + 0] = 0xFFFFFF;
        buf[y * buf_w + (buf_w - 1)] = 0xFFFFFF;
    }

    char label[64];
    if (info && (info->width || info->height)) {
        sprintf(label, "PNG %ux%u d=%u", info->width, info->height, info->depth);
    } else {
        sprintf(label, "PNG (unknown)");
    }

    int lx = (buf_w - (int)(strlen(label) * 8)) / 2;
    int ly = (buf_h - 8) / 2;
    if (lx < 2) lx = 2;
    if (ly < 2) ly = 2;

    draw_str_buf(buf, buf_w, buf_h, lx + 1, ly + 1, label, 0x000000);
    draw_str_buf(buf, buf_w, buf_h, lx, ly, label, 0xFFFFFF);

    if (filename) {
        int fx = (buf_w - (int)(strlen(filename) * 8)) / 2;
        if (fx < 2) fx = 2;
        draw_str_buf(buf, buf_w, buf_h, fx, ly + 12, filename, 0xCCCCCC);
    }
}
