#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "colors.h"
#include "usb_hid_keys.h"
// #include "bitmap_graphics_db.h"
#include "bitmap_graphics.h"
#include "qfp16.h"
#include "textures.h"

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define mapWidth 20
#define mapHeight 18
#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 124 
#define WINDOW_WIDTH 120
#define WINDOW_HEIGTH 64
#define MOVEMENT_STEP 8
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

uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 2;
uint8_t currentScale = SCALE;

uint8_t xOffset = SCREEN_WIDTH  / (SCALE * 2);
uint8_t yOffset = SCREEN_HEIGHT / (SCALE * 2);


const uint16_t w = WINDOW_WIDTH;
const uint16_t h = WINDOW_HEIGTH; 

bool wireMode = false;

uint8_t buffer[WINDOW_HEIGTH][WINDOW_WIDTH];
uint8_t floorColors[WINDOW_HEIGTH];


bool gamestate_changed = true;
uint8_t gamestate = 1;  //  0          1         2        3
uint8_t gamestate_prev = 1;
#define GAMESTATE_INIT 0
#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
#define GAMESTATE_CALCULATING 3

#define ROTATION_STEPS 24

qFP16_t dirXValues[ROTATION_STEPS];
qFP16_t dirYValues[ROTATION_STEPS];
qFP16_t planeXValues[ROTATION_STEPS];
qFP16_t planeYValues[ROTATION_STEPS];
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
  {1,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,4,0,0,0,2,2,2,2,2,0,0,3,0,3,0,3,0,1},
  {1,0,4,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1},
  {1,0,4,0,0,0,2,0,0,0,2,0,0,3,0,0,0,3,0,1},
  {1,0,4,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,1},
  {1,0,4,0,0,0,2,2,0,2,2,0,0,3,0,3,0,3,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,4,4,4,4,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,0,0,4,0,0,0,0,1},
  {1,4,0,0,0,0,2,0,4,0,4,4,4,0,4,0,0,4,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,4,0,4,0,0,4,0,1},
  {1,4,0,4,4,4,4,4,4,0,0,0,4,0,4,0,0,4,0,1},
  {1,4,0,0,0,0,0,0,0,0,4,4,4,0,4,0,0,4,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,1},
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



