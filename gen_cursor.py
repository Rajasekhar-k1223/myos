import struct
import math
import os

width = 16
height = 16

def write_bmp(filename, draw_func):
    pixels = bytearray(width * height * 3)
    
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * 3
            r, g, b = draw_func(x, y)
            pixels[idx] = max(0, min(255, b))
            pixels[idx+1] = max(0, min(255, g))
            pixels[idx+2] = max(0, min(255, r))
            
    # Width is 16 (multiple of 4), no padding needed
    file_size = 54 + len(pixels)
    bmp_header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54)
    dib_header = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    
    with open(f'/opt/apps/myos/initrd/{filename}', 'wb') as f:
        f.write(bmp_header)
        f.write(dib_header)
        f.write(pixels)

# Anti-aliased macOS/Ubuntu style pointer
def draw_cursor(x, y):
    # Base shape from previous array
    bitmap = [
        [1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
        [1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
        [1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0],
        [1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0],
        [1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0],
        [1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0],
        [1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0],
        [1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0],
        [1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0],
        [1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0],
        [1,2,2,2,2,2,1,1,1,1,0,0,0,0,0,0],
        [1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0],
        [1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0],
        [1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0],
        [1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0],
        [0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0],
    ]
    if y < 16 and x < 16:
        v = bitmap[y][x]
        if v == 1: return 0, 0, 0       # Black border
        if v == 2: return 255, 255, 255 # White fill
    return 0, 0, 0 # Transparent

write_bmp('cursor.bmp', draw_cursor)
print("Cursor generated!")
