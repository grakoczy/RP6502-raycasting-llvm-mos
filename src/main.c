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

#define PLAYER_POS_X 8
#define PLAYER_POS_Y 5 
#define PLAYER_ANGLE 0
#define PLAYER_SPEED 0.1
#define PLAYER_ROT_SPEED 1
#define PLAYER_SIZE_SCALE 4
#define PLAYER_LINE_LEN SCREEN_WIDTH

#define FOV  64
#define HALF_FOV  FOV / 2
#define SCALE  48
#define NUM_RAYS SCREEN_WIDTH / SCALE
#define HALF_NUM_RAYS NUM_RAYS / 2
#define DELTA_ANGLE 4
#define MAX_DEPTH 20


#define SCREEN_DIST HALF_WIDTH / 0.57735026919

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
    0.01000f, 0.024541f, 0.049068f, 0.073565f, 0.098017f, 0.122411f, 0.146730f, 0.170962f, 
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
    0.01000f, -0.024541f, -0.049068f, -0.073565f, -0.098017f, -0.122411f, -0.146730f, -0.170962f, 
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

#define sin(index) (sine_values[(index) % NUM_POINTS])
#define cos(index) (sine_values[((index) + (NUM_POINTS / 4)) % NUM_POINTS])

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

// Function to check if the player is colliding with a wall
bool check_wall(int x, int y) {
    // Ensure x, y are within map bounds
    if (x >= 0 && x < MAP_WIDTH && y >= 0 && y < MAP_HEIGHT) {
        return world_map[y][x] == 1;
    }
    return true;  // Return true (indicating a wall) if out of bounds
}

// Function to check for collision and move the player accordingly
void check_wall_collision(float dx, float dy) {
    // Scale factor based on player size and delta time
    float scale = PLAYER_SIZE_SCALE;

    // Check for collision on x-axis
    if (!check_wall((int)(player.x + dx * scale), (int)(player.y))) {
        player.x += dx;
    }

    // Check for collision on y-axis
    if (!check_wall((int)(player.x), (int)(player.y + dy * scale))) {
        player.y += dy;
    }
}

