from PIL import Image
import numpy as np
import os
import struct

# Define your predefined list of image files
image_files = [
    "greystone32x32.png",        # Texture 0
    "greystone32x32dark.png",    # Texture 1
    "redbrick32x32.png",         # Texture 2
    "redbrick32x32dark.png",     # Texture 3
    # "bluestone32x32.png",      # Texture 4
    # "bluestone32x32dark.png",  # Texture 5
    # "colorstone32x32.png",     # Texture 6
    # "colorstone32x32dark.png"  # Texture 7
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

def save_textures_to_binary(file_name, textures, texture_size):
    """Save textures as a binary file."""
    width, height = texture_size
    num_textures = len(textures)
    
    print(f"Creating binary texture file: {file_name}")
    print(f"  Texture size: {width}x{height}")
    print(f"  Number of textures: {num_textures}")
    print(f"  Total size: {num_textures * width * height} bytes")
    
    with open(file_name, 'wb') as f:
        # Write each texture sequentially
        for i, texture in enumerate(textures):
            print(f"  Writing texture {i}: {image_files[i]}")
            # Convert to bytes and write
            texture_bytes = bytes(texture)
            f.write(texture_bytes)
    
    print(f"Binary texture file created successfully!")

def generate_header_constants(output_file, texture_size, num_textures):
    """Generate a companion header file with texture constants."""
    width, height = texture_size
    
    header_content = f"#ifndef TEXTURE_DATA_H\n"
    header_content += f"#define TEXTURE_DATA_H\n\n"
    header_content += f"#include <stdint.h>\n\n"
    header_content += f"// Texture dimensions\n"
    header_content += f"#define texWidth {width}\n"
    header_content += f"#define texHeight {height}\n"
    header_content += f"#define NUM_TEXTURES {num_textures}\n\n"
    header_content += f"// Texture base address in XRAM (set this in CMakeLists.txt)\n"
    header_content += f"#define TEXTURE_BASE 0x10000\n\n"
    header_content += f"// Helper function to get texture pixel\n"
    header_content += f"inline uint8_t getTexturePixel(uint8_t texNum, uint16_t offset) {{\n"
    header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 10) + offset;\n"
    header_content += f"    RIA.step0 = 0;\n"
    header_content += f"    return RIA.rw0;\n"
    header_content += f"}}\n\n"
    header_content += f"// Optimized function to fetch entire texture column\n"
    header_content += f"extern uint8_t texColumnBuffer[{height}];\n"
    header_content += f"inline void fetchTextureColumn(uint8_t texNum, uint8_t texX) {{\n"
    header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 10) + texX;\n"
    header_content += f"    RIA.step0 = {width};\n"
    header_content += f"    for (uint8_t i = 0; i < {height}; ++i) {{\n"
    header_content += f"        texColumnBuffer[i] = RIA.rw0;\n"
    header_content += f"    }}\n"
    header_content += f"}}\n\n"
    header_content += f"#endif // TEXTURE_DATA_H\n"
    
    with open(output_file, 'w') as f:
        f.write(header_content)
    
    print(f"\nCompanion header file created: {output_file}")

def main():
    # Define the output binary file name
    output_binary = "textures.bin"
    output_header = "textures.h"
    texture_size = (32, 32)  # Define a fixed texture size (32x32)

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

    # Save all textures to a binary file
    save_textures_to_binary(output_binary, textures, texture_size)
    
    # Generate companion header file with constants and helper functions
    generate_header_constants(output_header, texture_size, len(textures))
    
    print("\n" + "="*60)
    print("Next steps:")
    print("="*60)
    print("1. Update CMakeLists.txt:")
    print("   rp6502_asset(raycast 0x11000 textures.bin)")
    print("   rp6502_executable(raycast textures.bin.rp6502 ...)")
    print("="*60)

if __name__ == "__main__":
    main()