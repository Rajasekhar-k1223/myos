#include "freetype.h"
#include "kheap.h"
#include "string.h"
#include "mathf.h"

/*
 * Include stb_truetype.h for type and function declarations only.
 * STB_TRUETYPE_IMPLEMENTATION is NOT defined here — ttf.c already compiled
 * all stbtt_ functions; we just need the struct definitions and prototypes.
 * The STBTT_ macros only need to be set for the allocator so that the
 * header-inlined code (if any) uses the same heap as the implementation.
 */
#define STBTT_malloc(x, u) ((void)(u), kmalloc(x))
#define STBTT_free(x, u)   ((void)(u), kfree(x))
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset
#define STBTT_strlen       strlen
#define STBTT_assert(x)    ((void)0)
/* Math stubs -- only needed during rasterisation (in ttf.c's compilation) */
#define STBTT_ifloor(x)    ((int)(x))
#define STBTT_iceil(x)     ((int)((x) + 0.9999f))
#define STBTT_sqrt(x)      sqrtf(x)
#define STBTT_pow(x, y)    (0)
#define STBTT_fmod(x, y)   fmodf(x, y)
#define STBTT_cos(x)       cosf(x)
#define STBTT_acos(x)      acosf(x)
#define STBTT_fabs(x)      fabsf(x)
#include "stb_truetype.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Read big-endian uint16 from a byte pointer. */
static unsigned short ft__u16be(const uint8_t* p) {
    return (unsigned short)(((unsigned)p[0] << 8) | p[1]);
}

/* Scan the TrueType offset table (at base[0]) for a named table.
   Returns the absolute byte offset from base, or 0 if not found. */
static int ft__find_table(const uint8_t* base, const char* tag) {
    unsigned num = ft__u16be(base + 4);
    for (unsigned i = 0; i < num; i++) {
        const uint8_t* e = base + 12 + i * 16;
        if (e[0] == (uint8_t)tag[0] && e[1] == (uint8_t)tag[1] &&
            e[2] == (uint8_t)tag[2] && e[3] == (uint8_t)tag[3])
            return (int)(((unsigned)e[8]<<24)|((unsigned)e[9]<<16)|((unsigned)e[10]<<8)|e[11]);
    }
    return 0;
}

