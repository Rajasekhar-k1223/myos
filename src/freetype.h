#pragma once
/*
 * FreeType 2 API compatibility shim for ElseaOS.
 *
 * Exposes the standard FreeType API (FT_Init_FreeType, FT_New_Memory_Face,
 * FT_Load_Char, FT_GlyphSlot, etc.) so that any code written against the real
 * FreeType library can be compiled here without modification.  The actual
 * rasterisation is delegated to stb_truetype, which is already proven in the
 * kernel via src/ttf.c and src/nvg_backend.c.
 *
 * Supported subset:
 *   ✓ FT_Init_FreeType / FT_Done_FreeType
 *   ✓ FT_New_Memory_Face / FT_Done_Face
 *   ✓ FT_Set_Pixel_Sizes / FT_Set_Char_Size
 *   ✓ FT_Get_Char_Index / FT_Load_Glyph / FT_Load_Char
 *   ✓ FT_Render_Glyph (NORMAL mode, 8-bit grey)
 *   ✓ slot->bitmap, bitmap_left, bitmap_top, advance
 *   ✓ face->ascender, descender, height, units_per_EM, num_glyphs
 *   ✗ Outlines, stroker, cache, LCD filtering, bitmap fonts, OpenType GSUB/GPOS
 */

#include <stdint.h>

/* ── Error codes ─────────────────────────────────────────────────────────── */
typedef int FT_Error;
#define FT_Err_Ok                   0
#define FT_Err_Cannot_Open_Resource 1
#define FT_Err_Unknown_File_Format  2
#define FT_Err_Invalid_Face_Handle  3
#define FT_Err_Invalid_Glyph_Index  4
#define FT_Err_Invalid_Argument     5
#define FT_Err_Out_Of_Memory        6

/* ── Primitive types ─────────────────────────────────────────────────────── */
typedef long           FT_Long;
typedef int            FT_Int;
typedef int            FT_Int32;
typedef unsigned int   FT_UInt;
typedef unsigned long  FT_ULong;
typedef unsigned char  FT_Byte;
typedef signed long    FT_Pos;      /* 26.6 fixed-point when used for coords */
typedef void*          FT_Library;  /* opaque handle */

/* ── Fixed-point vector (26.6) ───────────────────────────────────────────── */
typedef struct { FT_Pos x, y; } FT_Vector;

/* ── Pixel mode ──────────────────────────────────────────────────────────── */
#define FT_PIXEL_MODE_GRAY 2

/* ── Bitmap ──────────────────────────────────────────────────────────────── */
typedef struct {
    int            rows;
    int            width;
    int            pitch;          /* bytes per row; always width for our output */
    unsigned char* buffer;         /* 8-bit grey (0=transparent, 255=opaque) */
    unsigned char  pixel_mode;     /* FT_PIXEL_MODE_GRAY */
    unsigned char  num_grays;      /* 256 */
} FT_Bitmap;

/* ── Per-glyph metrics, all values in 26.6 fixed-point pixels ───────────── */
typedef struct {
    FT_Pos width,  height;
    FT_Pos horiBearingX, horiBearingY;
    FT_Pos horiAdvance;
    FT_Pos vertBearingX, vertBearingY;
    FT_Pos vertAdvance;
} FT_Glyph_Metrics;

/* ── Glyph slot ──────────────────────────────────────────────────────────── */
typedef struct FT_GlyphSlotRec_ {
    FT_Bitmap        bitmap;
    FT_Int           bitmap_left;  /* horizontal bearing in pixels */
    FT_Int           bitmap_top;   /* pixels from baseline to bitmap top (positive = above) */
    FT_Vector        advance;      /* advance in 26.6 */
    FT_Glyph_Metrics metrics;
} FT_GlyphSlotRec, *FT_GlyphSlot;

/* ── Face ────────────────────────────────────────────────────────────────── */
typedef struct FT_FaceRec_ {
    /* Public fields (match the layout callers expect) */
    FT_Long      num_glyphs;
    FT_Long      face_flags;
    FT_Int       units_per_EM;
    FT_Int       ascender;   /* 26.6 */
    FT_Int       descender;  /* 26.6, typically negative */
    FT_Int       height;     /* 26.6 line height */
    FT_GlyphSlot glyph;

    /* Private -- do not access from outside freetype.c */
    struct {
        void*         stbtt_info;   /* kmalloc'd stbtt_fontinfo */
        uint8_t*      bitmap_buf;   /* current glyph's bitmap (stbtt-allocated) */
        float         scale;        /* stbtt scale factor (pixels / font-unit) */
        int           pixel_height;
    } _priv;
} FT_FaceRec, *FT_Face;

/* ── Load flags ──────────────────────────────────────────────────────────── */
#define FT_LOAD_DEFAULT        0x0000
#define FT_LOAD_NO_SCALE       0x0001
#define FT_LOAD_NO_HINTING     0x0002
#define FT_LOAD_RENDER         0x0004
#define FT_LOAD_NO_BITMAP      0x0008
#define FT_LOAD_FORCE_AUTOHINT 0x0020
#define FT_LOAD_TARGET_NORMAL  0x000000
#define FT_LOAD_TARGET_LIGHT   (1 << 16)
#define FT_LOAD_TARGET_MONO    (2 << 16)
#define FT_LOAD_TARGET_LCD     (3 << 16)

/* ── Render mode ─────────────────────────────────────────────────────────── */
typedef enum {
    FT_RENDER_MODE_NORMAL = 0,
    FT_RENDER_MODE_LIGHT,
    FT_RENDER_MODE_MONO,
    FT_RENDER_MODE_LCD
} FT_Render_Mode;

/* ── Face flags ──────────────────────────────────────────────────────────── */
#define FT_FACE_FLAG_SCALABLE   (1L << 0)
#define FT_FACE_FLAG_HORIZONTAL (1L << 2)

/* ── 26.6 helpers ────────────────────────────────────────────────────────── */
#define FT_26DOT6(n)         ((FT_Pos)((n) * 64))
#define FT_26DOT6_TO_INT(n)  ((int)((n) >> 6))

/* ── API ─────────────────────────────────────────────────────────────────── */
FT_Error FT_Init_FreeType   (FT_Library* alibrary);
FT_Error FT_Done_FreeType   (FT_Library  library);

FT_Error FT_New_Memory_Face (FT_Library     library,
                             const uint8_t* file_base,
                             long           file_size,
                             long           face_index,
                             FT_Face*       aface);
FT_Error FT_Done_Face       (FT_Face face);

FT_Error FT_Set_Pixel_Sizes (FT_Face face,
                             FT_UInt pixel_width,
                             FT_UInt pixel_height);
FT_Error FT_Set_Char_Size   (FT_Face face,
                             FT_Long char_width,
                             FT_Long char_height,
                             FT_UInt horz_res,
                             FT_UInt vert_res);

FT_UInt  FT_Get_Char_Index  (FT_Face face, FT_ULong charcode);
FT_Error FT_Load_Glyph      (FT_Face face, FT_UInt glyph_index, FT_Int32 load_flags);
FT_Error FT_Load_Char       (FT_Face face, FT_ULong charcode, FT_Int32 load_flags);
FT_Error FT_Render_Glyph    (FT_GlyphSlot slot, FT_Render_Mode render_mode);
