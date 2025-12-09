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
#include "textures32.h"
#include "FpF.hpp"
#include "maze.h"

using namespace mn::MFixedPoint;

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
#define WINDOW_WIDTH 96
#define WINDOW_HEIGTH 54
#define SCALE 2
#define MIN_SCALE 8
#define MAX_SCALE 2

FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1); //These HAVE TO be float, or something with a lot more precision
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0); //the 2d raycaster version of camera plane
FpF16<7> moveSpeed(0.2); //the constant value is in squares/second
FpF16<7> playerScale(5);
FpF16<7> sin_r(0.2588190451); // precomputed value of sin(pi 1/12 rad)
FpF16<7> cos_r(0.96592582628); 

uint16_t startX = 151+(26-mapWidth)/2;
uint16_t startY = 137+(26-mapHeight)/2;

FpF16<7> prevDirX, prevDirY;                                                       

uint16_t prevPlayerX, prevPlayerY;

int8_t currentStep = 1;
int8_t movementStep = 4; 


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

#define ROTATION_STEPS 24

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

uint8_t currentRotStep = 1; // Tracks the current rotation step

FpF16<7> invW;
FpF16<7> halfH(h / 2);


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

void drawBufferDouble() {
    // Start with the base addresses.
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    uint8_t* buffer_ptr = buffer;

    // The amount to advance the pointers on each new row.
    const uint16_t screen_stride = SCREEN_WIDTH * 2;
    const uint8_t buffer_stride = WINDOW_WIDTH;

    for (uint8_t j = 0; j < h; ++j) {
        // --- First Scaled Row ---
        // Set address and step ONCE per row for maximum speed.
        RIA.addr0 = screen_addr;
        RIA.step0 = 1;

        uint8_t* p = buffer_ptr; // Use a temporary pointer for the inner loop.
        for (uint8_t i = 0; i < w; i += 4) {
            uint8_t color0 = p[0];
            uint8_t color1 = p[1];
            uint8_t color2 = p[2];
            uint8_t color3 = p[3];
            p += 4;

            // Write each color twice for 2x horizontal scaling.
            RIA.rw0 = color0; RIA.rw0 = color0;
            RIA.rw0 = color1; RIA.rw0 = color1;
            RIA.rw0 = color2; RIA.rw0 = color2;
            RIA.rw0 = color3; RIA.rw0 = color3;
        }

        // --- Second Scaled Row ---
        // Set the address for the next line down.
        RIA.addr0 = screen_addr + SCREEN_WIDTH;
        RIA.step0 = 1;

        p = buffer_ptr; // Reset the temporary pointer.
        for (uint8_t i = 0; i < w; i += 4) {
            uint8_t color0 = p[0];
            uint8_t color1 = p[1];
            uint8_t color2 = p[2];
            uint8_t color3 = p[3];
            p += 4;

            RIA.rw0 = color0; RIA.rw0 = color0;
            RIA.rw0 = color1; RIA.rw0 = color1;
            RIA.rw0 = color2; RIA.rw0 = color2;
            RIA.rw0 = color3; RIA.rw0 = color3;
        }

        // Advance to the next row using fast addition instead of multiplication.
        screen_addr += screen_stride;
        buffer_ptr += buffer_stride;
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
}



int raycastF()
{


    for (uint8_t x = 0; x < w; x += currentStep)
    {
      //calculate ray position and direction

 
      FpF16<7> cameraX = cameraXValues[currentRotStep][x];
      FpF16<7> rayDirX = rayDirXValues[currentRotStep][x];
      FpF16<7> rayDirY = rayDirYValues[currentRotStep][x];


      //which box of the map we're in
      int mapX = (int)posX;
      int mapY = (int)posY; 

      //length of ray from current position to next x or y-side
      FpF16<7> sideDistX;
      FpF16<7> sideDistY;

      float fsideDistX;
      float fsideDistY;

      //length of ray from one x or y-side to next x or y-side
      //these are derived as:
      //deltaDistX = sqrt(1 + (rayDirY * rayDirY) / (rayDirX * rayDirX))
      //deltaDistY = sqrt(1 + (rayDirX * rayDirX) / (rayDirY * rayDirY))
      //which can be simplified to abs(|rayDir| / rayDirX) and abs(|rayDir| / rayDirY)
      //where |rayDir| is the length of the vector (rayDirX, rayDirY). Its length,
      //unlike (dirX, dirY) is not 1, however this does not matter, only the
      //ratio between deltaDistX and deltaDistY matters, due to the way the DDA
      //stepping further below works. So the values can be computed as below.
      // Division through zero is prevented, even though technically that's not
      // needed in C++ with IEEE 754 floating point values.
  
      FpF16<7> deltaDistX = deltaDistXValues[currentRotStep][x];
      FpF16<7> deltaDistY = deltaDistYValues[currentRotStep][x];

      int idx = (int)deltaDistX;
      int fdx = (int)deltaDistX;


      FpF16<7> perpWallDist;

      //what direction to step in x or y-direction (either +1 or -1)
      int stepX;
      int stepY;

      int hit = 0; //was there a wall hit?
      int side; //was a NS or a EW wall hit?

      //calculate step and initial sideDist
      if(rayDirX < FpF16<7>(0))
      {
        stepX = -1;
        sideDistX = (posX - mapX) * deltaDistX;
      }
      else
      {
        stepX = 1;
        sideDistX = (FpF16<7>(mapX + 1.0) - posX) * deltaDistX;
      }
      if(rayDirY < FpF16<7>(0))
      {
        stepY = -1;
        sideDistY = (posY - mapY) * deltaDistY;
      }
      else
      {
        stepY = 1;
        sideDistY = (FpF16<7>(mapY + 1.0) - posY) * deltaDistY;
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

     
      //Calculate distance projected on camera direction. This is the shortest distance from the point where the wall is
      //hit to the camera plane. Euclidean to center camera point would give fisheye effect!
      //This can be computed as (mapX - posX + (1 - stepX) / 2) / rayDirX for side == 0, or same formula with Y
      //for size == 1, but can be simplified to the code below thanks to how sideDist and deltaDist are computed:
      //because they were left scaled to |rayDir|. sideDist is the entire length of the ray above after the multiple
      //steps, but we subtract deltaDist once because one step more into the wall was taken above.
      if(side == 0) perpWallDist = (sideDistX - deltaDistX);
      else          perpWallDist = (sideDistY - deltaDistY);

      // Get the raw 16-bit fixed-point value of the distance
      int16_t rawDist = perpWallDist.GetRawVal();
      uint8_t lineHeight;

      // Use the fast lookup table for nearby walls (distance < 4.0)
      if (rawDist >= 0 && rawDist < 1024) {
          lineHeight = lineHeightTable[rawDist];
      } 
      // For distant walls, fall back to the slower but accurate division.
      else {
          // Prevent division by zero or negative distances, though unlikely.
          if (perpWallDist > FpF16<7>(0)) {
              lineHeight = (int)(FpF16<7>(h) / perpWallDist);
              // The result of the division could still be > h for distances < 1
              // that were not caught by the table (e.g., if rawDist was negative).
              if (lineHeight > h) lineHeight = h;
          } else {
              // If distance is zero or negative, wall is infinitely high.
              lineHeight = h;
          }
      }
      
      //texturing calculations
      uint8_t texNum = (worldMap[mapX][mapY] - 1) * 2 + side; //1 subtracted from it so that texture 0 can be used!
      int8_t drawStart = (-lineHeight >> 1) + (h >> 1);
      if (drawStart < 0) drawStart = 0;
      uint8_t drawEnd = drawStart + lineHeight;
      // It's crucial to clamp drawEnd to the window height to prevent buffer overflows.
      if (drawEnd > h) drawEnd = h;

      // Draw ceiling
      uint8_t i, color;
      color = 14;
      for (uint8_t y = 0; y < drawStart; ++y) {
          uint8_t* bufPtr = &buffer[y * w + x];
          bufPtr[0] = color;
          if (currentStep > 1) {
            bufPtr[1] = color;
            bufPtr[2] = color;
            bufPtr[3] = color;
          }
      }

      FpF16<7> wallX; //where exactly the wall was hit
      if(side == 0) wallX = posY + perpWallDist * rayDirY;
      else          wallX = posX + perpWallDist * rayDirX;
      wallX -= floorFixed((wallX));

      //x coordinate on the texture
      int texX = int(wallX * FpF16<7>(texWidth));
      if(side == 0 && rayDirX > FpF16<7>(0)) texX = texWidth - texX - 1;
      if(side == 1 && rayDirY < FpF16<7>(0)) texX = texWidth - texX - 1;


      // 'raw_step' is the raw 9.7 fixed-point integer representing the texture step per screen pixel.
      int16_t raw_step = texStepValues[lineHeight].GetRawVal();

      // 'raw_texPos' is the starting texture Y-coordinate, also in 9.7 fixed-point format.
      int16_t raw_texPos;

      // This logic applies only when the wall is taller than the screen, forcing it to be clipped.
      if (lineHeight > h)
      {
          // Corrected formula for h=54: raw_texPos = 16*128 - (864*128 / lineHeight)
          raw_texPos = 2048 - (int16_t)(110592L / lineHeight); // for 32 pixels
        //   raw_texPos = 2048 - (int16_t)(55296L / lineHeight); // for 16 pixels texture

          // --- ROBUSTNESS FIX ---
          // Due to integer division truncation, raw_texPos can become slightly
          // negative. This prevents the sign-extension bug during the right-shift,
          // which would cause texY to wrap around to 31.
          if (raw_texPos < 0) {
              raw_texPos = 0;
          }
      }
      else
      {
          // If the wall fits on screen, the texture is not clipped vertically,
          // so we can start drawing from its top (texY = 0).
          raw_texPos = 0;
      }

      // Loop to draw the textured vertical wall slice.
      for (uint8_t y = drawStart; y < drawEnd; ++y) {
        // Get the integer part of the texture position by shifting right by 7.
        // Mask with (texHeight - 1) to wrap the coordinate.
        uint8_t texY = (raw_texPos >> 7) & (texHeight - 1);

        // Advance the fixed-point texture position for the next screen pixel.
        raw_texPos += raw_step;

        // Calculate the 1D index into the texture array (texWidth is 32, so << 5).
        uint16_t texIdx = (texY << 5) + texX;
        // uint16_t texIdx = (texY << 4) + texX; //for 16 pixels texture

        color = texture[texNum][texIdx];

        uint8_t* bufPtr = &buffer[y * w + x];
        bufPtr[0] = color;
        if (currentStep > 1) {
          bufPtr[1] = color;
          color = texture[texNum][(texIdx + 1) & texSize];
          bufPtr[2] = color;
          bufPtr[3] = color;
        }
      }

      // Draw floor
      for (uint8_t y = drawEnd; y < h; ++y) {
        uint8_t* bufPtr = &buffer[y * w + x];
        color = floorColors[y];
        bufPtr[0] = color;
        if (currentStep > 1) {
          bufPtr[1] = color;
          bufPtr[2] = color;
          bufPtr[3] = color;
        }
      }
  }
  bigMode? drawBufferDouble() : drawBufferRegular();
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

    // uint16_t x = (int)(posX * ts);
    // uint16_t y = (int)(posY * ts);

    // uint16_t x = 159;
    // uint16_t y = 150;

    // int8_t lX =  (int)(dirX * l); // calculate line direction
    // int8_t lY =  (int)(dirY * l); 
    // int8_t plX = (int)(prevDirX * l); 
    // int8_t plY = (int)(prevDirY * l); 

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
    // draw_map(buffer);
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

    draw_ui();


    draw_map();
    draw_player();

    handleCalculation();



    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 16 && currentStep > 2) { 
            currentStep = 1;
            draw_map();
            draw_player();
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
                printf("posX: %i, posY: %i\n", FP16ToIntPercent(posX), FP16ToIntPercent(posY));

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


