from PIL import Image

def process(fin, fout):
    img = Image.open(fin).convert("RGBA")
    img = img.resize((32, 32), Image.Resampling.LANCZOS)
    bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
    res = Image.alpha_composite(bg, img).convert("RGB")
    res.save(fout, format="BMP")

process("/home/ubuntu/.gemini/antigravity-ide/brain/6252f0f6-4b6e-4d22-a54b-c54ae4ad8127/icon_expl_1783689927246.png", "initrd/icon_expl.bmp")
process("/home/ubuntu/.gemini/antigravity-ide/brain/6252f0f6-4b6e-4d22-a54b-c54ae4ad8127/icon_sett_1783689944783.png", "initrd/icon_sett.bmp")
