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

# def conv_tile(name_in, size, name_out):
#     with Image.open(name_in) as im:
#         with open("./" + name_out, "wb") as o:
#             rgb_im = im.convert("RGB")
#             im2 = rgb_im.resize(size=[size, size])
#             for y in range(0, im2.height):
#                 for x in range(0, im2.width):
#                     r, g, b = im2.getpixel((x, y))
#                     ansi_color = rgb_to_ansi(r, g, b)
#                     o.write(ansi_color.to_bytes(1, byteorder="little", signed=False))

def conv_spr(name_in, size_x, size_y, name_out):
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
conv_spr("textures/pixel-320x180.png", 320, 180, "textures/pixel-320x180.bin")