void drawBuffer() {
  // Loop through each row
  for (uint8_t j = 0; j < h; j++) {
      // Calculate the starting address of the row
      uint16_t row_addr = ((SCREEN_WIDTH) * (yOffset + j)) + (xOffset);

      // Set the initial address and step for the row
      RIA.addr0 = row_addr;
      RIA.step0 = 1; // Move 2 bytes per pixel in 16bpp mode

      // Fill the row with the color
      for (uint8_t i = 0; i < w; i+=8) {
          RIA.rw0 = buffer[j][i] ;
          RIA.rw0 = buffer[j][i+1] ;
          RIA.rw0 = buffer[j][i+2] ;
          RIA.rw0 = buffer[j][i+3] ;
          RIA.rw0 = buffer[j][i+4] ;
          RIA.rw0 = buffer[j][i+5] ;
          RIA.rw0 = buffer[j][i+6] ;
          RIA.rw0 = buffer[j][i+7] ;
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

    // Compute for right rotation (0.25 rad steps)
    for (int i = 0; i < ROTATION_STEPS; i++) {
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
  // qFP16_t invH = qFP16_Div(one, qFP16_IntToFP(h));
  
  for(uint8_t x = 0; x < w; x += currentStep)
  {
    
    // qFP16_t cameraX = qFP16_Sub(qFP16_Mul(qFP16_IntToFP(x << 1), invW), one);
    qFP16_t cameraX = cameraXValues[currentRotStep][x];
    // printf("cameraX: %s\n", qFP16_FPToA(cameraX, ans, 4));
    // qFP16_t rayDirX = qFP16_Add(qFP16_Mul(planeX, cameraX), dirX);
    // qFP16_t rayDirY = qFP16_Add(qFP16_Mul(planeY, cameraX), dirY);
    qFP16_t rayDirX = rayDirXValues[currentRotStep][x];
    qFP16_t rayDirY = rayDirYValues[currentRotStep][x];
    // printf("rayDirX: %s\n", qFP16_FPToA(rayDirX, ans, 4));
    // printf("rayDirY: %s\n", qFP16_FPToA(rayDirY, ans, 4));

    // printf("x: %i, cameraX: %f\n", x, cameraX);
    //which box of the map we're in
    int16_t mapX = (int16_t)qFP16_FPToInt(posX);
    int16_t mapY = (int16_t)qFP16_FPToInt(posY);

    //length of ray from current position to next x or y-side
    qFP16_t sideDistX;
    qFP16_t sideDistY;

    
    //length of ray from one x or y-side to next x or y-side
    //these are derived as:
    //deltaDistX = sqrt(1 + (rayDirY * rayDirY) / (rayDirX * rayDirX))
    //deltaDistY = sqrt(1 + (rayDirX * rayDirX) / (rayDirY * rayDirY))
    //which can be simplified to f_abs(|rayDir| / rayDirX) and f_abs(|rayDir| / rayDirY)
    //where |rayDir| is the length of the vector (rayDirX, rayDirY). Its length,
    //unlike (dirX, dirY) is not 1, however this does not matter, only the
    //ratio between deltaDistX and deltaDistY matters, due to the way the DDA
    //currentStepping further below works. So the values can be computed as below.
    // Division through zero is prevented, even though technically that's not
    // needed in C++ with IEEE 754 floating point16_t values.
    
    // printf("fdeltaDistX: %i, fdeltaDistY: %i\n", (int)(fdeltaDistX*1000), (int)(fdeltaDistY*1000));

  
    qFP16_t deltaDistX = (rayDirX == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirX));
    qFP16_t deltaDistY = (rayDirY == 0) ? MAXQVAL : qFP16_Abs(qFP16_Div(one, rayDirY));
    // printf("deltaDistX: %s\n", qFP16_FPToA(deltaDistX, ans, 4));
    // printf("deltaDistY: %s\n", qFP16_FPToA(deltaDistY, ans, 4));

    qFP16_t perpWallDist;

    //what direction to step in x or y-direction (either +1 or -1)
    int8_t stepX;
    int8_t stepY;

    int8_t hit = 0; //was there a wall hit?
    int8_t side; //was a NS or a EW wall hit?
    //calculate step and initial sideDist
    if(rayDirX < 0)
    {
      stepX = -1;
      sideDistX = qFP16_Mul(qFP16_Sub(posX, qFP16_IntToFP(mapX)), deltaDistX);
    }
    else
    {
      stepX = 1;
      sideDistX = qFP16_Mul(qFP16_Sub(qFP16_Add(qFP16_IntToFP(mapX), one), posX), deltaDistX);
    }
    if(rayDirY < 0)
    {
      stepY = -1;
      sideDistY = qFP16_Mul(qFP16_Sub(posY, qFP16_IntToFP(mapY)), deltaDistY);
    }
    else
    {
      stepY = 1;
      sideDistY = qFP16_Mul(qFP16_Sub(qFP16_Add(qFP16_IntToFP(mapY), one), posY), deltaDistY);
    }
    //perform DDA
    while(hit == 0)
    {
      //jump to next map square, either in x-direction, or in y-direction
      if(sideDistX < sideDistY)
      {
        // fsideDistX += fdeltaDistX;
        sideDistX = qFP16_Add(sideDistX, deltaDistX);
        mapX += stepX;
        side = 0;
      }
      else
      {
        // fsideDistY += fdeltaDistY;
        sideDistY = qFP16_Add(sideDistY, deltaDistY);
        mapY += stepY;
        side = 1;
      }
      //Check if ray has hit a wall
      if(worldMap[mapX][mapY] > 0) hit = 1;
    }
    //Calculate distance projected on camera direction. This is the shortest distance from the point16_t where the wall is
    //hit to the camera plane. Euclidean to center camera point16_t would give fisheye effect!
    //This can be computed as (mapX - posX + (1 - stepX) / 2) / rayDirX for side == 0, or same formula with Y
    //for size == 1, but can be simplified to the code below thanks to how sideDist and deltaDist are computed:
    //because they were left scaled to |rayDir|. sideDist is the entire length of the ray above after the multiple
    //steps, but we subtract deltaDist once because one step more into the wall was taken above.
    
    if(side == 0) perpWallDist = qFP16_Sub(sideDistX, deltaDistX);
    else          perpWallDist = qFP16_Sub(sideDistY, deltaDistY);

    //Calculate height of line to draw on screen
    uint16_t lineHeight = (uint16_t)qFP16_FPToInt(qFP16_Div(qFP16_IntToFP(h), perpWallDist));
    if (lineHeight > h) lineHeight = h;
    
    // //choose wall color
    // ColorRGBA color;
    // uint8_t r, g, b;
    // uint16_t wallColor, color = WHITE;
    uint8_t color8, color;
    // switch(worldMap[mapX][mapY])
    // {
    //   case 1: 
    //     // r = mapValue(lineHeight, 2, h, 150, 255);
    //     // r = 150 + lineHeight;
    //     // g = 40;
    //     // b = 40;
    //     if (side == 0)
    //       color8 = mapValue(lineHeight, 2, h, 196, 201);
    //     else 
    //       color8 = mapValue(lineHeight, 2, h, 124, 129);
    //     break; //red
    //   case 2:
    //     // r = 41; 
    //     // g = mapValue(lineHeight, 2, h, 150, 255);
    //     // g = 150 + lineHeight;
    //     // b = 41;
    //     if (side == 0)
    //       color8 = mapValue(lineHeight, 2, h, 118, 123);
    //     else
    //       color8 = mapValue(lineHeight, 2, h, 40, 45);
    //     break; //green
    //   case 3:
    //     // r = 40;
    //     // g = mapValue(lineHeight, 2, h, 120, 227);
    //     // b = mapValue(lineHeight, 2, h, 150, 255);
    //     // g = 120 + lineHeight;
    //     // b = 150 + lineHeight;
    //     if (side == 0)
    //       color8 = mapValue(lineHeight, 2, h, 28, 33);
    //     else
    //       color8 = mapValue(lineHeight, 2, h, 17, 21);
    //     break; //blue
    //   case 4: 
    //     // r = g = b = mapValue(lineHeight, 2, h, 150, 255);
    //     // r = g = b = 180 + lineHeight;
    //     if (side == 0) 
    //       color8 = mapValue(lineHeight, 2, h, 244, 255);
    //     else 
    //       color8 = mapValue(lineHeight, 2, h-20, 235, 243);
    //     break; //white
    //   case 5: 
    //     // r = mapValue(lineHeight, 2, h, 150, 255);
    //     // g = mapValue(lineHeight, 2, h, 150, 255);
    //     // r = 180 + lineHeight;
    //     // g = 180 + lineHeight;
    //     // b = 40;
    //     if (side == 0)
    //       color8 = mapValue(lineHeight, 2, h, 220, 225);
    //     else
    //       color8 = mapValue(lineHeight, 2, h, 214, 219);
    //     break; //yellow
    // }

    // printf("color: %i\n", color);
    // printf("r: %i, g: %i, b: %i\n", r, g, b);
    // //give x and y sides different brightness
    // if(side == 1) {
    //   r = r - 20;
    //   g = g - 20;
    //   b = g - 20;
    // }

    // printf("r: %i, g: %i, b: %i\n", r, g, b);

    // wallColor = COLOR_FROM_RGB8(r, g, b);

    int8_t drawStart = -lineHeight / 2 + h / 2;
    if(drawStart < 0) drawStart = 0;
    uint8_t drawEnd = drawStart + lineHeight;

    // uint16_t skyColor = COLOR_FROM_RGB8(52, 168, 235);
    uint8_t skyColor = 14;

    // for (uint8_t i = 1; i <= currentStep; i++){
      color = skyColor;
      for (uint8_t y = 0; y < drawStart; y++) {
        if (currentStep == 2) {
          buffer[y][x] = color;
          buffer[y][x+1] = color;
        } else {
          buffer[y][x] = color;
          buffer[y][x+1] = color;
          buffer[y][x+2] = color;
          buffer[y][x+3] = color;
          buffer[y][x+4] = color;
          buffer[y][x+5] = color;
          buffer[y][x+6] = color;
          buffer[y][x+7] = color;
          buffer[y][x+8] = color;
          buffer[y][x+9] = color;
        }
      }
      // color = color8;
      // for (uint8_t y = drawStart; y < drawEnd; y++) {
      //   if (currentStep == 2) {
      //     buffer[y][x] = color;
      //     buffer[y][x+1] = color;
      //   } else {
      //     buffer[y][x] = color;
      //     buffer[y][x+1] = color;
      //     buffer[y][x+2] = color;
      //     buffer[y][x+3] = color;
      //     buffer[y][x+4] = color;
      //     buffer[y][x+5] = color;
      //     buffer[y][x+6] = color;
      //     buffer[y][x+7] = color;
      //     buffer[y][x+8] = color;
      //     buffer[y][x+9] = color;
      //   }
      // }
      for (uint8_t y = drawEnd; y < h; y++) {
        color = floorColors[y];
        if (currentStep == 2) {
          buffer[y][x] = color;
          buffer[y][x+1] = color;
        } else {
          buffer[y][x] = color;
          buffer[y][x+1] = color;
          buffer[y][x+2] = color;
          buffer[y][x+3] = color;
          buffer[y][x+4] = color;
          buffer[y][x+5] = color;
          buffer[y][x+6] = color;
          buffer[y][x+7] = color;
          buffer[y][x+8] = color;
          buffer[y][x+9] = color;
        }
      }

      //texturing calculations
      uint8_t texNum = (worldMap[mapX][mapY] - 1) * 2 + side; //1 subtracted from it so that texture 0 can be used!

      

      // Calculate wallX
      qFP16_t wallX;
      if (side == 0) wallX = qFP16_Add(posY, qFP16_Mul(perpWallDist, rayDirY));
      else           wallX = qFP16_Add(posX, qFP16_Mul(perpWallDist, rayDirX));

      // Calculate fractional part using bitwise AND with the fixed-point fraction mask (assuming 16-bit fixed point)
      wallX = wallX & 0xFFFF; // Equivalent to wallX - qFP16_Floor(wallX)

      // x coordinate on the texture
      uint8_t texX = (wallX * texWidth) >> 16; // qFP16_Mul(wallX, qFP16_Constant(texWidth))
      if ((side == 0 && rayDirX > 0) || (side == 1 && rayDirY < 0)) {
          texX = texWidth - texX - 1;
      }

      // Precompute texPos step
      // qFP16_t step = (texHeight << 16) / lineHeight; // 
      qFP16_t step = qFP16_Div(qFP16_Constant(texHeight), qFP16_IntToFP(lineHeight));
      // Starting texture coordinate (simplified)
      qFP16_t texPos = ((drawStart - (h >> 1) + (lineHeight >> 1)) * step);

      // Precompute texture offset (optimized)
      uint8_t texOffsetX = texX;

      // Loop through each pixel from drawStart to drawEnd
      for (int y = drawStart; y < drawEnd; y++) {
          // Get texY and increment texPos
          uint8_t texY = (texPos >> 16) & (texHeight - 1);
          texPos += step;

          // Fetch the color from texture memory
          color = texture[texNum][(texY * texWidth) + texOffsetX];

          // Optimize darkening using bitwise operation
          // if (side == 1) {
          //     color = (color >> 1) & 0x7F7F7F; // Shift right to halve brightness and mask
          // }

          // Assign color to buffer (could optimize with loop unrolling or SIMD if needed)
          if (currentStep == 2) {
            buffer[y][x] = color;
            buffer[y][x+1] = color;
          } else {
            buffer[y][x] = color;
            buffer[y][x+1] = color;
            buffer[y][x+2] = color;
            buffer[y][x+3] = color;
            buffer[y][x+4] = color;
            buffer[y][x+5] = color;
            buffer[y][x+6] = color;
            buffer[y][x+7] = color;
            buffer[y][x+8] = color;
            buffer[y][x+9] = color;
          }
      }
    // }

    // show progress
    // draw_pixel(color, x + xOffset, yOffset - 1);
  }
  drawBuffer();
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

void draw_ui() {
  // draw_rect(COLOR_FROM_RGB8(50, 50, 50), 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  // draw_rect(COLOR_FROM_RGB8(50, 50, 50), xOffset, yOffset, w, h); 
  draw_rect(LIGHT_GRAY, 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  draw_rect(LIGHT_GRAY, xOffset, yOffset, w, h); 
}

// Function to draw the world map using the draw_rect function
void draw_map() {

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
              // case 1: color = COLOR_FROM_RGB8(255, 0, 0); break; //red
              // case 2: color = COLOR_FROM_RGB8(0, 255, 0); break; //green
              // case 3: color = COLOR_FROM_RGB8(0, 0, 255); break; //blue
              // case 4: color = COLOR_FROM_RGB8(255, 255, 255); break; //white
              // case 5: color = COLOR_FROM_RGB8(255, 255, 43); break; //yellow
            }
            
            // Draw a wall tile (represented by a white rectangle)
            if (wireMode) {
              draw_pixel(color, i * TILE_SIZE, j * TILE_SIZE);
            } else {
              draw_rect(color, i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE);
            }
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

    bool handled_key = true;
    while (1){
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
                //if ((((i<<3)+j)>3) && (new_key != (keystates[i] & (1<<j)))) {
                    // printf( "key %d %s\n", ((i<<3)+j), (new_key ? "pressed" : "released"));
                //}
            }
            keystates[i] = new_keys;
        }
        // check for a key down
        if (!(keystates[0] & 1)) {
            if (!handled_key) { // handle only once per single keypress
                // handle the keystrokes
                handled_key = true;
                break;
            }
        } else { // no keys down
            handled_key = false;
        }
    }
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
    sin_r = qFP16_FloatToFP(0.24740395925); // precomputed value of sin(0.25 rad)
    cos_r = qFP16_FloatToFP(0.96891242171f);

    prevPlayerX = (uint16_t)(qFP16_FPToInt(posX) * TILE_SIZE);
    prevPlayerY = (uint16_t)(qFP16_FPToInt(posY) * TILE_SIZE);
    prevDirX = dirX;
    prevDirY = dirY;

    gamestate = GAMESTATE_INIT;

    // generateTextures();

    printf("Precalculating values...\n");
    precalculateRotations();
    

    for (uint8_t i = 0; i < ROTATION_STEPS; i++) {
      printf("i: %i, dirX: %s\n", i, qFP16_FPToA(dirXValues[i], ans, 4));
    }

    

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    bpp = bits_per_pixel();
    erase_canvas();

    printf("width: %i, height: %i\n", canvas_width(), canvas_height());


    // Precompute sine and cosine values
    set_text_color(color(WHITE,bpp==8));

    draw_ui();


    draw_map();

    handleCalculation();



    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 16 && currentStep > 2) { 
            currentStep = 2;
            draw_map();
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
                  // qFP16_t oldDirX = dirX;
                  // dirX = qFP16_Sub(qFP16_Mul(dirX, cos_r), qFP16_Mul(dirY, sin_r));
                  // dirY = qFP16_Add(qFP16_Mul(oldDirX, sin_r), qFP16_Mul(dirY, cos_r));
                  // qFP16_t oldPlaneX = planeX;
                  // planeX = qFP16_Sub(qFP16_Mul(planeX, cos_r), qFP16_Mul(planeY, sin_r));
                  // planeY = qFP16_Add(qFP16_Mul(oldPlaneX, sin_r), qFP16_Mul(planeY, cos_r));;

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
                  // qFP16_t oldDirX = dirX;
                  // dirX = qFP16_Sub(qFP16_Mul(dirX, cos_r), qFP16_Mul(dirY, -sin_r));
                  // dirY = qFP16_Add(qFP16_Mul(oldDirX, -sin_r), qFP16_Mul(dirY, cos_r));
                  // qFP16_t oldPlaneX = planeX;
                  // planeX = qFP16_Sub(qFP16_Mul(planeX, cos_r), qFP16_Mul(planeY, -sin_r));
                  // planeY = qFP16_Add(qFP16_Mul(oldPlaneX, -sin_r), qFP16_Mul(planeY, cos_r));;

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
                if (key(KEY_3)) {
                    show_buffers_indicators = !show_buffers_indicators;
                }
                if (key(KEY_M)) {
                  wireMode = !wireMode;
                }

                if (key(KEY_ESC)) {
                    break;
                }
                handled_key = true;

                printf("currentRotStep: %i\n", currentRotStep); 
                printf("dirX: %s\n", qFP16_FPToA(dirXValues[currentRotStep], ans, 4));
                printf("dirX: %s\n", qFP16_FPToA(dirX, ans, 4));
                printf("dirY: %s\n", qFP16_FPToA(dirY, ans, 4));
                printf("planeX: %s\n", qFP16_FPToA(planeX, ans, 4));
                printf("planeY: %s\n", qFP16_FPToA(planeY, ans, 4));

                if (!paused) {
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = MOVEMENT_STEP;
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


