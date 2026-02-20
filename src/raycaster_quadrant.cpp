#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
extern "C" {
    #include <unistd.h>
    #include <fcntl.h>
}
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics.hpp"
#include "textures.h"
#include "sprites.h"
#include "FpF.hpp"
#include "maze.h"
#include "palette.h"

// Declare C functions for C++ linkage
extern "C" {
    int open(const char* filename, int flags, ...);
    int close(int fd);
    long lseek(int fd, long offset, int whence);
    int read_xram(unsigned address, unsigned count, int fd);
}

__attribute__((section(".zp.bss"))) static uint8_t zp_x;
__attribute__((section(".zp.bss"))) static uint8_t zp_y;
__attribute__((section(".zp.bss"))) static uint8_t zp_mapX;
__attribute__((section(".zp.bss"))) static uint8_t zp_mapY;
__attribute__((section(".zp.bss"))) static uint8_t zp_side;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistX;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistY;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaX;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaY;

using namespace mn::MFixedPoint;

#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
#define MAX_WINDOW_WIDTH 88
#define MAX_WINDOW_HEIGHT 56
#define CLOCK_TICKS_PER_SEC 100

#define ROTATION_STEPS 32
#define QUADRANT_STEPS 8 

FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1); 
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0); 
FpF16<7> moveSpeed(0.25); 
FpF16<7> playerScale(5);

// sin(pi/16) and cos(pi/16) for 11.25 degree steps
FpF16<7> sin_r(0.19509032201); 
FpF16<7> cos_r(0.9807852804); 

// uint16_t startX = 151+mapWidth/2;
// uint16_t startY = 137+mapHeight/2;
uint16_t startX = 285;
uint16_t startY = 94;

uint16_t prevPlayerX, prevPlayerY;

const uint8_t SCAN_FRAMES = 120;
const uint8_t SCAN_DECAY_MAX = 80;
bool scan_active = false;
uint8_t scan_frame = 0;
uint8_t scan_decay[mapHeight][mapWidth];
bool map_visible = false;
uint8_t map_draw_distance = 3;

int8_t currentStep = 1;
int8_t movementStep = 2; 
uint8_t coarseRayStep = 4;
uint8_t coarseSidePercent = 33;

uint8_t w = MAX_WINDOW_WIDTH;
uint8_t h = MAX_WINDOW_HEIGHT; 

// uint8_t xOffset = 60;
// uint8_t yOffset = 10; 
uint8_t xOffset = ((SCREEN_WIDTH - w * 2) / 2) - 8;
uint8_t yOffset = ((SCREEN_HEIGHT - h * 2) / 2) - 20;

uint8_t fps = 0;
bool interlacedMode = false;

// Texture repeat factor: 1=no repeat, 2=repeat 2x, 4=repeat 4x
const uint8_t texRepeat = 4;

uint8_t buffer[MAX_WINDOW_HEIGHT * MAX_WINDOW_WIDTH];

// --- Floor/Ceiling Texturing ---
#define FLOOR_BLOCK_IDLE 1
#define FLOOR_BLOCK_MOVING 1
#define FLOOR_TEX_REPEAT 2  // How many times texture repeats per map cell (independent of walls)
#define FLOOR_TEX  4   // texture index for floor
#define CEIL_TEX   5   // texture index for ceiling
#define FLOOR_MAX_COLS (MAX_WINDOW_WIDTH  / FLOOR_BLOCK_IDLE)  // 96 for block=1
#define FLOOR_MAX_ROWS (MAX_WINDOW_HEIGHT / 2 / FLOOR_BLOCK_IDLE) // 32 for block=1
#define FLOOR_STRIDE   FLOOR_MAX_COLS
#define FLOOR_MOVE_SAMPLE_X 2
#define FLOOR_MOVE_ROW_STEP 2

#define FLOOR_COLOR 23
#define CEILING_COLOR 26

uint8_t floorCols;   // actual columns for current w
uint8_t floorRows;   // actual rows for current h
uint8_t floorBlockCurrent = FLOOR_BLOCK_IDLE;

// Low-res tile maps: pre-computed per frame
uint8_t floorTexelIdx[FLOOR_MAX_ROWS * FLOOR_STRIDE];

// Fast lookup: screen row Y -> tile row index (eliminates per-pixel divisions)
uint8_t floorYtoTileY[MAX_WINDOW_HEIGHT];
uint8_t ceilYtoTileY[MAX_WINDOW_HEIGHT];
uint8_t floorXtoTileX[MAX_WINDOW_WIDTH];

// Precomputed row distances (9.7 fixed point) — one per low-res row
int16_t rowDistTable[FLOOR_MAX_ROWS];

// Precomputed reciprocal of floorCols in 9.7 fixed point
// invFloorCols = 128 / floorCols (so multiply then >>7 replaces divide)
int16_t invFloorCols;

// Precomputed row pointers to eliminate yi * FLOOR_STRIDE multiplications
uint8_t* floorTileRows[FLOOR_MAX_ROWS];

bool floorRowSkipEnabled = true; // toggle aggressive row skipping while moving

static inline uint8_t getWallRayStepAtX(uint8_t x) {
    if (currentStep < 2) return 1;

    uint8_t centerStep = (movementStep > 0) ? (uint8_t)movementStep : 1;
    if (centerStep > 4) centerStep = 4;
    uint8_t sideStep = (coarseRayStep >= centerStep) ? coarseRayStep : centerStep;
    if (sideStep > 4) sideStep = 4;
    if (coarseSidePercent == 0 || sideStep == centerStep) return centerStep;

    uint16_t sideWidth16 = ((uint16_t)w * (uint16_t)coarseSidePercent) / 100;
    if (sideWidth16 > (w >> 1)) sideWidth16 = (w >> 1);
    uint8_t sideWidth = (uint8_t)sideWidth16;
    if (sideWidth == 0) return centerStep;

    if (x < sideWidth || x >= (uint8_t)(w - sideWidth)) {
        return sideStep;
    }
    return centerStep;
}

static inline void fillSpan(uint8_t* dst, uint8_t span, uint8_t value) {
    dst[0] = value;
    if (span > 1) dst[1] = value;
    if (span > 2) dst[2] = value;
    if (span > 3) dst[3] = value;
}

static inline void fillZSpan(int16_t* zbuf, uint8_t x, uint8_t span, int16_t value) {
    zbuf[x] = value;
    if (span > 1) zbuf[x + 1] = value;
    if (span > 2) zbuf[x + 2] = value;
    if (span > 3) zbuf[x + 3] = value;
}

static inline uint8_t ceilDivByStep(uint8_t value, uint8_t step) {
    if (step <= 1) return value;
    if (step == 2) return (uint8_t)((value + 1) >> 1);
    if (step == 4) return (uint8_t)((value + 3) >> 2);
    return (uint8_t)((value + step - 1) / step);
}

#define FLOOR_MODE_TEXTURES 0
#define FLOOR_MODE_PLAIN    1
uint8_t floorDisplayMode = FLOOR_MODE_PLAIN;

// Cached floor/ceiling textures (16x16) to avoid per-frame XRAM reads
uint8_t floorTexCache[256];
uint8_t ceilTexCache[256];
bool floorTexCacheReady = false;

// ZBuffer for sprite depth testing (stores perpendicular wall distance per stripe)
int16_t ZBuffer[MAX_WINDOW_WIDTH];

// Sprite structure
struct Sprite {
    FpF16<7> x;
    FpF16<7> y;
    uint8_t texture;
};

// Sprite definitions
#define numSprites 10
Sprite sprites[numSprites];
uint8_t sprite_scan_decay[numSprites];
bool render_sprites = true; // toggle sprite rendering

#define SPRITE_MAX_VISIBLE_MOVING 4
#define SPRITE_MAX_DIST_TILES_MOVING 10
#define SPRITE_MIN_HEIGHT_MOVING 4
#define SPRITE_MAX_STRIPES_MOVING 24


bool gamestate_changed = true;
uint8_t gamestate = 1;  
uint8_t gamestate_prev = 1;
#define GAMESTATE_INIT 0
#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
#define GAMESTATE_CALCULATING 3

uint8_t lineHeightTable[1024]; 

// Base Vector Tables (Only 1 Quadrant)
FpF16<7> dirXValues[QUADRANT_STEPS];
FpF16<7> dirYValues[QUADRANT_STEPS];
FpF16<7> planeXValues[QUADRANT_STEPS];
FpF16<7> planeYValues[QUADRANT_STEPS];

FpF16<7> texStepValues[256];

// --- OPTIMIZATION: Cached Tables ---
// Q1 Positive (Standard)
FpF16<7> rayDirX_Q1[QUADRANT_STEPS][MAX_WINDOW_WIDTH];
FpF16<7> rayDirY_Q1[QUADRANT_STEPS][MAX_WINDOW_WIDTH];

// Q1 Negative (Pre-calculated negation to avoid runtime math/copying)
FpF16<7> rayDirX_Q1_Neg[QUADRANT_STEPS][MAX_WINDOW_WIDTH];
FpF16<7> rayDirY_Q1_Neg[QUADRANT_STEPS][MAX_WINDOW_WIDTH];

// Delta Dists for Q0 only (Q1 swaps X/Y, Q2 reuses Q0, Q3 reuses Q1 swap)
FpF16<7> deltaDistX_Q0[QUADRANT_STEPS][MAX_WINDOW_WIDTH];
FpF16<7> deltaDistY_Q0[QUADRANT_STEPS][MAX_WINDOW_WIDTH];

// These point to the correct row in the tables above
FpF16<7>* activeRayDirX;
FpF16<7>* activeRayDirY;
FpF16<7>* activeDeltaDistX;
FpF16<7>* activeDeltaDistY;

FpF16<7> cameraXValues[MAX_WINDOW_WIDTH];