/* Refresh the public face metrics for the current scale. */
static void ft__update_metrics(FT_Face face) {
    stbtt_fontinfo* info = (stbtt_fontinfo*)face->_priv.stbtt_info;
    float sc = face->_priv.scale;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    face->ascender  = (FT_Int)( asc  * sc * 64.0f);
    face->descender = (FT_Int)( desc * sc * 64.0f);
    face->height    = (FT_Int)((asc - desc + gap) * sc * 64.0f);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

FT_Error FT_Init_FreeType(FT_Library* alibrary) {
    if (!alibrary) return FT_Err_Invalid_Argument;
    static int s_sentinel = 1;
    *alibrary = (FT_Library)&s_sentinel;
    return FT_Err_Ok;
}

FT_Error FT_Done_FreeType(FT_Library library) {
    (void)library;
    return FT_Err_Ok;
}

FT_Error FT_New_Memory_Face(FT_Library library, const uint8_t* file_base,
                            long file_size, long face_index, FT_Face* aface) {
    (void)library; (void)file_size;
    if (!file_base || !aface) return FT_Err_Invalid_Argument;

    FT_FaceRec* face = (FT_FaceRec*)kmalloc(sizeof(FT_FaceRec));
    if (!face) return FT_Err_Out_Of_Memory;
    memset(face, 0, sizeof(*face));

    stbtt_fontinfo* info = (stbtt_fontinfo*)kmalloc(sizeof(stbtt_fontinfo));
    if (!info) { kfree(face); return FT_Err_Out_Of_Memory; }
    memset(info, 0, sizeof(*info));

    int offset = stbtt_GetFontOffsetForIndex(file_base, (int)face_index);
    if (offset < 0 || !stbtt_InitFont(info, file_base, offset)) {
        kfree(info); kfree(face);
        return FT_Err_Unknown_File_Format;
    }

    FT_GlyphSlotRec* slot = (FT_GlyphSlotRec*)kmalloc(sizeof(FT_GlyphSlotRec));
    if (!slot) { kfree(info); kfree(face); return FT_Err_Out_Of_Memory; }
    memset(slot, 0, sizeof(*slot));

    face->_priv.stbtt_info   = info;
    face->_priv.bitmap_buf   = NULL;
    face->_priv.pixel_height = 16;
    face->glyph              = slot;
    face->num_glyphs         = (FT_Long)info->numGlyphs;
    face->face_flags         = FT_FACE_FLAG_SCALABLE | FT_FACE_FLAG_HORIZONTAL;

    /* Read unitsPerEm directly from the 'head' table (offset 18, uint16 BE).
       info->data and info->fontstart give us the font's byte position.
       The table offset from ft__find_table is from the file start (not fontstart). */
    {
        const uint8_t* font_start = info->data + info->fontstart;
        int head_off = ft__find_table(font_start, "head");
        face->units_per_EM = (head_off > 0) ?
            (FT_Int)ft__u16be(font_start + head_off + 18) : 1000;
    }

    /* Default scale: 16px */
    face->_priv.scale = stbtt_ScaleForPixelHeight(info, 16.0f);
    ft__update_metrics(face);

    *aface = face;
    return FT_Err_Ok;
}

FT_Error FT_Done_Face(FT_Face face) {
    if (!face) return FT_Err_Invalid_Face_Handle;
    if (face->_priv.bitmap_buf) { kfree(face->_priv.bitmap_buf); face->_priv.bitmap_buf = NULL; }
    if (face->_priv.stbtt_info) { kfree(face->_priv.stbtt_info); }
    if (face->glyph)            { kfree(face->glyph); }
    kfree(face);
    return FT_Err_Ok;
}

/* ── Size setting ────────────────────────────────────────────────────────── */

FT_Error FT_Set_Pixel_Sizes(FT_Face face, FT_UInt pixel_width, FT_UInt pixel_height) {
    (void)pixel_width;
    if (!face) return FT_Err_Invalid_Face_Handle;
    int px = (pixel_height > 0) ? (int)pixel_height : (int)pixel_width;
    if (px <= 0) px = 16;
    stbtt_fontinfo* info = (stbtt_fontinfo*)face->_priv.stbtt_info;
    face->_priv.pixel_height = px;
    face->_priv.scale = stbtt_ScaleForPixelHeight(info, (float)px);
    ft__update_metrics(face);
    return FT_Err_Ok;
}

FT_Error FT_Set_Char_Size(FT_Face face, FT_Long char_width, FT_Long char_height,
                          FT_UInt horz_res, FT_UInt vert_res) {
    if (!face) return FT_Err_Invalid_Face_Handle;
    /* Convert 26.6 point size + DPI to pixels */
    FT_Long pts26 = char_height ? char_height : char_width;
    unsigned dpi  = vert_res  ? vert_res  : horz_res;
    if (dpi == 0) dpi = 72;
    /* pixels = (points/64) * dpi / 72 */
    int px = (int)((pts26 * (long)dpi) / (64L * 72L));
    if (px < 1) px = 1;
    return FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px);
}

/* ── Glyph loading ───────────────────────────────────────────────────────── */

FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong charcode) {
    if (!face) return 0;
    stbtt_fontinfo* info = (stbtt_fontinfo*)face->_priv.stbtt_info;
    int idx = stbtt_FindGlyphIndex(info, (int)(unsigned)charcode);
    return (idx < 0) ? 0 : (FT_UInt)idx;
}

