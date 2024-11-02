#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>
#include <inttypes.h>
#include "colors.h"
#include "usb_hid_keys.h"
// #include "bitmap_graphics_db.h"
#include "bitmap_graphics.h"

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define mapWidth 20
#define mapHeight 18
#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 124 
#define MIN_STEP 5
#define TILE_SIZE 2
#define SCALE 128

float posX = 9, posY = 11;  //x and y start position
float dirX =  0, dirY = -1; //initial direction vector
float planeX = 0.66, planeY = 0; //the 2d raycaster version of camera plane
// float dirX =  -0.681, dirY = -0.731; //initial direction vector
// float planeX = 0.482, planeY = -0.449; //the 2d raycaster version of camera plane
float moveSpeed = 0.5; //the constant value is in squares/second
// float sin_r = 0.09983341664; // precomputed value of sin(0.1 rad)
// float cos_r = 0.99500416527; 
float sin_r = 0.24740395925; // precomputed value of sin(0.25 rad)
float cos_r = 0.96891242171;

uint8_t currentStep = MIN_STEP;

#define xOffset SCREEN_WIDTH / 4
#define yOffset SCREEN_HEIGHT / 4

uint16_t w = SCREEN_WIDTH / 2;
uint16_t h = SCREEN_HEIGHT / 2; 

bool wireMode = false;

uint8_t lineHeigths[SCREEN_WIDTH / 2];
uint8_t sides[SCREEN_WIDTH / 2];
uint16_t colors[SCREEN_WIDTH / 2];


bool gamestate_changed = true;
uint8_t gamestate = 1;  //  0          1         2        3
uint8_t gamestate_prev = 1;
#define GAMESTATE_INIT 0
#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
#define GAMESTATE_CALCULATING 3


// for double buffering
uint16_t buffers[2];
uint8_t active_buffer = 0;

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
  {1,4,0,0,0,0,5,0,4,0,4,4,4,0,4,0,0,4,0,1},
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


