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

// XRAM locations
#define KEYBOARD_INPUT 0xFF10 // KEYBOARD_BYTES of bitmask data

// 256 bytes HID code max, stored in 32 uint8
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};

// keystates[code>>3] gets contents from correct byte in array
// 1 << (code&7) moves a 1 into proper position to mask with byte contents
// final & gives 1 if key is pressed, 0 if not
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))


#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 124
#define HALF_WIDTH SCREEN_WIDTH / 2
#define HALF_HEIGHT SCREEN_HEIGHT / 2

#define PLAYER_POS_X 1.5
#define PLAYER_POS_Y 5 
#define PLAYER_ANGLE 0
#define PLAYER_SPEED 0.1
#define PLAYER_ROT_SPEED 1
#define PLAYER_SIZE_SCALE 6

#define FOV  1,0471975511965977
#define HALF_FOV  FOV / 2
#define NUM_RAYS WIDTH / 2
#define HALF_NUM_RAYS NUM_RAYS / 2
#define DELTA__ANGLE FOV / NUM_RAYS
#define MAX_DEPTH 20


//SCREEN_DIST = HALF_WIDTH / math.tan(HALF_FOV)
//SCALE  SCREEN_WIDTH / NUM_RAYS

// for double buffering
uint16_t buffers[2];
uint8_t active_buffer = 0;

uint8_t bpp;

#define MAP_WIDTH 16
#define MAP_HEIGHT 9
#define TILE_SIZE 13

// 1 = wall, 0 = empty space
const uint8_t mini_map[MAP_HEIGHT][MAP_WIDTH] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
};

int8_t world_map[MAP_HEIGHT][MAP_WIDTH] = {0};

typedef struct {
    float x, y;  
    int16_t angle; 
} Player;

Player player;


// Precompute sine and cosine values for rotation
#define NUM_POINTS 256
float sine_values[NUM_POINTS] = {
    0.000000f, 0.024541f, 0.049068f, 0.073565f, 0.098017f, 0.122411f, 0.146730f, 0.170962f, 
    0.195090f, 0.219101f, 0.242980f, 0.266713f, 0.290285f, 0.313682f, 0.336890f, 0.359895f, 
    0.382683f, 0.405241f, 0.427555f, 0.449611f, 0.471397f, 0.492898f, 0.514103f, 0.534998f, 
    0.555570f, 0.575808f, 0.595699f, 0.615232f, 0.634393f, 0.653173f, 0.671559f, 0.689541f, 
    0.707107f, 0.724247f, 0.740951f, 0.757209f, 0.773010f, 0.788346f, 0.803208f, 0.817585f, 
    0.831470f, 0.844854f, 0.857728f, 0.870087f, 0.881921f, 0.893224f, 0.903989f, 0.914210f, 
    0.923880f, 0.932993f, 0.941544f, 0.949528f, 0.956940f, 0.963776f, 0.970031f, 0.975702f, 
    0.980785f, 0.985278f, 0.989177f, 0.992480f, 0.995185f, 0.997290f, 0.998795f, 0.999699f, 
    1.000000f, 0.999699f, 0.998795f, 0.997290f, 0.995185f, 0.992480f, 0.989177f, 0.985278f, 
    0.980785f, 0.975702f, 0.970031f, 0.963776f, 0.956940f, 0.949528f, 0.941544f, 0.932993f, 
    0.923880f, 0.914210f, 0.903989f, 0.893224f, 0.881921f, 0.870087f, 0.857728f, 0.844854f, 
    0.831470f, 0.817585f, 0.803208f, 0.788346f, 0.773010f, 0.757209f, 0.740951f, 0.724247f, 
    0.707107f, 0.689541f, 0.671559f, 0.653173f, 0.634393f, 0.615232f, 0.595699f, 0.575808f, 
    0.555570f, 0.534998f, 0.514103f, 0.492898f, 0.471397f, 0.449611f, 0.427555f, 0.405241f, 
    0.382683f, 0.359895f, 0.336890f, 0.313682f, 0.290285f, 0.266713f, 0.242980f, 0.219101f, 
    0.195090f, 0.170962f, 0.146730f, 0.122411f, 0.098017f, 0.073565f, 0.049068f, 0.024541f, 
    0.000000f, -0.024541f, -0.049068f, -0.073565f, -0.098017f, -0.122411f, -0.146730f, -0.170962f, 
    -0.195090f, -0.219101f, -0.242980f, -0.266713f, -0.290285f, -0.313682f, -0.336890f, -0.359895f, 
    -0.382683f, -0.405241f, -0.427555f, -0.449611f, -0.471397f, -0.492898f, -0.514103f, -0.534998f, 
    -0.555570f, -0.575808f, -0.595699f, -0.615232f, -0.634393f, -0.653173f, -0.671559f, -0.689541f, 
    -0.707107f, -0.724247f, -0.740951f, -0.757209f, -0.773010f, -0.788346f, -0.803208f, -0.817585f, 
    -0.831470f, -0.844854f, -0.857728f, -0.870087f, -0.881921f, -0.893224f, -0.903989f, -0.914210f, 
    -0.923880f, -0.932993f, -0.941544f, -0.949528f, -0.956940f, -0.963776f, -0.970031f, -0.975702f, 
    -0.980785f, -0.985278f, -0.989177f, -0.992480f, -0.995185f, -0.997290f, -0.998795f, -0.999699f, 
    -1.000000f, -0.999699f, -0.998795f, -0.997290f, -0.995185f, -0.992480f, -0.989177f, -0.985278f, 
    -0.980785f, -0.975702f, -0.970031f, -0.963776f, -0.956940f, -0.949528f, -0.941544f, -0.932993f, 
    -0.923880f, -0.914210f, -0.903989f, -0.893224f, -0.881921f, -0.870087f, -0.857728f, -0.844854f, 
    -0.831470f, -0.817585f, -0.803208f, -0.788346f, -0.773010f, -0.757209f, -0.740951f, -0.724247f, 
    -0.707107f, -0.689541f, -0.671559f, -0.653173f, -0.634393f, -0.615232f, -0.595699f, -0.575808f, 
    -0.555570f, -0.534998f, -0.514103f, -0.492898f, -0.471397f, -0.449611f, -0.427555f, -0.405241f, 
    -0.382683f, -0.359895f, -0.336890f, -0.313682f, -0.290285f, -0.266713f, -0.242980f, -0.219101f, 
    -0.195090f, -0.170962f, -0.146730f, -0.122411f, -0.098017f, -0.073565f, -0.049068f, -0.024541f
};

