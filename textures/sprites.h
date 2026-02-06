#ifndef SPRITE_DATA_H
#define SPRITE_DATA_H

#include <stdint.h>

// Sprite dimensions
#define spriteWidth 16
#define spriteHeight 16
#define NUM_SPRITES 3

// Sprite base address in XRAM (set this in CMakeLists.txt)
#define SPRITE_BASE 0x1E500

// Helper function to get sprite pixel
inline uint8_t getSpritePixel(uint8_t spriteNum, uint16_t offset) {
    RIA.addr0 = SPRITE_BASE + (spriteNum << 8) + offset;
    RIA.step0 = 0;
    return RIA.rw0;
}

// Optimized function to fetch entire texture column
extern uint8_t sprColumnBuffer[16];
inline void fetchSpriteColumn(uint8_t spriteNum, uint8_t spriteX) {
    RIA.addr0 = SPRITE_BASE + (spriteNum << 8) + spriteX;
    RIA.step0 = 16;
    sprColumnBuffer[0]  = RIA.rw0;
    sprColumnBuffer[1]  = RIA.rw0;
    sprColumnBuffer[2]  = RIA.rw0;
    sprColumnBuffer[3]  = RIA.rw0;
    sprColumnBuffer[4]  = RIA.rw0;
    sprColumnBuffer[5]  = RIA.rw0;
    sprColumnBuffer[6]  = RIA.rw0;
    sprColumnBuffer[7]  = RIA.rw0;
    sprColumnBuffer[8]  = RIA.rw0;
    sprColumnBuffer[9]  = RIA.rw0;
    sprColumnBuffer[10] = RIA.rw0;
    sprColumnBuffer[11] = RIA.rw0;
    sprColumnBuffer[12] = RIA.rw0;
    sprColumnBuffer[13] = RIA.rw0;
    sprColumnBuffer[14] = RIA.rw0;
    sprColumnBuffer[15] = RIA.rw0;
}
#endif // SPRITE_DATA_H
