#include "ttf.h"
#include "vector.h"
#include "kernel.h"

// Big-Endian read helpers
static uint16_t read16(uint8_t* p) { return (p[0] << 8) | p[1]; }
static uint32_t read32(uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static int16_t read16s(uint8_t* p) { return (int16_t)read16(p); }

static uint8_t* ttf_base = 0;
static uint32_t head_offset = 0;
static uint32_t maxp_offset = 0;
static uint32_t loca_offset = 0;
static uint32_t glyf_offset = 0;
static uint32_t cmap_offset = 0;

static int16_t  indexToLocFormat = 0;
static uint16_t numGlyphs = 0;

static uint32_t kern_offset = 0;
static uint16_t units_per_em = 1000;

uint16_t ttf_get_upm(void) { return units_per_em > 0 ? units_per_em : 1000; }

static uint32_t get_table_offset(const char* tag) {
    uint16_t numTables = read16(ttf_base + 4);
    uint8_t* table_dir = ttf_base + 12;
    for (int i = 0; i < numTables; i++) {
        uint8_t* entry = table_dir + i * 16;
        if (entry[0] == tag[0] && entry[1] == tag[1] && entry[2] == tag[2] && entry[3] == tag[3]) {
            return read32(entry + 8);
        }
    }
    return 0;
}

int ttf_is_loaded(void) { return ttf_base != 0; }

void ttf_init(uint8_t* font_data) {
    ttf_base = font_data;
    if (!ttf_base) return;

    head_offset = get_table_offset("head");
    maxp_offset = get_table_offset("maxp");
    loca_offset = get_table_offset("loca");
    glyf_offset = get_table_offset("glyf");
    cmap_offset = get_table_offset("cmap");

    if (!head_offset || !maxp_offset || !loca_offset || !glyf_offset || !cmap_offset) {
        terminal_printf("[TTF] Error: Missing essential tables\n");
        ttf_base = 0;
        return;
    }

    indexToLocFormat = read16s(ttf_base + head_offset + 50);
    numGlyphs        = read16(ttf_base + maxp_offset + 4);
    units_per_em     = read16(ttf_base + head_offset + 18);
    kern_offset      = get_table_offset("kern");

    terminal_printf("[TTF] Initialized. Glyphs=%d UPM=%d kern=%s\n",
                    numGlyphs, units_per_em, kern_offset ? "yes" : "no");
}

uint16_t ttf_get_glyph_index(uint16_t charcode) {
    if (!ttf_base) return 0;
    
    uint16_t numTables = read16(ttf_base + cmap_offset + 2);
    uint32_t subtable_offset = 0;
    
    // Find format 4 (Unicode) subtable
    for (int i = 0; i < numTables; i++) {
        uint8_t* record = ttf_base + cmap_offset + 4 + i * 8;
        uint16_t platformID = read16(record);
        uint16_t encodingID = read16(record + 2);
        if ((platformID == 3 && encodingID == 1) || (platformID == 0 && encodingID == 3)) {
            subtable_offset = read32(record + 4);
            break;
        }
    }
    
    if (!subtable_offset) return 0;
    
    uint8_t* table = ttf_base + cmap_offset + subtable_offset;
    uint16_t format = read16(table);
    if (format != 4) return 0; // Only support format 4 for now
    
    uint16_t segCountX2 = read16(table + 6);
    uint16_t segCount = segCountX2 / 2;
    
    uint8_t* endCountPtr = table + 14;
    uint8_t* startCountPtr = endCountPtr + segCountX2 + 2;
    uint8_t* idDeltaPtr = startCountPtr + segCountX2;
    uint8_t* idRangeOffsetPtr = idDeltaPtr + segCountX2;
    
    for (int i = 0; i < segCount; i++) {
        uint16_t endCount = read16(endCountPtr + i * 2);
        if (endCount >= charcode) {
            uint16_t startCount = read16(startCountPtr + i * 2);
            if (startCount <= charcode) {
                uint16_t idDelta = read16(idDeltaPtr + i * 2);
                uint16_t idRangeOffset = read16(idRangeOffsetPtr + i * 2);
                
                if (idRangeOffset == 0) {
                    return (charcode + idDelta) & 0xFFFF;
                } else {
                    uint32_t offset = (idRangeOffsetPtr + i * 2 - table) + idRangeOffset + (charcode - startCount) * 2;
                    uint16_t glyphIndex = read16(table + offset);
                    if (glyphIndex != 0) {
                        return (glyphIndex + idDelta) & 0xFFFF;
                    }
                    return 0;
                }
            }
            break;
        }
    }
    return 0;
}

// Minimal flag parser for simple glyphs
#define FLAG_ON_CURVE 1
#define FLAG_X_SHORT  2
#define FLAG_Y_SHORT  4
#define FLAG_REPEAT   8
#define FLAG_X_SAME   16
#define FLAG_Y_SAME   32

/* Recursion depth guard for compound glyphs — prevents infinite loops from malformed fonts */
static int ttf_compound_depth = 0;

void ttf_render_glyph(uint16_t glyph_index, float scale, float offset_x, float offset_y, uint32_t* buffer, int width, int height, uint32_t color) {
    if (!ttf_base || glyph_index >= numGlyphs) return;
    
    uint32_t glyph_offset;
    uint32_t next_glyph_offset;
    
    if (indexToLocFormat == 0) {
        glyph_offset = read16(ttf_base + loca_offset + glyph_index * 2) * 2;
        next_glyph_offset = read16(ttf_base + loca_offset + glyph_index * 2 + 2) * 2;
    } else {
        glyph_offset = read32(ttf_base + loca_offset + glyph_index * 4);
        next_glyph_offset = read32(ttf_base + loca_offset + glyph_index * 4 + 4);
    }
    
    if (glyph_offset == next_glyph_offset) return; // Empty glyph
    
    uint8_t* glyph = ttf_base + glyf_offset + glyph_offset;
    int16_t numberOfContours = read16s(glyph);
    if (numberOfContours < 0) {
        /* Compound glyph — render each component recursively */
        if (++ttf_compound_depth > 8) { ttf_compound_depth--; return; }
#define CG_ARG_1_2_WORDS    0x0001
#define CG_ARGS_ARE_XY      0x0002
#define CG_MORE_COMPONENTS  0x0020
#define CG_WE_HAVE_SCALE    0x0008
#define CG_WE_HAVE_XY_SCALE 0x0040
#define CG_WE_HAVE_2X2      0x0080
        uint8_t* component = glyph + 10;
        int more = 1;
        while (more) {
            uint16_t flags     = read16(component); component += 2;
            uint16_t glyph_idx = read16(component); component += 2;
            int16_t arg1, arg2;
            if (flags & CG_ARG_1_2_WORDS) {
                arg1 = read16s(component); component += 2;
                arg2 = read16s(component); component += 2;
            } else {
                arg1 = (int8_t)*component++;
                arg2 = (int8_t)*component++;
            }
            float xx = 1.0f, xy = 0.0f, yx = 0.0f, yy = 1.0f;
            if (flags & CG_WE_HAVE_SCALE) {
                xx = yy = (float)read16s(component) / 16384.0f; component += 2;
            } else if (flags & CG_WE_HAVE_XY_SCALE) {
                xx = (float)read16s(component) / 16384.0f; component += 2;
                yy = (float)read16s(component) / 16384.0f; component += 2;
            } else if (flags & CG_WE_HAVE_2X2) {
                xx = (float)read16s(component) / 16384.0f; component += 2;
                xy = (float)read16s(component) / 16384.0f; component += 2;
                yx = (float)read16s(component) / 16384.0f; component += 2;
                yy = (float)read16s(component) / 16384.0f; component += 2;
            }
            (void)xy; (void)yx;
            float dx = (flags & CG_ARGS_ARE_XY) ? (float)arg1 * scale : 0.0f;
            float dy = (flags & CG_ARGS_ARE_XY) ? (float)arg2 * scale : 0.0f;
            if (flags & (CG_WE_HAVE_SCALE | CG_WE_HAVE_XY_SCALE | CG_WE_HAVE_2X2)) {
                dx = dx * xx; dy = dy * yy;
            }
            ttf_render_glyph(glyph_idx, scale, offset_x + dx, offset_y - dy,
                             buffer, width, height, color);
            more = (flags & CG_MORE_COMPONENTS);
        }
        ttf_compound_depth--;
        return;
    }
    
    uint16_t* endPtsOfContours = (uint16_t*)(glyph + 10);
    uint16_t numPoints = read16((uint8_t*)&endPtsOfContours[numberOfContours - 1]) + 1;
    
    uint16_t instructionLength = read16((uint8_t*)&endPtsOfContours[numberOfContours]);
    uint8_t* flags_ptr = (uint8_t*)&endPtsOfContours[numberOfContours] + 2 + instructionLength;
    
    // We need to buffer points because flags are compressed
    uint8_t flags[numPoints];
    int flag_idx = 0;
    while (flag_idx < numPoints) {
        uint8_t flag = *flags_ptr++;
        flags[flag_idx++] = flag;
        if (flag & FLAG_REPEAT) {
            uint8_t repeat = *flags_ptr++;
            for (int r = 0; r < repeat && flag_idx < numPoints; r++) flags[flag_idx++] = flag;
        }
    }
    
    int16_t x[numPoints];
    int16_t y[numPoints];
    
    int16_t current_x = 0;
    uint8_t* coord_ptr = flags_ptr;
    for (int i = 0; i < numPoints; i++) {
        if (flags[i] & FLAG_X_SHORT) {
            uint8_t val = *coord_ptr++;
            current_x += (flags[i] & FLAG_X_SAME) ? val : -val;
        } else if (!(flags[i] & FLAG_X_SAME)) {
            current_x += read16s(coord_ptr);
            coord_ptr += 2;
        }
        x[i] = current_x;
    }
    
    int16_t current_y = 0;
    for (int i = 0; i < numPoints; i++) {
        if (flags[i] & FLAG_Y_SHORT) {
            uint8_t val = *coord_ptr++;
            current_y += (flags[i] & FLAG_Y_SAME) ? val : -val;
        } else if (!(flags[i] & FLAG_Y_SAME)) {
            current_y += read16s(coord_ptr);
            coord_ptr += 2;
        }
        y[i] = current_y;
    }
    
    // Convert to scaled screen coordinates
    float pts_x[numPoints];
    float pts_y[numPoints];
    for (int i = 0; i < numPoints; i++) {
        pts_x[i] = offset_x + x[i] * scale;
        // TTF Y-axis is up, Screen Y-axis is down
        pts_y[i] = offset_y - y[i] * scale;
    }
    
    // Render using Vector Engine
    vec_init();
    
    int pt_idx = 0;
    for (int c = 0; c < numberOfContours; c++) {
        int end_pt = read16((uint8_t*)&endPtsOfContours[c]);
        int start_pt = pt_idx;
        
        vec_move_to(pts_x[start_pt], pts_y[start_pt]);
        
        for (int i = start_pt + 1; i <= end_pt; i++) {
            int on_curve = flags[i] & FLAG_ON_CURVE;
            if (on_curve) {
                vec_line_to(pts_x[i], pts_y[i]);
            } else {
                // Next point must be on_curve (in simple TTF). If not, we compute midpoint.
                int next = (i == end_pt) ? start_pt : i + 1;
                float cx = pts_x[i];
                float cy = pts_y[i];
                float nx = pts_x[next];
                float ny = pts_y[next];
                
                if (!(flags[next] & FLAG_ON_CURVE)) {
                    nx = (cx + nx) / 2.0f;
                    ny = (cy + ny) / 2.0f;
                    // We don't advance i here, next iteration will handle the next off-curve or on-curve
                } else {
                    i++; // skip next since it's used as endpoint
                }
                vec_quad_to(cx, cy, nx, ny);
            }
        }
        // Close contour
        if (!(flags[end_pt] & FLAG_ON_CURVE)) {
            // Unhandled case: end point is off-curve
        } else {
            vec_line_to(pts_x[start_pt], pts_y[start_pt]);
        }
        
        pt_idx = end_pt + 1;
    }
    
    vec_fill(buffer, width, height, color);
}

/* Binary search kern table (format 0) for pair (left, right). Returns kern value in pixels. */
static int ttf_get_kern(uint16_t left, uint16_t right, int font_size) {
    if (!kern_offset || !units_per_em) return 0;
    uint8_t* kt = ttf_base + kern_offset;
    /* kern table: version(2), nTables(2) */
    uint16_t nTables = read16(kt + 2);
    uint8_t* sub = kt + 4;
    for (uint16_t t = 0; t < nTables; t++) {
        uint16_t length   = read16(sub + 2);
        uint16_t coverage = read16(sub + 4);
        uint16_t format   = coverage >> 8;
        if (format == 0 && (coverage & 1)) { /* horizontal kerning */
            uint16_t nPairs = read16(sub + 6);
            uint8_t* pairs  = sub + 14;
            /* binary search */
            int lo = 0, hi = (int)nPairs - 1;
            uint32_t key = ((uint32_t)left << 16) | right;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                uint16_t kl = read16(pairs + mid * 6);
                uint16_t kr = read16(pairs + mid * 6 + 2);
                uint32_t k  = ((uint32_t)kl << 16) | kr;
                if (k == key) {
                    int16_t val = read16s(pairs + mid * 6 + 4);
                    return (int)(val * font_size) / (int)units_per_em;
                }
                if (k < key) lo = mid + 1; else hi = mid - 1;
            }
        }
        sub += length;
    }
    return 0;
}

void ttf_draw_string(uint32_t* buffer, int width, int height, int x, int y, const char* str, int font_size, uint32_t color) {
    float upm   = (float)(units_per_em > 0 ? units_per_em : 1000);
    float scale = (float)font_size / upm;
    float cur_x = (float)x;
    uint16_t prev_gid = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        uint16_t gid = ttf_get_glyph_index((uint8_t)str[i]);
        if (kern_offset && prev_gid && gid)
            cur_x += (float)ttf_get_kern(prev_gid, gid, font_size);
        if (gid)
            ttf_render_glyph(gid, scale, cur_x, (float)y, buffer, width, height, color);
        cur_x += upm * scale;   /* = font_size px per char — proportional to actual em */
        prev_gid = gid;
    }
}
