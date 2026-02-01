#ifndef TEXTURE_DATA_H
#define TEXTURE_DATA_H

#include <stdint.h>

// Texture dimensions
#define texWidth 16
#define texHeight 16
#define NUM_TEXTURES 4

// Texture base address in XRAM (set this in CMakeLists.txt)
#define TEXTURE_BASE 0x1E100

// Helper function to get texture pixel
inline uint8_t getTexturePixel(uint8_t texNum, uint16_t offset) {
    RIA.addr0 = TEXTURE_BASE + (texNum << 8) + offset;
    RIA.step0 = 0;
    return RIA.rw0;
}

// Optimized function to fetch entire texture column
extern uint8_t texColumnBuffer[16];
inline void fetchTextureColumn(uint8_t texNum, uint8_t texX) {
    RIA.addr0 = TEXTURE_BASE + (texNum << 8) + texX;
    RIA.step0 = 16;
    for (uint8_t i = 0; i < 16; ++i) {
        texColumnBuffer[i] = RIA.rw0;
    }
}

#endif // TEXTURE_DATA_H
