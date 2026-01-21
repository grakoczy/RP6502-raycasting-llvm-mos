#!/usr/bin/python3
from PIL import Image
import numpy as np
import re
import os

def load_palette():
    """Reads palette.h and returns only the 'Image Zone' (indices 64-255)"""
    palette = []
    if not os.path.exists("palette.h"):
        print("Error: palette.h not found. Generate it first!")
        exit(1)
        
    with open("palette.h", "r") as f:
        # Match hex values 0xXXXX
        matches = re.findall(r'0x([0-9A-Fa-f]{4})', f.read())
        for m in matches:
            val = int(m, 16)
            # Convert RGB555 back to RGB888 for distance calculation
            r = (val & 0x1F) << 3
            g = ((val >> 6) & 0x1F) << 3
            b = ((val >> 11) & 0x1F) << 3
            palette.append((r, g, b))
    
    # We skip ANSI (16), Sky (16), and Floor (32) = 64 total
    return np.array(palette[64:]) 

def conv_image(name_in, size_x, size_y, name_out):
    """Converts image to 8-bit indexed binary using the custom palette zone (64-255)"""
    img_palette = load_palette()
    
    print(f"Converting {name_in} to {name_out} ({size_x}x{size_y})...")
    
    with Image.open(name_in) as im:
        rgb_im = im.convert("RGB")
        im2 = rgb_im.resize((size_x, size_y))
        
        with open(name_out, "wb") as o:
            for y in range(im2.height):
                for x in range(im2.width):
                    r, g, b = im2.getpixel((x, y))
                    
                    # Find the closest color in the 192 colors we reserved for images
                    distances = np.sqrt(np.sum((img_palette - [r, g, b])**2, axis=1))
                    
                    # Argmin gives index 0-191. We add 64 to map it to palette indices 64-255
                    color_index = np.argmin(distances) + 64
                    
                    o.write(int(color_index).to_bytes(1, 'little'))
    print("Done.")

def rp6502_rgb_sprite_bpp16(r, g, b, a):
    """Convert RGBA to RP6502 sprite format (RGB555 with transparency)"""
    if a < 128:  # Transparent pixel
        return 0
    else:
        # RGB555 format with bit 5 set (non-transparent marker)
        return ((((b>>3)<<11)|((g>>3)<<6)|(r>>3))|1<<5)

def conv_sprite_bpp16(name_in, size, name_out):
    """Convert PNG to RP6502 BPP16 sprite format"""
    print(f"Converting {name_in} to {name_out} ({size}x{size})...")
    
    with Image.open(name_in) as im:
        with open(name_out, "wb") as o:
            rgba_im = im.convert("RGBA")
            im2 = rgba_im.resize(size=[size, size])
            
            transparent = 0
            colored = 0
            
            for y in range(0, im2.height):
                for x in range(0, im2.width):
                    r, g, b, a = im2.getpixel((x, y))
                    pixel_value = rp6502_rgb_sprite_bpp16(r, g, b, a)
                    
                    if pixel_value == 0:
                        transparent += 1
                    else:
                        colored += 1
                    
                    o.write(pixel_value.to_bytes(2, byteorder="little", signed=False))
            
            print(f"Done. Transparent: {transparent}, Colored: {colored}")
            print(f"File size: {size*size*2} bytes")

# Example usage (you can add this to the bottom or call from another script)
if __name__ == "__main__":
    # For backgrounds (no transparency needed)
    conv_image("steampunk_control_panel_320x180.png", 320, 180, "background-320x180.bin")
    
    # For sprites (WITH transparency support)
    conv_sprite_bpp16("needle-32x32.png", 32, "needle-32x32.bin")