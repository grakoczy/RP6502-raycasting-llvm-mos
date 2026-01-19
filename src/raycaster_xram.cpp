#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>

extern "C" {
    #include <unistd.h>
    #include <fcntl.h>
}

#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics.hpp"
#include "textures.h"
#include "FpF.hpp"
#include "maze.h"
#include "palette.h"

// ============================================================================
// XRAM Memory Map (now that 0x10000-0x1E0FF is free from pixel-320x180.bin)
// ============================================================================
#define XRAM_LINEHEIGHT_TABLE    0x10000  // 4KB (4096 bytes)
#define XRAM_TEXOFFSET_TABLE     0x11000  // 128 bytes (64 entries * 2 bytes)
#define XRAM_TEXSTEP_VALUES      0x11100  // ~220 bytes ((WINDOW_HEIGHT+1) * 2)
#define XRAM_ROTATION_DATA_START 0x11200  // Start of rotation data arrays

// Each rotation step needs storage for all columns
// Per rotation: 4 arrays * WINDOW_WIDTH * sizeof(FpF16<7>)
// = 4 * 96 * 2 = 768 bytes per rotation
// 32 rotations * 768 = 24,576 bytes total
#define BYTES_PER_ROTATION       768
#define XRAM_RAYDIRX_OFFSET      0
#define XRAM_RAYDIRY_OFFSET      (WINDOW_WIDTH * 2)
#define XRAM_DELTADX_OFFSET      (WINDOW_WIDTH * 4)
#define XRAM_DELTADY_OFFSET      (WINDOW_WIDTH * 6)

// Calculate XRAM address for specific rotation step
#define XRAM_ROTATION_ADDR(step) (XRAM_ROTATION_DATA_START + ((step) * BYTES_PER_ROTATION))

// ============================================================================
// Zero Page Variables (for fastest access in tight loops)
// ============================================================================
__attribute__((section(".zp.bss"))) static uint8_t zp_x;
__attribute__((section(".zp.bss"))) static uint8_t zp_y;
__attribute__((section(".zp.bss"))) static int zp_mapX;
__attribute__((section(".zp.bss"))) static int zp_mapY;
__attribute__((section(".zp.bss"))) static uint8_t zp_side;

using namespace mn::MFixedPoint;

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
#define WINDOW_WIDTH 96
#define WINDOW_HEIGTH 54
#define SCALE 2
#define MIN_SCALE 8
#define ROTATION_STEPS 32

// ============================================================================
// Game State Variables
// ============================================================================
FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1);
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0);
FpF16<7> moveSpeed(0.2);
FpF16<7> playerScale(5);
FpF16<7> sin_r(0.19509032201);
FpF16<7> cos_r(0.9807852804);

uint16_t startX = 151+(26-mapWidth)/2;
uint16_t startY = 137+(26-mapHeight)/2;

FpF16<7> prevDirX, prevDirY;
uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 1;
int8_t movementStep = 2;

uint8_t xOffset = 68;
uint8_t yOffset = 18;

uint16_t texSize = texWidth * texHeight -1;

const uint8_t w = WINDOW_WIDTH;
const uint8_t h = WINDOW_HEIGTH;

bool wireMode = false;
bool bigMode = true;

uint8_t buffer[WINDOW_HEIGTH * WINDOW_WIDTH];
uint8_t floorColors[WINDOW_HEIGTH];
uint8_t ceilingColors[WINDOW_HEIGTH];

bool gamestate_changed = true;
uint8_t gamestate = 1;
uint8_t gamestate_prev = 1;
#define GAMESTATE_INIT 0
#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
#define GAMESTATE_CALCULATING 3

uint8_t texColumnBuffer[32];
uint8_t currentRotStep = 1;

FpF16<7> invW;
FpF16<7> halfH(h / 2);

static const uint16_t texYOffsets[32] = {
    0, 32, 64, 96, 128, 160, 192, 224,
    256, 288, 320, 352, 384, 416, 448, 480,
    512, 544, 576, 608, 640, 672, 704, 736,
    768, 800, 832, 864, 896, 928, 960, 992
};

// ============================================================================
// RAM Buffers for Active Rotation (768 bytes total - only current rotation!)
// ============================================================================
FpF16<7> currentRayDirX[WINDOW_WIDTH];
FpF16<7> currentRayDirY[WINDOW_WIDTH];
FpF16<7> currentDeltaDistX[WINDOW_WIDTH];
FpF16<7> currentDeltaDistY[WINDOW_WIDTH];

