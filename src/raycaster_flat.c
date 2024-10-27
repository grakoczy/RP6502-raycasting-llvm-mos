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



#define mapWidth 24
#define mapHeight 24
#define SCREEN_WIDTH 240 
#define SCREEN_HEIGHT 124 
#define STEP 20

float posX = 22, posY = 12;  //x and y start position
float dirX = -1, dirY = 0; //initial direction vector
float planeX = 0, planeY = 0.66; //the 2d raycaster version of camera plane
float moveSpeed = 0.3; //the constant value is in squares/second
float sin_r = 0.09983341664; // precomputed value of sin(0.1 rad)
float cos_r = 0.99500416527;


int w = SCREEN_WIDTH;
int h = SCREEN_HEIGHT;

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


int worldMap[mapWidth][mapHeight]=
{
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,2,2,2,2,0,0,0,0,3,0,3,0,3,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,3,0,0,0,3,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,2,0,2,2,0,0,0,0,3,0,3,0,3,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,0,0,0,5,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
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


void raycast(uint16_t buffer)
{
    for(int x = 0; x < w; x += STEP)
    {
      //calculate ray position and direction
      float cameraX = 2 * x / (float)w - 1; //x-coordinate in camera space
      float rayDirX = dirX + planeX * cameraX;
      float rayDirY = dirY + planeY * cameraX;

      // printf("x: %i, cameraX: %f\n", x, cameraX);
      //which box of the map we're in
      int mapX = (int)posX;
      int mapY = (int)posY;

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
      // needed in C++ with IEEE 754 floating point values.
      float deltaDistX = (rayDirX == 0) ? 1e30 : f_abs(1 / rayDirX);
      float deltaDistY = (rayDirY == 0) ? 1e30 : f_abs(1 / rayDirY);

      float perpWallDist;

      //what direction to step in x or y-direction (either +1 or -1)
      int stepX;
      int stepY;

      int hit = 0; //was there a wall hit?
      int side; //was a NS or a EW wall hit?
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
      //Calculate distance projected on camera direction. This is the shortest distance from the point where the wall is
      //hit to the camera plane. Euclidean to center camera point would give fisheye effect!
      //This can be computed as (mapX - posX + (1 - stepX) / 2) / rayDirX for side == 0, or same formula with Y
      //for size == 1, but can be simplified to the code below thanks to how sideDist and deltaDist are computed:
      //because they were left scaled to |rayDir|. sideDist is the entire length of the ray above after the multiple
      //steps, but we subtract deltaDist once because one step more into the wall was taken above.
      if(side == 0) perpWallDist = (sideDistX - deltaDistX);
      else          perpWallDist = (sideDistY - deltaDistY);

      //Calculate height of line to draw on screen
      int lineHeight = (int)(h / perpWallDist);

      //calculate lowest and highest pixel to fill in current stripe
      int drawStart = -lineHeight / 2 + h / 2;
      if(drawStart < 0) drawStart = 0;
      int drawEnd = lineHeight / 2 + h / 2;
      if(drawEnd >= h) drawEnd = h - 1;

      // //choose wall color
      // ColorRGBA color;
      uint16_t color = WHITE;
      switch(worldMap[mapX][mapY])
      {
        case 1:  color = RED;    break; //red
        case 2:  color = GREEN;  break; //green
        case 3:  color = BLUE;   break; //blue
        case 4:  color = WHITE;  break; //white
        default: color = YELLOW; break; //yellow
      }

      // //give x and y sides different brightness
      // if(side == 1) {color = color / 2;}

      // //draw the pixels of the stripe as a vertical line
      // verLine(x, drawStart, drawEnd, color);

      // Draw walls (this part also needs your graphics library for drawing)
      int x1 = x;
      int y1 = drawStart;
      int x2 = x+STEP;
      int y2 = drawEnd;
      // fill_rect2buffer(color, x1, y1, x2, y2, buffer);
      draw_vline2buffer(color, x, drawStart, drawEnd, buffer);

      // HPEN hPen = CreatePen(PS_SOLID, 2, color);  // Yellow color
      // HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);  
      // MoveToEx(hdc, x, drawStart, NULL);
      // LineTo(hdc, x, drawEnd);

      // // Restore the old pen and delete the new pen
      // SelectObject(hdc, hOldPen);
      // DeleteObject(hPen);
    }
    // //timing for input and FPS counter
    // oldTime = time;
    // time = getTicks();
    // float frameTime = (time - oldTime) / 1000.0; //frameTime is the time this frame has taken, in seconds
    // print(1.0 / frameTime); //FPS counter
    // redraw();
    // cls();

    // //speed modifiers
    // moveSpeed = delta_time * 5.0; //the constant value is in squares/second
    
}

void single_raycast(uint16_t buffer, int x)
{
    
      //calculate ray position and direction
      float cameraX = 2 * x / (float)w - 1; //x-coordinate in camera space
      float rayDirX = dirX + planeX * cameraX;
      float rayDirY = dirY + planeY * cameraX;

      printf("x: %i, cameraX: %f\n", x, cameraX);
      //which box of the map we're in
      int mapX = (int)posX;
      int mapY = (int)posY;

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
      // needed in C++ with IEEE 754 floating point values.
      float deltaDistX = (rayDirX == 0) ? 1e30 : f_abs(1 / rayDirX);
      float deltaDistY = (rayDirY == 0) ? 1e30 : f_abs(1 / rayDirY);

      float perpWallDist;

      //what direction to step in x or y-direction (either +1 or -1)
      int stepX;
      int stepY;

      int hit = 0; //was there a wall hit?
      int side; //was a NS or a EW wall hit?
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
      //Calculate distance projected on camera direction. This is the shortest distance from the point where the wall is
      //hit to the camera plane. Euclidean to center camera point would give fisheye effect!
      //This can be computed as (mapX - posX + (1 - stepX) / 2) / rayDirX for side == 0, or same formula with Y
      //for size == 1, but can be simplified to the code below thanks to how sideDist and deltaDist are computed:
      //because they were left scaled to |rayDir|. sideDist is the entire length of the ray above after the multiple
      //steps, but we subtract deltaDist once because one step more into the wall was taken above.
      if(side == 0) perpWallDist = (sideDistX - deltaDistX);
      else          perpWallDist = (sideDistY - deltaDistY);

      //Calculate height of line to draw on screen
      int lineHeight = (int)(h / perpWallDist);

      //calculate lowest and highest pixel to fill in current stripe
      int drawStart = -lineHeight / 2 + h / 2;
      if(drawStart < 0) drawStart = 0;
      int drawEnd = lineHeight / 2 + h / 2;
      if(drawEnd >= h) drawEnd = h - 1;

      // //choose wall color
      // ColorRGBA color;
      uint16_t color = WHITE;
      switch(worldMap[mapX][mapY])
      {
        case 1:  color = RED;    break; //red
        case 2:  color = GREEN;  break; //green
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

int main() {
    bool handled_key = false;
    bool paused = true;
    bool show_buffers_indicators = true;
    uint8_t mode = 0;
    uint8_t i = 0;

    

    // init_bitmap_graphics(0xFF00, buffers[active_buffer], 0, 2, 240, 124, 16);
    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT, 8);
    bpp = bits_per_pixel();
    // erase_canvas();

    printf("width: %i, height: %i\n", canvas_width(), canvas_height());

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
    // set_cursor(10, 110);
    // draw_string("Precomputing sine and cosine values...");
    // draw_string2buffer("Precomputing sine and cosine values...", buffers[active_buffer]);
    // precompute_sin_cos();

    // for (int i = 0; i < 20; i++) {
    //     printf("i: %i, sin: %f, cos: %f\n", i, sine_values[i], cosine_values[i]);
    // }
    // erase_canvas();

    set_cursor(10, 110);
    draw_string2buffer("Press SPACE to start/stop", buffers[active_buffer]);
    // draw_string("Press SPACE to start/stop");

    // draw_map(buffers[!active_buffer]);
    

    while (true) {

        // draw_map();

        // draw_player();

        if (!paused) {

            // draw on inactive buffer
            erase_buffer(buffers[!active_buffer]);
            if(show_buffers_indicators){
                draw_circle2buffer(WHITE, (active_buffer ? SCREEN_WIDTH - 20 : 20), 20, 8, buffers[!active_buffer]);
                set_cursor((active_buffer ? SCREEN_WIDTH - 22 : 18), 17);
                draw_string2buffer((active_buffer ? "0" : "1"), buffers[!active_buffer]);
            }
            // draw_map(buffers[!active_buffer]);
            // draw_player(buffers[!active_buffer]);
            // for (int8_t i = -4; i < 5; i++) {
            //     ray_cast_angle(buffers[!active_buffer], player.angle+i);
            // }
            // for (uint8_t x = (SCREEN_WIDTH / 2) - 10; x < (SCREEN_WIDTH / 2) + 10; x += 5) {
            //   single_raycast(x, buffers[!active_buffer]);
            // }
            raycast(buffers[!active_buffer]);

            // int d = 0;
            // while (d < 10000) {
            //     d++;
            //     draw_pixel2buffer(BLACK, 0, 0, buffers[active_buffer]);
            // }

            switch_buffer(buffers[!active_buffer]);

            // change active buffer
            active_buffer = !active_buffer;

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
            if (!handled_key) { // handle only once per single keypress
                // handle the keystrokes
                if (key(KEY_SPACE)) {
                    paused = !paused;
                    if(paused){
                        set_cursor(10, 110);
                        // // draw_string2buffer("Press SPACE to start", buffers[active_buffer]);
                        // draw_string("Press SPACE to start");
                    }
                }
                if (key(KEY_LEFT)){
                  //both camera direction and camera plane must be rotated
                  float oldDirX = dirX;
                  dirX = dirX * cos_r - dirY * sin_r;
                  dirY = oldDirX * sin_r + dirY * cos_r;
                  float oldPlaneX = planeX;
                  planeX = planeX * cos_r - planeY * sin_r;
                  planeY = oldPlaneX * sin_r + planeY * cos_r;
                }
                if (key(KEY_RIGHT)){
                    //both camera direction and camera plane must be rotated
                  float oldDirX = dirX;
                  dirX = dirX * cos_r - dirY * -sin_r;
                  dirY = oldDirX * -sin_r + dirY * cos_r;
                  float oldPlaneX = planeX;
                  planeX = planeX * cos_r - planeY * -sin_r;
                  planeY = oldPlaneX * -sin_r + planeY * cos_r;
                }
                if (key(KEY_UP)) {
                    if(worldMap[(int)(posX + dirX * moveSpeed)][(int)(posY)] == false) posX += dirX * moveSpeed;
                    if(worldMap[(int)(posX)][(int)(posY + dirY * moveSpeed)] == false) posY += dirY * moveSpeed;
                }
                if (key(KEY_DOWN)) {
                  if(worldMap[(int)(posX - dirX * moveSpeed)][(int)(posY)] == false) posX -= dirX * moveSpeed;
                  if(worldMap[(int)(posX)][(int)(posY - dirY * moveSpeed)] == false) posY -= dirY * moveSpeed;
                }
                if (key(KEY_3)) {
                    show_buffers_indicators = !show_buffers_indicators;
                }
                if (key(KEY_ESC)) {
                    break;
                }
                handled_key = true;
            }
        } else { // no keys down
            handled_key = false;
        }
        // erase_canvas();

    }

    return 0;
    
}

// // Windows procedure function
// LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

//     // int x, y, to_x, to_y;
//     PAINTSTRUCT ps;
//     HDC hdc = BeginPaint(hwnd, &ps);

//     switch (uMsg) {
//         case WM_PAINT: {
            

//             // Fill background with black
//             HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));  // Black color
//             FillRect(hdc, &ps.rcPaint, hBrush);
//             DeleteObject(hBrush);  // Clean up brush
            
//             // Set up white color for drawing the ship and other objects
//             // HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));  // White color for objects
//             // SelectObject(hdc, hPen);

//             // draw_map(hdc);
//             // draw_player(hdc);
//             // ray_cast_int(hdc);
//             // Update the delta time (elapsed time between frames)
//             // UpdateDeltaTime();

//             // for(int x = 0; x < w; x++)
//             // { 
//             //   single_raycast(hdc, x);
//             // }
//             single_raycast(hdc, SCREEN_WIDTH / 2);

//             // raycast(hdc);
            
//             // Delete the pen after use
//             // DeleteObject(hPen);
            
//             EndPaint(hwnd, &ps);
//         } break;

//         case WM_KEYDOWN:
//             switch (wParam) {
//                 case VK_LEFT:{
//                   //both camera direction and camera plane must be rotated
//                   float oldDirX = dirX;
//                   dirX = dirX * cos_r - dirY * sin_r;
//                   dirY = oldDirX * sin_r + dirY * cos_r;
//                   float oldPlaneX = planeX;
//                   planeX = planeX * cos_r - planeY * sin_r;
//                   planeY = oldPlaneX * sin_r + planeY * cos_r;
//                 } break;
//                 case VK_RIGHT:{
//                     //both camera direction and camera plane must be rotated
//                   float oldDirX = dirX;
//                   dirX = dirX * cos_r - dirY * -sin_r;
//                   dirY = oldDirX * -sin_r + dirY * cos_r;
//                   float oldPlaneX = planeX;
//                   planeX = planeX * cos_r - planeY * -sin_r;
//                   planeY = oldPlaneX * -sin_r + planeY * cos_r;
//                 } break;
//                 case VK_UP: {
//                     if(worldMap[int(posX + dirX * moveSpeed)][int(posY)] == false) posX += dirX * moveSpeed;
//                     if(worldMap[int(posX)][int(posY + dirY * moveSpeed)] == false) posY += dirY * moveSpeed;
//                 } break;
//                 case VK_DOWN: {
//                   if(worldMap[int(posX - dirX * moveSpeed)][int(posY)] == false) posX -= dirX * moveSpeed;
//                   if(worldMap[int(posX)][int(posY - dirY * moveSpeed)] == false) posY -= dirY * moveSpeed;
//                 }
//                 // case VK_SPACE:
//                 //     fire_bullet();  // Fire bullet on space press
//                 //     break;
//                 case VK_ESCAPE:
//                     PostQuitMessage(0); 
//                     break;
//             }
//             // player_movement(uMsg, wParam);
//             // ray_cast(hdc);
//             // ray_cast_int(hdc);
//             // raycast(hdc);
          
//             break;

//         case WM_KEYUP:
//             switch (wParam) {
//                 // case VK_UP: {
//                 //     show_fire = false;
//                 // } break;
//             }
//             break;

//         case WM_TIMER:
//             InvalidateRect(hwnd, NULL, FALSE);  // Request to redraw the window
//             break;

//         case WM_DESTROY:
//             PostQuitMessage(0);
//             break;

//         default:
//             return DefWindowProc(hwnd, uMsg, wParam, lParam);
//     }
//     return 0;
// }

// // Windows entry point
// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
//     // Register window class
//     WNDCLASS wc = {0};
//     wc.lpfnWndProc = WndProc;
//     wc.hInstance = hInstance;
//     wc.lpszClassName = "Raycaster";
//     RegisterClass(&wc);

//     // Create window
//     HWND hwnd = CreateWindow(wc.lpszClassName, "Raycaster", WS_OVERLAPPEDWINDOW,
//                              CW_USEDEFAULT, CW_USEDEFAULT, SCREEN_WIDTH, SCREEN_HEIGHT+20,
//                              NULL, NULL, hInstance, NULL);

//     ShowWindow(hwnd, nCmdShow);
//     UpdateWindow(hwnd);

//     // Initialize game
//     // init_game();
//     SetTimer(hwnd, 1, 50, NULL); // Set timer for game loop
//     // Initialize the timer for frame-independent movement
//     InitializeTimer();

//     // Main message loop
//     MSG msg;
//     while (GetMessage(&msg, NULL, 0, 0)) {
//         TranslateMessage(&msg);
//         DispatchMessage(&msg);
//     }

//     return (int) msg.wParam;
// }

