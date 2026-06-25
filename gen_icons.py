import struct
import math
import os

width = 32
height = 32

def write_bmp(filename, draw_func):
    pixels = bytearray(width * height * 3)
    
    for y in range(height):
        for x in range(width):
            idx = (y * width + x) * 3
            # BMP stores rows bottom-up. But our logic will just use y.
            r, g, b = draw_func(x, y)
            pixels[idx] = max(0, min(255, b))
            pixels[idx+1] = max(0, min(255, g))
            pixels[idx+2] = max(0, min(255, r))
            
    # Width is 32 (multiple of 4), no padding needed
    file_size = 54 + len(pixels)
    bmp_header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54)
    dib_header = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    
    with open(f'/opt/apps/myos/initrd/{filename}', 'wb') as f:
        f.write(bmp_header)
        f.write(dib_header)
        f.write(pixels)

# 1. Terminal Icon (Black box, green text prompt ">_")
def draw_terminal(x, y):
    if 2 <= x <= 29 and 2 <= y <= 29:
        if 5 <= x <= 12 and 15 <= y <= 22 and (x == y - 10 or x == 27 - y): # ">" shape
            return 0, 255, 0
        if 15 <= x <= 25 and 15 <= y <= 18: # "_" shape (bottom up coordinate! y is from bottom)
            return 0, 255, 0
        return 30, 30, 30
    return 100, 100, 100 # Border

# 2. Explorer Icon (Yellow folder)
def draw_explorer(x, y):
    # Base folder
    if 4 <= x <= 28 and 4 <= y <= 24:
        return 255, 200, 50
    # Folder tab
    if 4 <= x <= 14 and 25 <= y <= 28:
        return 255, 200, 50
    return 0, 0, 0 # Transparent background (we can mask it or leave black)

# 3. Settings Icon (Gear)
def draw_settings(x, y):
    cx, cy = 16, 16
    dist = math.sqrt((x-cx)**2 + (y-cy)**2)
    # Inner hole
    if dist < 4:
        return 0, 0, 0
    # Gear body
    if dist < 12:
        return 150, 150, 150
    # Teeth
    if dist < 15 and ((x+y) % 6 < 3 or abs(x-y) % 6 < 3):
        return 150, 150, 150
    return 0, 0, 0

# 4. Paint Icon (Palette / brush)
def draw_paint(x, y):
    cx, cy = 16, 16
    dist = math.sqrt((x-cx)**2 + (y-cy)**2)
    if dist < 12:
        # Paint blobs
        if math.sqrt((x-10)**2 + (y-20)**2) < 3: return 255, 0, 0
        if math.sqrt((x-20)**2 + (y-20)**2) < 3: return 0, 255, 0
        if math.sqrt((x-15)**2 + (y-10)**2) < 3: return 0, 0, 255
        return 200, 180, 150 # Wood palette
    # Thumb hole
    if math.sqrt((x-8)**2 + (y-10)**2) < 2:
        return 0, 0, 0
    return 0, 0, 0

os.makedirs('/opt/apps/myos/initrd', exist_ok=True)
write_bmp('icon_term.bmp', draw_terminal)
write_bmp('icon_expl.bmp', draw_explorer)
write_bmp('icon_sett.bmp', draw_settings)
write_bmp('icon_pnt.bmp', draw_paint)
print("Icons generated!")