// Floor/ceiling ray helpers per rotation step (raw 9.7 fixed)
int16_t floorRayDirX0raw[ROTATION_STEPS];
int16_t floorRayDirY0raw[ROTATION_STEPS];
int16_t floorDRayX[ROTATION_STEPS];
int16_t floorDRayY[ROTATION_STEPS];
int16_t floorRowRayXraw[ROTATION_STEPS][FLOOR_MAX_ROWS];
int16_t floorRowRayYraw[ROTATION_STEPS][FLOOR_MAX_ROWS];
int16_t floorStepXraw[ROTATION_STEPS][FLOOR_MAX_ROWS];
int16_t floorStepYraw[ROTATION_STEPS][FLOOR_MAX_ROWS];

// Cached inverse determinant for sprite projection
FpF16<7> invDetCache;
bool invDetValid = false;

int16_t texOffsetTable[256]; 
uint8_t texColumnBuffer[16]; 
uint8_t sprColumnBuffer[16]; // Buffer for sprite column data

// values for sprite rotation
static const int16_t sin_fix8_32[] = {
       0,   50,   98,  142,  181,  213,  237,  251,
     256,  251,  237,  213,  181,  142,   98,   50,
       0,  -50,  -98, -142, -181, -213, -237, -251,
    -256, -251, -237, -213, -181, -142,  -98,  -50,
       0
};

static const int16_t cos_fix8_32[] = {
     256,  251,  237,  213,  181,  142,   98,   50,
       0,  -50,  -98, -142, -181, -213, -237, -251,
    -256, -251, -237, -213, -181, -142,  -98,  -50,
       0,   50,   98,  142,  181,  213,  237,  251,
     256
};

// fix_8((1+(sin(theta_deg)-cos(theta_deg)))/2) for 32 steps
static const int16_t t2_fix8_32[] = {
       0,   27,   59,   93,  128,  163,  197,  229,
     256,  279,  295,  306,  309,  306,  295,  279,
     256,  229,  197,  163,  128,   93,   59,   27,
       0,  -23,  -39,  -50,  -53,  -50,  -39,  -23,
       0
};

uint8_t currentRotStep = 0; 

FpF16<7> invW;
FpF16<7> halfH(h / 2);

#define NEEDLE_SPRITE_ADDR 0xF100  // Sprite data (2048 bytes)
#define PALETTE_XRAM_ADDR 0xF900   // After sprite (0xF100 + 2048 = 0xF900)
#define NEEDLE_CONFIG_ADDR 0xFB00  // After palette (0xF900 + 512 = 0xFB00)
#define NEEDLE_SIZE 32                  // pixel sprite
#define LOG_NEEDLE_SIZE 5               // 2^5 = 32 (round up to power of 2)

#define NEEDLE_CENTER_X 292
#define NEEDLE_CENTER_Y 29

#define KEYBOARD_INPUT 0xFF10 
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

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
    if (value.GetRawVal() < 0) return -value;
    return value;
} 

inline FpF16<7> floorFixed(FpF16<7> a) {
    int16_t rawVal = a.GetRawVal();
    if (rawVal >= 0) {
        return FpF16<7>::FromRaw(rawVal & 0xFF80); 
    }
    else {
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

static inline int16_t mulFrac7Fast(int16_t value, uint8_t frac7) {
    if ((frac7 & 0x1F) == 0) {
        switch (frac7 >> 5) {
            case 0: return 0;
            case 1: return value >> 2;
            case 2: return value >> 1;
            case 3: return value - (value >> 2);
            default: return value;
        }
    }
    return (int16_t)(((int32_t)value * frac7) >> 7);
}

static inline uint8_t floorFracToTexCoord(uint8_t frac7) {
#if FLOOR_TEX_REPEAT == 1
    return (uint8_t)((frac7 >> 3) & 0x0F);
#elif FLOOR_TEX_REPEAT == 2
    return (uint8_t)((frac7 >> 2) & 0x0F);
#else
    return (uint8_t)((frac7 >> 1) & 0x0F);
#endif
}

static uint16_t isqrt16(uint16_t value) {
    uint16_t result = 0;
    uint16_t bit = 1u << 14;

    while (bit > value) {
        bit >>= 2;
    }

    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }

    return result;
}


void drawBufferDouble_Optimized() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr_loc = buffer;
    const uint8_t blocks = w >> 3;
    const bool coarseX = (currentStep >= 2);

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;
        RIA.addr1 = screen_addr + SCREEN_WIDTH;
        RIA.step1 = 1;
        uint8_t* p = buffer_ptr_loc;
        if (!coarseX) {
            for (uint8_t i = 0; i < blocks; ++i) {
                #define PUSH_PIXEL \
                    { \
                        uint8_t c = *p++; \
                        RIA.rw0 = c; RIA.rw0 = c; \
                        RIA.rw1 = c; RIA.rw1 = c; \
                    }
                PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
                PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
                #undef PUSH_PIXEL
            }
        } else {
            const uint8_t pairBlocks = w >> 4;
            for (uint8_t i = 0; i < pairBlocks; ++i) {
                #define PUSH_PIXEL_PAIR \
                    { \
                        uint8_t c = *p; p += 2; \
                        RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; \
                        RIA.rw1 = c; RIA.rw1 = c; RIA.rw1 = c; RIA.rw1 = c; \
                    }
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                #undef PUSH_PIXEL_PAIR
            }
            if (w & 0x08) {
                #define PUSH_PIXEL_PAIR \
                    { \
                        uint8_t c = *p; p += 2; \
                        RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; \
                        RIA.rw1 = c; RIA.rw1 = c; RIA.rw1 = c; RIA.rw1 = c; \
                    }
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                #undef PUSH_PIXEL_PAIR
            }
        }
        screen_addr += (SCREEN_WIDTH * 2);
        buffer_ptr_loc += w; 
    }
}

void drawBufferDouble_Optimized_Interlaced(bool oddField) {
    uint16_t screen_addr = SCREEN_WIDTH * (yOffset + (oddField ? 1 : 0)) + xOffset;
    uint8_t* buffer_ptr_loc = buffer;
    const uint8_t blocks = w >> 3;
    const bool coarseX = (currentStep >= 2);

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;
        uint8_t* p = buffer_ptr_loc;
        if (!coarseX) {
            for (uint8_t i = 0; i < blocks; ++i) {
                #define PUSH_PIXEL \
                    { \
                        uint8_t c = *p++; \
                        RIA.rw0 = c; RIA.rw0 = c; \
                    }
                PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
                PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
                #undef PUSH_PIXEL
            }
        } else {
            const uint8_t pairBlocks = w >> 4;
            for (uint8_t i = 0; i < pairBlocks; ++i) {
                #define PUSH_PIXEL_PAIR \
                    { \
                        uint8_t c = *p; p += 2; \
                        RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; \
                    }
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                #undef PUSH_PIXEL_PAIR
            }
            if (w & 0x08) {
                #define PUSH_PIXEL_PAIR \
                    { \
                        uint8_t c = *p; p += 2; \
                        RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; RIA.rw0 = c; \
                    }
                PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR; PUSH_PIXEL_PAIR;
                #undef PUSH_PIXEL_PAIR
            }
        }
        screen_addr += (SCREEN_WIDTH << 1);
        buffer_ptr_loc += w;
    }
}

void fillBuffer(uint8_t color) {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr_loc = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;
        RIA.addr1 = screen_addr + SCREEN_WIDTH;
        RIA.step1 = 1;
        uint8_t* p = buffer_ptr_loc;
        // Unroll 8 pixels per block (width must be multiple of 8)
        const uint8_t blocks = w >> 3;
        for (uint8_t i = 0; i < blocks; ++i) {
            #define PUSH_PIXEL \
                { \
                    uint8_t c = color; \
                    RIA.rw0 = c; RIA.rw0 = c; \
                    RIA.rw1 = c; RIA.rw1 = c; \
                }
            PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
            PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL; PUSH_PIXEL;
            #undef PUSH_PIXEL
        }
        screen_addr += (SCREEN_WIDTH * 2);
        buffer_ptr_loc += w; 
    }
}

static void draw_7segment_digit(uint16_t color, int8_t digit, uint16_t x, uint16_t y) {
    static const uint8_t segments[] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    if (digit < 0 || digit > 9) return;
    uint8_t mask = segments[digit];

    fill_rect_fast(BLACK, x, y, 6, 10); // Clear digit area

    auto draw_seg = [&](uint16_t sx, uint16_t sy, uint16_t slen, bool horizontal) {
        if (horizontal) draw_hline(color, sx, sy, slen);
        else draw_vline(color, sx, sy, slen);
    };

    if (mask & 0x01) draw_seg(x + 1, y, 4, true);      // a
    if (mask & 0x02) draw_seg(x + 5, y + 1, 3, false); // b
    if (mask & 0x04) draw_seg(x + 5, y + 5, 4, false); // c
    if (mask & 0x08) draw_seg(x + 1, y + 9, 4, true);  // d
    if (mask & 0x10) draw_seg(x, y + 5, 4, false);     // e
    if (mask & 0x20) draw_seg(x, y + 1, 3, false);     // f
    if (mask & 0x40) draw_seg(x + 1, y + 4, 4, true);     // g
}

void draw_7segment_double(uint16_t color, int8_t number, uint16_t x, uint16_t y) {
    if (number < 0) number = 0;
    if (number > 99) number = 99;
    draw_7segment_digit(color, number / 10, x, y);
    draw_7segment_digit(color, number % 10, x + 7, y);
}

