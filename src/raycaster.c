#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "colors.h"
#include "usb_hid_keys.h"
// #include "bitmap_graphics_db.h"
#include "bitmap_graphics.h"
#include "qfp16.h"
#include "textures32.h"

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define mapWidth 20
#define mapHeight 18
#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
#define WINDOW_WIDTH 96
#define WINDOW_HEIGTH 54
#define TILE_SIZE 2
#define SCALE 2
#define MIN_SCALE 8
#define MAX_SCALE 2

const qFP16_t MAXQVAL = qFP16_Constant(32767.999985);
const  qFP16_t one = qFP16_Constant(1.0f);
const  qFP16_t minusone = qFP16_Constant(-1.0f);


qFP16_t posX;
qFP16_t posY;  //x and y start position
qFP16_t dirX, dirY; //initial direction vector
qFP16_t prevDirX, prevDirY;
qFP16_t planeX, planeY; //the 2d raycaster version of camera plane
qFP16_t moveSpeed; //the constant value is in squares/second
qFP16_t sin_r; // precomputed value of sin(0.25 rad)
qFP16_t cos_r;

char ans[ 10 ];
char *buf[] = {"                                                                  "};

uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 1;
int8_t movementStep = 4; 
uint8_t currentScale = SCALE;



uint8_t xOffset = (uint8_t)((SCREEN_WIDTH - WINDOW_WIDTH * 2) / 2);
uint8_t yOffset = (uint8_t)((SCREEN_HEIGHT - WINDOW_HEIGTH * 2) / 2);


const uint16_t w = WINDOW_WIDTH;
const uint16_t h = WINDOW_HEIGTH; 

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

#define ROTATION_STEPS 20

qFP16_t dirXValues[ROTATION_STEPS];
qFP16_t dirYValues[ROTATION_STEPS];
qFP16_t planeXValues[ROTATION_STEPS];
qFP16_t planeYValues[ROTATION_STEPS];
qFP16_t texStepValues[WINDOW_HEIGTH+1];
qFP16_t cameraXValues[ROTATION_STEPS][WINDOW_WIDTH];
qFP16_t rayDirXValues[ROTATION_STEPS][WINDOW_WIDTH];
qFP16_t rayDirYValues[ROTATION_STEPS][WINDOW_WIDTH];
qFP16_t deltaDistXValues[ROTATION_STEPS][WINDOW_WIDTH];
qFP16_t deltaDistYValues[ROTATION_STEPS][WINDOW_WIDTH];
uint8_t currentRotStep = 1; // Tracks the current rotation step



uint8_t bpp;

// XRAM locations
#define KEYBOARD_INPUT 0xFF10 // KEYBOARD_BYTES of bitmask data

// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};

// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))


int16_t worldMap[mapHeight][mapWidth]=
{
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,2,0,0,0,2,2,2,2,2,0,0,3,0,3,0,3,0,1},
  {1,0,2,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1},
  {1,0,2,0,0,0,2,0,0,0,2,0,0,3,0,0,0,3,0,1},
  {1,0,2,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1},
  {1,0,2,0,0,0,2,2,0,2,2,0,0,3,0,3,0,3,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,2,2,2,2,2,2,2,2,0,0,0,0,0,2,2,2,2,0,1},
  {1,2,0,2,0,0,0,0,2,0,0,0,0,0,2,0,0,0,0,1},
  {1,2,0,0,0,0,2,0,2,0,2,2,2,0,2,0,0,2,0,1},
  {1,2,0,2,0,0,0,0,2,0,0,0,2,0,2,0,0,2,0,1},
  {1,2,0,2,2,2,2,2,2,0,0,0,2,0,2,0,0,2,0,1},
  {1,2,0,0,0,0,0,0,0,0,2,2,2,0,2,0,0,2,0,1},
  {1,2,2,2,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};


float f_abs(float value) {
    // Check if the value is negative
    if (value < 0) {
        // If negative, return the negation to make it positive
        return -value;
    }
    // Otherwise, return the original value
    return value;
}

