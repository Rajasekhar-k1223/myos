from PIL import Image, ImageDraw

def make_icon(name, bg_color, fg_color, symbol):
    img = Image.new('RGB', (32, 32), (255, 0, 255)) # Magenta background
    draw = ImageDraw.Draw(img)
    # Draw rounded rect
    draw.rounded_rectangle([2, 2, 29, 29], radius=6, fill=bg_color)
    # Draw symbol (simple text)
    draw.text((10, 10), symbol, fill=fg_color)
    img.save(name)

make_icon("initrd/icon_term.bmp", (30, 30, 40), (100, 255, 100), ">_")
make_icon("initrd/icon_expl.bmp", (255, 200, 50), (255, 255, 255), "F")
make_icon("initrd/icon_sett.bmp", (100, 100, 100), (200, 200, 200), "S")
make_icon("initrd/icon_pnt.bmp", (255, 100, 100), (255, 255, 255), "P")