FT_Error FT_Load_Glyph(FT_Face face, FT_UInt glyph_index, FT_Int32 load_flags) {
    if (!face) return FT_Err_Invalid_Face_Handle;
    stbtt_fontinfo* info = (stbtt_fontinfo*)face->_priv.stbtt_info;
    FT_GlyphSlot    slot = face->glyph;
    float sc = face->_priv.scale;

    /* Free the previous glyph bitmap if any */
    if (face->_priv.bitmap_buf) {
        kfree(face->_priv.bitmap_buf);
        face->_priv.bitmap_buf = NULL;
        slot->bitmap.buffer = NULL;
    }

    /* Horizontal metrics (in font units → 26.6 pixels) */
    int adv_w, lsb;
    stbtt_GetGlyphHMetrics(info, (int)glyph_index, &adv_w, &lsb);
    slot->advance.x = (FT_Pos)(adv_w * sc * 64.0f);
    slot->advance.y = 0;

    /* Glyph bounding box in font units */
    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBitmapBox(info, (int)glyph_index, sc, sc, &ix0, &iy0, &ix1, &iy1);
    int bw = ix1 - ix0;
    int bh = iy1 - iy0;

    /* Fill metrics in 26.6 */
    slot->metrics.width         = (FT_Pos)(bw * 64);
    slot->metrics.height        = (FT_Pos)(bh * 64);
    slot->metrics.horiBearingX  = (FT_Pos)(ix0 * 64);
    slot->metrics.horiBearingY  = (FT_Pos)(-iy0 * 64); /* iy0 is negative above baseline */
    slot->metrics.horiAdvance   = slot->advance.x;
    slot->metrics.vertBearingX  = 0;
    slot->metrics.vertBearingY  = 0;
    slot->metrics.vertAdvance   = 0;

    /* Render bitmap if requested (FT_LOAD_RENDER or no FT_LOAD_NO_BITMAP) */
    if (!(load_flags & FT_LOAD_NO_BITMAP) && bw > 0 && bh > 0) {
        int w, h, xoff, yoff;
        uint8_t* bm = stbtt_GetGlyphBitmap(info, sc, sc, (int)glyph_index,
                                            &w, &h, &xoff, &yoff);
        if (bm) {
            face->_priv.bitmap_buf   = bm;
            slot->bitmap.buffer      = bm;
            slot->bitmap.width       = w;
            slot->bitmap.rows        = h;
            slot->bitmap.pitch       = w;
            slot->bitmap.pixel_mode  = FT_PIXEL_MODE_GRAY;
            slot->bitmap.num_grays   = 255;
            /* xoff: pixels to the right of pen to reach bitmap left edge
               yoff: in stbtt coords (Y down), negative means bitmap top is
                     above the baseline.  FreeType bitmap_top = positive above baseline. */
            slot->bitmap_left = xoff;
            slot->bitmap_top  = -yoff;
        }
    } else {
        slot->bitmap.buffer = NULL;
        slot->bitmap.width  = 0;
        slot->bitmap.rows   = 0;
        slot->bitmap.pitch  = 0;
        slot->bitmap_left   = ix0;
        slot->bitmap_top    = -iy0;
    }

    return FT_Err_Ok;
}

FT_Error FT_Load_Char(FT_Face face, FT_ULong charcode, FT_Int32 load_flags) {
    FT_UInt gidx = FT_Get_Char_Index(face, charcode);
    return FT_Load_Glyph(face, gidx, load_flags);
}

FT_Error FT_Render_Glyph(FT_GlyphSlot slot, FT_Render_Mode render_mode) {
    (void)render_mode;
    /* Bitmap was already rendered inside FT_Load_Glyph (with FT_LOAD_RENDER
       or implicitly).  If the caller is asking after FT_LOAD_NO_BITMAP we
       can't satisfy that here — just return ok; the bitmap may be empty. */
    if (!slot) return FT_Err_Invalid_Argument;
    return FT_Err_Ok;
}