// ============================================================================
// Keyboard Input
// ============================================================================
#define KEYBOARD_INPUT 0xFF10
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

#define PALETTE_XRAM_ADDR 0xF100

// ============================================================================
// Helper Functions
// ============================================================================

void load_custom_palette() {
    RIA.addr0 = PALETTE_XRAM_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 256; i++) {
        uint16_t color = custom_palette[i];
        RIA.rw0 = (uint8_t)(color & 0xFF); 
        RIA.rw0 = (uint8_t)(color >> 8);   
    }
    set_canvas_palette(PALETTE_XRAM_ADDR);
}

inline FpF16<7> fp_abs(FpF16<7> value) {
    if (value.GetRawVal() < 0) 
        return -value;
    return value;
} 

inline FpF16<7> floorFixed(FpF16<7> a) {
    int16_t rawVal = a.GetRawVal();
    if (rawVal >= 0) {
        return FpF16<7>::FromRaw(rawVal & 0xFF80);
    } else {
        int16_t resultRaw = (rawVal & 0xFF80);
        if (rawVal & 0x007F) {
            resultRaw -= (1 << 7);
        }
        return FpF16<7>::FromRaw(resultRaw);
    }
}

uint8_t mapValue(uint8_t value, uint8_t in_min, uint8_t in_max, uint8_t out_min, uint8_t out_max) {
    return out_min + ((value - in_min) * (out_max - out_min)) / (in_max - in_min);
}

inline int FP16ToIntPercent(FpF16<7> number) {
    return static_cast<int>((float)(number) * 100);
}

// ============================================================================
// XRAM Lookup Functions
// ============================================================================

inline uint8_t getLineHeight(int16_t rawDist) {
    if (rawDist < 0) return h;
    if (rawDist >= 4096) return 1;
    
    RIA.addr0 = XRAM_LINEHEIGHT_TABLE + rawDist;
    RIA.step0 = 0;
    return RIA.rw0;
}

inline int16_t getTexOffset(uint8_t lineHeight) {
    if (lineHeight == 0) return 0;
    if (lineHeight >= 64) return 0;
    
    uint16_t addr = XRAM_TEXOFFSET_TABLE + (lineHeight * 2);
    RIA.addr0 = addr;
    RIA.step0 = 1;
    int16_t val = RIA.rw0;
    val |= (RIA.rw0 << 8);
    return val;
}

inline FpF16<7> getTexStep(uint8_t lineHeight) {
    if (lineHeight == 0) return FpF16<7>(texHeight);
    if (lineHeight >= WINDOW_HEIGTH) lineHeight = WINDOW_HEIGTH;
    
    uint16_t addr = XRAM_TEXSTEP_VALUES + (lineHeight * 2);
    RIA.addr0 = addr;
    RIA.step0 = 1;
    int16_t val = RIA.rw0;
    val |= (RIA.rw0 << 8);
    return FpF16<7>::FromRaw(val);
}

// ============================================================================
// Load Rotation Data from XRAM to RAM
// ============================================================================

void loadRotationData(uint8_t rotStep) {
    uint16_t rotation_base = XRAM_ROTATION_ADDR(rotStep);
    
    // Load rayDirX
    RIA.addr0 = rotation_base + XRAM_RAYDIRX_OFFSET;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < WINDOW_WIDTH; i++) {
        int16_t val = RIA.rw0;
        val |= (RIA.rw0 << 8);
        currentRayDirX[i] = FpF16<7>::FromRaw(val);
    }
    
    // Load rayDirY
    RIA.addr0 = rotation_base + XRAM_RAYDIRY_OFFSET;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < WINDOW_WIDTH; i++) {
        int16_t val = RIA.rw0;
        val |= (RIA.rw0 << 8);
        currentRayDirY[i] = FpF16<7>::FromRaw(val);
    }
    
    // Load deltaDistX
    RIA.addr0 = rotation_base + XRAM_DELTADX_OFFSET;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < WINDOW_WIDTH; i++) {
        int16_t val = RIA.rw0;
        val |= (RIA.rw0 << 8);
        currentDeltaDistX[i] = FpF16<7>::FromRaw(val);
    }
    
    // Load deltaDistY
    RIA.addr0 = rotation_base + XRAM_DELTADY_OFFSET;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < WINDOW_WIDTH; i++) {
        int16_t val = RIA.rw0;
        val |= (RIA.rw0 << 8);
        currentDeltaDistY[i] = FpF16<7>::FromRaw(val);
    }
}

// ============================================================================
// Drawing Functions
// ============================================================================

