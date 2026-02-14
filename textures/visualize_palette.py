import re
from PIL import Image, ImageDraw, ImageFont

def load_palette_from_h(filename="palette.h"):
    palette = []
    try:
        with open(filename, "r") as f:
            content = f.read()
            # Find hex values 0xXXXX
            matches = re.findall(r'0x([0-9A-Fa-f]{4})', content)
            for m in matches:
                val = int(m, 16)
                # Convert RP6502 RGB555 back to RGB888
                # Format: bbbbbgggggrrrrr
                r = (val & 0x1F) << 3
                g = ((val >> 6) & 0x1F) << 3
                b = ((val >> 11) & 0x1F) << 3
                palette.append((r, g, b))
    except FileNotFoundError:
        print(f"Error: {filename} not found.")
        return None
    return palette

def draw_palette_grid(palette):
    block_size = 40
    columns = 16
    rows = 16 # 16*16 = 256 colors
    
    img_w = columns * block_size
    img_h = rows * block_size
    
    img = Image.new("RGB", (img_w, img_h), "white")
    draw = ImageDraw.Draw(img)
    
    for i, color in enumerate(palette):
        col = i % columns
        row = i // columns
        
        x0 = col * block_size
        y0 = row * block_size
        x1 = x0 + block_size
        y1 = y0 + block_size
        
        # Draw color block
        draw.rectangle([x0, y0, x1, y1], fill=color, outline="black")
        
        # Determine text color (white for dark blocks, black for light)
        luminance = (0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2]) / 255
        text_col = "white" if luminance < 0.5 else "black"
        
        # Label with index
        draw.text((x0 + 2, y0 + 2), str(i), fill=text_col)

    # img.show()
    img.save("palette_preview.png")
    print("Saved preview to palette_preview.png")

if __name__ == "__main__":
    pal = load_palette_from_h("palette.h")
    if pal:
        draw_palette_grid(pal)