void precalculateRotations() {
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    invW = FpF16<7>(1) / FpF16<7>(w); 
    FpF16<7> fw = FpF16<7>(w);
    
    texStepValues[0] = FpF16<7>(texHeight * texRepeat); 
    for (uint16_t i = 1; i < 256; i++) {
      texStepValues[i] = FpF16<7>(texHeight * texRepeat) / FpF16<7>((int16_t)i);
    }

    for(uint8_t x = 0; x < w; x++) {
        cameraXValues[x] = FpF16<7>(2 * x) / fw - FpF16<7>(1);
    }

    
    const int16_t NEAR_ZERO_THRESHOLD = 2;
    const FpF16<7> MAX_DELTA_DIST(127);
    
    for (uint8_t i = 0; i < QUADRANT_STEPS; i++) {
      
      dirXValues[i] = currentDirX;
      dirYValues[i] = currentDirY;
      planeXValues[i] = currentPlaneX;
      planeYValues[i] = currentPlaneY;

      for(uint8_t x = 0; x < w; x++) {
          FpF16<7> rayDirX = currentDirX + currentPlaneX * cameraXValues[x];
          FpF16<7> rayDirY = currentDirY + currentPlaneY * cameraXValues[x];
          
          // Store Q1 positive ray directions
          rayDirX_Q1[i][x] = rayDirX;
          rayDirY_Q1[i][x] = rayDirY;
          rayDirX_Q1_Neg[i][x] = -rayDirX;
          rayDirY_Q1_Neg[i][x] = -rayDirY;

          // Calculate safe deltaDist helper
          auto calcDelta = [&](FpF16<7> rayDir) -> FpF16<7> {
              int16_t raw = rayDir.GetRawVal();
              if (raw < 0) raw = -raw;
              if (raw <= NEAR_ZERO_THRESHOLD) {
                  return MAX_DELTA_DIST;
              }
              FpF16<7> delta = FpF16<7>(1) / fp_abs(rayDir);
              return (delta > MAX_DELTA_DIST) ? MAX_DELTA_DIST : delta;
          };

          // Q0: 0-90° - Use base directions (other quadrants derived at runtime)
          deltaDistX_Q0[i][x] = calcDelta(rayDirX);
          deltaDistY_Q0[i][x] = calcDelta(rayDirY);
      }

      int16_t rayDirX0raw = (dirXValues[i] - planeXValues[i]).GetRawVal();
      int16_t rayDirY0raw = (dirYValues[i] - planeYValues[i]).GetRawVal();
      int16_t rayDirX1raw = (dirXValues[i] + planeXValues[i]).GetRawVal();
      int16_t rayDirY1raw = (dirYValues[i] + planeYValues[i]).GetRawVal();

      for (uint8_t q = 0; q < 4; q++) {
          uint8_t rot = i + q * QUADRANT_STEPS;
          int16_t x0 = rayDirX0raw;
          int16_t y0 = rayDirY0raw;
          int16_t x1 = rayDirX1raw;
          int16_t y1 = rayDirY1raw;

          if (q == 1) {
              x0 = -rayDirY0raw; y0 = rayDirX0raw;
              x1 = -rayDirY1raw; y1 = rayDirX1raw;
          } else if (q == 2) {
              x0 = -rayDirX0raw; y0 = -rayDirY0raw;
              x1 = -rayDirX1raw; y1 = -rayDirY1raw;
          } else if (q == 3) {
              x0 = rayDirY0raw; y0 = -rayDirX0raw;
              x1 = rayDirY1raw; y1 = -rayDirX1raw;
          }

          floorRayDirX0raw[rot] = x0;
          floorRayDirY0raw[rot] = y0;
          floorDRayX[rot] = x1 - x0;
          floorDRayY[rot] = y1 - y0;
      }

      FpF16<7> oldDirX = currentDirX;
      currentDirX = currentDirX * cos_r - currentDirY * sin_r;
      currentDirY = oldDirX * sin_r + currentDirY * cos_r;

      FpF16<7> oldPlaneX = currentPlaneX;
      currentPlaneX = currentPlaneX * cos_r - currentPlaneY * sin_r;
      currentPlaneY = oldPlaneX * sin_r + currentPlaneY * cos_r;
    }
}

static void precalculateFloorRowTablesForRot(uint8_t rotStep,
                                             int16_t rayDirX0raw,
                                             int16_t rayDirY0raw,
                                             int16_t dRayX,
                                             int16_t dRayY) {
    if (floorRows == 0) return;
    for (uint8_t yi = 0; yi < floorRows; yi++) {
        int16_t rowDist = rowDistTable[yi];
        floorRowRayXraw[rotStep][yi] = (int16_t)(((int32_t)rowDist * rayDirX0raw) >> 7);
        floorRowRayYraw[rotStep][yi] = (int16_t)(((int32_t)rowDist * rayDirY0raw) >> 7);
        int16_t totalStepX = (int16_t)(((int32_t)rowDist * dRayX) >> 7);
        int16_t totalStepY = (int16_t)(((int32_t)rowDist * dRayY) >> 7);
        floorStepXraw[rotStep][yi] = (int16_t)(((int32_t)totalStepX * invFloorCols) >> 14);
        floorStepYraw[rotStep][yi] = (int16_t)(((int32_t)totalStepY * invFloorCols) >> 14);
    }
}

static void precalculateFloorRowTablesAllRot() {
    for (uint8_t rot = 0; rot < ROTATION_STEPS; rot++) {
        precalculateFloorRowTablesForRot(rot,
                                         floorRayDirX0raw[rot],
                                         floorRayDirY0raw[rot],
                                         floorDRayX[rot],
                                         floorDRayY[rot]);
    }
}

void updateRaycasterVectors() {
    uint8_t quad = currentRotStep / QUADRANT_STEPS;
    uint8_t idx = currentRotStep % QUADRANT_STEPS;

    FpF16<7> currDirX, currDirY, currPlaneX, currPlaneY;

    switch(quad) {
        case 0: // 0-90 deg
            currDirX = dirXValues[idx]; 
            currDirY = dirYValues[idx];
            currPlaneX = planeXValues[idx]; 
            currPlaneY = planeYValues[idx];
            
            activeRayDirX = rayDirX_Q1[idx];
            activeRayDirY = rayDirY_Q1[idx];
            activeDeltaDistX = deltaDistX_Q0[idx];
            activeDeltaDistY = deltaDistY_Q0[idx];
            break;

        case 1: // 90-180 deg - (x,y)->(-y,x): swap delta X/Y
            currDirX = -dirYValues[idx]; 
            currDirY = dirXValues[idx];
            currPlaneX = -planeYValues[idx]; 
            currPlaneY = planeXValues[idx];

            activeRayDirX = rayDirY_Q1_Neg[idx];
            activeRayDirY = rayDirX_Q1[idx];
            activeDeltaDistX = deltaDistY_Q0[idx];  // Swapped: Y->X
            activeDeltaDistY = deltaDistX_Q0[idx];  // Swapped: X->Y
            break;

        case 2: // 180-270 deg - (x,y)->(-x,-y): same abs values as Q0
            currDirX = -dirXValues[idx]; 
            currDirY = -dirYValues[idx];
            currPlaneX = -planeXValues[idx]; 
            currPlaneY = -planeYValues[idx];

            activeRayDirX = rayDirX_Q1_Neg[idx];
            activeRayDirY = rayDirY_Q1_Neg[idx];
            activeDeltaDistX = deltaDistX_Q0[idx];  // Same as Q0
            activeDeltaDistY = deltaDistY_Q0[idx];  // Same as Q0
            break;

        case 3: // 270-360 deg - (x,y)->(y,-x): swap delta X/Y
            currDirX = dirYValues[idx]; 
            currDirY = -dirXValues[idx];
            currPlaneX = planeYValues[idx]; 
            currPlaneY = -planeXValues[idx];

            activeRayDirX = rayDirY_Q1[idx];
            activeRayDirY = rayDirX_Q1_Neg[idx];
            activeDeltaDistX = deltaDistY_Q0[idx];  // Swapped: Y->X
            activeDeltaDistY = deltaDistX_Q0[idx];  // Swapped: X->Y
            break;
    }

    dirX = currDirX;
    dirY = currDirY;
    planeX = currPlaneX;
    planeY = currPlaneY;

    // Cache inverse determinant for sprite projection
    FpF16<7> det = planeX * dirY - dirX * planeY;
    if (det.GetRawVal() == 0) {
        invDetValid = false;
    } else {
        invDetCache = FpF16<7>(1) / det;
        invDetValid = true;
    }
}

void precalculateFloorTables() {
    uint8_t halfH = h / 2;
    uint8_t floorBlock = floorBlockCurrent;
    floorCols = w / floorBlock;
    floorRows = halfH / floorBlock;
    if (floorRows > FLOOR_MAX_ROWS) floorRows = FLOOR_MAX_ROWS;
    if (floorCols > FLOOR_MAX_COLS) floorCols = FLOOR_MAX_COLS;

    // Precompute reciprocal with 14-bit precision for accuracy:
    // invFloorCols = (1<<14) / floorCols
    // Used as: (val * invFloorCols) >> 14  ≈  val / floorCols
    if (floorCols > 0) {
        invFloorCols = (int16_t)((1L << 14) / floorCols);
    } else {
        invFloorCols = (1 << 14) - 1;
    }

    // Precompute rowDistance for each low-res row (9.7 fixed point)
    // yi=0 is closest to horizon, yi=floorRows-1 is closest to screen edge
    // Center pixel of block yi: p = yi * floorBlock + floorBlock/2
    // rowDistance = halfH / p  (in 9.7: (halfH << 7) / p)
    for (uint8_t yi = 0; yi < floorRows; yi++) {
        int16_t p = (int16_t)(yi * floorBlock + floorBlock / 2);
        if (p <= 0) p = 1;
        rowDistTable[yi] = (int16_t)(((int32_t)halfH << 7) / p);
        
        // Precompute row pointers (eliminates yi * FLOOR_STRIDE multiplication)
        floorTileRows[yi] = &floorTexelIdx[yi * FLOOR_STRIDE];
    }
    
    // Precompute lookup tables: screen row Y -> tile row index
    // This eliminates per-pixel divisions in the rendering loop
    for (uint8_t y = 0; y < halfH; y++) {
        uint8_t dist = halfH - 1 - y;
        uint8_t yi = dist / floorBlock;
        if (yi >= floorRows) yi = floorRows - 1;
        ceilYtoTileY[y] = yi;
    }
    for (uint8_t y = 0; y < halfH; y++) {
        uint8_t dist = y;
        uint8_t yi = dist / floorBlock;
        if (yi >= floorRows) yi = floorRows - 1;
        floorYtoTileY[y + halfH] = yi;
    }

    for (uint8_t x = 0; x < w; x++) {
        uint8_t xi = x / floorBlock;
        if (xi >= floorCols) xi = floorCols - 1;
        floorXtoTileX[x] = xi;
    }
    
    // printf("Floor tables: %dx%d blocks, block=%d, texRepeat=%d\n", 
            // floorCols, floorRows, floorBlock, FLOOR_TEX_REPEAT);

    // Keep floor row caches in sync after resize without requiring a rotation
    // Rebuild floor row tables for all rotation steps (done on resize/quality changes only)
    precalculateFloorRowTablesAllRot();
}

