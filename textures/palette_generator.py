import os
import numpy as np
from PIL import Image

# Config
IMAGE_FILES = ["steampunk_control_panel_ansi_palette_320x180.png", "greystone32x32.png", "greystone32x32dark.png", "redbrick32x32.png", "redbrick32x32dark.png"]
SKY_COUNT = 16
GRAY_COUNT = 32
IMAGE_COUNT = 208 # 256 - 16 - 32

def generate_palette():
    palette = []
    
    # 1. Sky Gradient (Indices 0-15): Dark Blue to Light Blue
    # Formula: start + (end - start) * (i / steps)
    for i in range(SKY_COUNT):
        r = int(0 + (135 - 0) * (i / (SKY_COUNT - 1)))
        g = int(20 + (206 - 20) * (i / (SKY_COUNT - 1)))
        b = int(60 + (250 - 60) * (i / (SKY_COUNT - 1)))
        palette.append((r, g, b))

    # 2. Gray Gradient (Indices 16-47): Black to White
    for i in range(GRAY_COUNT):
        v = int(255 * i / (GRAY_COUNT - 1))
        palette.append((v, v, v))

    # 3. Image Colors (Indices 48-255)
    all_px = []
    for f in IMAGE_FILES:
        if os.path.exists(f):
            img = Image.open(f).convert("RGB")
            all_px.append(np.array(img).reshape(-1, 3))
    
    if all_px:
        combined = Image.fromarray(np.vstack(all_px).reshape(1, -1, 3).astype('uint8'))
        # Using MAXCOVERAGE for better quality in retro palettes
        quantized = combined.quantize(colors=IMAGE_COUNT, method=Image.MAXCOVERAGE)
        q_pal = quantized.getpalette()[:IMAGE_COUNT*3]
        for i in range(0, len(q_pal), 3):
            palette.append((q_pal[i], q_pal[i+1], q_pal[i+2]))
    
    # Pad to 256 colors if necessary
    while len(palette) < 256:
        palette.append((0, 0, 0))
        
    return palette

def write_header(pal):
    with open("palette.h", "w") as f:
        f.write("#ifndef PALETTE_H\n#define PALETTE_H\n#include <stdint.h>\n\n")
        f.write("const uint16_t custom_palette[256] = {\n")
        for r, g, b in pal:
            # RP6502 RGB555: (((b>>3)<<11)|((g>>3)<<6)|(r>>3))
            # Mask with 0xFFFF to ensure it's strictly a 16-bit constant
            val = (((b >> 3) << 11) | ((g >> 3) << 6) | (r >> 3)) & 0xFFFF
            f.write(f"    0x{val:04X},\n")
        f.write("};\n\n#endif")

write_header(generate_palette())