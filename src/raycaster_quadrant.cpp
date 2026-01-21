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
#define WINDOW_HEIGTH 54

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

FpF16<7> prevDirX, prevDirY;                                                       
uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 1;
int8_t movementStep = 2; 

uint8_t xOffset = 68;
uint8_t yOffset = 18; 

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

// Delta Dists (Only need positive, as dist is always abs())
FpF16<7> deltaDistX_Q1[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistY_Q1[QUADRANT_STEPS][WINDOW_WIDTH];

// --- OPTIMIZATION: Pointers instead of Arrays ---
// These point to the correct row in the tables above
FpF16<7>* activeRayDirX;
FpF16<7>* activeRayDirY;
FpF16<7>* activeDeltaDistX;
FpF16<7>* activeDeltaDistY;

FpF16<7> cameraXValues[WINDOW_WIDTH];

int16_t texOffsetTable[64]; 
uint8_t texColumnBuffer[32]; 

uint8_t currentRotStep = 0; 

FpF16<7> invW;
FpF16<7> halfH(h / 2);

#define KEYBOARD_INPUT 0xFF10 
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))
#define PALETTE_XRAM_ADDR 0xF100 

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
        // Unroll 8 pixels
        for (uint8_t i = 0; i < 12; ++i) {
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
        buffer_ptr_loc += 96; 
    }
}

void precalculateRotations() {
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    invW = FpF16<7>(1) / FpF16<7>(w); 
    FpF16<7> fw =  FpF16<7>(w);
    
    texStepValues[0] = FpF16<7>(texHeight); 
    for (uint8_t i = 1; i < WINDOW_HEIGTH+1; i++) {
      texStepValues[i] =  FpF16<7>(texHeight) / FpF16<7>(i);
    }

    // Precalculate cameraX
    for(uint8_t x = 0; x < w; x++) {
        cameraXValues[x] = FpF16<7>(2 * x) / fw - FpF16<7>(1);
    }

    printf("Precalc Q1 & Neg...");
    
    for (uint8_t i = 0; i < QUADRANT_STEPS; i++) {
      
      dirXValues[i] = currentDirX;
      dirYValues[i] = currentDirY;
      planeXValues[i] = currentPlaneX;
      planeYValues[i] = currentPlaneY;

      for(uint8_t x = 0; x < w; x++) {
          FpF16<7> rayDirX = currentDirX + currentPlaneX * cameraXValues[x];
          FpF16<7> rayDirY = currentDirY + currentPlaneY * cameraXValues[x];
          
          // Store Positive
          rayDirX_Q1[i][x] = rayDirX;
          rayDirY_Q1[i][x] = rayDirY;

          // --- OPTIMIZATION: Pre-calculate Negative ---
          // This allows us to just swap pointers later instead of doing math
          rayDirX_Q1_Neg[i][x] = -rayDirX;
          rayDirY_Q1_Neg[i][x] = -rayDirY;

          // Delta Dist (always positive)
          if (rayDirX == 0 || (rayDirX.GetRawVal() == -261) ) { 
             deltaDistX_Q1[i][x] = 127;
          } else { 
             deltaDistX_Q1[i][x] = fp_abs(FpF16<7>(1) / rayDirX); 
          }

          if (rayDirY == 0 || (rayDirY.GetRawVal() == -261)) { 
             deltaDistY_Q1[i][x] = 127;
          } else { 
             deltaDistY_Q1[i][x] = fp_abs(FpF16<7>(1) / rayDirY); 
          }
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

// --- OPTIMIZATION: Zero-Copy Vector Update ---
void updateRaycasterVectors() {
    uint8_t quad = currentRotStep / QUADRANT_STEPS;
    uint8_t idx = currentRotStep % QUADRANT_STEPS;

    FpF16<7> currDirX, currDirY, currPlaneX, currPlaneY;

    // We simply point to the correct pre-calculated arrays based on rotation symmetry.
    // No loops, no copying.
    switch(quad) {
        case 0: // 0-90 deg
            currDirX = dirXValues[idx]; currDirY = dirYValues[idx];
            currPlaneX = planeXValues[idx]; currPlaneY = planeYValues[idx];
            
            activeRayDirX = rayDirX_Q1[idx];
            activeRayDirY = rayDirY_Q1[idx];
            activeDeltaDistX = deltaDistX_Q1[idx];
            activeDeltaDistY = deltaDistY_Q1[idx];
            break;

        case 1: // 90-180 deg: (x,y) -> (-y, x)
            currDirX = -dirYValues[idx]; currDirY = dirXValues[idx];
            currPlaneX = -planeYValues[idx]; currPlaneY = planeXValues[idx];

            // RayX uses NegY table, RayY uses PosX table
            activeRayDirX = rayDirY_Q1_Neg[idx]; 
            activeRayDirY = rayDirX_Q1[idx];
            
            // Delta distances swap X/Y
            activeDeltaDistX = deltaDistY_Q1[idx];
            activeDeltaDistY = deltaDistX_Q1[idx];
            break;

        case 2: // 180-270 deg: (x,y) -> (-x, -y)
            currDirX = -dirXValues[idx]; currDirY = -dirYValues[idx];
            currPlaneX = -planeXValues[idx]; currPlaneY = -planeYValues[idx];

            // RayX uses NegX table, RayY uses NegY table
            activeRayDirX = rayDirX_Q1_Neg[idx];
            activeRayDirY = rayDirY_Q1_Neg[idx];
            
            // Delta distances same as Q1
            activeDeltaDistX = deltaDistX_Q1[idx];
            activeDeltaDistY = deltaDistY_Q1[idx];
            break;

        case 3: // 270-360 deg: (x,y) -> (y, -x)
            currDirX = dirYValues[idx]; currDirY = -dirXValues[idx];
            currPlaneX = planeYValues[idx]; currPlaneY = -planeXValues[idx];

            // RayX uses PosY table, RayY uses NegX table
            activeRayDirX = rayDirY_Q1[idx];
            activeRayDirY = rayDirX_Q1_Neg[idx];
            
            // Delta distances swap X/Y
            activeDeltaDistX = deltaDistY_Q1[idx];
            activeDeltaDistY = deltaDistX_Q1[idx];
            break;
    }

    dirX = currDirX;
    dirY = currDirY;
    planeX = currPlaneX;
    planeY = currPlaneY;
}

void precalculateLineHeights() {
    lineHeightTable[0] = h; 
    for (int i = 1; i < 1024; ++i) {
        FpF16<7> dist = FpF16<7>::FromRaw(i);
        FpF16<7> heightFp = FpF16<7>(h) / dist;
        int height = (int)heightFp;
        if (height > h || height < 0) height = h;
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
        
        uint8_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = h;
        }
        
        uint8_t texNum = (worldMap[zp_mapX][zp_mapY] - 1) * 2 + zp_side;
        int8_t drawStart = (-lineHeight >> 1) + 27; 
        if (drawStart < 0) drawStart = 0;
        uint8_t drawEnd = drawStart + lineHeight;
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
        
        int16_t raw_step = texStepValues[lineHeight].GetRawVal();
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
                uint8_t texY = (raw_texPos >> 7) & 63; 
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
                uint8_t texY = (raw_texPos >> 7) & 63;
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
    
    drawBufferDouble_Optimized();
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
    prevDirX = dirX;
    prevDirY = dirY;

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