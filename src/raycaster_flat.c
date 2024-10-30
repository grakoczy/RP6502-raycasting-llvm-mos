#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>
#include <inttypes.h>
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics_db.h"



#define mapWidth 20
#define mapHeight 18
#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 124 
#define MIN_STEP 5
#define TILE_SIZE 5

float posX = 11.8, posY = 11.3;  //x and y start position
float dirX =  -1, dirY = -0; //initial direction vector
float planeX = 0, planeY = 0.66; //the 2d raycaster version of camera plane
float moveSpeed = 0.3; //the constant value is in squares/second
float sin_r = 0.09983341664; // precomputed value of sin(0.1 rad)
float cos_r = 0.99500416527;

uint8_t currentStep = 10;

#define xOffset mapWidth * TILE_SIZE + 10
#define yOffset SCREEN_HEIGHT / 6 

uint16_t w = SCREEN_WIDTH / 2;
uint16_t h = SCREEN_HEIGHT / 2; 

bool wireMode = false;


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
    draw_rect2buffer(DARK_GRAY, 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, buffer); // draw frame
    draw_rect2buffer(DARK_GRAY, xOffset, yOffset, w, h, buffer); // draw frame
    fill_rect2buffer(14, xOffset, yOffset, w, h/2, buffer);  // draw sky
    fill_rect2buffer(8, xOffset, yOffset + h/2, w, h/2, buffer); // draw floor
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
      int16_t lineHeight = (int16_t)(h / perpWallDist);

      //calculate lowest and highest pixel to fill in current stripe
      int16_t drawStart = -lineHeight / 2 + h / 2;
      if(drawStart < 0) drawStart = 0;
      // int16_t drawEnd = lineHeight / 2 + h / 2;
      int16_t drawEnd = lineHeight;
      if(drawEnd >= h) drawEnd = h - 1;

      printf("lineHeight: %i, drawStart: %i, drawEnd: %i\n", lineHeight, drawStart, drawEnd);

      // //choose wall color
      // ColorRGBA color;
      uint16_t color = WHITE;
      switch(worldMap[mapX][mapY])
      {
        case 1:  
          if (side == 1){ 
            color = mapValue(lineHeight, 2, h, 196, 201);
          } else {
            color = mapValue(lineHeight, 2, h, 52, 57);
          }
          break; //red
        case 2:
          if(side == 1) {
            color = mapValue(lineHeight, 2, h, 46, 51);
          } else { 
            color = mapValue(lineHeight, 2, h, 34, 39);;  
          }
          break; //green
        case 3:
          if (side == 1) {
            color = mapValue(lineHeight, 2, h, 28, 33);;
          } else { 
            color = mapValue(lineHeight, 2, h, 22, 27);;
          }   
          break; //blue
        case 4: 
          if (side == 1) { //light
            color = mapValue(lineHeight, 2, h, 249, 255);
          } else { //dark
            color = mapValue(lineHeight, 2, h, 237, 243);;
          }
          break; //white
        case 5: 
          if (side == 1) {
            color = mapValue(lineHeight, 2, h, 226, 231);;
          } else {
            color = mapValue(lineHeight, 2, h, 190, 195);;
          } 
          break; //yellow
      }

      printf("color: %i\n", color);

      // int c = [255 / (1 + lineHeight ** 5 * 0.00002)] * 3

      // //give x and y sides different brightness
      // if(side == 1) {color = color / 2;}

      // //draw the pixels of the stripe as a vertical line
      // verLine(x, drawStart, drawEnd, color);

      // Draw walls (this part also needs your graphics library for drawing)
      uint16_t x1 = w - x + xOffset - currentStep;
      uint16_t y1 = drawStart + yOffset;
      uint16_t width = currentStep;
      uint16_t height = drawEnd;

      if (!wireMode) {
        fill_rect2buffer(color, x1, y1, width, height, buffer);
      } else {
      draw_rect2buffer(color, x1, y1, width, height, buffer);
      }
      // draw_vline2buffer(color, x1, y1, drawEnd, buffer);

    }
    printf("raycasting finished\n");
    return 0;
}

