// ---------------------------------------------------------------------------
// bitmap_graphics.c
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

#include <rp6502.h>
#include <stdlib.h>
#include <stdint.h>
#include "bitmap_graphics.hpp"

#define BPP_MODE_8 3

static uint16_t canvas_w = 320;

void init_bitmap_graphics(uint16_t canvas_struct_address,
                          uint16_t canvas_data_address,
                          uint8_t  canvas_plane,
                          uint8_t  canvas_mode,
                          uint16_t canvas_width,
                          uint16_t canvas_height)
{
    canvas_w = canvas_width;

    xregn(1, 0, 0, 1, canvas_mode);

    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, x_wrap, false);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, y_wrap, false);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, x_pos_px, 0);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, y_pos_px, 0);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, width_px, canvas_width);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, height_px, canvas_height);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, xram_data_ptr, canvas_data_address);
    xram0_struct_set(canvas_struct_address, vga_mode3_config_t, xram_palette_ptr, 0xFFFF);

    xregn(1, 0, 1, 4, 3, BPP_MODE_8, canvas_struct_address, canvas_plane);
}

uint16_t random(uint16_t low_limit, uint16_t high_limit)
{
    return (uint16_t)((rand() % (high_limit - low_limit)) + low_limit);
}

void draw_pixel(uint8_t color, uint16_t x, uint16_t y)
{
    RIA.addr0 = canvas_w * y + x;
    RIA.rw0 = color;
}

void draw_vline(uint8_t color, uint16_t x, uint16_t y, uint16_t h)
{
    uint16_t row_addr = canvas_w * y + x;
    for (uint16_t i = 0; i < h; i++) {
        RIA.addr0 = row_addr;
        RIA.rw0 = color;
        row_addr += canvas_w;
    }
}

void draw_hline(uint8_t color, uint16_t x, uint16_t y, uint16_t w)
{
    RIA.addr0 = canvas_w * y + x;
    RIA.step0 = 1;
    for (uint16_t i = 0; i < w; i++) {
        RIA.rw0 = color;
    }
}

void fill_rect_fast(uint8_t color, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t row_addr = canvas_w * y + x;
    RIA.step0 = 1;
    for (uint16_t j = 0; j < h; j++) {
        RIA.addr0 = row_addr;
        for (uint16_t i = 0; i < w; i++) {
            RIA.rw0 = color;
        }
        row_addr += canvas_w;
    }
}