void drawBufferScanline_2on1off() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        uint16_t addr1 = screen_addr;
        uint16_t addr2 = screen_addr + SCREEN_WIDTH;
        
        uint8_t* p = buffer_ptr;
        
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
            uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;

            RIA.addr0 = addr1;
            RIA.step0 = 1;
            RIA.rw0 = c0; RIA.rw0 = c0; RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2; RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4; RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6; RIA.rw0 = c7; RIA.rw0 = c7;
            addr1 += 16;

            RIA.addr0 = addr2;
            RIA.step0 = 1;
            RIA.rw0 = c0; RIA.rw0 = c0; RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2; RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4; RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6; RIA.rw0 = c7; RIA.rw0 = c7;
            addr2 += 16;
        }

        screen_addr += SCREEN_WIDTH * 3;
        buffer_ptr += WINDOW_WIDTH;
    }
}

void drawBufferScanline_1on1off() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr;
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
            uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;

            RIA.rw0 = c0; RIA.rw0 = c0;
            RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2;
            RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4;
            RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6;
            RIA.rw0 = c7; RIA.rw0 = c7;
        }

        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }
}

void drawBufferScanline_Interlaced() {
    static bool draw_odd = false;
    
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    
    if (draw_odd) {
        screen_addr += SCREEN_WIDTH;
    }

    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr;
        
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
            uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;

            RIA.rw0 = c0; RIA.rw0 = c0;
            RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2;
            RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4;
            RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6;
            RIA.rw0 = c7; RIA.rw0 = c7;
        }

        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }

    draw_odd = !draw_odd;
}

void drawBufferScanline_Mixed() {
    static bool draw_odd = false;
    
    const uint8_t h1 = h / 6;
    const uint8_t h2 = (h * 5) / 6;
    
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h1; ++j) {
        for (uint8_t row = 0; row < 2; ++row) {
            RIA.addr0 = screen_addr + (row * SCREEN_WIDTH);
            RIA.step0 = 1;
            uint8_t* p = buffer_ptr;
            for (uint8_t i = 0; i < w; i += 8) {
                uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
                uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;
                RIA.rw0 = c0; RIA.rw0 = c0; RIA.rw0 = c1; RIA.rw0 = c1;
                RIA.rw0 = c2; RIA.rw0 = c2; RIA.rw0 = c3; RIA.rw0 = c3;
                RIA.rw0 = c4; RIA.rw0 = c4; RIA.rw0 = c5; RIA.rw0 = c5;
                RIA.rw0 = c6; RIA.rw0 = c6; RIA.rw0 = c7; RIA.rw0 = c7;
            }
        }
        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }

    for (uint8_t j = h1; j < h2; ++j) {
        RIA.addr0 = screen_addr + (draw_odd ? SCREEN_WIDTH : 0);
        RIA.step0 = 1;
        uint8_t* p = buffer_ptr;
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
            uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;
            RIA.rw0 = c0; RIA.rw0 = c0; RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2; RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4; RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6; RIA.rw0 = c7; RIA.rw0 = c7;
        }
        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }

    for (uint8_t j = h2; j < h; ++j) {
        for (uint8_t row = 0; row < 2; ++row) {
            RIA.addr0 = screen_addr + (row * SCREEN_WIDTH);
            RIA.step0 = 1;
            uint8_t* p = buffer_ptr;
            for (uint8_t i = 0; i < w; i += 8) {
                uint8_t c0 = *p++; uint8_t c1 = *p++; uint8_t c2 = *p++; uint8_t c3 = *p++;
                uint8_t c4 = *p++; uint8_t c5 = *p++; uint8_t c6 = *p++; uint8_t c7 = *p++;
                RIA.rw0 = c0; RIA.rw0 = c0; RIA.rw0 = c1; RIA.rw0 = c1;
                RIA.rw0 = c2; RIA.rw0 = c2; RIA.rw0 = c3; RIA.rw0 = c3;
                RIA.rw0 = c4; RIA.rw0 = c4; RIA.rw0 = c5; RIA.rw0 = c5;
                RIA.rw0 = c6; RIA.rw0 = c6; RIA.rw0 = c7; RIA.rw0 = c7;
            }
        }
        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }

    draw_odd = !draw_odd;
}

void drawBuffer1to1() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr;
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++; uint8_t c1 = *p++; 
            uint8_t c2 = *p++; uint8_t c3 = *p++;
            uint8_t c4 = *p++; uint8_t c5 = *p++; 
            uint8_t c6 = *p++; uint8_t c7 = *p++;

            RIA.rw0 = c0; RIA.rw0 = c0;
            RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2;
            RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4;
            RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6;
            RIA.rw0 = c7; RIA.rw0 = c7;
        }

        screen_addr += SCREEN_WIDTH;
        buffer_ptr += WINDOW_WIDTH;
    }
}

