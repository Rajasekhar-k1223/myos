from PIL import Image

try:
    img = Image.open("/home/ubuntu/.gemini/antigravity-ide/brain/89d680f8-28c2-4665-9f08-7274c8b90a88/media__1783433388643.jpg")
    img = img.resize((800, 600), Image.Resampling.LANCZOS)
    img = img.convert("RGB")
    img.save("isodir/elsea_bg.bmp")
    print("Successfully converted and saved elsea_bg.bmp!")
except Exception as e:
    print(f"Error: {e}")