int raycast(uint16_t buffer)
{
  //calculate and cache the results
  for(int16_t x = 0; x < w; x += currentStep)
  {
    //calculate ray position and direction
    float cameraX = 2 * x / (float)w - 1; //x-coordinate in camera space
    float rayDirX = dirX + planeX * cameraX;
    float rayDirY = dirY + planeY * cameraX;

    // printf("x: %i, cameraX: %f\n", x, cameraX);
    //which box of the map we're in
    int16_t mapX = (int16_t)posX;
    int16_t mapY = (int16_t)posY;

    //length of ray from current position to next x or y-side
    float sideDistX;
    float sideDistY;

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
    float deltaDistX = (rayDirX == 0) ? 1e30 : f_abs(1 / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30 : f_abs(1 / rayDirY);

    float perpWallDist;

    //what direction to step in x or y-direction (either +1 or -1)
    int8_t stepX;
    int8_t stepY;

    int8_t hit = 0; //was there a wall hit?
    int8_t side; //was a NS or a EW wall hit?
    //calculate step and initial sideDist
    if(rayDirX < 0)
    {
      stepX = -1;
      sideDistX = (posX - mapX) * deltaDistX;
    }
    else
    {
      stepX = 1;
      sideDistX = (mapX + 1.0 - posX) * deltaDistX;
    }
    if(rayDirY < 0)
    {
      stepY = -1;
      sideDistY = (posY - mapY) * deltaDistY;
    }
    else
    {
      stepY = 1;
      sideDistY = (mapY + 1.0 - posY) * deltaDistY;
    }
    //perform DDA
    while(hit == 0)
    {
      //jump to next map square, either in x-direction, or in y-direction
      if(sideDistX < sideDistY)
      {
        sideDistX += deltaDistX;
        mapX += stepX;
        side = 0;
      }
      else
      {
        sideDistY += deltaDistY;
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
    if(side == 0) perpWallDist = (sideDistX - deltaDistX);
    else          perpWallDist = (sideDistY - deltaDistY);

    //Calculate height of line to draw on screen
    uint8_t lineHeight = (uint8_t)(h / perpWallDist);
    if (lineHeight > h) lineHeight = h;
    // printf("lineHeight: %i, h: %i\n", lineHeight, h);

    lineHeigths[x] = lineHeight;
    sides[x] = side;

    
    // //choose wall color
    // ColorRGBA color;
    uint8_t r, g, b;
    uint16_t color = WHITE;
    switch(worldMap[mapX][mapY])
    {
      case 1: 
        r = mapValue(lineHeight, 2, h, 150, 255);
        g = 40;
        b = 40;
        break; //red
      case 2:
        r = 41; 
        g = mapValue(lineHeight, 2, h, 150, 255);
        b = 41;
        break; //green
      case 3:
        r = 40;
        g = mapValue(lineHeight, 2, h, 120, 227);
        b = mapValue(lineHeight, 2, h, 150, 255);
 
        break; //blue
      case 4: 
        r = g = b = mapValue(lineHeight, 2, h, 150, 255);
        break; //white
      case 5: 
        r = mapValue(lineHeight, 2, h, 150, 255);
        g = mapValue(lineHeight, 2, h, 150, 255);
        b = 40;
        break; //yellow
    }

    // printf("color: %i\n", color);
    // printf("r: %i, g: %i, b: %i\n", r, g, b);
    // //give x and y sides different brightness
    if(side == 1) {
      r = r - 20;
      g = g - 20;
      b = g - 20;
    }

    // printf("r: %i, g: %i, b: %i\n", r, g, b);

    color = COLOR_FROM_RGB8(r, g, b);
    colors[x] = color;
  }
  return 0;
}

int raycastInt(uint16_t buffer)
{
  //calculate and cache the results
  for(int16_t x = 0; x < w; x += currentStep)
  {
    //calculate ray position and direction
    float cameraX = 2 * x / (float)w - 1; //x-coordinate in camera space
    float rayDirX = dirX + planeX * cameraX;
    float rayDirY = dirY + planeY * cameraX;

    int16_t iRayDirX = rayDirX * SCALE;
    int16_t iRayDirY = rayDirY * SCALE;
    if (iRayDirX == 0) iRayDirX = 1;
    if (iRayDirY == 0 && rayDirY > 0) { 
      iRayDirY = 1;
    } else if (iRayDirY == 0 && rayDirY < 0){
        iRayDirY = -1;
    }

    // printf("x: %i, cameraX: %f\n", x, cameraX);
    //which box of the map we're in
    int16_t mapX = (int16_t)posX;
    int16_t mapY = (int16_t)posY;
    int16_t iMapX = (int16_t)(posX * SCALE);
    int16_t iMapY = (int16_t)(posY * SCALE);
    uint8_t sMapX, sMapY;

    //length of ray from current position to next x or y-side
    // float sideDistX;
    // float sideDistY;
    int16_t iSideDistX;
    int16_t iSideDistY;

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
    float deltaDistX = (rayDirX == 0) ? 1e30 : f_abs(1 / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30 : f_abs(1 / rayDirY);

    //avoid overflow
    uint16_t iDeltaDistX = (deltaDistX < SCALE) ? (uint16_t)(deltaDistX * SCALE) : 6000;
    uint16_t iDeltaDistY = (deltaDistY < SCALE) ? (uint16_t)(deltaDistY * SCALE) : 6000;

    // float perpWallDist;

    int16_t iPerpWallDist;

    //what direction to step in x or y-direction (either +1 or -1)
    int16_t stepX, iStepX;
    int16_t stepY, iStepY;

    int8_t hit = 0, iHit = 0; //was there a wall hit?
    int8_t side, iSide; //was a NS or a EW wall hit?

    //calculate step and initial sideDist
    // if(rayDirX < 0)
    // {
    //   stepX = -1;
    //   sideDistX = (posX - mapX) * deltaDistX;
    // }
    // else
    // {
    //   stepX = 1;
    //   sideDistX = (mapX + 1.0 - posX) * deltaDistX;
    // }
    // if(rayDirY < 0)
    // {
    //   stepY = -1;
    //   sideDistY = (posY - mapY) * deltaDistY;
    // }
    // else
    // {
    //   stepY = 1;
    //   sideDistY = (mapY + 1.0 - posY) * deltaDistY;
    // }

    //int 
    if(iRayDirX < 0)
    {
      iStepX = -SCALE;
      iSideDistX = (int16_t)((posX - mapX) * deltaDistX * SCALE);
    }
    else
    {
      iStepX = SCALE;
      iSideDistX = (int16_t)((mapX + 1.0 - posX) * deltaDistX * SCALE);
    }
    if(iRayDirY < 0)
    {
      iStepY = -SCALE;
      iSideDistY = (int16_t)((posY - mapY) * deltaDistY * SCALE);
    }
    else
    {
      iStepY = SCALE;
      iSideDistY = (int16_t)((mapY + 1.0 - posY) * deltaDistY * SCALE);
    }

  
    //perform DDA
    // while(hit == 0)
    // {
    //   //jump to next map square, either in x-direction, or in y-direction
    //   if(sideDistX < sideDistY)
    //   {
    //     sideDistX += deltaDistX;
    //     mapX += stepX;
    //     side = 0;
    //   }
    //   else
    //   {
    //     sideDistY += deltaDistY;
    //     mapY += stepY;
    //     side = 1;
    //   }
    //   //Check if ray has hit a wall
    //   if(worldMap[mapX][mapY] > 0) hit = 1;
    // }

    while(iHit == 0)
    {
      //jump to next map square, either in x-direction, or in y-direction
      if(iSideDistX < iSideDistY)
      {
        iSideDistX += iDeltaDistX;
        iMapX += iStepX;
        iSide = 0;
      }
      else
      {
        iSideDistY += iDeltaDistY;
        iMapY += iStepY;
        iSide = 1;
      }
      sMapX = iMapX / SCALE;
      sMapY = iMapY / SCALE;
      //Check if ray has hit a wall
      if(worldMap[sMapX][sMapY] > 0) iHit = 1;
    }

    //Calculate distance projected on camera direction. This is the shortest distance from the point16_t where the wall is
    //hit to the camera plane. Euclidean to center camera point16_t would give fisheye effect!
    //This can be computed as (mapX - posX + (1 - stepX) / 2) / rayDirX for side == 0, or same formula with Y
    //for size == 1, but can be simplified to the code below thanks to how sideDist and deltaDist are computed:
    //because they were left scaled to |rayDir|. sideDist is the entire length of the ray above after the multiple
    //steps, but we subtract deltaDist once because one step more into the wall was taken above.
    // if(side == 0) perpWallDist = (sideDistX - deltaDistX);
    // else          perpWallDist = (sideDistY - deltaDistY);

    if(iSide == 0) iPerpWallDist = (iSideDistX - iDeltaDistX);
    else          iPerpWallDist = (iSideDistY - iDeltaDistY);

    //Calculate height of line to draw on screen
    // uint8_t lineHeight = (int16_t)(h / perpWallDist);

    uint16_t iLineHeight = h * SCALE / iPerpWallDist;

    lineHeigths[x] = iLineHeight;
    sides[x] = iSide;

    
    // //choose wall color
    // ColorRGBA color;
    uint8_t r, g, b;
    uint16_t color = WHITE;
    switch(worldMap[sMapX][sMapY])
    {
      case 1: 
        r = mapValue(iLineHeight, 2, h, 150, 255);
        g = mapValue(iLineHeight, 2, h, 0, 43);
        b = mapValue(iLineHeight, 2, h, 0, 43);;
        break; //red
      case 2:
        r = mapValue(iLineHeight, 2, h, 20, 82); 
        g = mapValue(iLineHeight, 2, h, 150, 255);
        b = mapValue(iLineHeight, 2, h, 20, 43);

        break; //green
      case 3:
        r = mapValue(iLineHeight, 2, h, 20, 43);
        g = mapValue(iLineHeight, 2, h, 120, 227);
        b = mapValue(iLineHeight, 2, h, 150, 255);
 
        break; //blue
      case 4: 
        r = g = b = mapValue(iLineHeight, 2, h, 150, 255);
        break; //white
      case 5: 
        r = mapValue(iLineHeight, 2, h, 150, 255);
        g = mapValue(iLineHeight, 2, h, 150, 255);
        b = mapValue(iLineHeight, 2, h, 20, 43);
        break; //yellow
    }

    // printf("color: %i\n", color);

    // //give x and y sides different brightness
    if(iSide == 1) {
      r = r / 2;
      g = g / 2;
      b = g / 2;
    }

    color = COLOR_FROM_RGB8(r, g, b);
    colors[x] = color;
  }
  return 0;
}

void drawWalls(uint16_t buffer) {

  // now it's time to draw
  uint16_t skyColor = COLOR_FROM_RGB8(52, 168, 235);
  uint16_t floorColor = COLOR_FROM_RGB8(110, 97, 89);
  
  for(int16_t x = 0; x < w; x += currentStep)
  {
    uint8_t lineHeight = lineHeigths[x];
    uint16_t color = colors[x];
    //calculate lowest and highest pixel to fill in current stripe
    int16_t drawStart = -lineHeight / 2 + h / 2;
    if(drawStart < 0) drawStart = 0;
    // int16_t drawEnd = lineHeight / 2 + h / 2;
    int16_t drawEnd = lineHeight;
    if(drawEnd >= h) drawEnd = h - 1;

    uint16_t x1 = x + xOffset;
    uint16_t y1 = drawStart + yOffset;
    uint16_t width = currentStep;
    uint16_t height = drawEnd;

    if (!wireMode) {
      fill_rect(skyColor, x1, yOffset, width, drawStart);
      fill_rect(color, x1, y1, width, height);
      fill_rect(floorColor, x1, y1+height, width, drawStart);
      draw_hline(COLOR_FROM_RGB8(0,0,0), xOffset, yOffset+h, w); //should be necessary, but I got some missing lines on at the bottom
      // fill_rect2buffer(color, x1, y1, width, height, buffer);
    } else {
    draw_rect(color, x1, y1, width, height);
    // draw_rect2buffer(color, x1, y1, width, height, buffer);
    }
    // draw_vline2buffer(color, x1, y1, drawEnd, buffer);

  }
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

void draw_ui(uint16_t buffer) {
  draw_rect(COLOR_FROM_RGB8(50, 50, 50), 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  draw_rect(COLOR_FROM_RGB8(50, 50, 50), xOffset, yOffset, w, h); 
  // draw_rect2buffer(DARK_GRAY, 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, buffer); // draw frame
  // draw_rect2buffer(DARK_GRAY, xOffset, yOffset, w, h, buffer); // draw frame
}

// Function to draw the world map using the draw_rect function
void draw_map(uint16_t buffer) {

    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
          if (worldMap[i][j] > 0) {
            uint16_t color = COLOR_FROM_RGB8(255, 255, 255);
            switch(worldMap[i][j])
            {
              case 1: color = COLOR_FROM_RGB8(255, 0, 0); break; //red
              case 2: color = COLOR_FROM_RGB8(0, 255, 0); break; //green
              case 3: color = COLOR_FROM_RGB8(0, 0, 255); break; //blue
              case 4: color = COLOR_FROM_RGB8(255, 255, 255); break; //white
              case 5: color = COLOR_FROM_RGB8(255, 255, 43); break; //yellow
            }
            
            // Draw a wall tile (represented by a white rectangle)
            draw_rect(color, i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE);
            // draw_rect2buffer(color, i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, buffer);
          } else {
            draw_rect(COLOR_FROM_RGB8(0,0,0), i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE);
          }
        }
    }
} 

void draw_player(uint16_t buffer){
    uint16_t x = (uint16_t)(posX * TILE_SIZE);
    uint16_t y = (uint16_t)(posY * TILE_SIZE);
    draw_line(COLOR_FROM_RGB8(92, 255, 190), x, y, x + (int16_t)(dirX * 4), y + (int16_t)(dirY * 4));
    draw_pixel(COLOR_FROM_RGB8(247, 118, 32), x, y);
    // draw_line2buffer(GREEN, x, y, x + (int16_t)(dirX * 20), y + (int16_t)(dirY * 20), buffer);
    // draw_pixel2buffer(color(YELLOW,bpp==8), x, y, buffer);
}

void handleCalculation(uint16_t buffer) {

    gamestate = GAMESTATE_CALCULATING;
    // draw_ui(buffer);
    draw_map(buffer);
    draw_player(buffer);
    raycast(buffer);
    // raycastInt(buffer);
    drawWalls(buffer);
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

    gamestate = GAMESTATE_INIT;

    // init_bitmap_graphics(0xFF00, buffers[active_buffer], 0, 2, 240, 124, 16);
    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 16);
    bpp = bits_per_pixel();
    erase_canvas();

    printf("width: %i, height: %i\n", canvas_width(), canvas_height());

    print_map();

    // WaitForAnyKey();

    // printf("float\n");
    // for (uint8_t x = 0; x<20; x++) {
    //   raycast(0);
    // }
    // printf("done\n");

    // WaitForAnyKey();
    // printf("int\n");
    // for (uint8_t x = 0; x<20; x++) {
    //   raycastInt(0);
    // }
    // printf("done\n");



    // return 0;

    // assign address for each buffer
    // buffers[0] = 0x0000;
    // buffers[1] = 0x7440;
    // erase_buffer(buffers[active_buffer]);
    // erase_buffer(buffers[!active_buffer]);

    // active_buffer = 0;
    // switch_buffer(buffers[active_buffer]);

    // Precompute sine and cosine values
    set_text_color(color(WHITE,bpp==8));

    draw_ui(buffers[!active_buffer]);
    handleCalculation(buffers[!active_buffer]);

    // switch_buffer(buffers[!active_buffer]);
    // change active buffer
    // active_buffer = !active_buffer;

    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 255) { 
          
          if (currentStep >= 5) {
            currentStep -= 5;
            if (currentStep == 0) currentStep = 2;

            printf("currentStep: %i\n", currentStep);
            // draw on inactive buffer
            // erase_buffer(buffers[!active_buffer]);
            // erase_canvas();
            handleCalculation(buffers[!active_buffer]);

            // switch_buffer(buffers[!active_buffer]);
            // change active buffer
            // active_buffer = !active_buffer;
          }
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
                if ((((i<<3)+j)>3) && (new_key != (keystates[i] & (1<<j)))) {
                    // printf( "key %d %s\n", ((i<<3)+j), (new_key ? "pressed" : "released"));
                }
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
                  float oldDirX = dirX;
                  dirX = dirX * cos_r - dirY * sin_r;
                  dirY = oldDirX * sin_r + dirY * cos_r;
                  float oldPlaneX = planeX;
                  planeX = planeX * cos_r - planeY * sin_r;
                  planeY = oldPlaneX * sin_r + planeY * cos_r;
                }
                if (key(KEY_LEFT)){
                  gamestate = GAMESTATE_MOVING;
                    //both camera direction and camera plane must be rotated
                  float oldDirX = dirX;
                  dirX = dirX * cos_r - dirY * -sin_r;
                  dirY = oldDirX * -sin_r + dirY * cos_r;
                  float oldPlaneX = planeX;
                  planeX = planeX * cos_r - planeY * -sin_r;
                  planeY = oldPlaneX * -sin_r + planeY * cos_r;
                }
                if (key(KEY_UP)) {
                  gamestate = GAMESTATE_MOVING;
                  if(worldMap[(int)(posX + dirX * moveSpeed)][(int)(posY)] == false) posX += dirX * moveSpeed;
                  if(worldMap[(int)(posX)][(int)(posY + dirY * moveSpeed)] == false) posY += dirY * moveSpeed;
                }
                if (key(KEY_DOWN)) {
                  gamestate = GAMESTATE_MOVING;
                  if(worldMap[(int)(posX - dirX * moveSpeed)][(int)(posY)] == false) posX -= dirX * moveSpeed;
                  if(worldMap[(int)(posX)][(int)(posY - dirY * moveSpeed)] == false) posY -= dirY * moveSpeed;
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

                printf("posX: %i, posY: %i, dirX: %i, dirY: %i\n", (int)(posX*1000), (int)(posY*1000), (int)(dirX*1000), (int)(dirY * 1000));
                printf("planeX: %i, planeY: %i\n", (int)(planeX*1000), (int)(planeY * 1000));

                if (!paused) {

                    // // draw on inactive buffer
                    // erase_buffer(buffers[!active_buffer]);
                    // // draw_rect2buffer(BLACK, w / 2, h / 2, w, h, buffers[!active_buffer]);
                    // if(show_buffers_indicators){
                    //     draw_circle2buffer(WHITE, (active_buffer ? SCREEN_WIDTH - 20 : 20), 20, 5, buffers[!active_buffer]);
                    //     set_cursor((active_buffer ? SCREEN_WIDTH - 22 : 18), 17);
                    //     draw_string2buffer((active_buffer ? "0" : "1"), buffers[!active_buffer]);
                    // }
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = 10;
                    }

                    // erase_canvas();
                    handleCalculation(buffers[!active_buffer]);

                    // switch_buffer(buffers[!active_buffer]);
                    // change active buffer
                    // active_buffer = !active_buffer;

                }
            }
        // } else { // no keys down
        //     handled_key = false;
        // }
        // erase_canvas();

    }

    return 0;
    
}


