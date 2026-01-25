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
__attribute__((section(".zp.bss"))) static int zp_mapX;
__attribute__((section(".zp.bss"))) static int zp_mapY;
__attribute__((section(".zp.bss"))) static uint8_t zp_side;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistX;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistY;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaX;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaY;

using namespace mn::MFixedPoint;

#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
#define WINDOW_WIDTH 96
#define WINDOW_HEIGTH 64

#define ROTATION_STEPS 48
#define QUADRANT_STEPS 12 

FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1); 
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0); 
FpF16<7> moveSpeed(0.2); 
FpF16<7> playerScale(5);

// sin(pi/24) and cos(pi/24) for 7.5 degree steps
FpF16<7> sin_r(0.130526192); 
FpF16<7> cos_r(0.991444861); 

uint16_t startX = 151+(26-mapWidth)/2;
uint16_t startY = 137+(26-mapHeight)/2;

uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 1;
int8_t movementStep = 2; 

uint8_t xOffset = 68;
uint8_t yOffset = 3; 

uint16_t texSize = texWidth * texHeight -1;
const uint8_t w = WINDOW_WIDTH;
const uint8_t h = WINDOW_HEIGTH; 
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

uint8_t lineHeightTable[1024]; 

// Base Vector Tables (Only 1 Quadrant)
FpF16<7> dirXValues[QUADRANT_STEPS];
FpF16<7> dirYValues[QUADRANT_STEPS];
FpF16<7> planeXValues[QUADRANT_STEPS];
FpF16<7> planeYValues[QUADRANT_STEPS];

FpF16<7> texStepValues[WINDOW_HEIGTH+1];

