import os
import numpy as np
from PIL import Image

# Config
IMAGE_FILES = ["steampunk_control_panel_320x180.png", "greystone32x32.png", "greystone32x32dark.png", "redbrick32x32.png", "redbrick32x32dark.png", "needle32x32.png", "barrel_16x16.png"]
ANSI_COUNT = 16
SKY_COUNT = 16
GRAY_COUNT = 32
IMAGE_COUNT = 256 - ANSI_COUNT - SKY_COUNT - GRAY_COUNT # 192 colors left

def generate_palette():
    palette = []
    
    # 1. Standard ANSI Colors (Indices 0-15)
    palette.extend([
        (0, 0, 0),       (128, 0, 0),     (0, 128, 0),     (128, 128, 0),
        (0, 0, 128),     (128, 0, 128),   (0, 128, 128),   (192, 192, 192),
        (128, 128, 128), (255, 0, 0),     (0, 255, 0),     (255, 255, 0),
        (0, 0, 255),     (255, 0, 255),   (0, 255, 255),   (255, 255, 255)
    ])

    # 2. Sky Gradient (Indices 16-31)
    for i in range(SKY_COUNT):
        r = int(0 + (135 - 0) * (i / (SKY_COUNT - 1)))
        g = int(20 + (206 - 20) * (i / (SKY_COUNT - 1)))
        b = int(60 + (250 - 60) * (i / (SKY_COUNT - 1)))
        palette.append((r, g, b))

    # 3. Gray Gradient (Indices 32-63)
    for i in range(GRAY_COUNT):
        v = int(255 * i / (GRAY_COUNT - 1))
        palette.append((v, v, v))

    # 4. Image Colors (Indices 64-255)
    all_px = []
    for f in IMAGE_FILES:
        if os.path.exists(f):
            img = Image.open(f).convert("RGB")
            all_px.append(np.array(img).reshape(-1, 3))
    
    if all_px:
        combined = Image.fromarray(np.vstack(all_px).reshape(1, -1, 3).astype('uint8'))
        quantized = combined.quantize(colors=IMAGE_COUNT, method=Image.MAXCOVERAGE)
        q_pal = quantized.getpalette()[:IMAGE_COUNT*3]
        for i in range(0, len(q_pal), 3):
            palette.append((q_pal[i], q_pal[i+1], q_pal[i+2]))
    
    while len(palette) < 256:
        palette.append((0, 0, 0))
        
    return palette

def write_header(pal):
    with open("palette.h", "w") as f:
        f.write("#ifndef PALETTE_H\n#define PALETTE_H\n#include <stdint.h>\n\n")
        f.write("const uint16_t custom_palette[256] = {\n")
        for r, g, b in pal:
            val = (((b >> 3) << 11) | ((g >> 3) << 6) | (r >> 3)) & 0xFFFF
            f.write(f"    0x{val:04X},\n")
        f.write("};\n\n#endif")

if __name__ == "__main__":
    write_header(generate_palette())
    print("palette.h generated: 0-15 ANSI, 16-31 Sky, 32-63 Gray, 64-255 Images")