uint8_t mapValue(uint8_t value, uint8_t in_min, uint8_t in_max, uint8_t out_min, uint8_t out_max) {
    return out_min + ((value - in_min) * (out_max - out_min)) / (in_max - in_min);
}

void drawBufferDouble() {
    RIA.step0 = 1; // Set step size once, as it does not change

    // Loop through each row
    for (uint8_t j = 0; j < h; j++) {
        uint16_t row_addr = SCREEN_WIDTH * (yOffset + (j << 1)) + xOffset;
        uint16_t bufferIndex = j * WINDOW_WIDTH;

        // Precompute the base row addresses
        uint16_t row_addr_next = row_addr + SCREEN_WIDTH;

        // Draw 8 2x2 pixel blocks in one iteration
        for (uint8_t i = 0; i < w; i += 8) {
            // Precompute addresses
            uint16_t addr1 = row_addr + (i << 1);
            uint16_t addr2 = row_addr_next + (i << 1);

            // Block 1
            uint16_t color1 = buffer[bufferIndex + i];
            RIA.addr0 = addr1;
            RIA.rw0 = color1;
            RIA.rw0 = color1;
            RIA.addr0 = addr2;
            RIA.rw0 = color1;
            RIA.rw0 = color1;

            // Block 2
            uint16_t color2 = buffer[bufferIndex + i + 1];
            RIA.addr0 = addr1 + 2;
            RIA.rw0 = color2;
            RIA.rw0 = color2;
            RIA.addr0 = addr2 + 2;
            RIA.rw0 = color2;
            RIA.rw0 = color2;

            // Block 3
            uint16_t color3 = buffer[bufferIndex + i + 2];
            RIA.addr0 = addr1 + 4;
            RIA.rw0 = color3;
            RIA.rw0 = color3;
            RIA.addr0 = addr2 + 4;
            RIA.rw0 = color3;
            RIA.rw0 = color3;

            // Block 4
            uint16_t color4 = buffer[bufferIndex + i + 3];
            RIA.addr0 = addr1 + 6;
            RIA.rw0 = color4;
            RIA.rw0 = color4;
            RIA.addr0 = addr2 + 6;
            RIA.rw0 = color4;
            RIA.rw0 = color4;

            // Block 5
            uint16_t color5 = buffer[bufferIndex + i + 4];
            RIA.addr0 = addr1 + 8;
            RIA.rw0 = color5;
            RIA.rw0 = color5;
            RIA.addr0 = addr2 + 8;
            RIA.rw0 = color5;
            RIA.rw0 = color5;

            // Block 6
            uint16_t color6 = buffer[bufferIndex + i + 5];
            RIA.addr0 = addr1 + 10;
            RIA.rw0 = color6;
            RIA.rw0 = color6;
            RIA.addr0 = addr2 + 10;
            RIA.rw0 = color6;
            RIA.rw0 = color6;

            // Block 7
            uint16_t color7 = buffer[bufferIndex + i + 6];
            RIA.addr0 = addr1 + 12;
            RIA.rw0 = color7;
            RIA.rw0 = color7;
            RIA.addr0 = addr2 + 12;
            RIA.rw0 = color7;
            RIA.rw0 = color7;

            // Block 8
            uint16_t color8 = buffer[bufferIndex + i + 7];
            RIA.addr0 = addr1 + 14;
            RIA.rw0 = color8;
            RIA.rw0 = color8;
            RIA.addr0 = addr2 + 14;
            RIA.rw0 = color8;
            RIA.rw0 = color8;
        }
    }
}



void drawBufferRegular() {
    // Loop through each row
    for (uint8_t j = 0; j < h; j++) {
        // Calculate the starting address of the row
        uint16_t row_addr = ((SCREEN_WIDTH) * (yOffset + j)) + (xOffset);

        // Set the initial address and step for the row
        RIA.addr0 = row_addr;
        RIA.step0 = 1; // Move 2 bytes per pixel in 16bpp mode

        // Fill the row with the color
        for (uint8_t i = 0; i < w; i += 8) {
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 1];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 2];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 3];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 4];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 5];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 6];
            RIA.rw0 = buffer[j * WINDOW_WIDTH + i + 7];
        }
    }
}

