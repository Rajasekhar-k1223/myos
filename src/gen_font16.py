#!/usr/bin/env python3
"""
Generate font16.h — an 8×16 bitmap font for ElseaOS.

Strategy:
  1. Try to parse Lat15-TerminusBold16.psf.gz (PSF1 or PSF2) for clean Latin glyphs.
  2. For any character not in the PSF file, fall back to scaling the existing
     font8x8 data by doubling each scanline row.
  3. Hand-craft the CP437 box-drawing characters (0xB9, 0xBA, 0xBB, 0xBC,
     0xC8, 0xC9, 0xCC, 0xCD) so the boot UI box looks correct at 8×16.
"""

import gzip, struct, sys, re

# ── 1. Load existing font8x8 data from font.h ───────────────────────────────
font8 = {}
try:
    with open('src/font.h', 'r') as f:
        content = f.read()
    # Match lines like: {0x3C, 0x66, ...}, /* N */
    for m in re.finditer(r'\{([^}]+)\},\s*/\*\s*(\d+)\s*\*/', content):
        idx = int(m.group(2))
        vals = [int(v.strip(), 16) for v in m.group(1).split(',')]
        font8[idx] = vals[:8]
except Exception as e:
    print(f"Warning: could not read font.h: {e}")

def scale8to16(rows8):
    """Double each row to convert 8×8 → 8×16."""
    out = []
    for row in rows8:
        out.append(row)
        out.append(row)
    return out[:16]

# ── 2. Parse PSF font ────────────────────────────────────────────────────────
psf_glyphs = {}   # codepoint → list of 16 bytes

PSF_PATH = '/usr/share/consolefonts/Lat15-TerminusBold16.psf.gz'
try:
    with gzip.open(PSF_PATH, 'rb') as gz:
        data = gz.read()

    magic = data[0:2]
    if magic == bytes([0x36, 0x04]):
        # PSF1
        charsize = data[3]          # bytes per glyph (= height)
        n_glyphs = 512 if (data[2] & 0x01) else 256
        offset   = 4
        for i in range(min(256, n_glyphs)):
            glyph = list(data[offset : offset + charsize])
            # Pad or trim to exactly 16 rows
            while len(glyph) < 16: glyph.append(0)
            psf_glyphs[i] = glyph[:16]
            offset += charsize
        print(f"Loaded PSF1: {len(psf_glyphs)} glyphs, charsize={charsize}")

    elif data[0:4] == bytes([0x72, 0xB5, 0x4A, 0x86]):
        # PSF2
        hdr_size   = struct.unpack_from('<I', data, 8)[0]
        n_glyphs   = struct.unpack_from('<I', data, 16)[0]
        bytes_per  = struct.unpack_from('<I', data, 20)[0]
        height     = struct.unpack_from('<I', data, 24)[0]
        width      = struct.unpack_from('<I', data, 28)[0]
        offset     = hdr_size
        bytes_row  = (width + 7) // 8   # bytes per row in file

        for i in range(min(256, n_glyphs)):
            rows = []
            for r in range(height):
                row_bytes = data[offset + r*bytes_row : offset + r*bytes_row + bytes_row]
                rows.append(row_bytes[0] if row_bytes else 0)
            while len(rows) < 16: rows.append(0)
            psf_glyphs[i] = rows[:16]
            offset += bytes_per
        print(f"Loaded PSF2: {len(psf_glyphs)} glyphs, {width}×{height}")
    else:
        print("Unknown PSF magic — using fallback scaling only")

except Exception as e:
    print(f"PSF load failed ({e}) — using fallback scaling for all glyphs")

# ── 3. CP437 box-drawing characters (hand-crafted 8×16) ─────────────────────
box = {
    # ╔  0xC9 — top-left double corner
    0xC9: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x1F,
           0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10],
    # ╗  0xBB — top-right double corner
    0xBB: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xF8,
           0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08],
    # ╚  0xC8 — bottom-left double corner
    0xC8: [0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
           0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    # ╝  0xBC — bottom-right double corner
    0xBC: [0x08,0x08,0x08,0x08,0x08,0x08,0x08,0xF8,
           0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    # ╠  0xCC — left tee double
    0xCC: [0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x1F,
           0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10],
    # ╣  0xB9 — right tee double
    0xB9: [0x08,0x08,0x08,0x08,0x08,0x08,0x08,0xF8,
           0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08],
    # ═  0xCD — horizontal double line (middle row)
    0xCD: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
           0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    # ║  0xBA — vertical double line
    0xBA: [0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24,
           0x24,0x24,0x24,0x24,0x24,0x24,0x24,0x24],
    # █  0xDB — full block
    0xDB: [0xFF]*16,
    # ░  0xB0 — light shade
    0xB0: [0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55,
           0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55],
    # ─  0xC4 — single horizontal
    0xC4: [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,
           0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],
    # │  0xB3 — single vertical
    0xB3: [0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
           0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08],
}

# ── 4. Build final 256-entry font ───────────────────────────────────────────
font16 = []
for i in range(256):
    if i in box:
        font16.append(box[i])
    elif i in psf_glyphs:
        font16.append(psf_glyphs[i])
    elif i in font8:
        font16.append(scale8to16(font8[i]))
    else:
        # Default: box outline
        font16.append([0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0x81,
                       0x81,0x81,0x81,0x81,0x81,0x81,0x81,0xFF])

# ── 5. Write font16.h ────────────────────────────────────────────────────────
out_path = 'src/font16.h'
with open(out_path, 'w') as f:
    f.write('#pragma once\n\n')
    f.write('/* 8x16 bitmap font for ElseaOS\n')
    f.write(' * Generated from Lat15-TerminusBold16.psf + CP437 box glyphs\n')
    f.write(' * Each character: 16 bytes, bit7=leftmost pixel */\n\n')
    f.write('static const unsigned char font8x16[256][16] = {\n')
    for i, rows in enumerate(font16):
        vals = ', '.join(f'0x{v:02X}' for v in rows)
        f.write(f'    {{{vals}}},  /* {i} */\n')
    f.write('};\n')

print(f"Written {out_path}  ({256} glyphs, 8x16)")
