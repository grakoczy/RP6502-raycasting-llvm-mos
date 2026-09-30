#ifndef TEXTURE_DATA_H
#define TEXTURE_DATA_H

#include <stdint.h>

// Texture dimensions
#define texWidth 32
#define texHeight 32
#define NUM_TEXTURES 4

// Texture base address in XRAM (set this in CMakeLists.txt)
#define TEXTURE_BASE 0x1E100

// Helper function to get texture pixel
inline uint8_t getTexturePixel(uint8_t texNum, uint16_t offset) {
    RIA.addr0 = TEXTURE_BASE + ((uint16_t)texNum << 10) + offset;
    RIA.step0 = 0;
    return RIA.rw0;
}

// Row y of texture column x is at offset (y << 5) + x
#define TEX_OFFSET(x, y) (((uint16_t)(y) << 5) + (x))

// Optimized function to fetch entire texture column
extern uint8_t texColumnBuffer[32];
inline void fetchTextureColumn(uint8_t texNum, uint8_t texX) {
    RIA.addr0 = TEXTURE_BASE + ((uint16_t)texNum << 10) + texX;
    RIA.step0 = 32;
    texColumnBuffer[0] = RIA.rw0;
    texColumnBuffer[1] = RIA.rw0;
    texColumnBuffer[2] = RIA.rw0;
    texColumnBuffer[3] = RIA.rw0;
    texColumnBuffer[4] = RIA.rw0;
    texColumnBuffer[5] = RIA.rw0;
    texColumnBuffer[6] = RIA.rw0;
    texColumnBuffer[7] = RIA.rw0;
    texColumnBuffer[8] = RIA.rw0;
    texColumnBuffer[9] = RIA.rw0;
    texColumnBuffer[10] = RIA.rw0;
    texColumnBuffer[11] = RIA.rw0;
    texColumnBuffer[12] = RIA.rw0;
    texColumnBuffer[13] = RIA.rw0;
    texColumnBuffer[14] = RIA.rw0;
    texColumnBuffer[15] = RIA.rw0;
    texColumnBuffer[16] = RIA.rw0;
    texColumnBuffer[17] = RIA.rw0;
    texColumnBuffer[18] = RIA.rw0;
    texColumnBuffer[19] = RIA.rw0;
    texColumnBuffer[20] = RIA.rw0;
    texColumnBuffer[21] = RIA.rw0;
    texColumnBuffer[22] = RIA.rw0;
    texColumnBuffer[23] = RIA.rw0;
    texColumnBuffer[24] = RIA.rw0;
    texColumnBuffer[25] = RIA.rw0;
    texColumnBuffer[26] = RIA.rw0;
    texColumnBuffer[27] = RIA.rw0;
    texColumnBuffer[28] = RIA.rw0;
    texColumnBuffer[29] = RIA.rw0;
    texColumnBuffer[30] = RIA.rw0;
    texColumnBuffer[31] = RIA.rw0;
}

#endif // TEXTURE_DATA_H
