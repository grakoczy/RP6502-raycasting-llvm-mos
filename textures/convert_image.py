#!/usr/bin/python3

#!/usr/bin/python3
from PIL import Image
import numpy as np
import re

def load_palette():
    palette = []
    with open("palette.h", "r") as f:
        matches = re.findall(r'0x([0-9A-Fa-f]{4})', f.read())
        for m in matches:
            val = int(m, 16)
            palette.append(((val & 0x1F) << 3, ((val >> 6) & 0x1F) << 3, ((val >> 11) & 0x1F) << 3))
    return np.array(palette[48:]) # Only use the image part

def conv_image(name_in, size_x, size_y, name_out):
    img_palette = load_palette()
    with Image.open(name_in) as im:
        with open(name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size_x, size_y])
            for y in range(im2.height):
                for x in range(im2.width):
                    r, g, b = im2.getpixel((x, y))
                    # Find closest color in the 48-255 range
                    distances = np.sqrt(np.sum((img_palette - [r, g, b])**2, axis=1))
                    color_index = np.argmin(distances) + 48
                    o.write(int(color_index).to_bytes(1, 'little'))

# Example usage:
conv_image("steampunk_control_panel_ansi_palette_320x180.png", 320, 180, "pixel-320x180.bin")
