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

#define COLOR_FROM_RGB8(r,g,b) (((b>>3)<<11)|((g>>3)<<6)|(r>>3))

#define mapWidth 20
#define mapHeight 18
#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 124 
#define MOVEMENT_STEP 10
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

const uint16_t w = 120;
const uint16_t h = 64; 

bool wireMode = false;

uint16_t buffer[h][w];
uint16_t floorColors[h];


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


void drawBuffer() {
  // Loop through each row
  for (uint8_t j = 0; j < h; j++) {
      // Calculate the starting address of the row
      uint16_t row_addr = ((SCREEN_WIDTH << 1) * (yOffset + j)) + (xOffset << 1);
      
      // Set the initial address and step for the row
      RIA.addr0 = row_addr;
      RIA.step0 = 1; // Move 2 bytes per pixel in 16bpp mode

      // Fill the row with the color
      for (uint8_t i = 0; i < w; i+=8) {
          RIA.rw0 = buffer[j][i] ;//& 0xFF;
          RIA.rw0 = buffer[j][i] >> 8;
          RIA.rw0 = buffer[j][i+1] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+1] >> 8;
          RIA.rw0 = buffer[j][i+2] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+2] >> 8;
          RIA.rw0 = buffer[j][i+3] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+3] >> 8;
          RIA.rw0 = buffer[j][i+4] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+4] >> 8;
          RIA.rw0 = buffer[j][i+5] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+5] >> 8;
          RIA.rw0 = buffer[j][i+6] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+6] >> 8;
          RIA.rw0 = buffer[j][i+7] ;//& 0xFF;
          RIA.rw0 = buffer[j][i+7] >> 8;
      }
  }
}