float cosine_values[NUM_POINTS] = {
    1.000000f, 0.999699f, 0.998795f, 0.997290f, 0.995185f, 0.992480f, 0.989177f, 0.985278f, 
    0.980785f, 0.975702f, 0.970031f, 0.963776f, 0.956940f, 0.949528f, 0.941544f, 0.932993f, 
    0.923880f, 0.914210f, 0.903989f, 0.893224f, 0.881921f, 0.870087f, 0.857728f, 0.844854f, 
    0.831470f, 0.817585f, 0.803208f, 0.788346f, 0.773010f, 0.757209f, 0.740951f, 0.724247f, 
    0.707107f, 0.689541f, 0.671559f, 0.653173f, 0.634393f, 0.615232f, 0.595699f, 0.575808f, 
    0.555570f, 0.534998f, 0.514103f, 0.492898f, 0.471397f, 0.449611f, 0.427555f, 0.405241f, 
    0.382683f, 0.359895f, 0.336890f, 0.313682f, 0.290285f, 0.266713f, 0.242980f, 0.219101f, 
    0.195090f, 0.170962f, 0.146730f, 0.122411f, 0.098017f, 0.073565f, 0.049068f, 0.024541f, 
    0.000000f, -0.024541f, -0.049068f, -0.073565f, -0.098017f, -0.122411f, -0.146730f, -0.170962f, 
    -0.195090f, -0.219101f, -0.242980f, -0.266713f, -0.290285f, -0.313682f, -0.336890f, -0.359895f, 
    -0.382683f, -0.405241f, -0.427555f, -0.449611f, -0.471397f, -0.492898f, -0.514103f, -0.534998f, 
    -0.555570f, -0.575808f, -0.595699f, -0.615232f, -0.634393f, -0.653173f, -0.671559f, -0.689541f, 
    -0.707107f, -0.724247f, -0.740951f, -0.757209f, -0.773010f, -0.788346f, -0.803208f, -0.817585f, 
    -0.831470f, -0.844854f, -0.857728f, -0.870087f, -0.881921f, -0.893224f, -0.903989f, -0.914210f, 
    -0.923880f, -0.932993f, -0.941544f, -0.949528f, -0.956940f, -0.963776f, -0.970031f, -0.975702f, 
    -0.980785f, -0.985278f, -0.989177f, -0.992480f, -0.995185f, -0.997290f, -0.998795f, -0.999699f, 
    -1.000000f, -0.999699f, -0.998795f, -0.997290f, -0.995185f, -0.992480f, -0.989177f, -0.985278f, 
    -0.980785f, -0.975702f, -0.970031f, -0.963776f, -0.956940f, -0.949528f, -0.941544f, -0.932993f, 
    -0.923880f, -0.914210f, -0.903989f, -0.893224f, -0.881921f, -0.870087f, -0.857728f, -0.844854f, 
    -0.831470f, -0.817585f, -0.803208f, -0.788346f, -0.773010f, -0.757209f, -0.740951f, -0.724247f, 
    -0.707107f, -0.689541f, -0.671559f, -0.653173f, -0.634393f, -0.615232f, -0.595699f, -0.575808f, 
    -0.555570f, -0.534998f, -0.514103f, -0.492898f, -0.471397f, -0.449611f, -0.427555f, -0.405241f, 
    -0.382683f, -0.359895f, -0.336890f, -0.313682f, -0.290285f, -0.266713f, -0.242980f, -0.219101f, 
    -0.195090f, -0.170962f, -0.146730f, -0.122411f, -0.098017f, -0.073565f, -0.049068f, -0.024541f
};

