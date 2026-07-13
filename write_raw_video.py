import os
import struct
from PIL import Image

NUM_FRAMES = 100
WIDTH = 160
HEIGHT = 160

out = open('initrd/logo.raw', 'wb')

for i in range(1, NUM_FRAMES + 1):
    fname = f"initrd/logo_frames/logo_{i:03d}.bmp"
    if not os.path.exists(fname):
        print(f"Missing {fname}")
        break
    img = Image.open(fname).convert('RGB')
    
    # Write 160x160 pixels in 32-bit XRGB (0, R, G, B)
    # vesa color format is usually `(r << 16) | (g << 8) | b`
    # So as Little Endian uint32, it's [B, G, R, 0]
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = img.getpixel((x, y))
            out.write(bytes([b, g, r, 0]))

out.close()
print("Wrote logo.raw")
