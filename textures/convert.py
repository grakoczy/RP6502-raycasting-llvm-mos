from PIL import Image
import numpy as np
import sys
import os

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
        # Convert to Python's int to avoid uint8 overflow issues
        r, g, b = int(r), int(g), int(b)
        cr, cg, cb = int(cr), int(cg), int(cb)

        # Euclidean distance in RGB space
        distance = (r - cr) ** 2 + (g - cg) ** 2 + (b - cb) ** 2
        if distance < min_distance:
            min_distance = distance
            closest_index = i
    return closest_index

def generate_texture_from_image(image_path):
    # Load the image
    image = Image.open(image_path).convert('RGB')
    width, height = image.size
    pixels = np.array(image)

    # Create the texture array
    texture = []

    for y in range(height):
        for x in range(width):
            r, g, b = pixels[y, x]
            color_index = closest_ansi_color(r, g, b)
            texture.append(color_index)

    return width, height, texture

def save_texture_to_header(file_name, width, height, texture):
    # Create a header file definition
    header_content = f"#ifndef TEXTURE_H\n#define TEXTURE_H\n\n"
    header_content += f"#include <stdint.h>\n\n"
    header_content += f"// Texture generated from image\n"
    header_content += f"#define TEXTURE_WIDTH {width}\n"
    header_content += f"#define TEXTURE_HEIGHT {height}\n"
    header_content += f"uint8_t texture[TEXTURE_WIDTH * TEXTURE_HEIGHT] = {{\n"

    # Add texture values to the header file
    for i, color in enumerate(texture):
        if i % width == 0:
            header_content += "\n    "
        header_content += f"{color}, "
    header_content = header_content.rstrip(", ")
    header_content += "\n};\n\n"
    header_content += f"#endif // TEXTURE_H\n"

    # Save to file
    with open(file_name, 'w') as f:
        f.write(header_content)

def main():
    if len(sys.argv) != 2:
        print("Usage: python generate_texture.py <input_image.png>")
        return

    image_path = sys.argv[1]
    output_file = os.path.splitext(os.path.basename(image_path))[0] + "_texture.h"

    width, height, texture = generate_texture_from_image(image_path)
    save_texture_to_header(output_file, width, height, texture)

    print(f"Texture header file generated: {output_file}")

if __name__ == "__main__":
    main()