void drawBufferDouble_v3() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        uint16_t addr1 = screen_addr;
        uint16_t addr2 = screen_addr + SCREEN_WIDTH;
        
        uint8_t* p = buffer_ptr;
        
        for (uint8_t i = 0; i < w; i += 8) {
            uint8_t c0 = *p++;
            uint8_t c1 = *p++;
            uint8_t c2 = *p++;
            uint8_t c3 = *p++;
            uint8_t c4 = *p++;
            uint8_t c5 = *p++;
            uint8_t c6 = *p++;
            uint8_t c7 = *p++;

            RIA.addr0 = addr1;
            RIA.step0 = 1;
            RIA.rw0 = c0; RIA.rw0 = c0;
            RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2;
            RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4;
            RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6;
            RIA.rw0 = c7; RIA.rw0 = c7;
            addr1 += 16;

            RIA.addr0 = addr2;
            RIA.step0 = 1;
            RIA.rw0 = c0; RIA.rw0 = c0;
            RIA.rw0 = c1; RIA.rw0 = c1;
            RIA.rw0 = c2; RIA.rw0 = c2;
            RIA.rw0 = c3; RIA.rw0 = c3;
            RIA.rw0 = c4; RIA.rw0 = c4;
            RIA.rw0 = c5; RIA.rw0 = c5;
            RIA.rw0 = c6; RIA.rw0 = c6;
            RIA.rw0 = c7; RIA.rw0 = c7;
            addr2 += 16;
        }

        screen_addr += SCREEN_WIDTH * 2;
        buffer_ptr += WINDOW_WIDTH;
    }
}

void drawBufferRegular() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    const uint16_t screen_stride = SCREEN_WIDTH;
    const uint8_t buffer_stride = WINDOW_WIDTH;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr;
        for (uint8_t i = 0; i < w; i += 8) {
            RIA.rw0 = p[0];
            RIA.rw0 = p[1];
            RIA.rw0 = p[2];
            RIA.rw0 = p[3];
            RIA.rw0 = p[4];
            RIA.rw0 = p[5];
            RIA.rw0 = p[6];
            RIA.rw0 = p[7];
            p += 8;
        }

        screen_addr += screen_stride;
        buffer_ptr += buffer_stride;
    }
}

// ============================================================================
// Precalculation Functions - Write to XRAM
// ============================================================================

