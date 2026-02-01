from PIL import Image
import numpy as np
import os
import re

# Your predefined list of image files
image_files = [
    "greystone16x16.png",
    "greystone16x16dark.png",
    "redbrick16x16.png",
    "redbrick16x16dark.png"#,
    # "pillar16x16.png"
]

def load_palette_from_h(filename="palette.h"):
    """Parses the RGB555 values from palette.h and converts them back to RGB888."""
    palette = []
    try:
        with open(filename, "r") as f:
            content = f.read()
            # Find all hex values like 0x1234
            matches = re.findall(r'0x([0-9A-Fa-f]{4})', content)
            for m in matches:
                val = int(m, 16)
                # Convert RGB555 back to RGB888 for PIL comparison
                # b = (val >> 11) & 0x1F, g = (val >> 6) & 0x1F, r = val & 0x1F
                r = (val & 0x1F) << 3
                g = ((val >> 6) & 0x1F) << 3
                b = ((val >> 11) & 0x1F) << 3
                palette.append((r, g, b))
    except FileNotFoundError:
        print(f"Error: {filename} not found. Run the palette generator first!")
        exit(1)
    return palette

# Load the hardware palette
HW_PALETTE = load_palette_from_h("palette.h")

def closest_texture_color(r, g, b):
    """
    Finds the closest color in the 'Image Zone' of the palette.
    Skips the first 48 indices (Sky/Floor).
    """
    texture_slice = np.array(HW_PALETTE[64:])
    distances = np.sqrt(np.sum((texture_slice - [r, g, b])**2, axis=1))
    return np.argmin(distances) + 64

def generate_texture_from_image(image_path, size):
    with Image.open(image_path) as im:
        rgb_im = im.convert("RGB")
        im2 = rgb_im.resize(size)
        texture_data = []
        for y in range(size[1]):
            for x in range(size[0]):
                r, g, b = im2.getpixel((x, y))
                texture_data.append(closest_texture_color(r, g, b))
        return texture_data

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
    header_content += f"#define TEXTURE_BASE 0x1E100\n\n"
    header_content += f"// Helper function to get texture pixel\n"
    header_content += f"inline uint8_t getTexturePixel(uint8_t texNum, uint16_t offset) {{\n"
    # header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 10) + offset;\n"
    header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 8) + offset;\n"
    header_content += f"    RIA.step0 = 0;\n"
    header_content += f"    return RIA.rw0;\n"
    header_content += f"}}\n\n"
    header_content += f"// Optimized function to fetch entire texture column\n"
    header_content += f"extern uint8_t texColumnBuffer[{height}];\n"
    header_content += f"inline void fetchTextureColumn(uint8_t texNum, uint8_t texX) {{\n"
    # header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 10) + texX;\n"
    header_content += f"    RIA.addr0 = TEXTURE_BASE + (texNum << 8) + texX;\n"
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
    texture_size = (16, 16)  # Define a fixed texture size (16x16)

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
    print("   rp6502_asset(raycast 0x1E100 textures.bin)")
    print("   rp6502_executable(raycast textures.bin.rp6502 ...)")
    print("="*60)

if __name__ == "__main__":
    main()