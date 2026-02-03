#ifndef SPRITE_DATA_H
#define SPRITE_DATA_H

#include <stdint.h>

// Sprite dimensions
#define spriteWidth 16
#define spriteHeight 16
#define NUM_SPRITES 1

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
    for (uint8_t i = 0; i < 16; ++i) {
        sprColumnBuffer[i] = RIA.rw0;
    }
}

#endif // SPRITE_DATA_H
