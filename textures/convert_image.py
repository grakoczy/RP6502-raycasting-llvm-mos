#!/usr/bin/python3

from PIL import Image

# Generate the 256-color ANSI palette
def generate_ansi_palette():
    palette = []
    # Standard ANSI Colors (0-15)
    palette.extend([
        (0, 0, 0),       (128, 0, 0),       (0, 128, 0),       (128, 128, 0),
        (0, 0, 128),     (128, 0, 128),     (0, 128, 128),     (192, 192, 192),
        (128, 128, 128), (255, 0, 0),       (0, 255, 0),       (255, 255, 0),
        (0, 0, 255),     (255, 0, 255),     (0, 255, 255),     (255, 255, 255),
    ])
    
    # Extended 6x6x6 color cube (16-231)
    for r in [0, 95, 135, 175, 215, 255]:
        for g in [0, 95, 135, 175, 215, 255]:
            for b in [0, 95, 135, 175, 215, 255]:
                palette.append((r, g, b))
    
    # Grayscale Colors (232-255)
    for gray in range(24):
        level = 8 + gray * 10
        palette.append((level, level, level))
    
    return palette

ansi_palette = generate_ansi_palette()

def closest_ansi_color(r, g, b):
    """Find the closest ANSI color index to the given RGB value."""
    min_distance = float('inf')
    closest_index = 0
    for i, (cr, cg, cb) in enumerate(ansi_palette):
        r, g, b = int(r), int(g), int(b)
        cr, cg, cb = int(cr), int(cg), int(cb)
        distance = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
        if distance < min_distance:
            min_distance = distance
            closest_index = i
    return closest_index


def rp6502_rgb_tile_bpp4(r1,g1,b1,r2,g2,b2):
    return (((b1>>7)<<6)|((g1>>7)<<5)|((r1>>7)<<4)|((b2>>7)<<2)|((g2>>7)<<1)|(r2>>7))

def conv_tile(name_in, size, name_out):
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size,size])
            for y in range(0, im2.height):
                for x in range(0, im2.width, 2):
                    r1, g1, b1 = im2.getpixel((x, y))
                    r2, g2, b2 = im2.getpixel((x+1, y))
                    o.write(
                        rp6502_rgb_tile_bpp4(r1,g1,b1,r2,g2,b2).to_bytes(
                            1, byteorder="little", signed=False
                        )
                    )

def rp6502_rgb_sprite_bpp16(r,g,b):
    if r==0 and g==0 and b==0:
        return 0
    else:
        return ((((b>>3)<<11)|((g>>3)<<6)|(r>>3))|1<<5)

def conv_spr(name_in, size, name_out):
    print("converting: ", name_in)
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size,size])
            for y in range(0, im2.height):
                for x in range(0, im2.width):
                    r, g, b = im2.getpixel((x, y))
                    o.write(
                        rp6502_rgb_sprite_bpp16(r,g,b).to_bytes(
                            2, byteorder="little", signed=False
                        )
                    )

# convert whole image matching colors to closest ansi color
def conv_image(name_in, size_x, size_y, name_out):
    with Image.open(name_in) as im:
        with open("./" + name_out, "wb") as o:
            rgb_im = im.convert("RGB")
            im2 = rgb_im.resize(size=[size_x, size_y])
            for y in range(0, im2.height):
                for x in range(0, im2.width):
                    r, g, b = im2.getpixel((x, y))
                    color_index = closest_ansi_color(r, g, b)
                    # texture.append(color_index)
                    # r, g, b = im2.getpixel((x, y))
                    # ansi_color = rgb_to_ansi(r, g, b)
                    o.write(color_index.to_bytes(1))

# Example usage:
# conv_tile("input_image.png", 16, "output_tile.bin")
conv_image("textures/steampunk_control_panel_ansi_palette_320x180.png", 320, 180, "textures/pixel-320x180.bin")
# conv_spr("textures/compas_needle40x40.png", 32, "textures/compas_needle.bin")