static inline void updateFloorBlockForCurrentStep() {
    uint8_t desiredBlock = (currentStep == 1) ? FLOOR_BLOCK_IDLE : FLOOR_BLOCK_MOVING;
    if (desiredBlock != floorBlockCurrent) {
        floorBlockCurrent = desiredBlock;
        precalculateFloorTables();
    }
}

void precalculateLineHeights() {
    lineHeightTable[0] = 255; 
    for (int i = 1; i < 1024; ++i) {
        FpF32<7> dist = FpF32<7>::FromRaw(i);
        FpF32<7> heightFp = FpF32<7>(h) / dist;
        int height = (int)heightFp;
        if (height > 255) height = 255;
        if (height < 0) height = 0;
        lineHeightTable[i] = (uint8_t)height;
    }

    texOffsetTable[0] = 0;
    for (int i = 1; i < 256; i++) {
        if (i <= 64) {
             texOffsetTable[i] = 0;
        } else {
             int32_t val = 4096 - (262144L / i);
             if (val < 0) val = 0;
             texOffsetTable[i] = (int16_t)val;
        }
    }
}

// Render sprites using raycasting sprite projection
void renderSprites() {
    if (numSprites == 0 || !render_sprites) return;

    const bool movingLowQuality = (currentStep >= 2);
    const uint8_t maxVisibleSprites = movingLowQuality ? SPRITE_MAX_VISIBLE_MOVING : numSprites;
    const int16_t maxDistRawMoving = (int16_t)(SPRITE_MAX_DIST_TILES_MOVING * 128);
    const uint8_t maxStripesMoving = movingLowQuality ? SPRITE_MAX_STRIPES_MOVING : w;

    uint8_t centerStripeStep = 1;
    uint8_t sideStripeStep = 1;
    uint8_t sideWidth = 0;
    uint8_t sideStart = 0;
    uint8_t sideEnd = w;
    if (movingLowQuality) {
        centerStripeStep = (movementStep > 0) ? (uint8_t)movementStep : 1;
        if (centerStripeStep > 4) centerStripeStep = 4;
        sideStripeStep = (coarseRayStep >= centerStripeStep) ? coarseRayStep : centerStripeStep;
        if (sideStripeStep > 4) sideStripeStep = 4;

        if (coarseSidePercent > 0 && sideStripeStep > centerStripeStep) {
            uint16_t sideWidth16 = ((uint16_t)w * (uint16_t)coarseSidePercent) / 100;
            if (sideWidth16 > (w >> 1)) sideWidth16 = (w >> 1);
            sideWidth = (uint8_t)sideWidth16;
            sideStart = sideWidth;
            sideEnd = (uint8_t)(w - sideWidth);
        }
    }
    
    // Calculate inverse determinant for camera transformation
    // invDet = 1 / (planeX * dirY - dirX * planeY)
    if (!invDetValid) return; // Avoid division by zero
    const int16_t invDetRaw = invDetCache.GetRawVal();
    const int16_t dirXRaw = dirX.GetRawVal();
    const int16_t dirYRaw = dirY.GetRawVal();
    const int16_t planeXRaw = planeX.GetRawVal();
    const int16_t planeYRaw = planeY.GetRawVal();
    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    
    int32_t w_half = (w >> 1);
    uint8_t renderedSprites = 0;
    uint8_t usedStripes = 0;
    
    for (int8_t i = numSprites - 1; i >= 0; i--) {
        if (movingLowQuality && renderedSprites >= maxVisibleSprites) break;

        // Translate sprite position to relative to camera (raw 9.7)
        int16_t spriteXraw = sprites[i].x.GetRawVal() - posXRaw;
        int16_t spriteYraw = sprites[i].y.GetRawVal() - posYRaw;

        if (movingLowQuality) {
            int16_t absX = (spriteXraw < 0) ? -spriteXraw : spriteXraw;
            int16_t absY = (spriteYraw < 0) ? -spriteYraw : spriteYraw;
            if (absX > maxDistRawMoving || absY > maxDistRawMoving) continue;
        }

        int32_t forward = (int32_t)dirXRaw * spriteXraw + (int32_t)dirYRaw * spriteYraw;
        if (forward <= 0) continue;

        // Transform sprite with the inverse camera matrix using raw math
        int32_t t1 = ((int32_t)dirYRaw * spriteXraw) >> 7;
        t1 -= ((int32_t)dirXRaw * spriteYraw) >> 7;
        int32_t t2 = ((int32_t)(-planeYRaw) * spriteXraw) >> 7;
        t2 += ((int32_t)planeXRaw * spriteYraw) >> 7;
        int16_t tx = (int16_t)(((int32_t)invDetRaw * t1) >> 7);
        int16_t ty = (int16_t)(((int32_t)invDetRaw * t2) >> 7);

        // Skip if sprite is behind camera
        if (ty <= 10) continue;
        
        int16_t tyIdx = ty;
        if (tyIdx > 1023) tyIdx = 1023;

        int32_t spriteScreenX = w_half + (w_half * tx) / ty;

        uint8_t spriteStripeStep = 1;
        if (movingLowQuality) {
            spriteStripeStep = centerStripeStep;
            if (sideWidth > 0 && (spriteScreenX < sideStart || spriteScreenX >= sideEnd)) {
                spriteStripeStep = sideStripeStep;
            }
        }
        
        // Calculate sprite positioning like a wall would be rendered
        // First get the wall height at this distance for reference
        int16_t wall_height = lineHeightTable[tyIdx];
        
        // Sprite is quarter of wall height (as specified)
        int16_t spr_height = wall_height / 2;
        if (spr_height < 1) spr_height = 1;
        if (movingLowQuality && spr_height < SPRITE_MIN_HEIGHT_MOVING) continue;
        
        // Position sprite like a short wall sitting on the floor
        // Walls are centered on h/2, so bottom is at: h/2 + wall_height/2
        // Sprite bottom aligns with wall bottom, sprite is just shorter
        int16_t wall_bottom = (h / 2) + (wall_height / 2);
        int16_t drawEndY = wall_bottom;
        int16_t drawStartY = drawEndY - spr_height;
        
        int16_t screen_drawStartY = drawStartY;
        int16_t screen_drawEndY = drawEndY;
        
        // Clamp to screen bounds
        if (screen_drawStartY < 0) screen_drawStartY = 0;
        if (screen_drawEndY > h) screen_drawEndY = h;
        
        if (screen_drawStartY >= screen_drawEndY) continue;
        
        int16_t spr_width = spr_height; // Square aspect ratio
        
        // Calculate draw boundaries X
        int32_t drawStartX_32 = -spr_width / 2 + spriteScreenX;
        int32_t drawEndX_32 = spr_width / 2 + spriteScreenX;
        
        int16_t drawStartX = (drawStartX_32 < 0) ? 0 : (int16_t)drawStartX_32;
        int16_t drawEndX = (drawEndX_32 > w) ? w : (int16_t)drawEndX_32;
        
        // Skip if sprite is completely off screen
        if (drawStartX >= drawEndX) continue;

        uint8_t spriteSpanX = (uint8_t)(drawEndX - drawStartX);
        uint8_t spriteStripes = ceilDivByStep(spriteSpanX, spriteStripeStep);
        if (movingLowQuality) {
            if (spriteStripes == 0) continue;
            if (usedStripes >= maxStripesMoving) break;
            if ((uint16_t)usedStripes + spriteStripes > maxStripesMoving) continue;
        }

        renderedSprites++;
        usedStripes = (uint8_t)(usedStripes + spriteStripes);
        
        // Precalculate stepping using existing table (avoids runtime divide)
        // texStepValues[h].raw = (texHeight * texRepeat * 128) / h = 8192 / h
        // sprite texStep (16.16) = (16 << 16) / h = 1048576 / h = raw << 7
        int32_t texStep = ((int32_t)texStepValues[spr_height].GetRawVal()) << 7;
        
        // Calculate initial texture X position
        int16_t logicalStartX = (int16_t)(-spr_width / 2 + spriteScreenX);
        int32_t texXPos = (int32_t)(drawStartX - logicalStartX) * texStep;

        // Calculate initial texture Y position
        // logicalStartY = drawStartY (unclamped)
        int32_t initialTexYPos = 0;
        if (drawStartY < 0) {
            initialTexYPos = (int32_t)(-drawStartY) * texStep;
        }
        
        // Get sprite distance
        int16_t spriteDistRaw = ty;
        
        // Calculate pointer to start of drawing area in buffer
        uint8_t* colStartPtr = &buffer[screen_drawStartY * w + drawStartX];

        // Cache for sprite column to avoid repeated fetches when scaling up
        int16_t lastTexX = -1;
        bool lastColAllTransparent = false;
        bool lastColAllOpaque = false;
        uint16_t lastOpaqueMask = 0;

        uint8_t spriteTex = sprites[i].texture;
        if (spriteTex >= NUM_SPRITES) {
            spriteTex %= NUM_SPRITES;
        }

        // Loop through every vertical stripe of the sprite on screen
        int32_t texAdvance = texStep * spriteStripeStep;
        for (int16_t stripe = drawStartX; stripe < drawEndX; stripe += spriteStripeStep) {
            
            // Draw sprite if closer than wall OR no wall at all
            if (spriteDistRaw > 0 && spriteDistRaw <= ZBuffer[stripe]) {
                int16_t texX = texXPos >> 16;
                if (texX > 15) texX = 15;

                // Only fetch if texture column changed
                if (texX != lastTexX) {
                    fetchSpriteColumn(spriteTex, (uint8_t)texX);
                    lastTexX = texX;
#if SPRITE_HAS_OPACITY_METADATA
                    if (movingLowQuality) {
                        uint8_t transparentCount = 0;
                        lastOpaqueMask = 0;
                        for (uint8_t sy = 0; sy < 16; sy++) {
                            if (sprColumnBuffer[sy] != 0x21) {
                                lastOpaqueMask |= (uint16_t)1 << sy;
                            } else {
                                transparentCount++;
                            }
                        }
                        lastColAllTransparent = (transparentCount == 16);
                        lastColAllOpaque = (transparentCount == 0);
                    } else {
                        lastOpaqueMask = fetchSpriteColumnMask(spriteTex, (uint8_t)texX);
                        lastColAllTransparent = (lastOpaqueMask == 0);
                        lastColAllOpaque = (lastOpaqueMask == 0xFFFF);
                    }
#else
                    uint8_t transparentCount = 0;
                    for (uint8_t sy = 0; sy < 16; sy++) {
                        if (sprColumnBuffer[sy] == 0x21) {
                            transparentCount++;
                        }
                    }
                    lastColAllTransparent = (transparentCount == 16);
                    lastColAllOpaque = (transparentCount == 0);
#endif
                }

                if (lastColAllTransparent) {
                    texXPos += texAdvance;
                    colStartPtr += spriteStripeStep;
                    continue;
                }
                
                uint8_t* pixelPtr = colStartPtr;
                int32_t texYPos = initialTexYPos;
                uint8_t spanWidth = spriteStripeStep;
                if ((int16_t)(stripe + spanWidth) > drawEndX) {
                    spanWidth = (uint8_t)(drawEndX - stripe);
                }
                bool spanVisible = true;
                for (uint8_t s = 0; s < spanWidth; s++) {
                    if (spriteDistRaw > ZBuffer[stripe + s]) {
                        spanVisible = false;
                        break;
                    }
                }
                if (!spanVisible) {
                    texXPos += texAdvance;
                    colStartPtr += spriteStripeStep;
                    continue;
                }
                
                // Draw vertical stripe
                for (int16_t y = screen_drawStartY; y < screen_drawEndY; y++) {
                    uint8_t texY = (uint8_t)((texYPos >> 16) & 0x0F);

                    if (lastColAllOpaque) {
                        uint8_t color = sprColumnBuffer[texY];
                        fillSpan(pixelPtr, spanWidth, color);
                    } else {
#if SPRITE_HAS_OPACITY_METADATA
                        if (lastOpaqueMask & ((uint16_t)1 << texY)) {
                            uint8_t color = sprColumnBuffer[texY];
                            fillSpan(pixelPtr, spanWidth, color);
                        }
#else
                        uint8_t color = sprColumnBuffer[texY];
                        if (color != 0x21) { // Treat color 0x21 as transparent
                            fillSpan(pixelPtr, spanWidth, color);
                        }
#endif
                    }
                    pixelPtr += w;
                    texYPos += texStep;
                }
            }
            texXPos += texAdvance;
            colStartPtr += spriteStripeStep;
        }
    }
}