void player_movement() {

    bool handled_key = false;
    float sin_a = sin(player.angle);
    float cos_a = cos(player.angle);
    float dx, dy = 0;
    float speed = PLAYER_SPEED;
    float speed_sin = speed * sin_a;
    float speed_cos = speed * cos_a;

    xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;

    // printf("sin_a: %i, cos_a: %i\n", (int16_t)sin_a*1000, (int16_t)cos_a*1000);
    // printf("speed sin: %i, speed cos: %i", (int16_t)speed_sin*1000, (int16_t)speed_cos*1000);

    // printf("1player x: %i, player y: %i\n", (int16_t)(player.x * 100), (int16_t)(player.y * 100));

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
        if (!handled_key) { // handle only once per single keypress
            // handle the keystrokes
            if (key(KEY_W)) { 
                dx += speed_cos;
                dy += speed_sin;
            }
            if (key(KEY_S)) {
                dx += -speed_cos;
                dy += -speed_sin;
            }
            if (key(KEY_A)) {
                dx += speed_sin;
                dy += -speed_cos;
            }
            if (key(KEY_D)) {
                dx += -speed_sin;
                dy += speed_cos;
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

    check_wall_collision(dx, dy);

}

void draw_player(uint16_t buffer){
    draw_circle2buffer(color(GREEN,bpp==8), (uint16_t)(player.x * TILE_SIZE), (uint16_t)(player.y * TILE_SIZE), 5, buffer);
    draw_line2buffer(GREEN, (uint16_t)(player.x * TILE_SIZE), (uint16_t)(player.y * TILE_SIZE), 
    (uint16_t)(player.x * TILE_SIZE + 10 * cos(player.angle)),
    (uint16_t)(player.y * TILE_SIZE + 10 * sin(player.angle)), buffer);
}

// Function to check if the current tile is a wall
bool is_wall(int x, int y) {
    // printf("is_wall x: %i, y: %i, is: %i\n", x, y, world_map[y][x]);
    return world_map[y][x] == 1;
}

// Function to perform ray-casting
void ray_cast(uint16_t buffer) {
    float ox = player.x;
    float oy = player.y;
    int x_map = (int)player.x;
    int y_map = (int)player.y;

    uint8_t ray_angle = (player.angle - HALF_FOV) % NUM_POINTS;

    for (int ray = 0; ray < NUM_RAYS; ray++) {
        // float sin_a = sin(ray_angle);
        // float cos_a = cos(ray_angle);
        // printf("ray: %i, angle: %i, sin_a: %f, cos_a: %f\n", ray, ray_angle, sin_a, cos_a);

        // Horizontal intersection calculations
        float y_hor, dy;
        if (sin(ray_angle) > 0) {
            y_hor = y_map + 1;
            dy = 1;
        } else {
            y_hor = y_map - 1e-6;
            dy = -1;
        }

        float depth_hor = (y_hor - oy) /sin(ray_angle);
        float x_hor = ox + depth_hor * cos(ray_angle);
        float delta_depth_hor = dy / sin(ray_angle);
        float dx_hor = delta_depth_hor * cos(ray_angle);

        // printf("ox: %f, oy: %f, y_hor: %f, x_hor: %f, defpth_hor: %f, delta_depth_hor: %f, dx_hor: %f\n", ox, oy, y_hor, x_hor, depth_hor, delta_depth_hor, dx_hor);

        for (int i = 0; i < MAX_DEPTH; i++) {
            int tile_hor_x = (int)x_hor;
            int tile_hor_y = (int)y_hor;
            // printf("tile_hor_x %i, tile_hor_y %i\n", tile_hor_x, tile_hor_y);

            if (is_wall(tile_hor_x, tile_hor_y)) {
                break;
            }

            x_hor += dx_hor;
            y_hor += dy;
            depth_hor += delta_depth_hor;
        }

        // Vertical intersection calculations
        float x_vert, dx;
        if (cos(ray_angle) > 0) {
            x_vert = x_map + 1;
            dx = 1;
        } else {
            x_vert = x_map - 1e-6;
            dx = -1;
        }

        float depth_vert = (x_vert - ox) / cos(ray_angle);
        float y_vert = oy + depth_vert * sin(ray_angle);
        float delta_depth_vert = dx / cos(ray_angle);
        float dy_vert = delta_depth_vert * sin(ray_angle);

        for (int i = 0; i < MAX_DEPTH; i++) {
            int tile_vert_x = (int)x_vert;
            int tile_vert_y = (int)y_vert;

            if (is_wall(tile_vert_x, tile_vert_y)) {
                break;
            }

            x_vert += dx;
            y_vert += dy_vert;
            depth_vert += delta_depth_vert;
        }

        // Compare depths and choose the closer one
        float depth = (depth_vert < depth_hor) ? depth_vert : depth_hor;

        // Remove the fishbowl effect
        // depth *= cos(player.angle a- ray_angle);
        // printf("depth: %f\n", depth);


        // int x = (uint16_t)(player.x * TILE_SIZE);
        // int y = (uint16_t)(player.y * TILE_SIZE); 
        // int to_x = ox * TILE_SIZE + TILE_SIZE * depth * cos(ray_angle);
        // int to_y = oy * TILE_SIZE + TILE_SIZE * depth * sin(ray_angle);
        // printf("x: %i, y: %i, to_x: %i, to_y: %i\n", x, y, to_x, to_y);
        // draw_line2buffer(GREEN, x, y, to_x, to_y, buffer);

        // Projection (calculating the height of the wall slice for each ray)
        float proj_height = SCREEN_DIST / (depth + 0.0001f);

        // Draw walls (this part also needs your graphics library for drawing)
        int x1 = ray * SCALE;
        int y1 = (int)(HALF_HEIGHT - proj_height / 2);
        int x2 = (int)(x1+SCALE);
        int y2 = (int)proj_height;
        fill_rect2buffer(WHITE, x1, y1, SCALE, (uint16_t)proj_height, buffer);
        // printf("x1 %i, y1 %i, x2 %i, y2 %i\n", x1, y1, x2, y2);

        ray_angle = (ray_angle + DELTA_ANGLE) % NUM_POINTS;
    }
}

void ray_cast_angle(uint16_t buffer, uint8_t ray_angle) {
    float ox = player.x;
    float oy = player.y;
    int x_map = (int)player.x;
    int y_map = (int)player.y;

    // uint8_t ray_angle = (player.angle - HALF_FOV) % NUM_POINTS;

    // for (int ray = 0; ray < NUM_RAYS; ray++) {
        // float sin_a = sin(ray_angle);
        // float cos_a = cos(ray_angle);
        // printf("ray: %i, angle: %i, sin_a: %f, cos_a: %f\n", ray, ray_angle, sin_a, cos_a);

        // Horizontal intersection calculations
        float y_hor, dy;
        if (sin(ray_angle) > 0) {
            y_hor = y_map + 1;
            dy = 1;
        } else {
            y_hor = y_map - 1e-6;
            dy = -1;
        }

        float depth_hor = (y_hor - oy) /sin(ray_angle);
        float x_hor = ox + depth_hor * cos(ray_angle);
        float delta_depth_hor = dy / sin(ray_angle);
        float dx_hor = delta_depth_hor * cos(ray_angle);

        // printf("ox: %f, oy: %f, y_hor: %f, x_hor: %f, defpth_hor: %f, delta_depth_hor: %f, dx_hor: %f\n", ox, oy, y_hor, x_hor, depth_hor, delta_depth_hor, dx_hor);

        for (int i = 0; i < MAX_DEPTH; i++) {
            int tile_hor_x = (int)x_hor;
            int tile_hor_y = (int)y_hor;
            // printf("tile_hor_x %i, tile_hor_y %i\n", tile_hor_x, tile_hor_y);

            if (is_wall(tile_hor_x, tile_hor_y)) {
                break;
            }

            x_hor += dx_hor;
            y_hor += dy;
            depth_hor += delta_depth_hor;
        }

        // Vertical intersection calculations
        float x_vert, dx;
        if (cos(ray_angle) > 0) {
            x_vert = x_map + 1;
            dx = 1;
        } else {
            x_vert = x_map - 1e-6;
            dx = -1;
        }

        float depth_vert = (x_vert - ox) / cos(ray_angle);
        float y_vert = oy + depth_vert * sin(ray_angle);
        float delta_depth_vert = dx / cos(ray_angle);
        float dy_vert = delta_depth_vert * sin(ray_angle);

        for (int i = 0; i < MAX_DEPTH; i++) {
            int tile_vert_x = (int)x_vert;
            int tile_vert_y = (int)y_vert;

            if (is_wall(tile_vert_x, tile_vert_y)) {
                break;
            }

            x_vert += dx;
            y_vert += dy_vert;
            depth_vert += delta_depth_vert;
        }

        // Compare depths and choose the closer one
        float depth = (depth_vert < depth_hor) ? depth_vert : depth_hor;

        // Remove the fishbowl effect
        // depth *= cos(player.angle a- ray_angle);
        // printf("depth: %f\n", depth);


        // int x = (uint16_t)(player.x * TILE_SIZE);
        // int y = (uint16_t)(player.y * TILE_SIZE); 
        // int to_x = ox * TILE_SIZE + TILE_SIZE * depth * cos(ray_angle);
        // int to_y = oy * TILE_SIZE + TILE_SIZE * depth * sin(ray_angle);
        // // printf("x: %i, y: %i, to_x: %i, to_y: %i\n", x, y, to_x, to_y);
        // draw_line2buffer(GREEN, x, y, to_x, to_y, buffer);
        
        // Projection (calculating the height of the wall slice for each ray)
        float proj_height = SCREEN_DIST / (depth + 0.0001f);

        // Draw walls (this part also needs your graphics library for drawing)
        // int x1 = ray * SCALE;
        // int y1 = (int)(HALF_HEIGHT - proj_height / 2);
        // int x2 = (int)(x1+SCALE);
        // int y2 = (int)proj_height;
        // draw_rect2buffer(WHITE, x1, y1, SCALE, (uint16_t)proj_height, buffer);
        // Rectangle(hdc, x1, y1, x2, y2);
        // printf("x1 %i, y1 %i, x2 %i, y2 %i\n", x1, y1, x2, y2);

        // ray_angle = (ray_angle + DELTA_ANGLE) % NUM_POINTS;
    // }
}



int main() {
    
    bool handled_key = false;
    bool paused = true;
    bool show_buffers_indicators = true;
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
    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT,8);
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

            player_movement();
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
            ray_cast(buffers[!active_buffer]);
            

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