void precalculateRotations() {
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    invW = FpF16<7>(1) / FpF16<7>(w);
    FpF16<7> fw = FpF16<7>(w);
    halfH = FpF16<7>(h >> 1);
    
    printf("Precalculating rotations to XRAM...\n");
    
    for (uint8_t i = 0; i < ROTATION_STEPS; i++) {
        printf(".");
        
        FpF16<7> oldDirX = currentDirX;
        currentDirX = currentDirX * cos_r - currentDirY * sin_r;
        currentDirY = oldDirX * sin_r + currentDirY * cos_r;

        FpF16<7> oldPlaneX = currentPlaneX;
        currentPlaneX = currentPlaneX * cos_r - currentPlaneY * sin_r;
        currentPlaneY = oldPlaneX * sin_r + currentPlaneY * cos_r;

        uint16_t rotation_base = XRAM_ROTATION_ADDR(i);
        
        // Temporary arrays for this rotation step
        FpF16<7> tempRayDirX[WINDOW_WIDTH];
        FpF16<7> tempRayDirY[WINDOW_WIDTH];
        FpF16<7> tempDeltaDistX[WINDOW_WIDTH];
        FpF16<7> tempDeltaDistY[WINDOW_WIDTH];
        
        // Calculate all values for this rotation
        for(uint8_t x = 0; x < w; x += currentStep) {
            FpF16<7> cameraX = FpF16<7>(2 * x) / fw - FpF16<7>(1);
            FpF16<7> rayDirX = currentDirX + currentPlaneX * cameraX;
            FpF16<7> rayDirY = currentDirY + currentPlaneY * cameraX;

            FpF16<7> deltaDistX, deltaDistY;

            if (rayDirX == 0 || (rayDirX.GetRawVal() == -261)) { 
                deltaDistX = 127;
            } else { 
                deltaDistX = fp_abs(FpF16<7>(1) / rayDirX); 
            }
            if (rayDirY == 0 || (rayDirY.GetRawVal() == -261)) { 
                deltaDistY = 127;
            } else { 
                deltaDistY = fp_abs(FpF16<7>(1) / rayDirY); 
            }
            
            tempRayDirX[x] = rayDirX;
            tempRayDirY[x] = rayDirY;
            tempDeltaDistX[x] = deltaDistX;
            tempDeltaDistY[x] = deltaDistY;
        }
        
        // Write rayDirX to XRAM
        RIA.addr0 = rotation_base + XRAM_RAYDIRX_OFFSET;
        RIA.step0 = 1;
        for(uint8_t x = 0; x < w; x++) {
            int16_t val = tempRayDirX[x].GetRawVal();
            RIA.rw0 = (uint8_t)(val & 0xFF);
            RIA.rw0 = (uint8_t)(val >> 8);
        }
        
        // Write rayDirY to XRAM
        RIA.addr0 = rotation_base + XRAM_RAYDIRY_OFFSET;
        RIA.step0 = 1;
        for(uint8_t x = 0; x < w; x++) {
            int16_t val = tempRayDirY[x].GetRawVal();
            RIA.rw0 = (uint8_t)(val & 0xFF);
            RIA.rw0 = (uint8_t)(val >> 8);
        }
        
        // Write deltaDistX to XRAM
        RIA.addr0 = rotation_base + XRAM_DELTADX_OFFSET;
        RIA.step0 = 1;
        for(uint8_t x = 0; x < w; x++) {
            int16_t val = tempDeltaDistX[x].GetRawVal();
            RIA.rw0 = (uint8_t)(val & 0xFF);
            RIA.rw0 = (uint8_t)(val >> 8);
        }
        
        // Write deltaDistY to XRAM
        RIA.addr0 = rotation_base + XRAM_DELTADY_OFFSET;
        RIA.step0 = 1;
        for(uint8_t x = 0; x < w; x++) {
            int16_t val = tempDeltaDistY[x].GetRawVal();
            RIA.rw0 = (uint8_t)(val & 0xFF);
            RIA.rw0 = (uint8_t)(val >> 8);
        }
    }

    printf("\nDone writing rotations to XRAM\n");
}

void precalculateLineHeights() {
    printf("Precalculating line heights to XRAM...\n");
    
    // Write expanded lineheight table to XRAM (4096 entries)
    RIA.addr0 = XRAM_LINEHEIGHT_TABLE;
    RIA.step0 = 1;
    RIA.rw0 = h; // Entry 0
    
    for (int i = 1; i < 4096; ++i) {
        FpF16<7> dist = FpF16<7>::FromRaw(i);
        FpF16<7> heightFp = FpF16<7>(h) / dist;
        int height = (int)heightFp;
        
        if (height > h || height < 0) {
            height = h;
        }
        
        RIA.rw0 = (uint8_t)height;
    }
    
    printf("Precalculating tex offsets to XRAM...\n");
    RIA.addr0 = XRAM_TEXOFFSET_TABLE;
    RIA.step0 = 1;
    
    for (int i = 0; i < 64; i++) {
        int16_t offset = (i == 0) ? 0 : (2048 - (int16_t)(110592L / i));
        if (offset < 0) offset = 0;
        
        RIA.rw0 = (uint8_t)(offset & 0xFF);
        RIA.rw0 = (uint8_t)(offset >> 8);
    }
    
    printf("Precalculating tex step values to XRAM...\n");
    RIA.addr0 = XRAM_TEXSTEP_VALUES;
    RIA.step0 = 1;
    
    // Entry 0 (avoid division by zero)
    int16_t val = FpF16<7>(texHeight).GetRawVal();
    RIA.rw0 = (uint8_t)(val & 0xFF);
    RIA.rw0 = (uint8_t)(val >> 8);
    
    for (uint8_t i = 1; i <= WINDOW_HEIGTH; i++) {
        FpF16<7> step = FpF16<7>(texHeight) / FpF16<7>(i);
        val = step.GetRawVal();
        RIA.rw0 = (uint8_t)(val & 0xFF);
        RIA.rw0 = (uint8_t)(val >> 8);
    }
    
    printf("Done\n");
}

// ============================================================================
// Raycasting Function - Optimized with XRAM lookups and RAM buffers
// ============================================================================