void single_raycast(uint16_t buffer, int16_t x)
{
    
      //calculate ray position and direction
      float cameraX = 2 * x / (float)w - 1; //x-coordinate in camera space
      float rayDirX = dirX + planeX * cameraX;
      float rayDirY = dirY + planeY * cameraX;

      printf("x: %i, cameraX: %f\n", x, cameraX);
      //which box of the map we're in
      int16_t mapX = (int)posX;
      int16_t mapY = (int)posY;

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
      //stepping further below works. So the values can be computed as below.
      // Division through zero is prevented, even though technically that's not
      // needed in C++ with IEEE 754 floating point16_t values.
      float deltaDistX = (rayDirX == 0) ? 1e30 : f_abs(1 / rayDirX);
      float deltaDistY = (rayDirY == 0) ? 1e30 : f_abs(1 / rayDirY);

      float perpWallDist;

      //what direction to step in x or y-direction (either +1 or -1)
      int16_t stepX;
      int16_t stepY;

      int16_t hit = 0; //was there a wall hit?
      int16_t side; //was a NS or a EW wall hit?
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
      int16_t lineHeight = (int)(h / perpWallDist);

      //calculate lowest and highest pixel to fill in current stripe
      int16_t drawStart = -lineHeight / 2 + h / 2;
      if(drawStart < 0) drawStart = 0;
      int16_t drawEnd = lineHeight / 2 + h / 2;
      if(drawEnd >= h) drawEnd = h - 1;

      // //choose wall color
      // ColorRGBA color;
      uint16_t color = WHITE;
      switch(worldMap[mapX][mapY])
      {
        case 1:  
          if (side == 1){ 
            color = DARK_RED; 
          } else {
            color = RED;
          }
          break; //red
        case 2:
          if(side == 1) {
            color = DARK_GREEN;
          } else { 
            color = GREEN;  
          }
          break; //green
        case 3:  color = BLUE;   break; //blue
        case 4:  color = WHITE;  break; //white
        default: color = YELLOW; break; //yellow
      }

      // //give x and y sides different brightness
      // if(side == 1) {color = color / 2;}

      // //draw the pixels of the stripe as a vertical line
      // verLine(x, drawStart, drawEnd, color);
      draw_vline2buffer(color, x, drawStart, drawEnd, buffer);
    
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

// Function to draw the world map using the draw_rect function
void draw_map(uint16_t buffer) {
    for (int i = 0; i < mapHeight; i++) {
        for (int j = 0; j < mapWidth; j++) {
          if (worldMap[i][j] > 0) {
            uint16_t color = WHITE;
            switch(worldMap[i][j])
            {
              case 1: color = RED; break; //red
              case 2: color = 46; break; //green
              case 3: color = 30; break; //blue
              case 4: color = WHITE; break; //white
              case 5: color = YELLOW; break; //yellow
            }
            
            // Draw a wall tile (represented by a white rectangle)
            draw_rect2buffer(color, i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, buffer);
          }
        }
    }
} 

void draw_player(uint16_t buffer){
    uint16_t x = (uint16_t)(posX * TILE_SIZE);
    uint16_t y = (uint16_t)(posY * TILE_SIZE);
    draw_line2buffer(GREEN, x, y, x + (int16_t)(dirX * 20), y + (int16_t)(dirY * 20), buffer);
    draw_pixel2buffer(color(YELLOW,bpp==8), x, y, buffer);
}

void handleCalculation(uint16_t buffer) {

    gamestate = GAMESTATE_CALCULATING;
    draw_map(buffer);
    draw_player(buffer);
    raycast(buffer);
    gamestate = GAMESTATE_IDLE;

}


int16_t main() {
    bool handled_key = false;
    bool paused = false;
    bool show_buffers_indicators = false;
    uint8_t mode = 0;
    uint8_t i = 0;
    uint8_t timer = 0;

    gamestate = GAMESTATE_INIT;

    // init_bitmap_graphics(0xFF00, buffers[active_buffer], 0, 2, 240, 124, 16);
    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    bpp = bits_per_pixel();
    // erase_canvas();

    printf("width: %i, height: %i\n", canvas_width(), canvas_height());

    print_map();

    // return 0;

    // assign address for each buffer
    buffers[0] = 0x0000;
    buffers[1] = 0x7440;
    erase_buffer(buffers[active_buffer]);
    erase_buffer(buffers[!active_buffer]);

    active_buffer = 0;
    switch_buffer(buffers[active_buffer]);

    // Precompute sine and cosine values
    set_text_color(color(WHITE,bpp==8));


    gamestate = GAMESTATE_CALCULATING;

    draw_map(buffers[!active_buffer]);
    draw_player(buffers[!active_buffer]);
    raycast(buffers[!active_buffer]);
    
    switch_buffer(buffers[!active_buffer]);
    // change active buffer
    active_buffer = !active_buffer;

    gamestate = GAMESTATE_IDLE;
    

    while (true) {

        if (gamestate == GAMESTATE_IDLE) timer++;

        if (timer == 255) { 
          
          if (currentStep >= 5) {
            currentStep -= 5;
            if (currentStep == 0) currentStep = 2;
            // draw on inactive buffer
            erase_buffer(buffers[!active_buffer]);

            handleCalculation(buffers[!active_buffer]);

            switch_buffer(buffers[!active_buffer]);
            // change active buffer
            active_buffer = !active_buffer;
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
                // printf("planeX: %i, planeY: %i\n", (int)(planeX*1000), (int)(planeY * 1000));

                if (!paused) {

                    // draw on inactive buffer
                    erase_buffer(buffers[!active_buffer]);
                    // draw_rect2buffer(BLACK, w / 2, h / 2, w, h, buffers[!active_buffer]);
                    if(show_buffers_indicators){
                        draw_circle2buffer(WHITE, (active_buffer ? SCREEN_WIDTH - 20 : 20), 20, 8, buffers[!active_buffer]);
                        set_cursor((active_buffer ? SCREEN_WIDTH - 22 : 18), 17);
                        draw_string2buffer((active_buffer ? "0" : "1"), buffers[!active_buffer]);
                    }
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = 10;
                    }

                    handleCalculation(buffers[!active_buffer]);

                    switch_buffer(buffers[!active_buffer]);
                    // change active buffer
                    active_buffer = !active_buffer;

                }
            }
        // } else { // no keys down
        //     handled_key = false;
        // }
        // erase_canvas();

    }

    return 0;
    
}