// Pre-compute low-res floor/ceiling tile maps for the current frame.
// Uses horizontal scanline approach (Lodev tutorial) but at very low resolution.
// Each tile covers floorBlockCurrent x floorBlockCurrent buffer pixels.
static void buildFloorCeilTexCache() {
    if (floorTexCacheReady) return;
    for (uint16_t texOffset = 0; texOffset < 256; texOffset++) {
        floorTexCache[texOffset] = getTexturePixel(FLOOR_TEX, texOffset);
        ceilTexCache[texOffset] = getTexturePixel(CEIL_TEX, texOffset);
    }
    floorTexCacheReady = true;
}

void computeFloorCeiling() {
    buildFloorCeilTexCache();

    // Ray directions at leftmost (x=0) and rightmost (x=w) screen edges
    int16_t rayDirX0raw = floorRayDirX0raw[currentRotStep];
    int16_t rayDirY0raw = floorRayDirY0raw[currentRotStep];
    
    int16_t posXRaw = posX.GetRawVal();
    int16_t posYRaw = posY.GetRawVal();
    
    bool movingLowQuality = (currentStep == 2);
    bool halfRateRows = movingLowQuality && floorRowSkipEnabled;
    uint8_t sampleXStep = movingLowQuality ? FLOOR_MOVE_SAMPLE_X : 1;

    uint8_t rowStep = halfRateRows ? FLOOR_MOVE_ROW_STEP : 1;
    for (uint8_t yi = 0; yi < floorRows; yi += rowStep) {
        // World position at leftmost pixel for this scanline
        int16_t floorXraw = posXRaw + floorRowRayXraw[currentRotStep][yi];
        int16_t floorYraw = posYRaw + floorRowRayYraw[currentRotStep][yi];
        
        // Step per low-res column, precomputed for this rotation
        int16_t stepXraw = floorStepXraw[currentRotStep][yi];
        int16_t stepYraw = floorStepYraw[currentRotStep][yi];
        
        // Start at center of first block: offset by half a block step
        floorXraw += stepXraw >> 1;
        floorYraw += stepYraw >> 1;
        
        uint8_t* row = floorTileRows[yi];
        
        for (uint8_t xi = 0; xi < floorCols; xi += sampleXStep) {
            uint8_t texX = floorFracToTexCoord((uint8_t)floorXraw & 0x7F);
            uint8_t texY = floorFracToTexCoord((uint8_t)floorYraw & 0x7F);
            uint16_t texOffset = ((uint16_t)texY << 4) | texX;

            row[xi] = (uint8_t)texOffset;
            for (uint8_t s = 1; s < sampleXStep; s++) {
                uint8_t x2 = xi + s;
                if (x2 >= floorCols) break;
                row[x2] = (uint8_t)texOffset;
            }

            floorXraw += (int16_t)(stepXraw * sampleXStep);
            floorYraw += (int16_t)(stepYraw * sampleXStep);
        }

        if (halfRateRows) {
            for (uint8_t r = 1; r < rowStep; r++) {
                if (yi + r >= floorRows) break;
                uint8_t* nextRow = floorTileRows[yi + r];
                for (uint8_t xi = 0; xi < floorCols; xi++) {
                    nextRow[xi] = row[xi];
                }
            }
        }
    }
}