void precalculateRotations() {
    // Starting values
    qFP16_t currentDirX = dirX;
    qFP16_t currentDirY = dirY;
    qFP16_t currentPlaneX = planeX;
    qFP16_t currentPlaneY = planeY;

    
    

    qFP16_t invW = qFP16_Div(one, qFP16_IntToFP(w));

    for (uint8_t i = 0; i < WINDOW_HEIGTH+1; i++) {
      texStepValues[i] = qFP16_Div(qFP16_Constant(texHeight), qFP16_IntToFP(i));
    }

    // Compute for right rotation (0.25 rad steps)
    for (uint8_t i = 0; i < ROTATION_STEPS; i++) {
      printf(".");
      // Rotate right (counterclockwise by 0.25 rad)
      qFP16_t oldDirX = currentDirX;
      currentDirX = qFP16_Sub(qFP16_Mul(currentDirX, cos_r), qFP16_Mul(currentDirY, sin_r));
      currentDirY = qFP16_Add(qFP16_Mul(oldDirX, sin_r), qFP16_Mul(currentDirY, cos_r));

      qFP16_t oldPlaneX = currentPlaneX;
      currentPlaneX = qFP16_Sub(qFP16_Mul(currentPlaneX, cos_r), qFP16_Mul(currentPlaneY, sin_r));
      currentPlaneY = qFP16_Add(qFP16_Mul(oldPlaneX, sin_r), qFP16_Mul(currentPlaneY, cos_r));

      dirXValues[i] = currentDirX;
      dirYValues[i] = currentDirY;
      planeXValues[i] = currentPlaneX;
      planeYValues[i] = currentPlaneY;

      for(uint8_t x = 0; x < w; x += currentStep)
      {
  
        qFP16_t cameraX = qFP16_Sub(qFP16_Mul(qFP16_IntToFP(x << 1), invW), one);
        qFP16_t rayDirX = qFP16_Add(qFP16_Mul(currentPlaneX, cameraX), currentDirX);
        qFP16_t rayDirY = qFP16_Add(qFP16_Mul(currentPlaneY, cameraX), currentDirY);
        qFP16_t deltaDistX = (rayDirX == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirX));
        qFP16_t deltaDistY = (rayDirY == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirY));
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

int raycastFP()
{
  // Precompute common values
  qFP16_t invW = qFP16_Div(one, qFP16_IntToFP(w));
  
  for (uint8_t x = 0; x < w; x += currentStep) {
    qFP16_t cameraX = cameraXValues[currentRotStep][x];
    qFP16_t rayDirX = rayDirXValues[currentRotStep][x];
    qFP16_t rayDirY = rayDirYValues[currentRotStep][x];

    int16_t mapX = (int16_t)qFP16_FPToInt(posX);
    int16_t mapY = (int16_t)qFP16_FPToInt(posY);

    // qFP16_t deltaDistX = (rayDirX == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirX));
    // qFP16_t deltaDistY = (rayDirY == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirY));
    qFP16_t deltaDistX = deltaDistXValues[currentRotStep][x];
    qFP16_t deltaDistY = deltaDistYValues[currentRotStep][x];

    qFP16_t sideDistX, sideDistY;
    int8_t stepX, stepY;

    // Precompute initial `sideDistX` and `sideDistY`
    if (rayDirX < 0) {
        stepX = -1;
        sideDistX = qFP16_Mul(qFP16_Sub(posX, qFP16_IntToFP(mapX)), deltaDistX);
    } else {
        stepX = 1;
        sideDistX = qFP16_Mul(qFP16_Sub(qFP16_Add(qFP16_IntToFP(mapX), one), posX), deltaDistX);
    }
    if (rayDirY < 0) {
        stepY = -1;
        sideDistY = qFP16_Mul(qFP16_Sub(posY, qFP16_IntToFP(mapY)), deltaDistY);
    } else {
        stepY = 1;
        sideDistY = qFP16_Mul(qFP16_Sub(qFP16_Add(qFP16_IntToFP(mapY), one), posY), deltaDistY);
    }

    // DDA loop to find wall intersection
    int8_t hit = 0, side;
    while (!hit) {
        if (sideDistX < sideDistY) {
            sideDistX = qFP16_Add(sideDistX, deltaDistX);
            mapX += stepX;
            side = 0;
        } else {
            sideDistY = qFP16_Add(sideDistY, deltaDistY);
            mapY += stepY;
            side = 1;
        }
        if (worldMap[mapX][mapY] > 0) hit = 1;
    }

    // Compute perpendicular wall distance
    qFP16_t perpWallDist = (side == 0) ? qFP16_Sub(sideDistX, deltaDistX) : qFP16_Sub(sideDistY, deltaDistY);
    // uint16_t lineHeight = (uint16_t)qFP16_FPToInt(qFP16_Div(qFP16_IntToFP(h), perpWallDist));
    uint8_t lineHeight = (uint8_t)((h << 4) / (perpWallDist >> 12));
    // printf("lineHeight: %i, new: %i, %s\n", lineHeight, (int)((h << 4) / (perpWallDist >> 12)), qFP16_FPToA(perpWallDist, ans, 4));

    if (lineHeight > h) lineHeight = h;

    int8_t drawStart = (-lineHeight >> 1) + (h >> 1);
    if (drawStart < 0) drawStart = 0;
    uint8_t drawEnd = drawStart + lineHeight;

    // Draw ceiling
    uint8_t i, color = 14;
    for (uint8_t y = 0; y < drawStart; ++y) {
        uint8_t* bufPtr = &buffer[y * w + x];
        // for (uint8_t i = 0; i < currentStep; ++i) {
        if (currentStep == 1) bufPtr[i] = color; // skyColor
        else {
          bufPtr[i] = color;
          bufPtr[i+1] = color;
          bufPtr[i+2] = color;
          bufPtr[i+3] = color;
          // bufPtr[i+4] = color;
          // bufPtr[i+5] = color;
        }        
    }

    // Draw wall
    uint8_t texNum = ((worldMap[mapX][mapY] - 1) << 1) + side;
    qFP16_t wallX = (side == 0) ? qFP16_Add(posY, qFP16_Mul(perpWallDist, rayDirY)) : qFP16_Add(posX, qFP16_Mul(perpWallDist, rayDirX));
    wallX = wallX & 0xFFFF; // Fractional part only
    uint8_t texX = (wallX << 5) >> 16;
    if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) texX = texWidth - texX - 1;

    // qFP16_t step = qFP16_Div(qFP16_Constant(texHeight), qFP16_IntToFP(lineHeight));
    qFP16_t step = texStepValues[lineHeight];
    // printf("step: %s, lineHeight: %i\n", qFP16_FPToA(step, ans, 4), lineHeight);
    // qFP16_t texPos = ((drawStart - (h >> 1) + (lineHeight >> 1)) * step);
    qFP16_t halfH =  qFP16_IntToFP(h >> 1);
    qFP16_t halfLh =  qFP16_IntToFP(lineHeight >> 1);
    
    qFP16_t texPos =qFP16_Mul(qFP16_Add(qFP16_Sub(qFP16_IntToFP(drawStart), halfH), halfLh), step);

    for (uint8_t y = drawStart; y < drawEnd; ++y) {
      uint8_t texY = (texPos >> 16) & (texHeight - 1);
      texPos += step;
      color = texture[texNum][(texY << 5) + texX];
      uint8_t* bufPtr = &buffer[y * w + x];
      if (currentStep == 1) bufPtr[i] = color; // skyColor
      else {
        bufPtr[i] = color;
        bufPtr[i+1] = color;
        color = texture[texNum][(texY << 5) + texX + 1];
        bufPtr[i+2] = color;
        bufPtr[i+3] = color;
        // color = texture[texNum][(texY << 5) + texX + 2];
        // bufPtr[i+4] = color;
        // bufPtr[i+5] = color;
      }
    }
    

    // Draw floor
    for (uint8_t y = drawEnd; y < h; ++y) {
        uint8_t* bufPtr = &buffer[y * w + x];
        color = floorColors[y];
        if (currentStep == 1) bufPtr[i] = color; // skyColor
        else {
          bufPtr[i] = color;
          bufPtr[i+1] = color;
          bufPtr[i+2] = color;
          bufPtr[i+3] = color;
          // bufPtr[i+4] = color;
          // bufPtr[i+5] = color;
        }
    }
  }
  bigMode? drawBufferDouble() : drawBufferRegular();

  // clear progress
  // draw_hline(COLOR_FROM_RGB8(0, 0, 0), xOffset, yOffset - 1, w);
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

void drawTexture() {
    // Loop through the screen's height
    for (uint16_t y = 0; y < SCREEN_HEIGHT; y++) {
        // Compute the corresponding Y coordinate in the texture
        uint8_t texY = y % texHeight;

        // Compute the base address of the current screen row
        uint16_t row_addr = y * SCREEN_WIDTH;

        // Set the starting address for the row
        RIA.addr0 = row_addr;
        RIA.step0 = 1; // Move 2 bytes per pixel in 16bpp mode

        // Loop through the screen's width
        for (uint16_t x = 0; x < SCREEN_WIDTH; x++) {
            // Compute the corresponding X coordinate in the texture
            uint8_t texX = x % texWidth;

            // Compute the texture index
            uint16_t texIndex = (texY * texWidth) + texX;

            // Retrieve the color from the texture
            uint8_t color = texture[0][texIndex];

            // Write the color to the screen buffer
            RIA.rw0 = color;
        }
    }
}



void draw_ui() {
  // draw_rect(LIGHT_GRAY, 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  // draw_rect(LIGHT_GRAY, xOffset, yOffset, w, h); 
  // fill_rect(BLACK, 5,110, 11 * 5, 8);
  // set_cursor(5, 110);
  // sprintf(*buf," step: %i ", movementStep);
  // draw_string(*buf);
}

// Function to draw the world map using the draw_rect function
void draw_map() {

    // fill_rect_fast (BLACK, 0, 0, mapWidth * TILE_SIZE, mapHeight * TILE_SIZE); // draw frame

    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
          if (worldMap[i][j] > 0) {
            uint8_t color;
            switch(worldMap[i][j])
            {
              case 1: color = 1; break; //red
              case 2: color = 2; break; //green
              case 3: color = 12; break; //blue
              case 4: color = 7; break; //white
              case 5: color = 11; break; //yellow
            }
            
            // Draw a wall tile (represented by a white rectangle)
              draw_rect(color, i * TILE_SIZE + 1, j * TILE_SIZE + 1, TILE_SIZE, TILE_SIZE);
            } else {
              draw_rect(BLACK, i * TILE_SIZE + 1, j * TILE_SIZE + 1, TILE_SIZE, TILE_SIZE);
            }
        } 
      }
} 