int raycastFP()
{
  // Precompute common values
  qFP16_t invW = qFP16_Div(one, qFP16_IntToFP(w));
  // qFP16_t invH = qFP16_Div(one, qFP16_IntToFP(h));
  
  for(uint8_t x = 0; x < w; x += currentStep)
  {
    
    qFP16_t cameraX = qFP16_Sub(qFP16_Mul(qFP16_IntToFP(x << 1), invW), one);
    // printf("cameraX: %s\n", qFP16_FPToA(cameraX, ans, 4));
    qFP16_t rayDirX = qFP16_Add(qFP16_Mul(planeX, cameraX), dirX);
    qFP16_t rayDirY = qFP16_Add(qFP16_Mul(planeY, cameraX), dirY);
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
    uint8_t r, g, b;
    uint16_t wallColor, color = WHITE;
    switch(worldMap[mapX][mapY])
    {
      case 1: 
        // r = mapValue(lineHeight, 2, h, 150, 255);
        r = 150 + lineHeight;
        g = 40;
        b = 40;
        break; //red
      case 2:
        r = 41; 
        // g = mapValue(lineHeight, 2, h, 150, 255);
        g = 150 + lineHeight;
        b = 41;
        break; //green
      case 3:
        r = 40;
        // g = mapValue(lineHeight, 2, h, 120, 227);
        // b = mapValue(lineHeight, 2, h, 150, 255);
        g = 120 + lineHeight;
        b = 150 + lineHeight;
        break; //blue
      case 4: 
        // r = g = b = mapValue(lineHeight, 2, h, 150, 255);
        r = g = b = 180 + lineHeight;
        break; //white
      case 5: 
        // r = mapValue(lineHeight, 2, h, 150, 255);
        // g = mapValue(lineHeight, 2, h, 150, 255);
        r = 180 + lineHeight;
        g = 180 + lineHeight;
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

    wallColor = COLOR_FROM_RGB8(r, g, b);

    int8_t drawStart = -lineHeight / 2 + h / 2;
    if(drawStart < 0) drawStart = 0;
    uint8_t drawEnd = drawStart + lineHeight;

    uint16_t skyColor = COLOR_FROM_RGB8(52, 168, 235);

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
      color = wallColor;
      for (uint8_t y = drawStart; y < drawEnd; y++) {
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
  draw_rect(COLOR_FROM_RGB8(50, 50, 50), 0, 0, SCREEN_WIDTH-1, SCREEN_HEIGHT-1); // draw frame
  draw_rect(COLOR_FROM_RGB8(50, 50, 50), xOffset, yOffset, w, h); 
}

// Function to draw the world map using the draw_rect function
void draw_map() {

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
    uint16_t x = (uint16_t)(qFP16_FPToInt(posX) * TILE_SIZE);
    uint16_t y = (uint16_t)(qFP16_FPToInt(posY) * TILE_SIZE);

    qFP16_t l = qFP16_Constant(4);
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
    prevPlayerX = (uint16_t)(qFP16_FPToInt(posX) * TILE_SIZE);
    prevPlayerY = (uint16_t)(qFP16_FPToInt(posY) * TILE_SIZE);
    prevDirX = dirX;
    prevDirY = dirY;

    // prepare floor colors array
    uint8_t r = 64, g = 40, b = 20;
    for(uint8_t i = 0; i < h; i++){
      floorColors[i] = COLOR_FROM_RGB8((r + i), (g + i), (b + i));
    }


    posX = qFP16_FloatToFP(9.0f);
    posY = qFP16_FloatToFP(11.0f);  //x and y start position
    dirX = qFP16_FloatToFP(0.0f);
    dirY = qFP16_FloatToFP(-1.0f); //initial direction vector

    planeX = qFP16_FloatToFP(0.66f);
    planeY = qFP16_FloatToFP(0.0f); //the 2d raycaster version of camera plane
    moveSpeed = qFP16_FloatToFP(0.5f); //the constant value is in squares/second
    sin_r = qFP16_FloatToFP(0.24740395925); // precomputed value of sin(0.25 rad)
    cos_r = qFP16_FloatToFP(0.96891242171f);

    gamestate = GAMESTATE_INIT;
    

    // init_bitmap_graphics(0xFF00, buffers[active_buffer], 0, 2, 240, 124, 16);
    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 16);
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
                  qFP16_t oldDirX = dirX;
                  dirX = qFP16_Sub(qFP16_Mul(dirX, cos_r), qFP16_Mul(dirY, sin_r));
                  dirY = qFP16_Add(qFP16_Mul(oldDirX, sin_r), qFP16_Mul(dirY, cos_r));
                  qFP16_t oldPlaneX = planeX;
                  planeX = qFP16_Sub(qFP16_Mul(planeX, cos_r), qFP16_Mul(planeY, sin_r));
                  planeY = qFP16_Add(qFP16_Mul(oldPlaneX, sin_r), qFP16_Mul(planeY, cos_r));;
                }
                if (key(KEY_LEFT)){
                  gamestate = GAMESTATE_MOVING;
                    //both camera direction and camera plane must be rotated
                  qFP16_t oldDirX = dirX;
                  dirX = qFP16_Sub(qFP16_Mul(dirX, cos_r), qFP16_Mul(dirY, -sin_r));
                  dirY = qFP16_Add(qFP16_Mul(oldDirX, -sin_r), qFP16_Mul(dirY, cos_r));
                  qFP16_t oldPlaneX = planeX;
                  planeX = qFP16_Sub(qFP16_Mul(planeX, cos_r), qFP16_Mul(planeY, -sin_r));
                  planeY = qFP16_Add(qFP16_Mul(oldPlaneX, -sin_r), qFP16_Mul(planeY, cos_r));;
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

                // printf("posX: %i, posY: %i, dirX: %i, dirY: %i\n", (int)(posX*1000), (int)(posY*1000), (int)(dirX*1000), (int)(dirY * 1000));
                // printf("planeX: %i, planeY: %i\n", (int)(planeX*1000), (int)(planeY * 1000));

                if (!paused) {
                    
                    if (gamestate == GAMESTATE_MOVING) {
                      currentStep = MOVEMENT_STEP - currentScale;
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