static int raycastF_NoFloorTex() {

    FpF16<7>* rayDirXPtr = activeRayDirX;
    FpF16<7>* rayDirYPtr = activeRayDirY;
    FpF16<7>* deltaDistXPtr = activeDeltaDistX;
    FpF16<7>* deltaDistYPtr = activeDeltaDistY;

    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    const uint8_t fracX = (uint8_t)(posXRaw & 0x7F);
    const uint8_t invFracX = (uint8_t)(128 - fracX);
    const uint8_t fracY = (uint8_t)(posYRaw & 0x7F);
    const uint8_t invFracY = (uint8_t)(128 - fracY);

    const int mapX_start = posXRaw >> 7;
    const int mapY_start = posYRaw >> 7;
    int8_t* mapPtr = (int8_t*)worldMap;

    uint8_t lastTexNum = 0xFF;
    uint8_t lastTexX = 0xFF;

    for (zp_x = 0; zp_x < w;) {
        uint8_t rayStep = getWallRayStepAtX(zp_x);
        uint8_t xSpan = rayStep;
        if ((uint16_t)zp_x + xSpan > w) {
            xSpan = (uint8_t)(w - zp_x);
        }

        zp_deltaX = deltaDistXPtr[zp_x].GetRawVal();
        zp_deltaY = deltaDistYPtr[zp_x].GetRawVal();
        int16_t rDX = rayDirXPtr[zp_x].GetRawVal();
        int16_t rDY = rayDirYPtr[zp_x].GetRawVal();

        zp_mapX = mapX_start;
        zp_mapY = mapY_start;

        uint8_t mapOffset = (zp_mapY << 4) + zp_mapX;
        int8_t mapStepX;
        int8_t mapStepY;

        if (rDX < 0) {
            mapStepX = -1;
            zp_sideDistX = mulFrac7Fast(zp_deltaX, fracX);
        } else {
            mapStepX = 1;
            zp_sideDistX = mulFrac7Fast(zp_deltaX, invFracX);
        }

        if (rDY < 0) {
            mapStepY = -16;
            zp_sideDistY = mulFrac7Fast(zp_deltaY, fracY);
        } else {
            mapStepY = 16;
            zp_sideDistY = mulFrac7Fast(zp_deltaY, invFracY);
        }

        while (mapPtr[mapOffset] == 0) {
            if (zp_sideDistX < zp_sideDistY) {
                zp_sideDistX += zp_deltaX;
                mapOffset += mapStepX;
                zp_side = 0;
            } else {
                zp_sideDistY += zp_deltaY;
                mapOffset += mapStepY;
                zp_side = 1;
            }
        }

        int16_t rawDist = (zp_side == 0) ?
            (zp_sideDistX - zp_deltaX) :
            (zp_sideDistY - zp_deltaY);

        int16_t zVal = (rawDist < 0) ? 0 : rawDist;
        fillZSpan(ZBuffer, zp_x, xSpan, zVal);

        uint16_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = (rawDist > 0) ?
                (int)(FpF16<7>(h) / FpF16<7>::FromRaw(rawDist)) : h;
            if (lineHeight > 255) lineHeight = 255;
        }

        uint8_t texNum = ((mapPtr[mapOffset] - 1) * 2 + zp_side) & (NUM_TEXTURES - 1);
        int16_t drawStart = (-((int16_t)lineHeight) >> 1) + (h >> 1);
        if (drawStart < 0) drawStart = 0;
        uint16_t drawEnd = drawStart + lineHeight;
        if (drawEnd > h) drawEnd = h;

        int16_t wallRaw;
        if (zp_side == 0) {
            wallRaw = posYRaw + (int16_t)(((int32_t)rawDist * rDY) >> 7);
        } else {
            wallRaw = posXRaw + (int16_t)(((int32_t)rawDist * rDX) >> 7);
        }

        uint8_t frac7 = wallRaw & 0x7F;
        int texX = (frac7 >> 1) & 0x0F;
        if (zp_side == 0 && rDX > 0) texX = texWidth - texX - 1;
        if (zp_side == 1 && rDY < 0) texX = texWidth - texX - 1;

        if (texNum != lastTexNum || texX != lastTexX) {
            fetchTextureColumn(texNum, texX);
            lastTexNum = texNum;
            lastTexX = texX;
        }

        int16_t raw_step = texStepValues[lineHeight].GetRawVal();
        int16_t raw_texPos = (lineHeight > h) ?
            texOffsetTable[lineHeight] : 0;
        if (raw_texPos < 0) raw_texPos = 0;

        uint8_t* bufPtr = &buffer[zp_x];

        if (currentStep == 2) {
            for (zp_y = 0; zp_y < drawStart; zp_y += 2) {
                uint8_t c0 = CEILING_COLOR; 
                fillSpan(bufPtr, xSpan, c0);
                bufPtr += w;
                if (zp_y + 1 < drawStart) {
                    fillSpan(bufPtr, xSpan, c0);
                    bufPtr += w;
                }
            }
            for (zp_y = drawStart; zp_y < drawEnd; zp_y += 2) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                uint8_t color = texColumnBuffer[texY];
                fillSpan(bufPtr, xSpan, color);
                bufPtr += w;
                if (zp_y + 1 < drawEnd) {
                    fillSpan(bufPtr, xSpan, color);
                    bufPtr += w;
                }
                raw_texPos += (raw_step << 1);
            }
            for (zp_y = drawEnd; zp_y < h; zp_y += 2) {
                uint8_t f0 = FLOOR_COLOR;
                fillSpan(bufPtr, xSpan, f0);
                bufPtr += w;
                if (zp_y + 1 < h) {
                    fillSpan(bufPtr, xSpan, f0);
                    bufPtr += w;
                }
            }
        } else {
            // uint8_t* c_ptr = ceilingColors;
            // uint8_t* f_ptr = floorColors;
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                *bufPtr = CEILING_COLOR;
                bufPtr += w;
            }
            // f_ptr += drawEnd;
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                *bufPtr = texColumnBuffer[texY];
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                *bufPtr = FLOOR_COLOR;
                bufPtr += w;
            }
        }

        zp_x += rayStep;

    }

    renderSprites();

    if (interlacedMode) {
        drawBufferDouble_Optimized_Interlaced(false);
    } else {
        drawBufferDouble_Optimized();
    }

    return 0;
}

int raycastF() {
    updateFloorBlockForCurrentStep();

    if (floorDisplayMode == FLOOR_MODE_PLAIN) {
        return raycastF_NoFloorTex();
    }

    // Pre-compute low-res floor/ceiling tile maps for this frame
    computeFloorCeiling();
    
    // 1. Use the pointers already set by updateRaycasterVectors()
    // This avoids 2D array indexing overhead inside the frame
    FpF16<7>* rayDirXPtr = activeRayDirX;
    FpF16<7>* rayDirYPtr = activeRayDirY;
    FpF16<7>* deltaDistXPtr = activeDeltaDistX;
    FpF16<7>* deltaDistYPtr = activeDeltaDistY;
    
    // 2. Pre-calculate fractional positions ONCE per frame
    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    const uint8_t fracX = (uint8_t)(posXRaw & 0x7F);
    const uint8_t invFracX = (uint8_t)(128 - fracX);
    const uint8_t fracY = (uint8_t)(posYRaw & 0x7F);
    const uint8_t invFracY = (uint8_t)(128 - fracY);

    const int mapX_start = posXRaw >> 7;
    const int mapY_start = posYRaw >> 7;
    int8_t* mapPtr = (int8_t*)worldMap;

    uint8_t lastTexNum = 0xFF;
    uint8_t lastTexX = 0xFF;
    for (zp_x = 0; zp_x < w;) {
        uint8_t rayStep = getWallRayStepAtX(zp_x);
        uint8_t xSpan = rayStep;
        if ((uint16_t)zp_x + xSpan > w) {
            xSpan = (uint8_t)(w - zp_x);
        }

        // Load cached values into Zero Page registers
        zp_deltaX = deltaDistXPtr[zp_x].GetRawVal();
        zp_deltaY = deltaDistYPtr[zp_x].GetRawVal();
        int16_t rDX = rayDirXPtr[zp_x].GetRawVal();
        int16_t rDY = rayDirYPtr[zp_x].GetRawVal();
        
        zp_mapX = mapX_start;
        zp_mapY = mapY_start;
        
        // Flattened map access optimization (mapWidth=16)
        uint8_t mapOffset = (zp_mapY << 4) + zp_mapX;
        int8_t mapStepX; 
        int8_t mapStepY;

        // 3. FAST SideDist Calculation
        if(rDX < 0) {
            mapStepX = -1;
            zp_sideDistX = mulFrac7Fast(zp_deltaX, fracX);
        } else {
            mapStepX = 1;
            zp_sideDistX = mulFrac7Fast(zp_deltaX, invFracX);
        }
        
        if(rDY < 0) {
            mapStepY = -16; // -mapWidth
            zp_sideDistY = mulFrac7Fast(zp_deltaY, fracY);
        } else {
            mapStepY = 16; // +mapWidth
            zp_sideDistY = mulFrac7Fast(zp_deltaY, invFracY);
        }
        
        // 4. Tight DDA Loop
        while(mapPtr[mapOffset] == 0) {
            if(zp_sideDistX < zp_sideDistY) {
                zp_sideDistX += zp_deltaX;
                mapOffset += mapStepX;
                zp_side = 0;
            } else {
                zp_sideDistY += zp_deltaY;
                mapOffset += mapStepY;
                zp_side = 1;
            }
        }
        
        // Coordinates reconstruction removed (unused)

        int16_t rawDist = (zp_side == 0) ? 
            (zp_sideDistX - zp_deltaX) : 
            (zp_sideDistY - zp_deltaY);
        
        // Store in ZBuffer for sprite rendering (store raw distance)
        int16_t zVal = (rawDist < 0) ? 0 : rawDist;
        fillZSpan(ZBuffer, zp_x, xSpan, zVal);
        
        uint16_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = (rawDist > 0) ?
                (int)(FpF16<7>(h) / FpF16<7>::FromRaw(rawDist)) : h;
            if (lineHeight > 255) lineHeight = 255;
        }
        
        uint8_t texNum = ((mapPtr[mapOffset] - 1) * 2 + zp_side) & (NUM_TEXTURES - 1);
        int16_t drawStart = (-((int16_t)lineHeight) >> 1) + (h >> 1);
        if (drawStart < 0) drawStart = 0;
        uint16_t drawEnd = drawStart + lineHeight;
        if (drawEnd > h) drawEnd = h;

        // Wall X calculation using raw int16 arithmetic (avoids 3 FpF multiplies)
        int16_t wallRaw;
        if (zp_side == 0) {
            wallRaw = posYRaw + (int16_t)(((int32_t)rawDist * rDY) >> 7);
        } else {
            wallRaw = posXRaw + (int16_t)(((int32_t)rawDist * rDX) >> 7);
        }
        // Extract 7-bit fractional part, scale by texRepeat*texWidth=64 (<<6), then >>7 = >>1
        // Result is 0..63, mask to texWidth with & 0x0F
        uint8_t frac7 = wallRaw & 0x7F;
        int texX = (frac7 >> 1) & 0x0F;
        if(zp_side == 0 && rDX > 0) texX = texWidth - texX - 1;
        if(zp_side == 1 && rDY < 0) texX = texWidth - texX - 1;

        if (texNum != lastTexNum || texX != lastTexX) {
            fetchTextureColumn(texNum, texX);
            lastTexNum = texNum;
            lastTexX = texX;
        }
        
        // Use precomputed table for all lineHeight values (no runtime division)
        int16_t raw_step = texStepValues[lineHeight].GetRawVal();
        int16_t raw_texPos = (lineHeight > h) ? 
            texOffsetTable[lineHeight] : 0;
        if (raw_texPos < 0) raw_texPos = 0;

        uint8_t* bufPtr = &buffer[zp_x];
        uint8_t xiSample = (uint8_t)(zp_x + (xSpan >> 1));
        if (xiSample >= w) xiSample = (uint8_t)(w - 1);
        uint8_t xi = floorXtoTileX[xiSample];  // low-res column index

        if (currentStep == 2) {
            for (zp_y = 0; zp_y < drawStart; zp_y += 2) {
                uint8_t yi0 = ceilYtoTileY[zp_y];
                uint8_t texOffset0 = floorTileRows[yi0][xi];
                uint8_t c0 = ceilTexCache[texOffset0];
                fillSpan(bufPtr, xSpan, c0);
                bufPtr += w;
                if (zp_y + 1 < drawStart) {
                    uint8_t yi1 = ceilYtoTileY[zp_y + 1];
                    uint8_t texOffset1 = floorTileRows[yi1][xi];
                    uint8_t c1 = ceilTexCache[texOffset1];
                    fillSpan(bufPtr, xSpan, c1);
                    bufPtr += w;
                }
            }
            for (zp_y = drawStart; zp_y < drawEnd; zp_y += 2) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                uint8_t color = texColumnBuffer[texY];
                fillSpan(bufPtr, xSpan, color);
                bufPtr += w;
                if (zp_y + 1 < drawEnd) {
                    fillSpan(bufPtr, xSpan, color);
                    bufPtr += w;
                }
                raw_texPos += (raw_step << 1);
            }
            for (zp_y = drawEnd; zp_y < h; zp_y += 2) {
                uint8_t yi0 = floorYtoTileY[zp_y];
                uint8_t texOffset0 = floorTileRows[yi0][xi];
                uint8_t f0 = floorTexCache[texOffset0];
                fillSpan(bufPtr, xSpan, f0);
                bufPtr += w;
                if (zp_y + 1 < h) {
                    uint8_t yi1 = floorYtoTileY[zp_y + 1];
                    uint8_t texOffset1 = floorTileRows[yi1][xi];
                    uint8_t f1 = floorTexCache[texOffset1];
                    fillSpan(bufPtr, xSpan, f1);
                    bufPtr += w;
                }
            }
        } else {
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                uint8_t yi = ceilYtoTileY[zp_y];
                uint8_t texOffset = floorTileRows[yi][xi];
                *bufPtr = ceilTexCache[texOffset];
                bufPtr += w;
            }
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                *bufPtr = texColumnBuffer[texY];
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                uint8_t yi = floorYtoTileY[zp_y];
                uint8_t texOffset = floorTileRows[yi][xi];
                *bufPtr = floorTexCache[texOffset];
                bufPtr += w;
            }
        }

        zp_x += rayStep;
    }

    // draw_scan_line();
    
    // Render sprites to buffer after walls but before drawing to screen
    renderSprites();
    
    // Draw buffer to screen
    if (interlacedMode) {
        drawBufferDouble_Optimized_Interlaced(false);
    } else {
        drawBufferDouble_Optimized();
    }
    
    return 0;
}