// // for double buffering
// uint16_t buffers[2];
// uint8_t active_buffer = 0;

// Constants for fixed-point trigonometry
enum {cA1 = 3370945099UL, cB1 = 2746362156UL, cC1 = 292421UL};
enum {n = 13, p = 32, q = 31, r = 3, a = 12};

// Function to calculate fixed-point sine
// https://www.nullhardware.com/blog/fixed-point-sine-and-cosine-for-embedded-systems/
//
// int16_t fpsin(int16_t i) {
//     i <<= 1;
//     uint8_t c = i < 0; // Set carry for output pos/neg

//     if(i == (i | 0x4000)) // Flip input value to corresponding value in range [0..8192]
//         i = (1 << 15) - i;
//     i = (i & 0x7FFF) >> 1;

//     uint32_t y = (cC1 * ((uint32_t)i)) >> n;
//     y = cB1 - (((uint32_t)i * y) >> r);
//     y = (uint32_t)i * (y >> n);
//     y = (uint32_t)i * (y >> n);
//     y = cA1 - (y >> (p - q));
//     y = (uint32_t)i * (y >> n);
//     y = (y + (1UL << (q - a - 1))) >> (q - a); // Rounding

//     return c ? -y : y;
// }

// // Function to calculate fixed-point cosine
// #define fpcos(i) fpsin((int16_t)(((uint16_t)(i)) + 8192U))

// void precompute_sin_cos() {
//     int16_t angle_step = 32768 / NUM_POINTS; // 32768 is 2^15, representing 2*pi

//     for (int i = 0; i < NUM_POINTS; i++) {
//         // sine_values[i] = fpsin(i * angle_step) / 4096;
//         cosine_values[i] = fpcos(i * angle_step) / 4096;
//     }
// }

// Function to generate the world map from the mini map
void get_map() {
    // Loop over each cell in the mini_map
    for (int j = 0; j < MAP_HEIGHT; j++) {
        for (int i = 0; i < MAP_WIDTH; i++) {
            // If the mini_map has a wall (value 1), set it in the world_map
            if (mini_map[j][i] == 1) {
                world_map[j][i] = 1;
            }
        }
    }
}

// Function to print the world map for debugging purposes
void print_map() {
    for (int j = 0; j < MAP_HEIGHT; j++) {
        for (int i = 0; i < MAP_WIDTH; i++) {
            printf("%d ", world_map[j][i]);
        }
        printf("\n");
    }
}

// Function to draw the world map using the draw_rect function
void draw_map(uint16_t buffer) {
    for (int j = 0; j < MAP_HEIGHT; j++) {
        for (int i = 0; i < MAP_WIDTH; i++) {
            if (world_map[j][i] == 1) {
                // Draw a wall tile (represented by a white rectangle)
                draw_rect2buffer(color(DARK_GRAY,bpp==8), i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE, buffer);
            } //else {
            //     // Draw an empty tile (represented by a black rectangle)
            //     draw_rect(COLOR_EMPTY, i * TILE_SIZE, j * TILE_SIZE, TILE_SIZE, TILE_SIZE);
            // }
        }
    }
} 

