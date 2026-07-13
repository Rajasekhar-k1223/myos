import sys
from PIL import Image

try:
    img = Image.open("/home/ubuntu/.gemini/antigravity-ide/brain/89d680f8-28c2-4665-9f08-7274c8b90a88/step_0_welcome_1783423093303.png")
    
    # Crop the logo (approximate coordinates from 1024x1024 mockup)
    # Let's say logo is centered around x=512, y=512. Let's crop a 256x256 square around it.
    # Actually, we can look at the image size. If it's 1024x1024, the logo is roughly in the middle.
    
    width, height = img.size
    print(f"Original size: {width}x{height}")
    
    # We want a 160x160 logo. Let's crop 250x250 and resize to 160x160.
    cx, cy = width//2, height//2 - 20
    logo = img.crop((cx-180, cy-180, cx+180, cy+180))
    logo = logo.resize((160, 160), Image.Resampling.LANCZOS)
    
    # Convert to 24-bit BMP
    logo = logo.convert("RGB")
    logo.save("elsea_logo.bmp")
    print("Saved elsea_logo.bmp")
    
    # Crop a clean part of the background for wallpaper
    bg = img.crop((0, 0, width, height//4))
    bg = bg.resize((800, 600), Image.Resampling.LANCZOS)
    bg = bg.convert("RGB")
    bg.save("elsea_bg.bmp")
    print("Saved elsea_bg.bmp")
    
except Exception as e:
    print(f"Error: {e}")