// --- OPTIMIZATION: Cached Tables ---
// Q1 Positive (Standard)
FpF16<7> rayDirX_Q1[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> rayDirY_Q1[QUADRANT_STEPS][WINDOW_WIDTH];

// Q1 Negative (Pre-calculated negation to avoid runtime math/copying)
FpF16<7> rayDirX_Q1_Neg[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> rayDirY_Q1_Neg[QUADRANT_STEPS][WINDOW_WIDTH];

// Delta Dists for ALL 4 quadrants (not just Q1)
FpF16<7> deltaDistX_Q0[QUADRANT_STEPS][WINDOW_WIDTH];  // 0-90°
FpF16<7> deltaDistY_Q0[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistX_Q1[QUADRANT_STEPS][WINDOW_WIDTH];  // 90-180°
FpF16<7> deltaDistY_Q1[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistX_Q2[QUADRANT_STEPS][WINDOW_WIDTH];  // 180-270°
FpF16<7> deltaDistY_Q2[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistX_Q3[QUADRANT_STEPS][WINDOW_WIDTH];  // 270-360°
FpF16<7> deltaDistY_Q3[QUADRANT_STEPS][WINDOW_WIDTH];

// These point to the correct row in the tables above
FpF16<7>* activeRayDirX;
FpF16<7>* activeRayDirY;
FpF16<7>* activeDeltaDistX;
FpF16<7>* activeDeltaDistY;

FpF16<7> cameraXValues[WINDOW_WIDTH];

int16_t texOffsetTable[64]; 
uint8_t texColumnBuffer[32]; 

// values for sprite rotation
static const int16_t sin_fix8_48[] = {
      0,  33,  66,  98, 128, 156, 181, 203, 222, 237, 247, 252,  // 0-82.5°
    256, 252, 247, 237, 222, 203, 181, 156, 128,  98,  66,  33,  // 90-172.5°
      0, -33, -66, -98,-128,-156,-181,-203,-222,-237,-247,-252,  // 180-262.5°
   -256,-252,-247,-237,-222,-203,-181,-156,-128, -98, -66, -33,  // 270-352.5°
      0
};

static const int16_t cos_fix8_48[] = {
    256, 252, 247, 237, 222, 203, 181, 156, 128,  98,  66,  33,  // 0-82.5°
      0, -33, -66, -98,-128,-156,-181,-203,-222,-237,-247,-252,  // 90-172.5°
   -256,-252,-247,-237,-222,-203,-181,-156,-128, -98, -66, -33,  // 180-262.5°
      0,  33,  66,  98, 128, 156, 181, 203, 222, 237, 247, 252,  // 270-352.5°
    256
};

// fix_8((1+(sin(theta_deg)-cos(theta_deg)))/2) for 48 steps
static const int16_t t2_fix8_48[] = {
      0,  19,  37,  56,  81, 103, 128, 150, 175, 196, 219, 237,  // 0-82.5°
    256, 271, 285, 294, 303, 306, 309, 306, 303, 294, 285, 271,  // 90-172.5°
    256, 237, 219, 196, 175, 150, 128, 103,  81,  56,  37,  19,  // 180-262.5°
      0, -15, -29, -38, -47, -50, -53, -50, -47, -38, -29, -15,  // 270-352.5°
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

#define NEEDLE_CENTER_X 294
#define NEEDLE_CENTER_Y 50

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

void drawBufferDouble_Optimized() {
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
                    uint8_t c = *p++; \
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

void drawBufferDouble_Optimized_Interlaced(bool oddField) {
    uint16_t screen_addr = SCREEN_WIDTH * (yOffset + (oddField ? 1 : 0)) + xOffset;
    uint8_t* buffer_ptr_loc = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;
        uint8_t* p = buffer_ptr_loc;
        // Unroll 8 pixels per block (width must be multiple of 8)
        const uint8_t blocks = w >> 3;
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
void precalculateRotations() {
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    invW = FpF16<7>(1) / FpF16<7>(w); 
    FpF16<7> fw = FpF16<7>(w);
    
    texStepValues[0] = FpF16<7>(texHeight); 
    for (uint8_t i = 1; i < WINDOW_HEIGTH+1; i++) {
      texStepValues[i] = FpF16<7>(texHeight) / FpF16<7>(i);
    }

    for(uint8_t x = 0; x < w; x++) {
        cameraXValues[x] = FpF16<7>(2 * x) / fw - FpF16<7>(1);
    }

    printf("Precalc Q1 & all quadrant deltas...");
    
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

          // Q0: 0-90° - Use base directions
          deltaDistX_Q0[i][x] = calcDelta(rayDirX);
          deltaDistY_Q0[i][x] = calcDelta(rayDirY);

          // Q1: 90-180° - (x,y) -> (-y, x)
          FpF16<7> rayDirX_Q1_t = -rayDirY;
          FpF16<7> rayDirY_Q1_t = rayDirX;
          deltaDistX_Q1[i][x] = calcDelta(rayDirX_Q1_t);
          deltaDistY_Q1[i][x] = calcDelta(rayDirY_Q1_t);

          // Q2: 180-270° - (x,y) -> (-x, -y)
          FpF16<7> rayDirX_Q2 = -rayDirX;
          FpF16<7> rayDirY_Q2 = -rayDirY;
          deltaDistX_Q2[i][x] = calcDelta(rayDirX_Q2);
          deltaDistY_Q2[i][x] = calcDelta(rayDirY_Q2);

          // Q3: 270-360° - (x,y) -> (y, -x)
          FpF16<7> rayDirX_Q3 = rayDirY;
          FpF16<7> rayDirY_Q3 = -rayDirX;
          deltaDistX_Q3[i][x] = calcDelta(rayDirX_Q3);
          deltaDistY_Q3[i][x] = calcDelta(rayDirY_Q3);
      }

      FpF16<7> oldDirX = currentDirX;
      currentDirX = currentDirX * cos_r - currentDirY * sin_r;
      currentDirY = oldDirX * sin_r + currentDirY * cos_r;

      FpF16<7> oldPlaneX = currentPlaneX;
      currentPlaneX = currentPlaneX * cos_r - currentPlaneY * sin_r;
      currentPlaneY = oldPlaneX * sin_r + currentPlaneY * cos_r;
    }
    printf("Done\n");
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
            activeDeltaDistX = deltaDistX_Q0[idx];  // Use Q0 deltas
            activeDeltaDistY = deltaDistY_Q0[idx];
            break;

        case 1: // 90-180 deg
            currDirX = -dirYValues[idx]; 
            currDirY = dirXValues[idx];
            currPlaneX = -planeYValues[idx]; 
            currPlaneY = planeXValues[idx];

            activeRayDirX = rayDirY_Q1_Neg[idx];
            activeRayDirY = rayDirX_Q1[idx];
            activeDeltaDistX = deltaDistX_Q1[idx];  // Use Q1 deltas
            activeDeltaDistY = deltaDistY_Q1[idx];
            break;

        case 2: // 180-270 deg
            currDirX = -dirXValues[idx]; 
            currDirY = -dirYValues[idx];
            currPlaneX = -planeXValues[idx]; 
            currPlaneY = -planeYValues[idx];

            activeRayDirX = rayDirX_Q1_Neg[idx];
            activeRayDirY = rayDirY_Q1_Neg[idx];
            activeDeltaDistX = deltaDistX_Q2[idx];  // Use Q2 deltas
            activeDeltaDistY = deltaDistY_Q2[idx];
            break;

        case 3: // 270-360 deg
            currDirX = dirYValues[idx]; 
            currDirY = -dirXValues[idx];
            currPlaneX = planeYValues[idx]; 
            currPlaneY = -planeXValues[idx];

            activeRayDirX = rayDirY_Q1[idx];
            activeRayDirY = rayDirX_Q1_Neg[idx];
            activeDeltaDistX = deltaDistX_Q3[idx];  // Use Q3 deltas
            activeDeltaDistY = deltaDistY_Q3[idx];
            break;
    }

    dirX = currDirX;
    dirY = currDirY;
    planeX = currPlaneX;
    planeY = currPlaneY;
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
    for (int i = 1; i < 64; i++) {
        texOffsetTable[i] = 2048 - (int16_t)(110592L / i);
        if (texOffsetTable[i] < 0) texOffsetTable[i] = 0;
    }
}

int raycastF() {
    // 1. Use the pointers already set by updateRaycasterVectors()
    // This avoids 2D array indexing overhead inside the frame
    FpF16<7>* rayDirXPtr = activeRayDirX;
    FpF16<7>* rayDirYPtr = activeRayDirY;
    FpF16<7>* deltaDistXPtr = activeDeltaDistX;
    FpF16<7>* deltaDistYPtr = activeDeltaDistY;
    
    // 2. Pre-calculate fractional positions ONCE per frame
    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    const int16_t fracX = posXRaw & 0x7F;        
    const int16_t invFracX = 128 - fracX;        
    const int16_t fracY = posYRaw & 0x7F;
    const int16_t invFracY = 128 - fracY;

    const int mapX_start = posXRaw >> 7;
    const int mapY_start = posYRaw >> 7;

    for (zp_x = 0; zp_x < w; zp_x += currentStep) {
        // Load cached values into Zero Page registers
        zp_deltaX = deltaDistXPtr[zp_x].GetRawVal();
        zp_deltaY = deltaDistYPtr[zp_x].GetRawVal();
        int16_t rDX = rayDirXPtr[zp_x].GetRawVal();
        int16_t rDY = rayDirYPtr[zp_x].GetRawVal();
        
        zp_mapX = mapX_start;
        zp_mapY = mapY_start;
        int8_t stepX, stepY;

        // 3. FAST SideDist Calculation
        if(rDX < 0) {
            stepX = -1;
            zp_sideDistX = ((int32_t)fracX * zp_deltaX) >> 7;
        } else {
            stepX = 1;
            zp_sideDistX = ((int32_t)invFracX * zp_deltaX) >> 7;
        }
        
        if(rDY < 0) {
            stepY = -1;
            zp_sideDistY = ((int32_t)fracY * zp_deltaY) >> 7;
        } else {
            stepY = 1;
            zp_sideDistY = ((int32_t)invFracY * zp_deltaY) >> 7;
        }
        
        // 4. Tight DDA Loop
        while(worldMap[zp_mapX][zp_mapY] == 0) {
            if(zp_sideDistX < zp_sideDistY) {
                zp_sideDistX += zp_deltaX;
                zp_mapX += stepX;
                zp_side = 0;
            } else {
                zp_sideDistY += zp_deltaY;
                zp_mapY += stepY;
                zp_side = 1;
            }
        }

        // 5. Calculate Distance using ZP variables
        int16_t rawDist = (zp_side == 0) ? 
            (zp_sideDistX - zp_deltaX) : 
            (zp_sideDistY - zp_deltaY);
        
        uint16_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = (rawDist > 0) ?
                (int)(FpF16<7>(h) / FpF16<7>::FromRaw(rawDist)) : h;
            if (lineHeight > 255) lineHeight = 255;
        }
        
        uint8_t texNum = (worldMap[zp_mapX][zp_mapY] - 1) * 2 + zp_side;
        int16_t drawStart = (-((int16_t)lineHeight) >> 1) + (h >> 1);
        if (drawStart < 0) drawStart = 0;
        uint16_t drawEnd = drawStart + lineHeight;
        if (drawEnd > h) drawEnd = h;

        // Wall X calculation using rDX and rDY
        FpF16<7> perpWallDist = FpF16<7>::FromRaw(rawDist);
        FpF16<7> wallX = (zp_side == 0) ? 
            (posY + perpWallDist * FpF16<7>::FromRaw(rDY)) : 
            (posX + perpWallDist * FpF16<7>::FromRaw(rDX));
        wallX -= floorFixed(wallX);
        
        int texX = (int)(wallX * FpF16<7>(texWidth));
        if(zp_side == 0 && rDX > 0) texX = texWidth - texX - 1;
        if(zp_side == 1 && rDY < 0) texX = texWidth - texX - 1;

        fetchTextureColumn(texNum, texX);
        
        int16_t raw_step = (lineHeight <= h) ?
            texStepValues[lineHeight].GetRawVal() :
            (FpF16<7>(texHeight) / FpF16<7>((int16_t)lineHeight)).GetRawVal();
        int16_t raw_texPos = (lineHeight > h) ? 
            texOffsetTable[(lineHeight > 63) ? 63 : lineHeight] : 0;
        if (raw_texPos < 0) raw_texPos = 0;

        uint8_t* bufPtr = &buffer[zp_x];
        uint8_t* c_ptr = ceilingColors;
        uint8_t* f_ptr = floorColors;

        if (currentStep == 2) {
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                uint8_t c = *c_ptr++; 
                bufPtr[0] = c; bufPtr[1] = c;
                bufPtr += w;
            }
            f_ptr += drawEnd; 
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                uint8_t color = texColumnBuffer[texY];
                bufPtr[0] = color; bufPtr[1] = color;
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                uint8_t color = *f_ptr++;  
                bufPtr[0] = color; bufPtr[1] = color;
                bufPtr += w;
            }
        } else {
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                *bufPtr = *c_ptr++;
                bufPtr += w;
            }
            f_ptr += drawEnd;
            for (zp_y = drawStart; zp_y < drawEnd; ++zp_y) {
                uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);
                *bufPtr = texColumnBuffer[texY];
                raw_texPos += raw_step;
                bufPtr += w;
            }
            for (zp_y = drawEnd; zp_y < h; ++zp_y) {
                *bufPtr = *f_ptr++;  
                bufPtr += w;
            }
        }
    }
    
    // drawBufferDouble_Optimized();
    if (bigMode) {
        drawBufferDouble_Optimized();
    } else {
        drawBufferDouble_Optimized_Interlaced(true);
    }
    return 0;
}

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
                default: printf("?"); 
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
          switch(worldMap[i][j])
          {
            case 1: color = 37; break; 
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

    // Map currentRotStep (0-47) to table index
    uint8_t i = (48 - currentRotStep) % 48;
    
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Update only the transform matrix (rotation)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_48[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_48[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_48[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_48[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_48[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_48[48-i]);
    
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


void init_needle_sprite() {
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Calculate positions with proper parentheses to avoid warnings
    int16_t needle_x = (NEEDLE_CENTER_X - NEEDLE_SIZE/2);
    int16_t needle_y = (NEEDLE_CENTER_Y - NEEDLE_SIZE/2);
    
    // Set initial rotation (0 degrees = pointing up)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_48[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_48[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_48[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_48[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_48[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_48[48-0-1]);
    
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

int16_t main() {
    bool handled_key = false;
    bool paused = false;
    bool show_buffers_indicators = true;
    uint8_t mode = 0;
    uint8_t i = 0;
    uint8_t timer = 0;

    for(int i = 0; i < h / 2; i++) {
        uint8_t sky_idx = mapValue(i, 0, h / 2, 16, 31);
        ceilingColors[i] = sky_idx;
        uint8_t floor_idx = mapValue(i, 0, h / 2, 32, 63); 
        floorColors[i + (h / 2)] = floor_idx;
    }

    prevPlayerX = (int)(posX * FpF16<7>(TILE_SIZE));
    prevPlayerY = (int)(posY * FpF16<7>(TILE_SIZE));

    gamestate = GAMESTATE_INIT;

    initializeMaze();

    printf("generating maze...\n");
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
    
    // Initialize active vectors
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

    draw_ui();
    draw_map();
    draw_player();
    init_needle_sprite();

    handleCalculation();

    gamestate = GAMESTATE_IDLE;
    
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
                  updateRaycasterVectors();
                }
                if (key(KEY_LEFT)){
                  gamestate = GAMESTATE_MOVING;
                  currentRotStep = (currentRotStep - 1 + ROTATION_STEPS) % ROTATION_STEPS;
                  updateRaycasterVectors();
                }
                if (key(KEY_UP)) {
                  gamestate = GAMESTATE_MOVING;
                  if(worldMap[int(posX + (dirX * moveSpeed) * playerScale)][int(posY)] == false) posX += (dirX * moveSpeed);
                  if(worldMap[int(posX)][int(posY + (dirY * moveSpeed) * playerScale)] == false) posY +=  (dirY * moveSpeed);
                }
                if (key(KEY_DOWN)) {
                  gamestate = GAMESTATE_MOVING;
                  if(worldMap[int(posX - (dirX * moveSpeed) * playerScale)][int(posY)] == false) posX -= (dirX * moveSpeed);
                  if(worldMap[int(posX)][int(posY - (dirY * moveSpeed) * playerScale)] == false) posY -= (dirY * moveSpeed);
                }
                if (key(KEY_M)) {
                    bigMode = !bigMode;
                    fillBuffer(BLACK);
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