void draw_player(){

    qFP16_t l = qFP16_Constant(4);
    qFP16_t ts = qFP16_Constant(TILE_SIZE);

    uint16_t x = (uint16_t)(qFP16_FPToInt(qFP16_Mul(posX, ts)));
    uint16_t y = (uint16_t)(qFP16_FPToInt(qFP16_Mul(posY, ts)));

    int8_t lX = (int8_t)(qFP16_FPToInt(qFP16_Mul(dirX, l)));
    int8_t lY = (int8_t)(qFP16_FPToInt(qFP16_Mul(dirY, l)));
    int8_t plX = (int8_t)(qFP16_FPToInt(qFP16_Mul(prevDirX, l)));
    int8_t plY = (int8_t)(qFP16_FPToInt(qFP16_Mul(prevDirY, l)));
    
    draw_line(COLOR_FROM_RGB8(0, 0, 0), prevPlayerX, prevPlayerY, prevPlayerX + plX, prevPlayerY + plY);
    draw_pixel(COLOR_FROM_RGB8(0, 0, 0), prevPlayerX, prevPlayerY);
    draw_line(COLOR_FROM_RGB8(92, 255, 190), x, y, x + lX, y + lY);
    draw_pixel(COLOR_FROM_RGB8(247, 118, 32), x, y);
    prevPlayerX = x;
    prevPlayerY = y;
    prevDirX = dirX;
    prevDirY = dirY;
}