int raycastF() {
    // Load current rotation from XRAM to RAM (only when rotation changes!)
    static uint8_t lastRotStep = 255;
    if (currentRotStep != lastRotStep) {
        loadRotationData(currentRotStep);
        lastRotStep = currentRotStep;
    }
    
    // Use RAM pointers for fast access
    FpF16<7>* rayDirXPtr = currentRayDirX;
    FpF16<7>* rayDirYPtr = currentRayDirY;
    FpF16<7>* deltaDistXPtr = currentDeltaDistX;
    FpF16<7>* deltaDistYPtr = currentDeltaDistY;
    
    const int posXInt = (int)posX;
    const int posYInt = (int)posY;
    const uint8_t stepSize = currentStep;
    
    const uint8_t* floorColorPtr = floorColors;
    const uint8_t* ceilingColorPtr = ceilingColors;
    
    for (zp_x = 0; zp_x < w; zp_x += currentStep)
    {
        FpF16<7> rayDirX = rayDirXPtr[zp_x];
        FpF16<7> rayDirY = rayDirYPtr[zp_x];
        FpF16<7> deltaDistX = deltaDistXPtr[zp_x];
        FpF16<7> deltaDistY = deltaDistYPtr[zp_x];
        
        int16_t rayDirXRaw = rayDirX.GetRawVal();
        int16_t rayDirYRaw = rayDirY.GetRawVal();
        
        zp_mapX = posXInt;
        zp_mapY = posYInt;

        FpF16<7> sideDistX, sideDistY;
        int stepX, stepY;

        if(rayDirXRaw < 0) {
            stepX = -1;
            sideDistX = (posX - FpF16<7>(zp_mapX)) * deltaDistX;
        } else {
            stepX = 1;
            sideDistX = (FpF16<7>(zp_mapX + 1) - posX) * deltaDistX;
        }
        
        if(rayDirYRaw < 0) {
            stepY = -1;
            sideDistY = (posY - FpF16<7>(zp_mapY)) * deltaDistY;
        } else {
            stepY = 1;
            sideDistY = (FpF16<7>(zp_mapY + 1) - posY) * deltaDistY;
        }
        
        int16_t sideDistXRaw = sideDistX.GetRawVal();
        int16_t sideDistYRaw = sideDistY.GetRawVal();
        int16_t deltaDistXRaw = deltaDistX.GetRawVal();
        int16_t deltaDistYRaw = deltaDistY.GetRawVal();
        
        while(worldMap[zp_mapX][zp_mapY] == 0) {
            if(sideDistXRaw < sideDistYRaw) {
                sideDistXRaw += deltaDistXRaw;
                zp_mapX += stepX;
                zp_side = 0;
            } else {
                sideDistYRaw += deltaDistYRaw;
                zp_mapY += stepY;
                zp_side = 1;
            }
        }

        int16_t rawDist = (zp_side == 0) ? 
            (sideDistXRaw - deltaDistXRaw) : 
            (sideDistYRaw - deltaDistYRaw);
        
        // Use XRAM lookup for line height
        uint8_t lineHeight = getLineHeight(rawDist);
        
        uint8_t texNum = (worldMap[zp_mapX][zp_mapY] - 1) * 2 + zp_side;
        int8_t drawStart = (-lineHeight >> 1) + (h >> 1);
        if (drawStart < 0) drawStart = 0;
        uint8_t drawEnd = drawStart + lineHeight;
        if (drawEnd > h) drawEnd = h;

        FpF16<7> perpWallDist = FpF16<7>::FromRaw(rawDist);
        FpF16<7> wallX = (zp_side == 0) ? 
            (posY + perpWallDist * rayDirY) : 
            (posX + perpWallDist * rayDirX);
        wallX -= floorFixed(wallX);
        
        int texX = (int)(wallX * FpF16<7>(texWidth));
        if(zp_side == 0 && rayDirXRaw > 0) texX = texWidth - texX - 1;
        if(zp_side == 1 && rayDirYRaw < 0) texX = texWidth - texX - 1;

        fetchTextureColumn(texNum, texX);
        
        // Use XRAM lookup for tex offset and step
        int16_t raw_texPos = getTexOffset(lineHeight);
        FpF16<7> texStep = getTexStep(lineHeight);
        int16_t raw_step = texStep.GetRawVal();
        
        if (raw_texPos < 0) raw_texPos = 0;

        uint8_t* bufPtr = &buffer[zp_x];
        uint8_t color;
        
        if (stepSize == 1) {
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                *bufPtr = ceilingColorPtr[zp_y];
                bufPtr += w;
            }
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                *bufPtr = texColumnBuffer[texY];
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                *bufPtr = floorColorPtr[zp_y];  
                bufPtr += w;
            }
        } 
        else if (stepSize == 2) {
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                uint8_t c = ceilingColorPtr[zp_y]; 
                bufPtr[0] = c;
                bufPtr[1] = c;
                bufPtr += w;
            }
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                color = texColumnBuffer[texY];
                bufPtr[0] = color;
                bufPtr[1] = color;
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                color = floorColorPtr[zp_y];  
                bufPtr[0] = color;
                bufPtr[1] = color;
                bufPtr += w;
            }
        }
    }
    
    if (!bigMode) {
        stepSize == 1 ? drawBufferDouble_v3() : drawBufferScanline_Interlaced();
    } else {
        drawBufferDouble_v3();
    }
    return 0;
}

