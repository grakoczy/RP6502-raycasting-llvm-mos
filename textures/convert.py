from PIL import Image
import numpy as np
import os

# Define your predefined list of image files
image_files = [
    "greystone16x16.png",        # Texture 0
    "greystone16x16dark.png",         # Texture 1
    "redbrick16x16.png",        # Texture 2
    "redbrick16x16dark.png",        # Texture 3
    # "bluestone16x16.png",        # Texture 4
    # "bluestone16x16dark.png",        # Texture 5
    # "colorstone16x16.png",         # Texture 6
    # "colorstone16x16dark.png"           # Texture 7
]

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

def generate_texture_from_image(image_path, texture_size):
    # Load the image and resize it to match the texture size
    image = Image.open(image_path).convert('RGB').resize(texture_size)
    width, height = image.size
    pixels = np.array(image)

    # Create the texture array
    texture = []

    for y in range(height):
        for x in range(width):
            r, g, b = pixels[y, x]
            color_index = closest_ansi_color(r, g, b)
            texture.append(color_index)

    return texture

def save_textures_to_header(file_name, textures, texture_size):
    width, height = texture_size
    num_textures = len(textures)

    # Create a header file definition
    header_content = f"#ifndef TEXTURE_DATA_H\n#define TEXTURE_DATA_H\n\n"
    header_content += f"#include <stdint.h>\n\n"
    header_content += f"#define texWidth {width}\n"
    header_content += f"#define texHeight {height}\n"
    header_content += f"#define NUM_TEXTURES {num_textures}\n"
    header_content += f"uint8_t texture[NUM_TEXTURES][texWidth * texHeight] = {{\n"

    # Add each texture's data to the header file
    for i, texture in enumerate(textures):
        header_content += f"    {{  // Texture {image_files[i]}\n        "
        for j, color in enumerate(texture):
            if j % width == 0 and j != 0:
                header_content += "\n        "
            header_content += f"{color}, "
        header_content = header_content.rstrip(", ")
        header_content += "\n    },\n"

    header_content += "};\n\n"
    header_content += f"#endif // TEXTURE_DATA_H\n"

    # Save to file
    with open(file_name, 'w') as f:
        f.write(header_content)

def main():
    # Define the output header file name
    output_file = "textures.h"
    texture_size = (32, 32)  # Define a fixed texture size (e.g., 16x16)

    # Generate textures from the predefined list of images
    textures = []
    for image_path in image_files:
        if os.path.exists(image_path):
            print(f"Processing {image_path}...")
            texture = generate_texture_from_image(image_path, texture_size)
            textures.append(texture)
        else:
            print(f"Warning: {image_path} not found. Using a blank texture.")
            textures.append([0] * (texture_size[0] * texture_size[1]))  # Blank texture

    # Save all textures to a single header file
    save_textures_to_header(output_file, textures, texture_size)
    print(f"Texture header file generated: {output_file}")

if __name__ == "__main__":
    main()