void draw_ui() {
  fill_rect_fast(18, startX, startY, 32, 32);
  
}

void draw_map() {
    uint8_t playerTileX = (uint8_t)(int)posX;
    uint8_t playerTileY = (uint8_t)(int)posY;
    uint16_t radius2 = 0;
    uint16_t prevRadius2 = 0;
    static uint16_t drawDist2 = (uint16_t)map_draw_distance * (uint16_t)map_draw_distance;
    if (scan_active) {
        uint8_t maxDx = playerTileX;
        uint8_t maxDx2 = (mapWidth - 1) - playerTileX;
        if (maxDx2 > maxDx) maxDx = maxDx2;

        uint8_t maxDy = playerTileY;
        uint8_t maxDy2 = (mapHeight - 1) - playerTileY;
        if (maxDy2 > maxDy) maxDy = maxDy2;

        uint16_t maxDist2 = (uint16_t)maxDx * (uint16_t)maxDx + (uint16_t)maxDy * (uint16_t)maxDy;
        if (maxDist2 == 0) maxDist2 = 1;

        radius2 = (uint16_t)(((uint32_t)scan_frame * maxDist2) / (SCAN_FRAMES - 1));
        if (scan_frame > 0) {
            prevRadius2 = (uint16_t)(((uint32_t)(scan_frame - 1) * maxDist2) / (SCAN_FRAMES - 1));
        }
    }

    // Scaled to 2x2 pixels for visibility
    for (uint8_t i = 0; i < mapHeight; i++) {
        int8_t* mapRow = worldMap[i];
        uint8_t* decayRow = scan_decay[i];
        uint16_t py = (uint16_t)(i << 1) + startY;
        for (uint8_t j = 0; j < mapWidth; j++) {
            uint8_t color = 18;
            int8_t cell = mapRow[j];
            if (cell > 0) {
                uint8_t dx = (j > playerTileX) ? (uint8_t)(j - playerTileX) : (uint8_t)(playerTileX - j);
                uint8_t dy = (i > playerTileY) ? (uint8_t)(i - playerTileY) : (uint8_t)(playerTileY - i);
                uint16_t dist2 = (uint16_t)dx * (uint16_t)dx + (uint16_t)dy * (uint16_t)dy;

                if (scan_active) {
                    if (dist2 >= prevRadius2 && dist2 <= radius2) {
                        decayRow[j] = SCAN_DECAY_MAX;
                        color = WHITE;
                    } else if (decayRow[j] > 0) {
                        if (cell == 1) {
                            color = mapValue(decayRow[j], 1, SCAN_DECAY_MAX, 18, 28);
                        } else {
                            color = DARK_RED;
                        }
                    }
                } else if (dist2 <= drawDist2) {
                    color = 32;
                }
            }
            uint16_t px = (uint16_t)(j << 1) + startX;
            fill_rect_fast(color, px, py, 2, 2);
        }
    }

    if (scan_active) {
        // Draw sprites only during active scan
        for (uint8_t i = 0; i < numSprites; i++) {
            Sprite* spr = &sprites[i];
            uint8_t* decay = &sprite_scan_decay[i];
            int8_t sX = (int8_t)(int)spr->x;
            int8_t sY = (int8_t)(int)spr->y;

            if (sX >= 0 && sX < mapWidth && sY >= 0 && sY < mapHeight) {
                uint8_t usX = (uint8_t)sX;
                uint8_t usY = (uint8_t)sY;
                uint8_t dx = (usX > playerTileX) ? (uint8_t)(usX - playerTileX) : (uint8_t)(playerTileX - usX);
                uint8_t dy = (usY > playerTileY) ? (uint8_t)(usY - playerTileY) : (uint8_t)(playerTileY - usY);
                uint16_t dist2 = (uint16_t)dx * (uint16_t)dx + (uint16_t)dy * (uint16_t)dy;

                if (dist2 >= prevRadius2 && dist2 <= radius2) {
                    *decay = SCAN_DECAY_MAX;
                }

                if (*decay > 0 && *decay < SCAN_DECAY_MAX) {
                    uint8_t color = 10 + spr->texture;
                    draw_pixel(color, (uint16_t)(usX << 1) + startX, (uint16_t)(usY << 1) + startY);
                }
            }
        }
    }
} 

void draw_needle() {

    // Map currentRotStep (0-31) to table index
    uint8_t i = (32 - currentRotStep) % 32;
    
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Update only the transform matrix (rotation)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_32[32-i]);
    
}


void draw_player(){
    if (!map_visible) {
        return;
    }
    // Scale 2x for visibility
    FpF16<7> ts(2);
    uint16_t x = (int)(posX * ts) + startX;
    uint16_t y = (int)(posY * ts) + startY;
    
    fill_rect_fast(DARK_GREEN, prevPlayerX, prevPlayerY, 2, 2);
    fill_rect_fast(YELLOW, x, y, 2, 2);

    draw_7segment_double(GREEN, (int16_t)(fps), 270, 68);
    // draw_7segment_double(GREEN, (int16_t)(posX), 270, 68);
    // draw_7segment_double(GREEN, (int16_t)(posY), 295, 68);
    draw_7segment_double(GREEN, (int16_t)(coarseSidePercent), 295, 68);
    prevPlayerX = x;
    prevPlayerY = y;
}

void handleCalculation() {
    gamestate = GAMESTATE_CALCULATING;
    draw_needle();
    raycastF();
    gamestate = GAMESTATE_IDLE;
}

void WaitForAnyKey(){
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;
    while (RIA.rw0 & 1);
}


void init_needle_sprite() {
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Calculate positions with proper parentheses to avoid warnings
    int16_t needle_x = (NEEDLE_CENTER_X - NEEDLE_SIZE/2);
    int16_t needle_y = (NEEDLE_CENTER_Y - NEEDLE_SIZE/2);
    
    // Set initial rotation (0 degrees = pointing up)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_32[32]);
    
    // Set position (centered) - use variables to avoid macro warnings
    xram0_struct_set(ptr, vga_mode4_asprite_t, x_pos_px, needle_x);
    xram0_struct_set(ptr, vga_mode4_asprite_t, y_pos_px, needle_y);
    
    // Set sprite data pointer
    xram0_struct_set(ptr, vga_mode4_asprite_t, xram_sprite_ptr, NEEDLE_SPRITE_ADDR);
    
    // CRITICAL: Set log_size correctly for 32x32 sprite
    xram0_struct_set(ptr, vga_mode4_asprite_t, log_size, 5);  // 2^5 = 32
    
    // No opacity metadata
    xram0_struct_set(ptr, vga_mode4_asprite_t, has_opacity_metadata, false);
    
    // Enable Mode 4 affine sprite plane
    // plane=1, num_sprites=1, affine=1
    xregn(1, 0, 1, 5, 4, 1, NEEDLE_CONFIG_ADDR, 1, 1);
}