// ============================================================================
// UI and Map Drawing Functions
// ============================================================================

void print_map() {
    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
            switch (worldMap[i][j]) {
                case 0: printf(" "); break;
                case 1: printf("#"); break;
                case 2: printf("x"); break;
                case 3: printf("O"); break;
                case 4: printf("."); break;
                case 5: printf("@"); break;
                default: printf("?"); break;
            }
        }
        printf("\n");
    }
}

void draw_ui() {
    fill_rect_fast(DARK_GREEN, 150, 136, 28, 28);
}

void draw_map() {
    FpF16<7> ts(TILE_SIZE);

    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
            if (worldMap[i][j] > 0) {
                uint8_t color;
                switch(worldMap[i][j]) {
                    case 1: color = 40; break;
                    case 2: color = RED; break;
                    case 3: color = BLUE; break;
                    case 4: color = WHITE; break;
                    case 5: color = YELLOW; break;
                }
                draw_rect(color, i * TILE_SIZE + startX, j * TILE_SIZE + startY, TILE_SIZE, TILE_SIZE);
            } else {
                draw_rect(DARK_GREEN, i * TILE_SIZE + startX, j * TILE_SIZE + startY, TILE_SIZE, TILE_SIZE);
            }
        }
    }
} 

void draw_needle() {
    FpF16<7> l(12);
    FpF16<7> ts(TILE_SIZE);

    uint16_t x = 293;
    uint16_t y = 50;

    int8_t lX = (int)(dirX * l);
    int8_t lY = (int)(dirY * l);
    int8_t plX = (int)(prevDirX * l);
    int8_t plY = (int)(prevDirY * l);

    FpF16<7> arrowWidth(2);
    int8_t arrowX1 = (int)(-dirY * arrowWidth);
    int8_t arrowY1 = (int)(dirX * arrowWidth);
    int8_t prevArrowX1 = (int)(-prevDirY * arrowWidth);
    int8_t prevArrowY1 = (int)(prevDirX * arrowWidth);

    draw_line(93, x + prevArrowX1 , y + prevArrowY1, x + plX, y + plY);
    draw_line(93, x - prevArrowX1 , y - prevArrowY1, x + plX, y + plY);
    draw_line(93, x + prevArrowX1 , y + prevArrowY1, x - plX, y - plY);
    draw_line(93, x - prevArrowX1 , y - prevArrowY1, x - plX, y - plY);

    draw_line(9, x + arrowX1 , y + arrowY1, x + lX, y + lY);
    draw_line(9, x - arrowX1 , y - arrowY1, x + lX, y + lY);
    draw_line(4, x + arrowX1 , y + arrowY1, x - lX, y - lY);
    draw_line(4, x - arrowX1 , y - arrowY1, x - lX, y - lY);
    draw_pixel(YELLOW, x, y);

    prevDirX = dirX;
    prevDirY = dirY;
}

void draw_player(){
    FpF16<7> ts(TILE_SIZE);

    uint16_t x = (int)(posX * ts) + startX;
    uint16_t y = (int)(posY * ts) + startY;
    
    draw_pixel(DARK_GREEN, prevPlayerX, prevPlayerY);
    draw_pixel(YELLOW, x, y);
    prevPlayerX = x;
    prevPlayerY = y;
}

void handleCalculation() {
    gamestate = GAMESTATE_CALCULATING;
    draw_needle();
    draw_player();
    raycastF();
    gamestate = GAMESTATE_IDLE;
}

void WaitForAnyKey(){
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;
    while (RIA.rw0 & 1);
}

// ============================================================================
// Main Function
// ============================================================================