void handleCalculation() {

    gamestate = GAMESTATE_CALCULATING;
    // draw_ui(buffer);
    // draw_map(buffer);
    draw_player();
    raycastFP();
    // printWalls();
    // drawWalls(buffer);
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

    // prepare floor colors array
    // uint8_t r = 64, g = 40, b = 20;
    for(uint8_t i = 0; i < h; i++){
      floorColors[i] = mapValue(i, 0, h, 232, 255);
    }


    posX = qFP16_FloatToFP(9.0f);
    posY = qFP16_FloatToFP(11.0f);  //x and y start position
    dirX = qFP16_FloatToFP(0.0f);
    dirY = qFP16_FloatToFP(-1.0f); //initial direction vector

    planeX = qFP16_FloatToFP(0.66f);
    planeY = qFP16_FloatToFP(0.0f); //the 2d raycaster version of camera plane
    moveSpeed = qFP16_FloatToFP(0.25f); //the constant value is in squares/second
    sin_r = qFP16_FloatToFP(0.309f); // precomputed value of sin(1 /20 pi  rad)
    cos_r = qFP16_FloatToFP(0.951f);

    prevPlayerX = (uint16_t)(qFP16_FPToInt(posX) * TILE_SIZE);
    prevPlayerY = (uint16_t)(qFP16_FPToInt(posY) * TILE_SIZE);
    prevDirX = dirX;
    prevDirY = dirY;

    gamestate = GAMESTATE_INIT;

    // generateTextures();

    printf("Precalculating values...\n");
    precalculateRotations();
    

    // WaitForAnyKey();

    

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    bpp = bits_per_pixel();
    erase_canvas();

    printf("width: %i, height: %i\n", canvas_width(), canvas_height());


    set_text_color(color(WHITE,bpp==8));

    drawTexture();

    draw_ui();


    draw_map();

    handleCalculation();



    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        // if (timer == 255 && currentStep == 2) { 
        //     currentStep = 1;
        //     draw_map();
        //     draw_ui();
        //     handleCalculation();
        // }

        if (timer == 64 && currentStep > 2) { 
            currentStep = 1;
            draw_map();
            draw_ui();
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
                        set_cursor(10, 110);
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
                  if(worldMap[qFP16_FPToInt(qFP16_Add(posX, qFP16_Mul(dirX, moveSpeed)))][qFP16_FPToInt(posY)] == false) 
                    posX = qFP16_Add(posX, qFP16_Mul(dirX, moveSpeed));
                  if(worldMap[qFP16_FPToInt(posX)][qFP16_FPToInt(qFP16_Add(posY, qFP16_Mul(dirY, moveSpeed)))] == false) 
                    posY = qFP16_Add(posY, qFP16_Mul(dirY, moveSpeed));
                }
                if (key(KEY_DOWN)) {
                  gamestate = GAMESTATE_MOVING;
                  if(worldMap[qFP16_FPToInt(qFP16_Sub(posX, qFP16_Mul(dirX, moveSpeed)))][qFP16_FPToInt(posY)] == false) 
                    posX = qFP16_Sub(posX, qFP16_Mul(dirX, moveSpeed));
                  if(worldMap[qFP16_FPToInt(posX)][qFP16_FPToInt(qFP16_Sub(posY, qFP16_Mul(dirY, moveSpeed)))] == false) 
                    posY = qFP16_Sub(posY, qFP16_Mul(dirY, moveSpeed));
                }
                if (key(KEY_M)) {
                    bigMode = !bigMode;
                    drawTexture();
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

                if (!paused) {
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = movementStep;
                    }

                    // erase_canvas();
                    handleCalculation();

                }
            }
        // } else { // no keys down
        //     handled_key = false;
        // }
        // erase_canvas();

    }

    return 0;
    
}


