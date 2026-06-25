import struct
import math

width = 128
height = 128
pixels = bytearray(width * height * 3)

# Generate a cool circular/gradient logo
center_x = width / 2
center_y = height / 2

for y in range(height):
    for x in range(width):
        idx = (y * width + x) * 3
        dx = x - center_x
        dy = y - center_y
        dist = math.sqrt(dx*dx + dy*dy)
        
        if dist < 50:
            # Inner circle (Cyan gradient)
            b = int(255)
            g = int(255 - dist * 2)
            r = int(100 + dist)
        elif dist < 60:
            # Border (White)
            b, g, r = 255, 255, 255
        else:
            # Background (Transparent/Black)
            b, g, r = 0, 0, 0
            
        # BMP stores pixels bottom-up, so we write BGR
        pixels[idx] = max(0, min(255, b))     # Blue
        pixels[idx+1] = max(0, min(255, g))   # Green
        pixels[idx+2] = max(0, min(255, r))   # Red

# Note: Width is a multiple of 4, so no row padding needed.
file_size = 54 + len(pixels)
bmp_header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54)
dib_header = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)

with open('initrd/logo.bmp', 'wb') as f:
    f.write(bmp_header)
    f.write(dib_header)
    f.write(pixels)