int16_t main() {
    bool handled_key = false;
    bool paused = false;
    bool show_buffers_indicators = true;
    uint8_t mode = 0;
    uint8_t i = 0;
    uint8_t timer = 0;

    // Initialize floor and ceiling colors
    for(int i = 0; i < h / 2; i++) {
        uint8_t sky_idx = mapValue(i, 0, h / 2, 16, 31);
        ceilingColors[i] = sky_idx;

        uint8_t floor_idx = mapValue(i, 0, h / 2, 32, 63); 
        floorColors[i + (h / 2)] = floor_idx;
    }

    prevPlayerX = (int)(posX * FpF16<7>(TILE_SIZE));
    prevPlayerY = (int)(posY * FpF16<7>(TILE_SIZE));
    prevDirX = dirX;
    prevDirY = dirY;

    gamestate = GAMESTATE_INIT;

    // Initialize maze
    initializeMaze();

    printf("Generating maze...\n");
    srand(4);
    int startPosX = (random(1, ((mapWidth - 2) / 2)) * 2 + 1);
    int startPoxY = (random(1, ((mapHeight - 2) / 2)) * 2 + 1);

    printf("startX: %i, startY: %i\n", startX, startY);
    
    iterativeDFS(startPosX, startPoxY);
    setEntryAndFinish(startPosX, startPoxY);

    posX = FpF16<7>(startPoxY);
    posY = FpF16<7>(startPosX);

    printf("Precalculating values...\n");
    precalculateRotations();
    precalculateLineHeights();

    // Load background image from filesystem
    printf("Loading background image...\n");
    int fd = open("pixel-320x180.bin", O_RDONLY);
    if(fd >= 0) {
        uint32_t filesize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);
        
        printf("File size: %lu bytes\n", filesize);
        read_xram(0x0000, filesize, fd);
        close(fd);
        printf("Background loaded successfully\n");
    } else {
        printf("WARNING: pixel-320x180.bin not found (fd=%i)\n", fd);
        printf("Canvas will be empty\n");
    }

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    
    // Upload custom palette
    RIA.addr0 = PALETTE_XRAM_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 256; i++) {
        uint16_t color = custom_palette[i];
        RIA.rw0 = (uint8_t)(color & 0xFF); 
        RIA.rw0 = (uint8_t)(color >> 8);   
    }
    xram0_struct_set(0xFF00, vga_mode3_config_t, xram_palette_ptr, PALETTE_XRAM_ADDR);

    draw_ui();
    draw_map();
    draw_player();
    handleCalculation();

    gamestate = GAMESTATE_IDLE;
    
    // Main game loop
    while (true) {
        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 2 && currentStep > 1) { 
            currentStep = 1;
            draw_map();
            handleCalculation();
        }

        xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;

        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
            uint8_t j, new_keys;
            RIA.addr0 = KEYBOARD_INPUT + i;
            new_keys = RIA.rw0;
            keystates[i] = new_keys;
        }

        if (!(keystates[0] & 1)) {
            if (key(KEY_SPACE)) {
                paused = !paused;
            }
            if (key(KEY_RIGHT)){
                gamestate = GAMESTATE_MOVING;
                currentRotStep = (currentRotStep + 1) % ROTATION_STEPS;
                dirX = currentRayDirX[0];
                if (currentRotStep == 0) dirX = 0;
                dirY = currentRayDirY[0];
            }
            if (key(KEY_LEFT)){
                gamestate = GAMESTATE_MOVING;
                currentRotStep = (currentRotStep - 1 + ROTATION_STEPS) % ROTATION_STEPS;
                dirX = currentRayDirX[0];
                if (currentRotStep == 0) dirX = 0;
                dirY = currentRayDirY[0];
            }
            if (key(KEY_UP)) {
                gamestate = GAMESTATE_MOVING;
                if(worldMap[int(posX + (dirX * moveSpeed) * playerScale)][int(posY)] == false) 
                    posX += (dirX * moveSpeed);
                if(worldMap[int(posX)][int(posY + (dirY * moveSpeed) * playerScale)] == false) 
                    posY += (dirY * moveSpeed);
            }
            if (key(KEY_DOWN)) {
                gamestate = GAMESTATE_MOVING;
                if(worldMap[int(posX - (dirX * moveSpeed) * playerScale)][int(posY)] == false) 
                    posX -= (dirX * moveSpeed);
                if(worldMap[int(posX)][int(posY - (dirY * moveSpeed) * playerScale)] == false) 
                    posY -= (dirY * moveSpeed);
            }
            if (key(KEY_M)) {
                bigMode = !bigMode;
                draw_ui();
            }
            if (key(KEY_ESC)) {
                break;
            }
            handled_key = true;

            if (!paused) {
                if (gamestate == GAMESTATE_MOVING) {
                    currentStep = movementStep;
                }
                handleCalculation();
            }
        }
    }

    return 0;
}