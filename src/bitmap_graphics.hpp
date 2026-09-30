// ---------------------------------------------------------------------------
// bitmap_graphics.h
//
// This library was written by tonyvr to simplify bitmap graphics programming
// of the RP6502 picocomputer designed by Rumbledethumps.
//
// This code is an adaptation of the vga_graphics library written by V. Hunter Adams
// from Cornell University, for his excellent RP2040 microcontroller programming course.
//
// https://github.com/vha3/Hunter-Adams-RP2040-Demos/tree/master/VGA_Graphics/VGA_Graphics_Primitives
//
// There doesn't seem to be a copyright or a license associated with his code.
// I don't care what you do with my version either -- have fun!
//
// Trimmed to the 8bpp primitives this demo uses.
// ---------------------------------------------------------------------------

#ifndef BITMAP_GRAPHICS_H
#define BITMAP_GRAPHICS_H

#include <stdint.h>

// Sets up an 8bpp mode 3 bitmap canvas.
void init_bitmap_graphics(uint16_t canvas_struct_address,
                          uint16_t canvas_data_address,
                          uint8_t  canvas_plane,
                          uint8_t  canvas_mode,
                          uint16_t canvas_width,
                          uint16_t canvas_height);

uint16_t random(uint16_t low_limit, uint16_t high_limit);

void draw_pixel(uint8_t color, uint16_t x, uint16_t y);
void draw_vline(uint8_t color, uint16_t x, uint16_t y, uint16_t h);
void draw_hline(uint8_t color, uint16_t x, uint16_t y, uint16_t w);
void fill_rect_fast(uint8_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

#endif // BITMAP_GRAPHICS_H