void placeSprites() {
    for (int i = 0; i < numSprites; i++) {
        int x, y;
        bool valid = false;
        int attempts = 0;
        while (!valid) {
            x = abs(rand()) % mapWidth;
            y = abs(rand()) % mapHeight;
            
            if (worldMap[y][x] == 0) {
                valid = true;
                // Check if occupied by another sprite within a wider area (avoid clusters)
                for (int j = 0; j < i; j++) {
                    int sx = (int)sprites[j].x;
                    int sy = (int)sprites[j].y;
                    
                    // Check for overlap within 2 tiles distance (covers 3x3 area centering on sprite)
                    if (abs(x - sx) < 2 && abs(y - sy) < 2) {
                        valid = false;
                        break;
                    }
                }
            }
            
            attempts++;
            if (attempts > 200) {
                // If we can't find a spot, just place it (or could skip)
                // We'll accept the last random pos if it was at least on a floor
                if (worldMap[y][x] == 0) valid = true;
            }
        }

        // Place in center of cell
        sprites[i].x = FpF16<7>(x) + FpF16<7>(0.5);
        sprites[i].y = FpF16<7>(y) + FpF16<7>(0.5);
        sprites[i].texture = abs(rand()) % NUM_SPRITES;
    }
}

void updateWindowSize(int8_t new_w) {
    if (new_w < 64) new_w = 64;
    if (new_w > MAX_WINDOW_WIDTH) new_w = MAX_WINDOW_WIDTH;
    
    // Round to nearest 8 pixels for unrolled loops
    new_w = (new_w + 4) & ~7;
    
    if (new_w == w) return;
    
    // Clear old window area
    fillBuffer(18);
    
    w = new_w;
    h = (w * 2) / 3;
    h = h & ~7; // Round to nearest 8 pixels
    if (h > MAX_WINDOW_HEIGHT) h = MAX_WINDOW_HEIGHT;
    
    xOffset = ((SCREEN_WIDTH - w * 2) / 2) - 8;
    yOffset = ((SCREEN_HEIGHT - h * 2) / 2) - 20;
    
    // Recalculate tables
    precalculateRotations();
    precalculateLineHeights();
    precalculateFloorTables();

    gamestate = GAMESTATE_MOVING;
}

int16_t main() {
    bool paused = false;
    uint8_t timer = 0;
    bool scan_key_latch = false;
    bool t_key_latch = false;
    bool q_key_latch = false;
    bool e_key_latch = false;
    bool m_key_latch = false;

    prevPlayerX = (int)(posX * FpF16<7>(TILE_SIZE));
    prevPlayerY = (int)(posY * FpF16<7>(TILE_SIZE));

    gamestate = GAMESTATE_INIT;

    initializeMaze();

    srand(4);
    int startPosX = (random(1, ((mapWidth - 2) / 2)) * 2 + 1);
    int startPoxY = (random(1, ((mapHeight - 2) / 2)) * 2 + 1);

    iterativeDFS(startPosX, startPoxY);
    setEntryAndFinish(startPosX, startPoxY);

    posX = FpF16<7>(startPosX);
    posY = FpF16<7>(startPoxY);

    // print_map();

    placeSprites();

    precalculateRotations();
    precalculateLineHeights();
    precalculateFloorTables();
    
    // Initialize active vectors
    currentRotStep = 15;
    updateRaycasterVectors(); 

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    RIA.addr0 = PALETTE_XRAM_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 256; i++) {
        uint16_t color = custom_palette[i];
        RIA.rw0 = (uint8_t)(color & 0xFF); 
        RIA.rw0 = (uint8_t)(color >> 8);   
    }

    xram0_struct_set(0xFF00, vga_mode3_config_t, xram_palette_ptr, PALETTE_XRAM_ADDR);

    uint16_t clock = ria_call_int(RIA_OP_CLOCK);
    uint16_t last_fps_clock = clock;

    draw_ui();
    init_needle_sprite();
    WaitForAnyKey();

    handleCalculation();


    while (true) {
        uint16_t frame_start_clock = ria_call_int(RIA_OP_CLOCK);
        if (!scan_active) {

            xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
            RIA.addr0 = KEYBOARD_INPUT;
            RIA.step0 = 1;

            for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
                keystates[i] = RIA.rw0;
            }

            if (!(keystates[0] & 1)) {
                bool space_down = key(KEY_SPACE);
                if (space_down && !scan_key_latch) {
                    scan_active = true;
                    scan_frame = 0;
                }
                scan_key_latch = space_down;

                // Window Resizing
                if (key(KEY_EQUAL) || key(KEY_KPPLUS)) { 
                    updateWindowSize(w + 8);
                }
                if (key(KEY_MINUS) || key(KEY_KPMINUS)) {
                    updateWindowSize(w - 8);
                }

                uint8_t rotateStep = 1;
                if (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) {
                    rotateStep = 2;
                }

                if (key(KEY_RIGHT)){
                    gamestate = GAMESTATE_MOVING;
                    currentRotStep = (currentRotStep + rotateStep) % ROTATION_STEPS;
                    updateRaycasterVectors();
                }
                if (key(KEY_LEFT)){
                    gamestate = GAMESTATE_MOVING;
                    currentRotStep = (currentRotStep - rotateStep + ROTATION_STEPS) % ROTATION_STEPS;
                    updateRaycasterVectors();
                }
                if (key(KEY_UP)) {
                    gamestate = GAMESTATE_MOVING;
                    if(worldMap[int(posY)][int(posX + (dirX * moveSpeed) * playerScale)] == false) posX += (dirX * moveSpeed);
                    if(worldMap[int(posY + (dirY * moveSpeed) * playerScale)][int(posX)] == false) posY +=  (dirY * moveSpeed);
                }
                if (key(KEY_DOWN)) {
                    gamestate = GAMESTATE_MOVING;
                    if(worldMap[int(posY)][int(posX - (dirX * moveSpeed) * playerScale)] == false) posX -= (dirX * moveSpeed);
                    if(worldMap[int(posY - (dirY * moveSpeed) * playerScale)][int(posX)] == false) posY -= (dirY * moveSpeed);
                }
                // Strafe Right
                if (key(KEY_D)) {
                    gamestate = GAMESTATE_MOVING;
                    // Perpendicular vector (rotate right 90 deg: x' = -y, y' = x)
                    // But based on analysis: Right is (-dirY, dirX)
                    FpF16<7> strafeX = -dirY;
                    FpF16<7> strafeY = dirX;

                    if(worldMap[int(posY)][int(posX + (strafeX * moveSpeed) * playerScale)] == false) posX += (strafeX * moveSpeed);
                    if(worldMap[int(posY + (strafeY * moveSpeed) * playerScale)][int(posX)] == false) posY += (strafeY * moveSpeed);
                }
                // Strafe Left
                if (key(KEY_A)) {
                    gamestate = GAMESTATE_MOVING;
                    // Perpendicular vector (rotate left 90 deg)
                    // Left is (dirY, -dirX)
                    FpF16<7> strafeX = dirY;
                    FpF16<7> strafeY = -dirX;

                    if(worldMap[int(posY)][int(posX + (strafeX * moveSpeed) * playerScale)] == false) posX += (strafeX * moveSpeed);
                    if(worldMap[int(posY + (strafeY * moveSpeed) * playerScale)][int(posX)] == false) posY += (strafeY * moveSpeed);
                }
                bool m_down = key(KEY_M);
                if (m_down && !m_key_latch) {
                    interlacedMode = !interlacedMode;
                    if (interlacedMode) {
                        fillBuffer(BLACK);
                    }
                    draw_ui();
                }
                m_key_latch = m_down;
                bool t_down = key(KEY_T);
                if (t_down && !t_key_latch) {
                    floorDisplayMode = (uint8_t)((floorDisplayMode + 1) % 2);
                }
                t_key_latch = t_down;

                bool q_down = key(KEY_Q);
                if (q_down && !q_key_latch) {
                    if (coarseSidePercent >= 5) {
                        coarseSidePercent = (uint8_t)(coarseSidePercent - 5);
                    } else {
                        coarseSidePercent = 0;
                    }
                }
                q_key_latch = q_down;

                bool e_down = key(KEY_E);
                if (e_down && !e_key_latch) {
                    if (coarseSidePercent <= 45) {
                        coarseSidePercent = (uint8_t)(coarseSidePercent + 5);
                    } else {
                        coarseSidePercent = 50;
                    }
                }
                e_key_latch = e_down;

                if (key(KEY_S)) {
                    render_sprites = !render_sprites;
                }
                if (key(KEY_R)) {
                    floorRowSkipEnabled = !floorRowSkipEnabled;
                }
                if (key(KEY_ESC)) {
                    break;
                }
            }

        }
        if (!paused && !scan_active) {
            if (gamestate == GAMESTATE_MOVING) {
                currentStep = movementStep;
                timer = 0;
            } else {
                if (timer < 2) timer++;
                if (timer == 2 && currentStep > 1) {
                    currentStep = 1;
                }
            }
            handleCalculation();
        }


        map_visible = true;
            draw_map();
            draw_player();
        if (scan_active) {
            for (uint8_t y = 0; y < mapHeight; y++) {
                for (uint8_t x = 0; x < mapWidth; x++) {
                    if (scan_decay[y][x] > 0) {
                        scan_decay[y][x]--;
                    }
                }
            }
                for (uint8_t i = 0; i < numSprites; i++) {
                    if (sprite_scan_decay[i] > 0) {
                        sprite_scan_decay[i]--;
                    }
                }
            }

            if (scan_frame + 1 >= SCAN_FRAMES + SCAN_DECAY_MAX) {
                scan_active = false;
                scan_frame = 0;
            } else {
                scan_frame++;
            }

        uint16_t frame_end_clock = ria_call_int(RIA_OP_CLOCK);
        uint16_t frame_ticks = frame_end_clock - frame_start_clock;
        if (frame_ticks > 0) {
            fps = (uint8_t)(CLOCK_TICKS_PER_SEC / frame_ticks);
        } else {
            fps = CLOCK_TICKS_PER_SEC;
        }
        if ((uint16_t)(frame_end_clock - last_fps_clock) >= CLOCK_TICKS_PER_SEC) {
            last_fps_clock = frame_end_clock;
        }
    }
    return 0;
}