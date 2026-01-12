#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics.hpp"
#include "textures.h"
#include "FpF.hpp"
#include "maze.h"

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


FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1); //These HAVE TO be float, or something with a lot more precision
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0); //the 2d raycaster version of camera plane
FpF16<7> moveSpeed(0.2); //the constant value is in squares/second
FpF16<7> playerScale(5);
FpF16<7> sin_r(0.2588190451); // precomputed value of sin(pi 1/16 rad)
FpF16<7> cos_r(0.96592582628); 

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


bool gamestate_changed = true;
uint8_t gamestate = 1;  //  0          1         2        3
uint8_t gamestate_prev = 1;
#define GAMESTATE_INIT 0
#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
#define GAMESTATE_CALCULATING 3


uint8_t lineHeightTable[1024]; 
FpF16<7> dirXValues[ROTATION_STEPS];
FpF16<7> dirYValues[ROTATION_STEPS];
FpF16<7> planeXValues[ROTATION_STEPS];
FpF16<7> planeYValues[ROTATION_STEPS];
FpF16<7> texStepValues[WINDOW_HEIGTH+1];
FpF16<7> cameraXValues[ROTATION_STEPS][WINDOW_WIDTH];
FpF16<7> rayDirXValues[ROTATION_STEPS][WINDOW_WIDTH];
FpF16<7> rayDirYValues[ROTATION_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistXValues[ROTATION_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistYValues[ROTATION_STEPS][WINDOW_WIDTH];
int16_t texOffsetTable[64]; 
uint8_t texColumnBuffer[32];  // Buffer for pre-fetching columns

uint8_t currentRotStep = 1; // Tracks the current rotation step

FpF16<7> invW;
FpF16<7> halfH(h / 2);

static const uint16_t texYOffsets[32] = {
    0, 32, 64, 96, 128, 160, 192, 224,
    256, 288, 320, 352, 384, 416, 448, 480,
    512, 544, 576, 608, 640, 672, 704, 736,
    768, 800, 832, 864, 896, 928, 960, 992
};


// XRAM locations
#define KEYBOARD_INPUT 0xFF10 // KEYBOARD_BYTES of bitmask data

// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};

// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))


float f_abs(float value) {
    // Check if the value is negative
    if (value < 0) {
        // If negative, return the negation to make it positive
        return -value;
    }
    // Otherwise, return the original value
    return value;
}

inline FpF16<7> fp_abs(FpF16<7> value) {
    // Check if the value is negative
    if (value.GetRawVal() < 0) 
        // If negative, return the negation to make it positive
        return -value;
    return value;
} 

inline FpF16<7> floorFixed(FpF16<7> a) {
    int16_t rawVal = a.GetRawVal();

    // For positive numbers, floor is the same as truncation.
    // We get the truncated value by masking off the 7 fractional bits.
    if (rawVal >= 0) {
        return FpF16<7>::FromRaw(rawVal & 0xFF80); // 0xFF80 is the mask for the 9 integer bits
    }
    // For negative numbers, we must round down to the next lowest integer
    // if there is any fractional part.
    else {
        // Truncate toward zero first.
        int16_t resultRaw = (rawVal & 0xFF80);
        // If there was any fractional part...
        if (rawVal & 0x007F) { // 0x007F is the mask for the 7 fractional bits
            // ...subtract 1.0 from the result.
            resultRaw -= (1 << 7); // 1.0 in Q9.7 format is 128.
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

void drawBufferDouble_v3() {
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    for (uint8_t j = 0; j < h; ++j) {
        // Set up for both rows at once to minimize overhead
        uint16_t addr1 = screen_addr;
        uint16_t addr2 = screen_addr + SCREEN_WIDTH;
        
        uint8_t* p = buffer_ptr;
        
        // Process 8 pixels at a time
        for (uint8_t i = 0; i < w; i += 8) {
            // Load 8 pixels
            uint8_t c0 = *p++;
            uint8_t c1 = *p++;
            uint8_t c2 = *p++;
            uint8_t c3 = *p++;
            uint8_t c4 = *p++;
            uint8_t c5 = *p++;
            uint8_t c6 = *p++;
            uint8_t c7 = *p++;

            // Write first row
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
            addr1 += 16; // Move by 16 (8 pixels * 2x scaling)

            // Write second row (same data)
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
    // Start with the base addresses.
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    // The amount to advance the pointers on each new row.
    const uint16_t screen_stride = SCREEN_WIDTH;
    const uint8_t buffer_stride = WINDOW_WIDTH;

    for (uint8_t j = 0; j < h; ++j) {
        // Set address and step ONCE per row.
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr;
        for (uint8_t i = 0; i < w; i += 8) {
            // The unrolled loop is already very efficient.
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

        // Advance to the next row using fast addition.
        screen_addr += screen_stride;
        buffer_ptr += buffer_stride;
    }
}

// Define the exact sizes to avoid confusion
#define RAY_DATA_SIZE 768    // 96 width * 2 bytes * 4 tables
#define VECTOR_DATA_SIZE 8   // 4 values * 2 bytes
#define TOTAL_STEP_SIZE (RAY_DATA_SIZE + VECTOR_DATA_SIZE) // 776 bytes

// void loadRotationStep(uint8_t step) {
//     uint32_t stepBaseAddr = TABLES_BASE + ((uint32_t)step * TOTAL_STEP_SIZE);
    
//     // DEBUG: Print the calculated address
//     printf("Loading step %d from address 0x%lX (decimal: %lu)\n", 
//            step, stepBaseAddr, stepBaseAddr);

//     // Fetch ray data - reading in little-endian format
//     auto fetchChunk = [&](uint32_t addr, int16_t* dest) {
//         RIA.addr0 = addr;
//         RIA.step0 = 1;
//         for(int i = 0; i < WINDOW_WIDTH; i++) {
//             uint8_t low = RIA.rw0;
//             uint8_t high = RIA.rw0;
//             dest[i] = (int16_t)(low | (high << 8));
//         }
//     };

//     fetchChunk(stepBaseAddr, activeRayDirX);
//     fetchChunk(stepBaseAddr + ROW_SIZE_BYTES, activeRayDirY);
//     fetchChunk(stepBaseAddr + (ROW_SIZE_BYTES*2), activeDeltaDistX);
//     fetchChunk(stepBaseAddr + (ROW_SIZE_BYTES*3), activeDeltaDistY);

//     // Fetch player vectors
//     uint32_t vectorAddr = stepBaseAddr + RAY_DATA_SIZE;
//     printf("  Vector address: 0x%lX (offset from base: %d)\n", 
//            vectorAddr, RAY_DATA_SIZE);
    
//     RIA.addr0 = vectorAddr; 
//     RIA.step0 = 1;

//     int16_t raw_vals[4];
//     for (int i = 0; i < 4; i++) {
//         uint8_t low = RIA.rw0;
//         uint8_t high = RIA.rw0;
//         raw_vals[i] = (int16_t)(low | (high << 8));
//     }
    
//     printf("  Raw values: dirX=%d dirY=%d planeX=%d planeY=%d\n",
//            raw_vals[0], raw_vals[1], raw_vals[2], raw_vals[3]);
    
//     dirX = FpF16<7>::FromRaw(raw_vals[0]);
//     dirY = FpF16<7>::FromRaw(raw_vals[1]);
//     planeX = FpF16<7>::FromRaw(raw_vals[2]);
//     planeY = FpF16<7>::FromRaw(raw_vals[3]);
// }


void precalculateRotations() {
    // Starting values
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    
    invW = FpF16<7>(1) / FpF16<7>(w); // qFP16_Div(one, qFP16_IntToFP(w));
    FpF16<7> fw =  FpF16<7>(w);

    halfH =  FpF16<7>(h >> 1);
    
    // Set a safe value for index 0, e.g., the maximum step.
    texStepValues[0] = FpF16<7>(texHeight); 
    // Start the loop from 1 to prevent division by zero.
    for (uint8_t i = 1; i < WINDOW_HEIGTH+1; i++) {
      texStepValues[i] =  FpF16<7>(texHeight) / FpF16<7>(i);
    }

    // Compute for right rotation (0.25 rad steps)
    for (uint8_t i = 0; i < ROTATION_STEPS; i++) {
      printf(".");
      // Rotate right (counterclockwise by 0.25 rad)
      FpF16<7> oldDirX = currentDirX;
      currentDirX = currentDirX * cos_r - currentDirY * sin_r;//  qFP16_Sub(qFP16_Mul(currentDirX, cos_r), qFP16_Mul(currentDirY, sin_r));
      currentDirY = oldDirX * sin_r + currentDirY * cos_r;// qFP16_Add(qFP16_Mul(oldDirX, sin_r), qFP16_Mul(currentDirY, cos_r));

      FpF16<7> oldPlaneX = currentPlaneX;
      currentPlaneX = currentPlaneX * cos_r - currentPlaneY * sin_r;// qFP16_Sub(qFP16_Mul(currentPlaneX, cos_r), qFP16_Mul(currentPlaneY, sin_r));
      currentPlaneY = oldPlaneX * sin_r + currentPlaneY * cos_r;// (qFP16_Mul(oldPlaneX, sin_r), qFP16_Mul(currentPlaneY, cos_r));

      dirXValues[i] = currentDirX;
      dirYValues[i] = currentDirY;
      planeXValues[i] = currentPlaneX;
      planeYValues[i] = currentPlaneY;

      for(uint8_t x = 0; x < w; x += currentStep)
      {
  
        FpF16<7> cameraX = FpF16<7>(2 * x) / fw - FpF16<7>(1); //x-coordinate in camera space;
        FpF16<7> rayDirX = currentDirX + currentPlaneX * cameraX;
        FpF16<7> rayDirY = currentDirY + currentPlaneY * cameraX;

        FpF16<7> deltaDistX, deltaDistY;

        if (rayDirX == 0 || (rayDirX.GetRawVal() == -261) ) { 
          deltaDistX = 127;
        } else { 
          deltaDistX = fp_abs(FpF16<7>(1) / rayDirX); 
        }
        if (rayDirY == 0 || (rayDirY.GetRawVal() == -261)) { 
          deltaDistY = 127;
        } else { 
          deltaDistY = fp_abs(FpF16<7>(1) / rayDirY); 
        }

        cameraXValues[i][x] = cameraX;
        rayDirXValues[i][x] = rayDirX;
        rayDirYValues[i][x] = rayDirY;
        deltaDistXValues[i][x] = deltaDistX;
        deltaDistYValues[i][x] = deltaDistY;
      }
    }

    dirXValues[0] =   dirX;
    dirYValues[0] =   dirY;
    planeXValues[0] = planeX;
    planeYValues[0] = planeY;

    printf("\nDone\n");

}

void precalculateLineHeights() {
    // The table maps a raw fixed-point distance value to a screen line height.
    // Index 0 represents a distance of 0.
    lineHeightTable[0] = h; // A distance of 0 means the wall is infinitely high.

    // Loop through possible raw distance values. A 1024-entry table covers
    // distances from just over 0 up to 3.99 (since 1023 / 128 ≈ 7.99).
    for (int i = 1; i < 1024; ++i) {
        // Use the new factory function to create a fixed-point number 'dist'
        // directly from the raw integer value 'i'.
        FpF16<7> dist = FpF16<7>::FromRaw(i);

        // Now perform the division using the FpF library's capabilities.
        // This is slow, but it only runs ONCE at startup.
        FpF16<7> heightFp = FpF16<7>(h) / dist;

        // Convert the result to an integer for the table.
        int height = (int)heightFp;

        // Clamp the value to prevent issues.
        // A very small distance can result in a huge calculated height.
        if (height > h || height < 0) {
            height = h;
        }
        
        lineHeightTable[i] = (uint8_t)height;
    }

    texOffsetTable[0] = 0;

    for (int i = 1; i < 64; i++) {
        // Pre-calculate the expensive division logic
        // Formula from your code: 2048 - (110592 / i)
        // We use long (L) to ensure precision before casting back
        texOffsetTable[i] = 2048 - (int16_t)(110592L / i);

        if (texOffsetTable[i] < 0) {
            texOffsetTable[i] = 0;
        }
    }
}

// Helper function for step 1 drawing (always inline)
__attribute__((always_inline))
inline void drawColumnStep1(uint8_t* bufPtr, uint8_t color) {
    *bufPtr = color;
}

// Add this new helper function
__attribute__((always_inline))
inline void drawColumnStep2(uint8_t* bufPtr, uint8_t color) {
    bufPtr[0] = color;
    bufPtr[1] = color;
}

// Helper function for step 4 drawing (always inline)
__attribute__((always_inline))
inline void drawColumnStep4(uint8_t* bufPtr, uint8_t color) {
    bufPtr[0] = color;
    bufPtr[1] = color;
    bufPtr[2] = color;
    bufPtr[3] = color;
}

int raycastF() {
    FpF16<7>* rayDirXPtr = &rayDirXValues[currentRotStep][0];
    FpF16<7>* rayDirYPtr = &rayDirYValues[currentRotStep][0];
    FpF16<7>* deltaDistXPtr = &deltaDistXValues[currentRotStep][0];
    FpF16<7>* deltaDistYPtr = &deltaDistYValues[currentRotStep][0];
    
    const int posXInt = (int)posX;
    const int posYInt = (int)posY;
    const uint8_t stepSize = currentStep;
    
    // Pre-cache floor colors pointer
    const uint8_t* floorColorPtr = floorColors;
    
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
        
        uint8_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = (rawDist > 0) ? 
                (int)(FpF16<7>(h) / FpF16<7>::FromRaw(rawDist)) : h;
            if (lineHeight > h) lineHeight = h;
        }
        
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

        // uint8_t* texPtr = &texture[texNum][texX];
        fetchTextureColumn(texNum, texX);
        int16_t raw_step = texStepValues[lineHeight].GetRawVal();
        int16_t raw_texPos = (lineHeight > h) ? 
            texOffsetTable[(lineHeight > 63) ? 63 : lineHeight] : 0;
        if (raw_texPos < 0) raw_texPos = 0;

        uint8_t* bufPtr = &buffer[zp_x];
        uint8_t color;
        
        if (stepSize == 1) {
            color = 14;
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                *bufPtr = color;
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
            color = 14;
            for (zp_y = 0; zp_y < drawStart; ++zp_y) {
                bufPtr[0] = color;
                bufPtr[1] = color;
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
    
    bigMode ? drawBufferDouble_v3() : drawBufferRegular();
    return 0;
}


void print_map() {
    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
            switch (worldMap[i][j]) {
                case 0:
                    printf(" "); // Empty space
                    break;
                case 1:
                    printf("#"); // Wall
                    break;
                case 2:
                    printf("x"); // Some kind of obstacle
                    break;
                case 3:
                    printf("O"); // Another type of obstacle
                    break;
                case 4:
                    printf("."); // Some different feature
                    break;
                case 5:
                    printf("@"); // Special object or feature
                    break;
                default:
                    printf("?"); // Unknown character
            }
        }
        printf("\n"); // New line at the end of each row
    }
}

// void drawTexture() {
//     // Loop through the screen's height
//     for (uint16_t y = 0; y < SCREEN_HEIGHT; y++) {
//         // Compute the corresponding Y coordinate in the texture
//         uint8_t texY = y % texHeight;

//         // Compute the base address of the current screen row
//         uint16_t row_addr = y * SCREEN_WIDTH;

//         // Set the starting address for the row
//         RIA.addr0 = row_addr;
//         RIA.step0 = 1; // Move 2 bytes per pixel in 16bpp mode

//         // Loop through the screen's width
//         for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
//             // Compute the corresponding X coordinate in the texture
//             uint8_t texX = x % texWidth;

//             // Compute the texture index
//             uint16_t texIndex = (texY * texWidth) + texX;

//             // Retrieve the color from the texture
//             uint8_t color = texture[0][texIndex];

//             // Write the color to the screen buffer
//             RIA.rw0 = color;
//         }
//     }
// }



void draw_ui() {
  // draw_rect(LIGHT_GRAY, 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  // draw_rect(LIGHT_GRAY, xOffset-1, yOffset-1, (w+1)*2, (h+1)*2); 
  // draw_rect(LIGHT_GRAY, xOffset-2, yOffset-2, (w+2)*2, (h+2)*2); 
  // draw_rect(LIGHT_GRAY, xOffset-3, yOffset-3, (w+3)*2, (h+3)*2); 
  // draw_rect(LIGHT_GRAY, xOffset-4, yOffset-4, (w+4)*2, (h+4)*2); 
  // fill_rect(BLACK, 5,110, 11 * 5, 8);
  // set_cursor(5, 110);
  // sprintf(*buf," step: %i ", movementStep);
  // draw_string(*buf);
  fill_rect_fast(2, 150, 136, 28, 28);
}

// Function to draw the world map using the draw_rect function
void draw_map() {

    // fill_rect_fast (BLACK, 0, 0, mapWidth * TILE_SIZE, mapHeight * TILE_SIZE); // draw frame
    

    FpF16<7> ts(TILE_SIZE);

    // uint16_t x = (int)(posX * ts);
    // uint16_t y = (int)(posY * ts);

    for (int i = 0; i < mapHeight; i++) {
      for (int j = 0; j < mapWidth; j++) {
        if (worldMap[i][j] > 0) {
          uint8_t color;
          switch(worldMap[i][j])
          {
            case 1: color = 236; break; //grey
            case 2: color = 40; break; //red
            case 3: color = BLUE; break; //blue
            case 4: color = 7; break; //white
            case 5: color = 11; break; //yellow
          }
          
          // Draw a wall tile (represented by a white rectangle)
          draw_rect(color, i * TILE_SIZE + startX, j * TILE_SIZE + startY, TILE_SIZE, TILE_SIZE);
          } else {
            draw_rect(2, i * TILE_SIZE + startX, j * TILE_SIZE + startY, TILE_SIZE, TILE_SIZE);
          }
      }
    }
    // draw_pixel(230, x+startX, y+startY);
} 

void draw_needle() {
    FpF16<7> l(12);
    FpF16<7> ts(TILE_SIZE);

    uint16_t x = 293;
    uint16_t y = 50;

    int8_t lX = (int)(dirX * l);  // calculate line direction
    int8_t lY = (int)(dirY * l);
    int8_t plX = (int)(prevDirX * l);
    int8_t plY = (int)(prevDirY * l);

    // Compass needle side length
    FpF16<7> arrowWidth(2);
    int8_t arrowX1 = (int)(-dirY * arrowWidth);  // Perpendicular offset
    int8_t arrowY1 = (int)(dirX * arrowWidth);
    int8_t prevArrowX1 = (int)(-prevDirY * arrowWidth);  // Perpendicular offset
    int8_t prevArrowY1 = (int)(prevDirX * arrowWidth);

    // Clear the previous compass needle
    draw_line(245, x + prevArrowX1 , y + prevArrowY1, x + plX, y + plY);  // left wing
    draw_line(245, x - prevArrowX1 , y - prevArrowY1, x + plX, y + plY);  // right wing
    draw_line(245, x + prevArrowX1 , y + prevArrowY1, x - plX, y - plY);  // left wing
    draw_line(245, x - prevArrowX1 , y - prevArrowY1, x - plX, y - plY);  // right wing

    // Draw the new compass needle
    // draw_line(COLOR_FROM_RGB8(92, 255, 190), x, y, x + lX, y + lY);  // main line
    draw_line(9, x + arrowX1 , y + arrowY1, x + lX, y + lY);  // left wing
    draw_line(9, x - arrowX1 , y - arrowY1, x + lX, y + lY);  // right wing
    draw_line(4, x + arrowX1 , y + arrowY1, x - lX, y - lY);  // left wing
    draw_line(4, x - arrowX1 , y - arrowY1, x - lX, y - lY);  // right wing
    draw_pixel(250, x, y);  // Draw central pixel

    // Update previous position and direction
    // prevPlayerX = x;
    // prevPlayerY = y;
    prevDirX = dirX;
    prevDirY = dirY;
}


void draw_player(){

    // FpF16<7> l(8);
    FpF16<7> ts(TILE_SIZE);

    uint16_t x = (int)(posX * ts) + startX;
    uint16_t y = (int)(posY * ts) + startY;
    
    // draw_line (243, prevPlayerX, prevPlayerY, prevPlayerX + plX, prevPlayerY + plY); // clear previous line
    draw_pixel(2, prevPlayerX, prevPlayerY);
    // draw_line(COLOR_FROM_RGB8(92, 255, 190), x, y, x + lX, y + lY);
    draw_pixel(230, x, y);
    prevPlayerX = x;
    prevPlayerY = y;
    // prevDirX = dirX;
    // prevDirY = dirY;
}

void handleCalculation() {

    gamestate = GAMESTATE_CALCULATING;
    // draw_ui();
    draw_needle();
    draw_player();
    raycastF();
    gamestate = GAMESTATE_IDLE;

}

void WaitForAnyKey(){

    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;
    while (RIA.rw0 & 1)
        ;
}

int16_t main() {
    bool handled_key = false;
    bool paused = false;
    bool show_buffers_indicators = true;
    uint8_t mode = 0;
    uint8_t i = 0;
    uint8_t timer = 0;

    for(i = 0; i < h; i++){
      floorColors[i] = mapValue(i, 0, h, 232, 250);
    }

    prevPlayerX = (int)(posX * FpF16<7>(TILE_SIZE));//(qFP16_FPToInt(posX) * TILE_SIZE);
    prevPlayerY = (int)(posY * FpF16<7>(TILE_SIZE));//(qFP16_FPToInt(posY) * TILE_SIZE);
    prevDirX = dirX;
    prevDirY = dirY;

    gamestate = GAMESTATE_INIT;

    // generateTextures();

    // Initialize the maze with walls
    initializeMaze();

    printf("generating maze...\n");
    srand(4);
    // Start DFS from a random position (must be odd indices to stay within grid)
    int startPosX = (random(1, ((mapWidth - 2) / 2)) * 2 + 1);
    int startPoxY = (random(1, ((mapHeight - 2) / 2)) * 2 + 1);


    printf("startX: %i, startY: %i\n", startX, startY);

    
    // Generate the maze using DFS
    iterativeDFS(startPosX, startPoxY);

    // Set entry and finish points inside the maze
    setEntryAndFinish(startPosX, startPoxY);

    posX = FpF16<7>(startPoxY);
    posY = FpF16<7>(startPosX);

    printf("Precalculating values...\n");
    precalculateRotations();
    precalculateLineHeights();

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    // erase_canvas();

    draw_ui();


    draw_map();
    draw_player();

    handleCalculation();



    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 2 && currentStep > 1) { 
            currentStep = 1;
            draw_map();
            // draw_player();
            // draw_ui();
            handleCalculation();
        }

        

        xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
        RIA.addr0 = KEYBOARD_INPUT;
        RIA.step0 = 0;

        // fill the keystates bitmask array
        for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
            uint8_t j, new_keys;
            RIA.addr0 = KEYBOARD_INPUT + i;
            new_keys = RIA.rw0;

            // check for change in any and all keys
            for (j = 0; j < 8; j++) {
                uint8_t new_key = (new_keys & (1<<j));
                // if ((((i<<3)+j)>3) && (new_key != (keystates[i] & (1<<j)))) {
                //     printf( "key %d %s\n", ((i<<3)+j), (new_key ? "pressed" : "released"));
                // }
            }

            keystates[i] = new_keys;
        }

        // check for a key down
        if (!(keystates[0] & 1)) {
            // if (!handled_key) { // handle only once per single keypress
                // handle the keystrokes
                if (key(KEY_SPACE)) {
                    paused = !paused;
                    if(paused){
                        // set_cursor(10, 110);
                        // // draw_string2buffer("Press SPACE to start", buffers[active_buffer]);
                        // draw_string("Press SPACE to start");
                    }
                }
                if (key(KEY_RIGHT)){
                  gamestate = GAMESTATE_MOVING;
                  //both camera direction and camera plane must be rotated
                  // Move to the next step in the right rotation
                  currentRotStep = (currentRotStep + 1) % ROTATION_STEPS;
                  dirX = dirXValues[currentRotStep];
                  if (currentRotStep == 0) dirX = 0;
                  dirY = dirYValues[currentRotStep];
                  planeX = planeXValues[currentRotStep];
                  planeY = planeYValues[currentRotStep];
                }
                if (key(KEY_LEFT)){
                  gamestate = GAMESTATE_MOVING;
                    //both camera direction and camera plane must be rotated
                  currentRotStep = (currentRotStep - 1 + ROTATION_STEPS) % ROTATION_STEPS;
                  dirX = dirXValues[currentRotStep];
                  if (currentRotStep == 0) dirX = 0;
                  dirY = dirYValues[currentRotStep];
                  planeX = planeXValues[currentRotStep];
                  planeY = planeYValues[currentRotStep];

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
                    // drawTexture();
                    draw_ui();
                }
                if (key(KEY_MINUS)) {
                  // if (movementStep > 4) {
                  //   movementStep -= 2;
                  //   draw_ui();
                  // }
              
                }

                if (key(KEY_ESC)) {
                    break;
                }
                handled_key = true;

                // printf("currentRotStep: %i\n", currentRotStep); 
                // printf("dirX: %s\n", qFP16_FPToA(dirXValues[currentRotStep], ans, 4));
                // printf("dirX: %s\n", qFP16_FPToA(dirX, ans, 4));
                // printf("dirY: %s\n", qFP16_FPToA(dirY, ans, 4));
                // printf("planeX: %s\n", qFP16_FPToA(planeX, ans, 4));
                // printf("planeY: %s\n", qFP16_FPToA(planeY, ans, 4));
                // printf("posX: %i, posY: %i\n", FP16ToIntPercent(posX), FP16ToIntPercent(posY));

                if (!paused) {
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = movementStep;
                    }

                    // erase_canvas();
                    handleCalculation();

                }
            }

    }

    return 0;
    
}