void player_movement() {

    bool handled_key = false;
    float sin_a = sine_values[player.angle];
    float cos_a = cosine_values[player.angle];
    float dx, dy = 0;
    float speed = PLAYER_SPEED;
    float speed_sin = speed * sin_a;
    float speed_cos = speed * cos_a;

    xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;

    printf("sin_a: %i, cos_a: %i\n", (int16_t)sin_a*1000, (int16_t)cos_a*1000);
    printf("speed sin: %i, speed cos: %i", (int16_t)speed_sin*1000, (int16_t)speed_cos*1000);

    // printf("1player x: %i, player y: %i\n", (int16_t)(player.x * 100), (int16_t)(player.y * 100));

    // fill the keystates bitmask array
    for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
        uint8_t j, new_keys;
        RIA.addr0 = KEYBOARD_INPUT + i;
        new_keys = RIA.rw0;

        // check for change in any and all keys
        for (j = 0; j < 8; j++) {
            uint8_t new_key = (new_keys & (1<<j));
            if ((((i<<3)+j)>3) && (new_key != (keystates[i] & (1<<j)))) {
                printf( "key %d %s\n", ((i<<3)+j), (new_key ? "pressed" : "released"));
            }
        }

        keystates[i] = new_keys;
    }

    // check for a key down
    if (!(keystates[0] & 1)) {
        if (!handled_key) { // handle only once per single keypress
            // handle the keystrokes
            if (key(KEY_W)) { 
                player.x += speed_cos;
                player.y += speed_sin;
            }
            if (key(KEY_S)) {
                player.x += -speed_cos;
                player.y += -speed_sin;
            }
            if (key(KEY_A)) {
                player.x += speed_cos;
                player.y += -speed_sin;
            }
            if (key(KEY_D)) {
                player.x += -speed_cos;
                player.y += speed_sin;
            }
            if (key(KEY_LEFT)) {
                player.angle -= PLAYER_ROT_SPEED;
            }
            if (key(KEY_RIGHT)) {
                player.angle += PLAYER_ROT_SPEED;
            }
            handled_key = true;
            printf("1player x: %i, player y: %i\n", (int16_t)(player.x * 100), (int16_t)(player.y * 100));
        }
    } 

    // player.x += dx;
    // player.y += dy;
    // printf("2player x: %f, player y: %f\n", player.x, player.y);
           

}

void draw_player(uint16_t buffer){
    draw_circle2buffer(color(GREEN,bpp==8), (uint16_t)(player.x * TILE_SIZE), (uint16_t)(player.y * TILE_SIZE), 5, buffer);
    draw_line2buffer(GREEN, (uint16_t)(player.x * TILE_SIZE), (uint16_t)(player.y * TILE_SIZE), 
    (uint16_t)(player.x * TILE_SIZE + 10 * cosine_values[player.angle]),
    (uint16_t)(player.y * TILE_SIZE + 10 * sine_values[player.angle]), buffer);
}



int main() {
    
    bool handled_key = false;
    bool paused = true;
    bool show_buffers_indicators = false;
    uint8_t mode = 0;
    uint8_t i = 0;

    player.x = PLAYER_POS_X;
    player.y = PLAYER_POS_Y;
    player.angle = PLAYER_ANGLE;

    printf("Precomputing sine and cosine values...\n");
    // draw_string2buffer("Precomputing sine and cosine values...", buffers[active_buffer]);
    // precompute_sin_cos();

    get_map();

    print_map();
    

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

    draw_map(buffers[!active_buffer]);
    

    while (true) {

        // draw_map();

        // draw_player();

        if (!paused) {

            player_movement();
            // draw on inactive buffer
            erase_buffer(buffers[!active_buffer]);
            if(show_buffers_indicators){
                draw_circle2buffer(WHITE, (active_buffer ? SCREEN_WIDTH - 20 : 20), 20, 8, buffers[!active_buffer]);
                set_cursor((active_buffer ? SCREEN_WIDTH - 22 : 18), 17);
                draw_string2buffer((active_buffer ? "0" : "1"), buffers[!active_buffer]);
            }
            draw_map(buffers[!active_buffer]);
            draw_player(buffers[!active_buffer]);
            

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
                if (key(KEY_1)) {
                    mode = 0;
                }
                if (key(KEY_2)) {
                    mode = 1;
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
