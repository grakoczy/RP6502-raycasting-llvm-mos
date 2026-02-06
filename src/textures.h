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

// Optimized function to fetch entire texture column (unrolled for 6502 speed)
extern uint8_t texColumnBuffer[16];
inline void fetchTextureColumn(uint8_t texNum, uint8_t texX) {
    RIA.addr0 = TEXTURE_BASE + (texNum << 8) + texX;
    RIA.step0 = 16;
    texColumnBuffer[0]  = RIA.rw0;
    texColumnBuffer[1]  = RIA.rw0;
    texColumnBuffer[2]  = RIA.rw0;
    texColumnBuffer[3]  = RIA.rw0;
    texColumnBuffer[4]  = RIA.rw0;
    texColumnBuffer[5]  = RIA.rw0;
    texColumnBuffer[6]  = RIA.rw0;
    texColumnBuffer[7]  = RIA.rw0;
    texColumnBuffer[8]  = RIA.rw0;
    texColumnBuffer[9]  = RIA.rw0;
    texColumnBuffer[10] = RIA.rw0;
    texColumnBuffer[11] = RIA.rw0;
    texColumnBuffer[12] = RIA.rw0;
    texColumnBuffer[13] = RIA.rw0;
    texColumnBuffer[14] = RIA.rw0;
    texColumnBuffer[15] = RIA.rw0;
}

#endif // TEXTURE_DATA_H
