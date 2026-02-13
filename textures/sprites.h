#ifndef SPRITE_DATA_H
#define SPRITE_DATA_H

#include <stdint.h>

// Sprite dimensions
#define spriteWidth 16
#define spriteHeight 16
#define NUM_SPRITES 3

// Sprite base address in XRAM (set this in CMakeLists.txt)
#define SPRITE_BASE 0x1E700

#define SPRITE_HAS_OPACITY_METADATA 1
#define SPRITE_BYTES_PER_SPRITE (spriteWidth * spriteHeight)
#define SPRITE_OPACITY_MASK_BYTES_PER_SPRITE (spriteWidth * 2)
#define SPRITE_OPACITY_MASK_BASE (SPRITE_BASE + (NUM_SPRITES * SPRITE_BYTES_PER_SPRITE))

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

inline uint16_t fetchSpriteColumnMask(uint8_t spriteNum, uint8_t spriteX) {
    RIA.addr0 = SPRITE_OPACITY_MASK_BASE + (uint16_t)(spriteNum * SPRITE_OPACITY_MASK_BYTES_PER_SPRITE) + ((uint16_t)spriteX << 1);
    RIA.step0 = 1;
    uint8_t lo = RIA.rw0;
    uint8_t hi = RIA.rw0;
    return (uint16_t)lo | ((uint16_t)hi << 8);
}
#endif // SPRITE_DATA_H
