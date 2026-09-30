#include <rp6502.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "colors.h"
#include "usb_hid_keys.h"
#include "bitmap_graphics.hpp"
#include "textures.h"
#include "sprites.h"
#include "FpF.hpp"
#include "maze.h"
#include "palette.h"

__attribute__((section(".zp.bss"))) static uint8_t zp_x;
__attribute__((section(".zp.bss"))) static uint8_t zp_side;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistX;
__attribute__((section(".zp.bss"))) static int16_t zp_sideDistY;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaX;
__attribute__((section(".zp.bss"))) static int16_t zp_deltaY;

using namespace mn::MFixedPoint;

#define SCREEN_WIDTH 320 
#define SCREEN_HEIGHT 180 
// The window is 120x80 doubled to 240x160: the largest 3:2 size that stays
// left of the HUD, which starts at x = 266.
#define WINDOW_WIDTH 120
#define WINDOW_HEIGHT 80
#define CLOCK_TICKS_PER_SEC 100

#define ROTATION_STEPS 32
#define QUADRANT_STEPS 8 

FpF16<7> posX(9);
FpF16<7> posY(11);
FpF16<7> dirX(0);
FpF16<7> dirY(-1); 
FpF16<7> planeX(0.66);
FpF16<7> planeY(0.0); 
FpF16<7> moveSpeed(0.125); 
FpF16<7> playerScale(5);

// sin(pi/16) and cos(pi/16) for 11.25 degree steps
FpF16<7> sin_r(0.19509032201); 
FpF16<7> cos_r(0.9807852804); 

uint16_t startX = 285;
uint16_t startY = 94;

uint16_t prevPlayerX, prevPlayerY;
bool prevPlayerDotValid = false;

const uint8_t SCAN_FRAMES = 120;
const uint8_t SCAN_DECAY_MAX = 80;
bool scan_active = false;
uint8_t scan_frame = 0;
uint8_t scan_decay[mapHeight][mapWidth];
bool map_visible = false;
uint8_t map_draw_distance = 3;

int8_t currentStep = 1;
int8_t movementStep = 2; 
uint8_t coarseRayStep = 4;
uint8_t coarseSidePercent = 33;

static constexpr uint8_t w = WINDOW_WIDTH;
static constexpr uint8_t h = WINDOW_HEIGHT;
static constexpr uint8_t xOffset = 9; 
static constexpr uint8_t yOffset = 5;

uint8_t fps = 0;
bool overlayUpdatesEnabled = true;
bool needleNeedsUpdate = true;
bool movingWallShadingEnabled = false;
uint8_t profileRaycastTicks = 0;
uint8_t profileBlitTicks = 0;

// Texture repeat factor: 1=no repeat, 2=repeat 2x, 4=repeat 4x
const uint8_t texRepeat = 4;

// Column-major: a column is contiguous so the renderer walks it with an 8-bit
// index, and the blit reads each row at constant addresses.
#define BUF_STRIDE WINDOW_HEIGHT
__attribute__((used)) uint8_t buffer[WINDOW_WIDTH * BUF_STRIDE];

static inline uint8_t* bufCol(uint8_t x) {
    return &buffer[(uint16_t)x * BUF_STRIDE];
}

#define FLOOR_COLOR 23
#define CEILING_COLOR 26

uint8_t wallCenterStep = 1;
uint8_t wallSideStep = 1;
uint8_t wallSideWidth = 0;
uint8_t wallSideStart = 0;
uint8_t wallSideEnd = 0;
bool wallUseSideStep = false;

static inline void updateWallStepParams() {
    if (currentStep < 2) {
        wallCenterStep = 1;
        wallSideStep = 1;
        wallSideWidth = 0;
        wallSideStart = 0;
        wallSideEnd = w;
        wallUseSideStep = false;
        return;
    }

    wallCenterStep = (movementStep > 0) ? (uint8_t)movementStep : 1;
    if (wallCenterStep > 4) wallCenterStep = 4;
    wallSideStep = (coarseRayStep >= wallCenterStep) ? coarseRayStep : wallCenterStep;
    if (wallSideStep > 4) wallSideStep = 4;

    wallUseSideStep = false;
    wallSideWidth = 0;
    wallSideStart = 0;
    wallSideEnd = w;

    if (coarseSidePercent == 0 || wallSideStep == wallCenterStep) return;

    uint16_t sideWidth16 = ((uint16_t)w * (uint16_t)coarseSidePercent) / 100;
    if (sideWidth16 > (w >> 1)) sideWidth16 = (w >> 1);
    wallSideWidth = (uint8_t)sideWidth16;
    if (wallSideWidth == 0) return;

    wallSideStart = wallSideWidth;
    wallSideEnd = (uint8_t)(w - wallSideWidth);
    wallUseSideStep = true;
}

static inline uint8_t getWallRayStepAtX(uint8_t x) {
    if (!wallUseSideStep) return wallCenterStep;
    if (x < wallSideStart || x >= wallSideEnd) return wallSideStep;
    return wallCenterStep;
}

// Zero-page operands for the column loops below, which are assembly because
// the compiler spills their loop state out of registers.
__attribute__((section(".zp.bss"), used)) uint8_t* zpa_col;
__attribute__((section(".zp.bss"), used)) uint8_t* zpa_col2;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_end;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_cnt;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_tpl;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_stl;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_sth;
__attribute__((section(".zp.bss"), used)) uint8_t zpa_tmp;

// Column writers. DUAL also writes the column two to the right: while moving
// the blit shows only even columns, so a four-wide ray covers columns x and x+2.
template <bool DUAL>
static inline void fillCol(uint8_t* col, uint8_t y0, uint8_t y1, uint8_t c) {
    if (y0 >= y1) return;
    zpa_col = col;
    if (DUAL) zpa_col2 = col + 2 * BUF_STRIDE;
    zpa_end = y1;
    uint8_t y = y0;
    if (DUAL) {
        asm volatile(
            "1:\n\t"
            "sta (zpa_col),y\n\t"
            "sta (zpa_col2),y\n\t"
            "iny\n\t"
            "cpy zpa_end\n\t"
            "bne 1b\n"
            : "+y"(y) : "a"(c) : "c", "v", "memory");
    } else {
        asm volatile(
            "1:\n\t"
            "sta (zpa_col),y\n\t"
            "iny\n\t"
            "cpy zpa_end\n\t"
            "bne 1b\n"
            : "+y"(y) : "a"(c) : "c", "v", "memory");
    }
}

// tp and step are 8.8 texel positions. Only the texel row modulo the texture
// height matters, so the integer part is kept masked in X.
#define TEX_STEP_ASM \
    "lda zpa_tpl\n\t" \
    "clc\n\t" \
    "adc zpa_stl\n\t" \
    "sta zpa_tpl\n\t" \
    "txa\n\t" \
    "adc zpa_sth\n\t" \
    "and #15\n\t" \
    "tax\n\t"

template <bool DUAL>
static inline void texCol(uint8_t* col, uint8_t y0, uint8_t y1, uint16_t tp, uint16_t step) {
    static_assert(texHeight == 16, "texel mask is hardcoded");
    if (y0 >= y1) return;
    zpa_col = col;
    if (DUAL) zpa_col2 = col + 2 * BUF_STRIDE;
    zpa_end = y1;
    zpa_tpl = (uint8_t)tp;
    zpa_stl = (uint8_t)step;
    zpa_sth = (uint8_t)(step >> 8);
    uint8_t y = y0;
    uint8_t t = (uint8_t)(tp >> 8) & (texHeight - 1);
    if (DUAL) {
        asm volatile(
            "1:\n\t"
            "lda texColumnBuffer,x\n\t"
            "sta (zpa_col),y\n\t"
            "sta (zpa_col2),y\n\t"
            TEX_STEP_ASM
            "iny\n\t"
            "cpy zpa_end\n\t"
            "bne 1b\n"
            : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
    } else {
        asm volatile(
            "1:\n\t"
            "lda texColumnBuffer,x\n\t"
            "sta (zpa_col),y\n\t"
            TEX_STEP_ASM
            "iny\n\t"
            "cpy zpa_end\n\t"
            "bne 1b\n"
            : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
    }
}

// One texel lookup per two rows, for half vertical resolution while moving.
template <bool DUAL>
static inline void texCol2(uint8_t* col, uint8_t y0, uint8_t y1, uint16_t tp, uint16_t step) {
    if (y0 >= y1) return;
    const uint16_t step2 = step << 1;
    zpa_col = col;
    uint8_t* col2 = col + 2 * BUF_STRIDE;
    if (DUAL) zpa_col2 = col2;
    zpa_tpl = (uint8_t)tp;
    zpa_stl = (uint8_t)step2;
    zpa_sth = (uint8_t)(step2 >> 8);
    uint8_t y = y0;
    uint8_t t = (uint8_t)(tp >> 8) & (texHeight - 1);
    const uint8_t pairs = (uint8_t)(y1 - y0) >> 1;
    if (pairs) {
        zpa_cnt = pairs;
        if (DUAL) {
            asm volatile(
                "1:\n\t"
                "lda texColumnBuffer,x\n\t"
                "sta (zpa_col),y\n\t"
                "sta (zpa_col2),y\n\t"
                "iny\n\t"
                "sta (zpa_col),y\n\t"
                "sta (zpa_col2),y\n\t"
                "iny\n\t"
                TEX_STEP_ASM
                "dec zpa_cnt\n\t"
                "bne 1b\n"
                : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
        } else {
            asm volatile(
                "1:\n\t"
                "lda texColumnBuffer,x\n\t"
                "sta (zpa_col),y\n\t"
                "iny\n\t"
                "sta (zpa_col),y\n\t"
                "iny\n\t"
                TEX_STEP_ASM
                "dec zpa_cnt\n\t"
                "bne 1b\n"
                : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
        }
    }
    if (y < y1) {
        uint8_t c = texColumnBuffer[t];
        col[y] = c;
        if (DUAL) col2[y] = c;
    }
}

static inline void fillZSpan(int16_t* zbuf, uint8_t x, uint8_t span, int16_t value) {
    zbuf[x] = value;
    if (span > 1) zbuf[x + 1] = value;
    if (span > 2) zbuf[x + 2] = value;
    if (span > 3) zbuf[x + 3] = value;
}

// ZBuffer for sprite depth testing (stores perpendicular wall distance per stripe)
int16_t ZBuffer[WINDOW_WIDTH];

// Sprite structure
struct Sprite {
    FpF16<7> x;
    FpF16<7> y;
    uint8_t texture;
};

// Sprite definitions
#define numSprites 10
Sprite sprites[numSprites];
uint8_t sprite_scan_decay[numSprites];
bool render_sprites = true; // toggle sprite rendering



#define GAMESTATE_IDLE 1
#define GAMESTATE_MOVING 2
uint8_t gamestate = GAMESTATE_IDLE;

uint8_t lineHeightTable[1024]; 

// Base Vector Tables (Only 1 Quadrant)
FpF16<7> dirXValues[QUADRANT_STEPS];
FpF16<7> dirYValues[QUADRANT_STEPS];
FpF16<7> planeXValues[QUADRANT_STEPS];
FpF16<7> planeYValues[QUADRANT_STEPS];

FpF16<7> texStepValues[256];

// --- OPTIMIZATION: Cached Tables ---
// Q1 Positive (Standard)
FpF16<7> rayDirX_Q1[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> rayDirY_Q1[QUADRANT_STEPS][WINDOW_WIDTH];

// Delta Dists for Q0 only (Q1 swaps X/Y, Q2 reuses Q0, Q3 reuses Q1 swap)
FpF16<7> deltaDistX_Q0[QUADRANT_STEPS][WINDOW_WIDTH];
FpF16<7> deltaDistY_Q0[QUADRANT_STEPS][WINDOW_WIDTH];

// These point to the correct row in the tables above
FpF16<7>* activeRayDirX;
FpF16<7>* activeRayDirY;
bool activeRayDirXNeg;
bool activeRayDirYNeg;
FpF16<7>* activeDeltaDistX;
FpF16<7>* activeDeltaDistY;

FpF16<7> cameraXValues[WINDOW_WIDTH];

// Cached inverse determinant for sprite projection
FpF16<7> invDetCache;
bool invDetValid = false;

int16_t texOffsetTable[256]; 
__attribute__((used)) uint8_t texColumnBuffer[16];
__attribute__((used)) uint8_t sprColumnBuffer[16]; // Buffer for sprite column data
__attribute__((used)) uint8_t sprOpaque[16];
uint8_t wallTexAvgColor[NUM_TEXTURES];

// First texture of each map cell type; the side-facing (dark) variant follows
// it. textures.bin holds a concrete and a redbrick pair: maze walls (cell 1)
// are concrete and the finish block (cell 2) is redbrick.
static const uint8_t cellTexture[4] = {0, 0, 2, 0};
static_assert(NUM_TEXTURES == 4, "cellTexture matches the textures.bin layout");

// values for sprite rotation
static const int16_t sin_fix8_32[] = {
       0,   50,   98,  142,  181,  213,  237,  251,
     256,  251,  237,  213,  181,  142,   98,   50,
       0,  -50,  -98, -142, -181, -213, -237, -251,
    -256, -251, -237, -213, -181, -142,  -98,  -50,
       0
};

static const int16_t cos_fix8_32[] = {
     256,  251,  237,  213,  181,  142,   98,   50,
       0,  -50,  -98, -142, -181, -213, -237, -251,
    -256, -251, -237, -213, -181, -142,  -98,  -50,
       0,   50,   98,  142,  181,  213,  237,  251,
     256
};

// fix_8((1+(sin(theta_deg)-cos(theta_deg)))/2) for 32 steps
static const int16_t t2_fix8_32[] = {
       0,   27,   59,   93,  128,  163,  197,  229,
     256,  279,  295,  306,  309,  306,  295,  279,
     256,  229,  197,  163,  128,   93,   59,   27,
       0,  -23,  -39,  -50,  -53,  -50,  -39,  -23,
       0
};

uint8_t currentRotStep = 0; 

#define NEEDLE_SPRITE_ADDR 0xF100  // Sprite data (2048 bytes)
#define PALETTE_XRAM_ADDR 0xF900   // After sprite (0xF100 + 2048 = 0xF900)
#define NEEDLE_CONFIG_ADDR 0xFB00  // After palette (0xF900 + 512 = 0xFB00)
#define NEEDLE_SIZE 32                  // pixel sprite

#define NEEDLE_CENTER_X 292
#define NEEDLE_CENTER_Y 29

#define KEYBOARD_INPUT 0xFF10 
#define KEYBOARD_BYTES 32
uint8_t keystates[KEYBOARD_BYTES] = {0};
#define key(code) (keystates[code >> 3] & (1 << (code & 7)))

inline FpF16<7> fp_abs(FpF16<7> value) {
    if (value.GetRawVal() < 0) return -value;
    return value;
} 

uint8_t mapValue(uint8_t value, uint8_t in_min, uint8_t in_max, uint8_t out_min, uint8_t out_max) {
    return out_min + ((value - in_min) * (out_max - out_min)) / (in_max - in_min);
}

// Quarter-square multiply: a*b = f(a+b) - f(|a-b|), f(n) = n*n/4. It replaces
// the generic 32-bit shift-and-add libcall in the per-column math.
__attribute__((used)) uint8_t sqLo[512];
__attribute__((used)) uint8_t sqHi[512];
__attribute__((section(".zp.bss"), used)) uint8_t zpm_a;
__attribute__((section(".zp.bss"), used)) uint8_t zpm_b;
__attribute__((section(".zp.bss"), used)) uint8_t zpm_lo;

static void buildSquareTables() {
    uint16_t sq = 0;
    for (uint16_t n = 0; n < 512; n++) {
        sqLo[n] = (uint8_t)sq;
        sqHi[n] = (uint8_t)(sq >> 8);
        sq += (n + 1) >> 1;
    }
}

static inline uint16_t mul8x8(uint8_t a, uint8_t b) {
    uint8_t hi = a;
    uint8_t lo;
    asm volatile(
        "sta zpm_a\n\t"
        "stx zpm_b\n\t"
        "clc\n\t"
        "adc zpm_b\n\t"
        "tax\n\t"
        "bcs 1f\n\t"
        "lda sqLo,x\n\t"
        "sta zpm_lo\n\t"
        "lda sqHi,x\n\t"
        "bcc 2f\n"
        "1:\n\t"
        "lda sqLo+256,x\n\t"
        "sta zpm_lo\n\t"
        "lda sqHi+256,x\n"
        "2:\n\t"
        "tay\n\t"
        "lda zpm_a\n\t"
        "sec\n\t"
        "sbc zpm_b\n\t"
        "bcs 3f\n\t"
        "eor #$FF\n\t"
        "adc #1\n"
        "3:\n\t"
        "tax\n\t"
        "lda zpm_lo\n\t"
        "sec\n\t"
        "sbc sqLo,x\n\t"
        "sta zpm_lo\n\t"
        "tya\n\t"
        "sbc sqHi,x\n"
        : "+a"(hi), "+x"(b), "=y"(lo) : : "c", "v", "memory");
    return ((uint16_t)hi << 8) | zpm_lo;
}

// (a * b) >> 7, truncated to 16 bits.
static inline uint16_t mulShr7(uint16_t a, uint8_t b) {
    uint16_t lo = mul8x8((uint8_t)a, b);
    uint16_t hi = mul8x8((uint8_t)(a >> 8), b);
    return (uint16_t)((hi << 1) + (lo >> 7));
}

// (a * b + 127) >> 7, truncated to 16 bits.
static inline uint16_t mulShr7Ceil(uint16_t a, uint8_t b) {
    uint16_t lo = mul8x8((uint8_t)a, b);
    uint16_t hi = mul8x8((uint8_t)(a >> 8), b);
    return (uint16_t)((hi << 1) + ((lo + 127) >> 7));
}

__attribute__((noinline)) static int32_t mul16s(int16_t a, int16_t b) {
    const bool neg = (a ^ b) < 0;
    const uint16_t ua = (a < 0) ? (uint16_t)-(uint16_t)a : (uint16_t)a;
    const uint16_t ub = (b < 0) ? (uint16_t)-(uint16_t)b : (uint16_t)b;
    const uint8_t aLo = (uint8_t)ua, aHi = (uint8_t)(ua >> 8);
    const uint8_t bLo = (uint8_t)ub, bHi = (uint8_t)(ub >> 8);
    // Most operands here are direction vectors below 256, so the high partial
    // products are usually skipped.
    uint32_t p = mul8x8(aLo, bLo);
    if (bHi) p += (uint32_t)mul8x8(aLo, bHi) << 8;
    if (aHi) {
        p += (uint32_t)mul8x8(aHi, bLo) << 8;
        if (bHi) p += (uint32_t)mul8x8(aHi, bHi) << 16;
    }
    return neg ? -(int32_t)p : (int32_t)p;
}

// posRaw + ((dist * rayDir) >> 7), matching the arithmetic shift of the
// signed 32-bit product.
static inline int16_t wallHitRaw(int16_t posRaw, int16_t dist, int16_t rayDir) {
    if (dist >= 0 && rayDir > -256 && rayDir < 256) {
        if (rayDir >= 0) return (int16_t)(posRaw + mulShr7((uint16_t)dist, (uint8_t)rayDir));
        return (int16_t)(posRaw - mulShr7Ceil((uint16_t)dist, (uint8_t)(-rayDir)));
    }
    return posRaw + (int16_t)(((int32_t)dist * rayDir) >> 7);
}

static inline int16_t mulFrac7Fast(int16_t value, uint8_t frac7) {
    if ((frac7 & 0x1F) == 0) {
        switch (frac7 >> 5) {
            case 0: return 0;
            case 1: return value >> 2;
            case 2: return value >> 1;
            case 3: return value - (value >> 2);
            default: return value;
        }
    }
    if (value >= 0) return (int16_t)mulShr7((uint16_t)value, frac7);
    return (int16_t)(((int32_t)value * frac7) >> 7);
}

static void buildWallTexAverages() {
    for (uint8_t texNum = 0; texNum < NUM_TEXTURES; texNum++) {
        uint16_t sum = 0;
        for (uint8_t texX = 0; texX < texWidth; texX++) {
            for (uint8_t texY = 0; texY < texHeight; texY++) {
                sum += getTexturePixel(texNum, ((uint16_t)texY << 4) + texX);
            }
        }
        wallTexAvgColor[texNum] = (uint8_t)(sum >> 8);
    }
}

static inline uint8_t shadeColorDistance(uint8_t baseColor, int16_t rawDist, uint8_t side) {
    int16_t shade = rawDist >> 7;
    if (shade > 24) shade = 24;
    if (shade < 0) shade = 0;

    int16_t color = (int16_t)baseColor - shade;
    if (side) color -= 3;
    if (color < 0) color = 0;
    return (uint8_t)color;
}

// Each buffer pixel is read from a constant address indexed by the row, so a
// pixel costs one indexed load and one store. RWD writes it doubled to the two
// screen rows addr0 and addr1 name. This is assembly because the compiler
// turns the constant addresses into pointers.
static_assert(WINDOW_WIDTH == 120, "blit is unrolled for 120 columns");

#define BLIT_STR_(x) #x
#define BLIT_STR(x) BLIT_STR_(x)
#define BLIT_PX(k) "ldx buffer+(" #k ")*" BLIT_STR(BUF_STRIDE) ",y\n\tstx $FFEE\n\t"
#define BLIT_8(k) \
    BLIT_PX(k) BLIT_PX(k + 1) BLIT_PX(k + 2) BLIT_PX(k + 3) \
    BLIT_PX(k + 4) BLIT_PX(k + 5) BLIT_PX(k + 6) BLIT_PX(k + 7)
#define BLIT_8_COARSE(k) \
    BLIT_PX(k) BLIT_PX(k + 2) BLIT_PX(k + 4) BLIT_PX(k + 6)
#define BLIT_ASM(body, row) asm volatile(body : : "y"(row) : "x", "memory")

#define BLIT_ROW \
    BLIT_8(0) BLIT_8(8) BLIT_8(16) BLIT_8(24) BLIT_8(32) BLIT_8(40) BLIT_8(48) \
    BLIT_8(56) BLIT_8(64) BLIT_8(72) BLIT_8(80) BLIT_8(88) BLIT_8(96) BLIT_8(104) \
    BLIT_8(112)
#define BLIT_ROW_COARSE \
    BLIT_8_COARSE(0) BLIT_8_COARSE(8) BLIT_8_COARSE(16) BLIT_8_COARSE(24) \
    BLIT_8_COARSE(32) BLIT_8_COARSE(40) BLIT_8_COARSE(48) BLIT_8_COARSE(56) \
    BLIT_8_COARSE(64) BLIT_8_COARSE(72) BLIT_8_COARSE(80) BLIT_8_COARSE(88) \
    BLIT_8_COARSE(96) BLIT_8_COARSE(104) BLIT_8_COARSE(112)

// While moving only the even columns are shown, each four pixels wide.
static void blitBuffer() {
    const bool coarseX = (currentStep >= 2);
    uint16_t screen_addr = SCREEN_WIDTH * yOffset + xOffset;
    RIA.step0 = coarseX ? 4 : 2;

    for (uint8_t j = 0; j < h; ++j) {
        RIA.addr0 = screen_addr;
        RIA.addr1 = screen_addr + SCREEN_WIDTH;
        if (coarseX) BLIT_ASM(BLIT_ROW_COARSE, j);
        else BLIT_ASM(BLIT_ROW, j);
        screen_addr += (SCREEN_WIDTH * 2);
    }
}

#undef BLIT_ROW_COARSE
#undef BLIT_ROW
#undef BLIT_ASM
#undef BLIT_8_COARSE
#undef BLIT_8
#undef BLIT_PX
#undef BLIT_STR
#undef BLIT_STR_

static void draw_7segment_digit(uint16_t color, int8_t digit, uint16_t x, uint16_t y) {
    static const uint8_t segments[] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    if (digit < 0 || digit > 9) return;
    uint8_t mask = segments[digit];

    fill_rect_fast(BLACK, x, y, 6, 10); // Clear digit area

    auto draw_seg = [&](uint16_t sx, uint16_t sy, uint16_t slen, bool horizontal) {
        if (horizontal) draw_hline(color, sx, sy, slen);
        else draw_vline(color, sx, sy, slen);
    };

    if (mask & 0x01) draw_seg(x + 1, y, 4, true);      // a
    if (mask & 0x02) draw_seg(x + 5, y + 1, 3, false); // b
    if (mask & 0x04) draw_seg(x + 5, y + 5, 4, false); // c
    if (mask & 0x08) draw_seg(x + 1, y + 9, 4, true);  // d
    if (mask & 0x10) draw_seg(x, y + 5, 4, false);     // e
    if (mask & 0x20) draw_seg(x, y + 1, 3, false);     // f
    if (mask & 0x40) draw_seg(x + 1, y + 4, 4, true);     // g
}

void draw_7segment_double(uint16_t color, int8_t number, uint16_t x, uint16_t y) {
    if (number < 0) number = 0;
    if (number > 99) number = 99;
    draw_7segment_digit(color, number / 10, x, y);
    draw_7segment_digit(color, number % 10, x + 7, y);
}

void precalculateRotations() {
    FpF16<7> currentDirX = dirX;
    FpF16<7> currentDirY = dirY;
    FpF16<7> currentPlaneX = planeX;
    FpF16<7> currentPlaneY = planeY;

    FpF16<7> fw = FpF16<7>(w);
    
    texStepValues[0] = FpF16<7>(texHeight * texRepeat); 
    for (uint16_t i = 1; i < 256; i++) {
      texStepValues[i] = FpF16<7>(texHeight * texRepeat) / FpF16<7>((int16_t)i);
    }

    for(uint8_t x = 0; x < w; x++) {
        cameraXValues[x] = FpF16<7>(2 * x) / fw - FpF16<7>(1);
    }

    
    const int16_t NEAR_ZERO_THRESHOLD = 2;
    const FpF16<7> MAX_DELTA_DIST(127);
    
    for (uint8_t i = 0; i < QUADRANT_STEPS; i++) {
      
      dirXValues[i] = currentDirX;
      dirYValues[i] = currentDirY;
      planeXValues[i] = currentPlaneX;
      planeYValues[i] = currentPlaneY;

      for(uint8_t x = 0; x < w; x++) {
          FpF16<7> rayDirX = currentDirX + currentPlaneX * cameraXValues[x];
          FpF16<7> rayDirY = currentDirY + currentPlaneY * cameraXValues[x];
          
          // Store Q1 positive ray directions
          rayDirX_Q1[i][x] = rayDirX;
          rayDirY_Q1[i][x] = rayDirY;

          // Calculate safe deltaDist helper
          auto calcDelta = [&](FpF16<7> rayDir) -> FpF16<7> {
              int16_t raw = rayDir.GetRawVal();
              if (raw < 0) raw = -raw;
              if (raw <= NEAR_ZERO_THRESHOLD) {
                  return MAX_DELTA_DIST;
              }
              FpF16<7> delta = FpF16<7>(1) / fp_abs(rayDir);
              return (delta > MAX_DELTA_DIST) ? MAX_DELTA_DIST : delta;
          };

          // Q0: 0-90° - Use base directions (other quadrants derived at runtime)
          deltaDistX_Q0[i][x] = calcDelta(rayDirX);
          deltaDistY_Q0[i][x] = calcDelta(rayDirY);
      }

      FpF16<7> oldDirX = currentDirX;
      currentDirX = currentDirX * cos_r - currentDirY * sin_r;
      currentDirY = oldDirX * sin_r + currentDirY * cos_r;

      FpF16<7> oldPlaneX = currentPlaneX;
      currentPlaneX = currentPlaneX * cos_r - currentPlaneY * sin_r;
      currentPlaneY = oldPlaneX * sin_r + currentPlaneY * cos_r;
    }
}

void updateRaycasterVectors() {
    uint8_t quad = currentRotStep / QUADRANT_STEPS;
    uint8_t idx = currentRotStep % QUADRANT_STEPS;

    FpF16<7> currDirX, currDirY, currPlaneX, currPlaneY;

    switch(quad) {
        case 0: // 0-90 deg
            currDirX = dirXValues[idx]; 
            currDirY = dirYValues[idx];
            currPlaneX = planeXValues[idx]; 
            currPlaneY = planeYValues[idx];
            
            activeRayDirX = rayDirX_Q1[idx];
            activeRayDirY = rayDirY_Q1[idx];
            activeRayDirXNeg = false;
            activeRayDirYNeg = false;
            activeDeltaDistX = deltaDistX_Q0[idx];
            activeDeltaDistY = deltaDistY_Q0[idx];
            break;

        case 1: // 90-180 deg - (x,y)->(-y,x): swap delta X/Y
            currDirX = -dirYValues[idx]; 
            currDirY = dirXValues[idx];
            currPlaneX = -planeYValues[idx]; 
            currPlaneY = planeXValues[idx];

            activeRayDirX = rayDirY_Q1[idx];
            activeRayDirY = rayDirX_Q1[idx];
            activeRayDirXNeg = true;
            activeRayDirYNeg = false;
            activeDeltaDistX = deltaDistY_Q0[idx];  // Swapped: Y->X
            activeDeltaDistY = deltaDistX_Q0[idx];  // Swapped: X->Y
            break;

        case 2: // 180-270 deg - (x,y)->(-x,-y): same abs values as Q0
            currDirX = -dirXValues[idx]; 
            currDirY = -dirYValues[idx];
            currPlaneX = -planeXValues[idx]; 
            currPlaneY = -planeYValues[idx];

            activeRayDirX = rayDirX_Q1[idx];
            activeRayDirY = rayDirY_Q1[idx];
            activeRayDirXNeg = true;
            activeRayDirYNeg = true;
            activeDeltaDistX = deltaDistX_Q0[idx];  // Same as Q0
            activeDeltaDistY = deltaDistY_Q0[idx];  // Same as Q0
            break;

        case 3: // 270-360 deg - (x,y)->(y,-x): swap delta X/Y
            currDirX = dirYValues[idx]; 
            currDirY = -dirXValues[idx];
            currPlaneX = planeYValues[idx]; 
            currPlaneY = -planeXValues[idx];

            activeRayDirX = rayDirY_Q1[idx];
            activeRayDirY = rayDirX_Q1[idx];
            activeRayDirXNeg = false;
            activeRayDirYNeg = true;
            activeDeltaDistX = deltaDistY_Q0[idx];  // Swapped: Y->X
            activeDeltaDistY = deltaDistX_Q0[idx];  // Swapped: X->Y
            break;
    }

    dirX = currDirX;
    dirY = currDirY;
    planeX = currPlaneX;
    planeY = currPlaneY;

    // Cache inverse determinant for sprite projection
    FpF16<7> det = planeX * dirY - dirX * planeY;
    if (det.GetRawVal() == 0) {
        invDetValid = false;
    } else {
        invDetCache = FpF16<7>(1) / det;
        invDetValid = true;
    }
}

void precalculateLineHeights() {
    lineHeightTable[0] = 255; 
    for (int i = 1; i < 1024; ++i) {
        FpF32<7> dist = FpF32<7>::FromRaw(i);
        FpF32<7> heightFp = FpF32<7>(h) / dist;
        int height = (int)heightFp;
        if (height > 255) height = 255;
        if (height < 0) height = 0;
        lineHeightTable[i] = (uint8_t)height;
    }

    // A wall taller than the window starts (i - h) / 2 rows above it, which is
    // that many 9.7 texture steps of 8192 / i.
    texOffsetTable[0] = 0;
    for (int i = 1; i < 256; i++) {
        if (i <= h) {
             texOffsetTable[i] = 0;
        } else {
             int32_t val = 4096 - ((4096L * h) / i);
             if (val < 0) val = 0;
             texOffsetTable[i] = (int16_t)val;
        }
    }
}

// One sprite column; tp and step are 7.9 texel positions. OPAQUE skips the
// per-texel opacity test.
template <bool OPAQUE>
static inline void spriteCol(uint8_t* col, uint8_t y0, uint8_t y1, uint16_t tp, uint16_t step) {
    if (y0 >= y1) return;
    zpa_col = col;
    zpa_end = y1;
    zpa_tpl = (uint8_t)tp;
    zpa_tmp = (uint8_t)(tp >> 8);
    zpa_stl = (uint8_t)step;
    zpa_sth = (uint8_t)(step >> 8);
    uint8_t y = y0;
    uint8_t t = (uint8_t)(tp >> 9) & 0x0F;
#define SPRITE_STEP_ASM \
    "lda zpa_tpl\n\t" \
    "clc\n\t" \
    "adc zpa_stl\n\t" \
    "sta zpa_tpl\n\t" \
    "lda zpa_tmp\n\t" \
    "adc zpa_sth\n\t" \
    "sta zpa_tmp\n\t" \
    "lsr\n\t" \
    "and #15\n\t" \
    "tax\n\t" \
    "iny\n\t" \
    "cpy zpa_end\n\t" \
    "bne 1b\n"
    if (OPAQUE) {
        asm volatile(
            "1:\n\t"
            "lda sprColumnBuffer,x\n\t"
            "sta (zpa_col),y\n\t"
            SPRITE_STEP_ASM
            : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
    } else {
        asm volatile(
            "1:\n\t"
            "lda sprOpaque,x\n\t"
            "beq 2f\n\t"
            "lda sprColumnBuffer,x\n\t"
            "sta (zpa_col),y\n"
            "2:\n\t"
            SPRITE_STEP_ASM
            : "+y"(y), "+x"(t) : : "a", "c", "v", "memory");
    }
#undef SPRITE_STEP_ASM
}

// Render sprites using raycasting sprite projection
void renderSprites() {
    if (numSprites == 0 || !render_sprites) return;

    const bool movingLowQuality = (currentStep >= 2);

    uint8_t centerStripeStep = 1;
    uint8_t sideStripeStep = 1;
    uint8_t sideWidth = 0;
    uint8_t sideStart = 0;
    uint8_t sideEnd = w;
    if (movingLowQuality) {
        centerStripeStep = (movementStep > 0) ? (uint8_t)movementStep : 1;
        if (centerStripeStep > 4) centerStripeStep = 4;
        sideStripeStep = (coarseRayStep >= centerStripeStep) ? coarseRayStep : centerStripeStep;
        if (sideStripeStep > 4) sideStripeStep = 4;

        if (coarseSidePercent > 0 && sideStripeStep > centerStripeStep) {
            uint16_t sideWidth16 = ((uint16_t)w * (uint16_t)coarseSidePercent) / 100;
            if (sideWidth16 > (w >> 1)) sideWidth16 = (w >> 1);
            sideWidth = (uint8_t)sideWidth16;
            sideStart = sideWidth;
            sideEnd = (uint8_t)(w - sideWidth);
        }
    }
    
    // Calculate inverse determinant for camera transformation
    // invDet = 1 / (planeX * dirY - dirX * planeY)
    if (!invDetValid) return; // Avoid division by zero
    const int16_t invDetRaw = invDetCache.GetRawVal();
    const int16_t dirXRaw = dirX.GetRawVal();
    const int16_t dirYRaw = dirY.GetRawVal();
    const int16_t planeXRaw = planeX.GetRawVal();
    const int16_t planeYRaw = planeY.GetRawVal();
    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    
    int32_t w_half = (w >> 1);
    
    for (int8_t i = numSprites - 1; i >= 0; i--) {
        // Translate sprite position to relative to camera (raw 9.7)
        int16_t spriteXraw = sprites[i].x.GetRawVal() - posXRaw;
        int16_t spriteYraw = sprites[i].y.GetRawVal() - posYRaw;

        int32_t forward = mul16s(dirXRaw, spriteXraw) + mul16s(dirYRaw, spriteYraw);
        if (forward <= 0) continue;

        // Transform sprite with the inverse camera matrix using raw math
        int16_t t1 = (int16_t)((mul16s(dirYRaw, spriteXraw) >> 7) - (mul16s(dirXRaw, spriteYraw) >> 7));
        int16_t t2 = (int16_t)((mul16s((int16_t)-planeYRaw, spriteXraw) >> 7) + (mul16s(planeXRaw, spriteYraw) >> 7));
        int16_t tx = (int16_t)(mul16s(invDetRaw, t1) >> 7);
        int16_t ty = (int16_t)(mul16s(invDetRaw, t2) >> 7);

        // Skip if sprite is behind camera
        if (ty <= 10) continue;
        
        int16_t tyIdx = ty;
        if (tyIdx > 1023) tyIdx = 1023;

        // Past |tx| = 3*ty the sprite center is 3 half-windows off axis, and
        // its half width (at most 63) cannot bring it back for w >= 64.
        const int16_t txLimit = (int16_t)(ty * 3);
        if (tx > txLimit || tx < -txLimit) continue;
        int32_t spriteScreenX = w_half + mul16s((int16_t)w_half, tx) / ty;

        uint8_t spriteStripeStep = 1;
        if (movingLowQuality) {
            spriteStripeStep = centerStripeStep;
            if (sideWidth > 0 && (spriteScreenX < sideStart || spriteScreenX >= sideEnd)) {
                spriteStripeStep = sideStripeStep;
            }
        }
        
        // Calculate sprite positioning like a wall would be rendered
        // First get the wall height at this distance for reference
        int16_t wall_height = lineHeightTable[tyIdx];
        
        // Sprite is quarter of wall height (as specified)
        int16_t spr_height = wall_height / 2;
        if (spr_height < 1) spr_height = 1;
        
        // Position sprite like a short wall sitting on the floor
        // Walls are centered on h/2, so bottom is at: h/2 + wall_height/2
        // Sprite bottom aligns with wall bottom, sprite is just shorter
        int16_t wall_bottom = (h / 2) + (wall_height / 2);
        int16_t drawEndY = wall_bottom;
        int16_t drawStartY = drawEndY - spr_height;
        
        int16_t screen_drawStartY = drawStartY;
        int16_t screen_drawEndY = drawEndY;
        
        // Clamp to screen bounds
        if (screen_drawStartY < 0) screen_drawStartY = 0;
        if (screen_drawEndY > h) screen_drawEndY = h;
        
        if (screen_drawStartY >= screen_drawEndY) continue;
        
        int16_t spr_width = spr_height; // Square aspect ratio
        
        // Calculate draw boundaries X
        int32_t drawStartX_32 = -spr_width / 2 + spriteScreenX;
        int32_t drawEndX_32 = spr_width / 2 + spriteScreenX;
        
        int16_t drawStartX = (drawStartX_32 < 0) ? 0 : (int16_t)drawStartX_32;
        int16_t drawEndX = (drawEndX_32 > w) ? w : (int16_t)drawEndX_32;
        
        // Skip if sprite is completely off screen
        if (drawStartX >= drawEndX) continue;
        
        // Precalculate stepping using existing table (avoids runtime divide)
        // texStepValues[h].raw = (texHeight * texRepeat * 128) / h = 8192 / h
        // sprite texStep (16.16) = (16 << 16) / h = 1048576 / h = raw << 7
        // Texture positions run in 7.9 fixed point (the same bits the 16.16
        // form uses), so they fit 16 bits: a sprite spans at most 16 texels.
        const uint16_t texStep = (uint16_t)texStepValues[spr_height].GetRawVal();

        // Calculate initial texture X position
        int16_t logicalStartX = (int16_t)(-spr_width / 2 + spriteScreenX);
        uint16_t texXPos = (uint16_t)mul16s((int16_t)(drawStartX - logicalStartX), (int16_t)texStep);

        const uint16_t texStepY = texStep;
        uint16_t initialTexYPos = 0;
        if (drawStartY < 0) {
            initialTexYPos = (uint16_t)mul16s((int16_t)-drawStartY, (int16_t)texStepY);
        }
        const uint8_t sy0 = (uint8_t)screen_drawStartY;
        const uint8_t sy1 = (uint8_t)screen_drawEndY;

        // Get sprite distance
        int16_t spriteDistRaw = ty;

        uint8_t* colStartPtr = bufCol((uint8_t)drawStartX);
        const uint16_t colAdvance = (uint16_t)spriteStripeStep * BUF_STRIDE;

        // Cache for sprite column to avoid repeated fetches when scaling up
        int16_t lastTexX = -1;
        bool lastColAllTransparent = false;
        bool lastColAllOpaque = false;
        uint16_t lastOpaqueMask = 0;

        uint8_t spriteTex = sprites[i].texture;
        if (spriteTex >= NUM_SPRITES) {
            spriteTex %= NUM_SPRITES;
        }

        // Loop through every vertical stripe of the sprite on screen
        const uint16_t texAdvance = (uint16_t)(texStep * spriteStripeStep);
        for (int16_t stripe = drawStartX; stripe < drawEndX; stripe += spriteStripeStep) {
            
            // Draw sprite if closer than wall OR no wall at all
            if (spriteDistRaw > 0 && spriteDistRaw <= ZBuffer[stripe]) {
                int16_t texX = texXPos >> 9;
                if (texX > 15) texX = 15;

                // Only fetch if texture column changed
                if (texX != lastTexX) {
                    fetchSpriteColumn(spriteTex, (uint8_t)texX);
                    lastTexX = texX;
#if SPRITE_HAS_OPACITY_METADATA
                    if (movingLowQuality) {
                        uint8_t transparentCount = 0;
                        lastOpaqueMask = 0;
                        for (uint8_t sy = 0; sy < 16; sy++) {
                            if (sprColumnBuffer[sy] != 0x21) {
                                lastOpaqueMask |= (uint16_t)1 << sy;
                            } else {
                                transparentCount++;
                            }
                        }
                        lastColAllTransparent = (transparentCount == 16);
                        lastColAllOpaque = (transparentCount == 0);
                    } else {
                        lastOpaqueMask = fetchSpriteColumnMask(spriteTex, (uint8_t)texX);
                        lastColAllTransparent = (lastOpaqueMask == 0);
                        lastColAllOpaque = (lastOpaqueMask == 0xFFFF);
                    }
                    uint16_t mask = lastOpaqueMask;
                    for (uint8_t sy = 0; sy < 16; sy++) {
                        sprOpaque[sy] = (uint8_t)(mask & 1);
                        mask >>= 1;
                    }
#else
                    uint8_t transparentCount = 0;
                    for (uint8_t sy = 0; sy < 16; sy++) {
                        sprOpaque[sy] = (sprColumnBuffer[sy] != 0x21);
                        if (!sprOpaque[sy]) {
                            transparentCount++;
                        }
                    }
                    lastColAllTransparent = (transparentCount == 16);
                    lastColAllOpaque = (transparentCount == 0);
#endif
                }

                if (lastColAllTransparent) {
                    texXPos += texAdvance;
                    colStartPtr += colAdvance;
                    continue;
                }

                uint8_t spanWidth = spriteStripeStep;
                if ((int16_t)(stripe + spanWidth) > drawEndX) {
                    spanWidth = (uint8_t)(drawEndX - stripe);
                }
                bool spanVisible = true;
                for (uint8_t s = 0; s < spanWidth; s++) {
                    if (spriteDistRaw > ZBuffer[stripe + s]) {
                        spanVisible = false;
                        break;
                    }
                }
                if (!spanVisible) {
                    texXPos += texAdvance;
                    colStartPtr += colAdvance;
                    continue;
                }

                uint8_t* c = colStartPtr;
                for (uint8_t s = 0; s < spanWidth; s++, c += BUF_STRIDE) {
                    // The moving blit shows only even columns
                    if (movingLowQuality && ((uint8_t)(stripe + s) & 1)) continue;
                    if (lastColAllOpaque) {
                        spriteCol<true>(c, sy0, sy1, initialTexYPos, texStepY);
                    } else {
                        spriteCol<false>(c, sy0, sy1, initialTexYPos, texStepY);
                    }
                }
            }
            texXPos += texAdvance;
            colStartPtr += colAdvance;
        }
    }
}

struct WallColumn {
    uint8_t ds;
    uint8_t de;
    bool shaded;
    uint8_t shadeColor;
    uint16_t texPos;
    uint16_t texStep;
};

template <bool DUAL>
static void drawColumn(uint8_t* col, const WallColumn& wc, bool coarse) {
    fillCol<DUAL>(col, 0, wc.ds, CEILING_COLOR);
    fillCol<DUAL>(col, wc.de, h, FLOOR_COLOR);
    if (wc.shaded) {
        fillCol<DUAL>(col, wc.ds, wc.de, wc.shadeColor);
    } else if (coarse) {
        texCol2<DUAL>(col, wc.ds, wc.de, wc.texPos, wc.texStep);
    } else {
        texCol<DUAL>(col, wc.ds, wc.de, wc.texPos, wc.texStep);
    }
}

void raycastF() {
    uint16_t raycastStartClock = ria_call_int(RIA_OP_CLOCK);

    const bool coarse = (currentStep >= 2);

    FpF16<7>* rayDirXPtr = activeRayDirX;
    FpF16<7>* rayDirYPtr = activeRayDirY;
    FpF16<7>* deltaDistXPtr = activeDeltaDistX;
    FpF16<7>* deltaDistYPtr = activeDeltaDistY;
    const bool negX = activeRayDirXNeg;
    const bool negY = activeRayDirYNeg;

    const int16_t posXRaw = posX.GetRawVal();
    const int16_t posYRaw = posY.GetRawVal();
    const uint8_t fracX = (uint8_t)(posXRaw & 0x7F);
    const uint8_t invFracX = (uint8_t)(128 - fracX);
    const uint8_t fracY = (uint8_t)(posYRaw & 0x7F);
    const uint8_t invFracY = (uint8_t)(128 - fracY);

    // Flattened 8-bit map index of the player's cell
    static_assert(mapWidth == 16 && mapHeight == 16, "map index is (y << 4) + x");
    const uint8_t mapOffsetStart = (uint8_t)(((posYRaw >> 7) << 4) + (posXRaw >> 7));
    int8_t* mapPtr = (int8_t*)worldMap;

    uint8_t lastTexNum = 0xFF;
    uint8_t lastTexX = 0xFF;
    const bool writeZ = render_sprites;
    const bool useMovingShading = movingWallShadingEnabled && coarse;
    updateWallStepParams();

    for (zp_x = 0; zp_x < w;) {
        uint8_t rayStep = getWallRayStepAtX(zp_x);
        uint8_t xSpan = rayStep;
        if ((uint16_t)zp_x + xSpan > w) {
            xSpan = (uint8_t)(w - zp_x);
        }

        zp_deltaX = deltaDistXPtr[zp_x].GetRawVal();
        zp_deltaY = deltaDistYPtr[zp_x].GetRawVal();
        int16_t rDX = rayDirXPtr[zp_x].GetRawVal();
        int16_t rDY = rayDirYPtr[zp_x].GetRawVal();
        if (negX) rDX = -rDX;
        if (negY) rDY = -rDY;

        uint8_t mapOffset = mapOffsetStart;
        int8_t mapStepX = (rDX < 0) ? -1 : 1;
        int8_t mapStepY = (rDY < 0) ? -16 : 16;
        if (rDX < 0) {
            zp_sideDistX = mulFrac7Fast(zp_deltaX, fracX);
        } else {
            zp_sideDistX = mulFrac7Fast(zp_deltaX, invFracX);
        }
        if (rDY < 0) {
            zp_sideDistY = mulFrac7Fast(zp_deltaY, fracY);
        } else {
            zp_sideDistY = mulFrac7Fast(zp_deltaY, invFracY);
        }

        while (mapPtr[mapOffset] == 0) {
            if (zp_sideDistX < zp_sideDistY) {
                zp_sideDistX += zp_deltaX;
                mapOffset += mapStepX;
                zp_side = 0;
            } else {
                zp_sideDistY += zp_deltaY;
                mapOffset += mapStepY;
                zp_side = 1;
            }
        }

        int16_t rawDist = (zp_side == 0) ?
            (zp_sideDistX - zp_deltaX) :
            (zp_sideDistY - zp_deltaY);

        // Store in ZBuffer for sprite rendering (store raw distance)
        if (writeZ) {
            int16_t zVal = (rawDist < 0) ? 0 : rawDist;
            fillZSpan(ZBuffer, zp_x, xSpan, zVal);
        }

        uint16_t lineHeight;
        if (rawDist >= 0 && rawDist < 1024) {
            lineHeight = lineHeightTable[rawDist];
        } else {
            lineHeight = (rawDist > 0) ?
                (int)(FpF16<7>(h) / FpF16<7>::FromRaw(rawDist)) : h;
            if (lineHeight > 255) lineHeight = 255;
        }

        uint8_t texNum = (uint8_t)(cellTexture[(uint8_t)mapPtr[mapOffset] & 3] + zp_side);
        int16_t drawStart = (-((int16_t)lineHeight) >> 1) + (h >> 1);
        if (drawStart < 0) drawStart = 0;
        uint16_t drawEnd = drawStart + lineHeight;
        if (drawEnd > h) drawEnd = h;

        WallColumn wc;
        wc.ds = (uint8_t)drawStart;
        wc.de = (uint8_t)drawEnd;
        wc.shaded = useMovingShading;
        wc.shadeColor = 0;
        wc.texPos = 0;
        wc.texStep = 0;

        if (useMovingShading) {
            uint8_t baseColor = wallTexAvgColor[texNum];
            wc.shadeColor = shadeColorDistance(baseColor, rawDist, zp_side);
        } else {
            int16_t wallRaw = (zp_side == 0) ?
                wallHitRaw(posYRaw, rawDist, rDY) :
                wallHitRaw(posXRaw, rawDist, rDX);
            // Extract 7-bit fractional part, scale by texRepeat*texWidth=64 (<<6), then >>7 = >>1
            uint8_t frac7 = wallRaw & 0x7F;
            uint8_t texX = (frac7 >> 1) & 0x0F;
            if (zp_side == 0 && rDX > 0) texX = texWidth - texX - 1;
            if (zp_side == 1 && rDY < 0) texX = texWidth - texX - 1;

            if (texNum != lastTexNum || texX != lastTexX) {
                fetchTextureColumn(texNum, texX);
                lastTexNum = texNum;
                lastTexX = texX;
            }

            // 9.7 table values doubled into 8.8
            int16_t rawTexPos = (lineHeight > h) ? texOffsetTable[lineHeight] : 0;
            if (rawTexPos < 0) rawTexPos = 0;
            wc.texPos = (uint16_t)rawTexPos << 1;
            wc.texStep = (uint16_t)texStepValues[lineHeight].GetRawVal() << 1;
        }

        uint8_t* col = bufCol(zp_x);
        if (coarse && xSpan > 2) {
            drawColumn<true>(col, wc, coarse);
        } else {
            drawColumn<false>(col, wc, coarse);
        }

        zp_x += rayStep;
    }

    uint16_t raycastEndClock = ria_call_int(RIA_OP_CLOCK);
    profileRaycastTicks = (uint8_t)(raycastEndClock - raycastStartClock);

    // Render sprites to buffer after walls but before drawing to screen
    renderSprites();

    uint16_t blitStartClock = ria_call_int(RIA_OP_CLOCK);
    blitBuffer();
    uint16_t blitEndClock = ria_call_int(RIA_OP_CLOCK);
    profileBlitTicks = (uint8_t)(blitEndClock - blitStartClock);
}

void draw_ui() {
  fill_rect_fast(18, startX, startY, 32, 32);
  
}

void draw_map() {
    uint8_t playerTileX = (uint8_t)(int)posX;
    uint8_t playerTileY = (uint8_t)(int)posY;
    uint16_t radius2 = 0;
    uint16_t prevRadius2 = 0;
    static uint16_t drawDist2 = (uint16_t)map_draw_distance * (uint16_t)map_draw_distance;
    if (scan_active) {
        uint8_t maxDx = playerTileX;
        uint8_t maxDx2 = (mapWidth - 1) - playerTileX;
        if (maxDx2 > maxDx) maxDx = maxDx2;

        uint8_t maxDy = playerTileY;
        uint8_t maxDy2 = (mapHeight - 1) - playerTileY;
        if (maxDy2 > maxDy) maxDy = maxDy2;

        uint16_t maxDist2 = (uint16_t)maxDx * (uint16_t)maxDx + (uint16_t)maxDy * (uint16_t)maxDy;
        if (maxDist2 == 0) maxDist2 = 1;

        radius2 = (uint16_t)(((uint32_t)scan_frame * maxDist2) / (SCAN_FRAMES - 1));
        if (scan_frame > 0) {
            prevRadius2 = (uint16_t)(((uint32_t)(scan_frame - 1) * maxDist2) / (SCAN_FRAMES - 1));
        }
    }

    // Scaled to 2x2 pixels for visibility
    for (uint8_t i = 0; i < mapHeight; i++) {
        int8_t* mapRow = worldMap[i];
        uint8_t* decayRow = scan_decay[i];
        uint16_t py = (uint16_t)(i << 1) + startY;
        for (uint8_t j = 0; j < mapWidth; j++) {
            uint8_t color = 18;
            int8_t cell = mapRow[j];
            if (cell > 0) {
                uint8_t dx = (j > playerTileX) ? (uint8_t)(j - playerTileX) : (uint8_t)(playerTileX - j);
                uint8_t dy = (i > playerTileY) ? (uint8_t)(i - playerTileY) : (uint8_t)(playerTileY - i);
                uint16_t dist2 = (uint16_t)dx * (uint16_t)dx + (uint16_t)dy * (uint16_t)dy;

                if (scan_active) {
                    if (dist2 >= prevRadius2 && dist2 <= radius2) {
                        decayRow[j] = SCAN_DECAY_MAX;
                        color = WHITE;
                    } else if (decayRow[j] > 0) {
                        if (cell == 1) {
                            color = mapValue(decayRow[j], 1, SCAN_DECAY_MAX, 18, 28);
                        } else {
                            color = DARK_RED;
                        }
                    }
                } else if (dist2 <= drawDist2) {
                    color = 32;
                }
            }
            uint16_t px = (uint16_t)(j << 1) + startX;
            fill_rect_fast(color, px, py, 2, 2);
        }
    }

    if (scan_active) {
        // Draw sprites only during active scan
        for (uint8_t i = 0; i < numSprites; i++) {
            Sprite* spr = &sprites[i];
            uint8_t* decay = &sprite_scan_decay[i];
            int8_t sX = (int8_t)(int)spr->x;
            int8_t sY = (int8_t)(int)spr->y;

            if (sX >= 0 && sX < mapWidth && sY >= 0 && sY < mapHeight) {
                uint8_t usX = (uint8_t)sX;
                uint8_t usY = (uint8_t)sY;
                uint8_t dx = (usX > playerTileX) ? (uint8_t)(usX - playerTileX) : (uint8_t)(playerTileX - usX);
                uint8_t dy = (usY > playerTileY) ? (uint8_t)(usY - playerTileY) : (uint8_t)(playerTileY - usY);
                uint16_t dist2 = (uint16_t)dx * (uint16_t)dx + (uint16_t)dy * (uint16_t)dy;

                if (dist2 >= prevRadius2 && dist2 <= radius2) {
                    *decay = SCAN_DECAY_MAX;
                }

                if (*decay > 0 && *decay < SCAN_DECAY_MAX) {
                    uint8_t color = 10 + spr->texture;
                    draw_pixel(color, (uint16_t)(usX << 1) + startX, (uint16_t)(usY << 1) + startY);
                }
            }
        }
    }
} 

void draw_needle() {

    // Map currentRotStep (0-31) to table index
    uint8_t i = (32 - currentRotStep) % 32;
    
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Update only the transform matrix (rotation)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_32[i]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_32[32-i]);
    
}


void draw_player(bool drawHud){
    if (!map_visible) {
        return;
    }
    // Scale 2x for visibility
    FpF16<7> ts(2);
    uint16_t x = (int)(posX * ts) + startX;
    uint16_t y = (int)(posY * ts) + startY;

    if (prevPlayerDotValid) {
        fill_rect_fast(DARK_GREEN, prevPlayerX, prevPlayerY, 2, 2);
    }
    fill_rect_fast(YELLOW, x, y, 2, 2);

    if (drawHud) {
        draw_7segment_double(GREEN, (int16_t)(fps), 270, 68);
        draw_7segment_double(GREEN, (int16_t)(coarseSidePercent), 295, 68);
    }
    prevPlayerX = x;
    prevPlayerY = y;
    prevPlayerDotValid = true;
}

void handleCalculation() {
    if (overlayUpdatesEnabled && needleNeedsUpdate) {
        draw_needle();
        needleNeedsUpdate = false;
    }
    raycastF();
    gamestate = GAMESTATE_IDLE;
}

void WaitForAnyKey(){
    xregn(0, 0, 0, 1, KEYBOARD_INPUT);
    RIA.addr0 = KEYBOARD_INPUT;
    RIA.step0 = 0;
    while (RIA.rw0 & 1);
}


void init_needle_sprite() {
    uint16_t ptr = NEEDLE_CONFIG_ADDR;
    
    // Calculate positions with proper parentheses to avoid warnings
    int16_t needle_x = (NEEDLE_CENTER_X - NEEDLE_SIZE/2);
    int16_t needle_y = (NEEDLE_CENTER_Y - NEEDLE_SIZE/2);
    
    // Set initial rotation (0 degrees = pointing up)
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[0], cos_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[1], -sin_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[2], NEEDLE_SIZE * t2_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[3], sin_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[4], cos_fix8_32[0]);
    xram0_struct_set(ptr, vga_mode4_asprite_t, transform[5], NEEDLE_SIZE * t2_fix8_32[32]);
    
    // Set position (centered) - use variables to avoid macro warnings
    xram0_struct_set(ptr, vga_mode4_asprite_t, x_pos_px, needle_x);
    xram0_struct_set(ptr, vga_mode4_asprite_t, y_pos_px, needle_y);
    
    // Set sprite data pointer
    xram0_struct_set(ptr, vga_mode4_asprite_t, xram_sprite_ptr, NEEDLE_SPRITE_ADDR);
    
    // CRITICAL: Set log_size correctly for 32x32 sprite
    xram0_struct_set(ptr, vga_mode4_asprite_t, log_size, 5);  // 2^5 = 32
    
    // No opacity metadata
    xram0_struct_set(ptr, vga_mode4_asprite_t, has_opacity_metadata, false);
    
    // Enable Mode 4 affine sprite plane
    // plane=1, num_sprites=1, affine=1
    xregn(1, 0, 1, 5, 4, 1, NEEDLE_CONFIG_ADDR, 1, 1);
}

void placeSprites() {
    for (int i = 0; i < numSprites; i++) {
        int x, y;
        bool valid = false;
        int attempts = 0;
        while (!valid) {
            x = abs(rand()) % mapWidth;
            y = abs(rand()) % mapHeight;
            
            if (worldMap[y][x] == 0) {
                valid = true;
                // Check if occupied by another sprite within a wider area (avoid clusters)
                for (int j = 0; j < i; j++) {
                    int sx = (int)sprites[j].x;
                    int sy = (int)sprites[j].y;
                    
                    // Check for overlap within 2 tiles distance (covers 3x3 area centering on sprite)
                    if (abs(x - sx) < 2 && abs(y - sy) < 2) {
                        valid = false;
                        break;
                    }
                }
            }
            
            attempts++;
            if (attempts > 200) {
                // If we can't find a spot, just place it (or could skip)
                // We'll accept the last random pos if it was at least on a floor
                if (worldMap[y][x] == 0) valid = true;
            }
        }

        // Place in center of cell
        sprites[i].x = FpF16<7>(x) + FpF16<7>(0.5);
        sprites[i].y = FpF16<7>(y) + FpF16<7>(0.5);
        sprites[i].texture = abs(rand()) % NUM_SPRITES;
    }
}

#ifdef RAYCAST_BENCH
// Build with -DRAYCAST_BENCH to check the multipliers, render a fixed sequence
// of frames and print the clock ticks (1/100 s) each rendering mode took.
static uint16_t benchRaycast, benchBlit;

static uint16_t benchPass(int8_t step) {
    currentStep = step;
    benchRaycast = benchBlit = 0;
    uint16_t start = ria_call_int(RIA_OP_CLOCK);
    for (uint8_t f = 0; f < ROTATION_STEPS; f++) {
        currentRotStep = (uint8_t)((currentRotStep + 1) % ROTATION_STEPS);
        updateRaycasterVectors();
        raycastF();
        benchRaycast += profileRaycastTicks;
        benchBlit += profileBlitTicks;
    }
    return (uint16_t)(ria_call_int(RIA_OP_CLOCK) - start);
}

static void benchLine(const char* name, int8_t step) {
    uint16_t total = benchPass(step);
    printf("%s %u (raycast %u, blit %u)\n", name, total, benchRaycast, benchBlit);
}

static void runBench() {
    xregn(1, 0, 0, 1, 0);
    printf("\nBENCH %ux%u, %u frames each, ticks:\n", w, h, ROTATION_STEPS);
    {
        uint16_t bad = 0;
        uint8_t a = 0;
        do {
            uint8_t b = 0;
            do {
                if (mul8x8(a, b) != (uint16_t)a * b) bad++;
            } while (++b);
        } while (++a);
        int16_t sv[] = {0, 1, -1, 127, -128, 255, -256, 300, -3000, 32767, -32768};
        for (uint8_t i = 0; i < 11; i++)
            for (uint8_t j = 0; j < 11; j++)
                if (mul16s(sv[i], sv[j]) != (int32_t)sv[i] * sv[j]) bad++;
        printf("mul check: %u bad\n", bad);
    }
    benchLine("idle  ", 1);
    benchLine("moving", 2);
    currentStep = 1;
    uint16_t t0 = ria_call_int(RIA_OP_CLOCK);
    for (uint8_t i = 0; i < 100; i++) blitBuffer();
    uint16_t t1 = ria_call_int(RIA_OP_CLOCK);
    for (uint8_t i = 0; i < 100; i++) renderSprites();
    uint16_t t2 = ria_call_int(RIA_OP_CLOCK);
    printf("x100: blit %u, sprites %u\n", t1 - t0, t2 - t1);
}
#endif

int16_t main() {
    uint8_t timer = 0;
    bool scan_key_latch = false;
    bool q_key_latch = false;
    bool e_key_latch = false;
    bool f_key_latch = false;
    bool g_key_latch = false;

    prevPlayerX = (int)(posX * FpF16<7>(TILE_SIZE));
    prevPlayerY = (int)(posY * FpF16<7>(TILE_SIZE));

    initializeMaze();

    srand(4);
    int startPosX = (random(1, ((mapWidth - 2) / 2)) * 2 + 1);
    int startPoxY = (random(1, ((mapHeight - 2) / 2)) * 2 + 1);

    iterativeDFS(startPosX, startPoxY);
    setEntryAndFinish(startPosX, startPoxY);

    posX = FpF16<7>(startPosX);
    posY = FpF16<7>(startPoxY);

    placeSprites();

    buildSquareTables();
    precalculateRotations();
    precalculateLineHeights();
    buildWallTexAverages();
    
    // Initialize active vectors
    currentRotStep = 15;
    updateRaycasterVectors(); 

    init_bitmap_graphics(0xFF00, 0x0000, 0, 2, SCREEN_WIDTH, SCREEN_HEIGHT);
    RIA.addr0 = PALETTE_XRAM_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 256; i++) {
        uint16_t color = custom_palette[i];
        RIA.rw0 = (uint8_t)(color & 0xFF); 
        RIA.rw0 = (uint8_t)(color >> 8);   
    }

    xram0_struct_set(0xFF00, vga_mode3_config_t, xram_palette_ptr, PALETTE_XRAM_ADDR);

    draw_ui();
    init_needle_sprite();
#ifdef RAYCAST_SHOT
    // RAYCAST_SHOT is the quality step (1 idle, 2 moving); RAYCAST_SHOT_ROT
    // picks the view.
    currentStep = RAYCAST_SHOT;
    currentRotStep = RAYCAST_SHOT_ROT;
    updateRaycasterVectors();
    raycastF();
    while (true) {}
#endif
#ifdef RAYCAST_BENCH
    runBench();
    return 0;
#endif
    WaitForAnyKey();

    handleCalculation();


    while (true) {
        uint16_t frame_start_clock = ria_call_int(RIA_OP_CLOCK);
        if (!scan_active) {

            xregn( 0, 0, 0, 1, KEYBOARD_INPUT);
            RIA.addr0 = KEYBOARD_INPUT;
            RIA.step0 = 1;

            for (uint8_t i = 0; i < KEYBOARD_BYTES; i++) {
                keystates[i] = RIA.rw0;
            }

            if (!(keystates[0] & 1)) {
                bool space_down = key(KEY_SPACE);
                if (space_down && !scan_key_latch) {
                    scan_active = true;
                    scan_frame = 0;
                }
                scan_key_latch = space_down;

                uint8_t rotateStep = 1;
                if (key(KEY_LEFTSHIFT) || key(KEY_RIGHTSHIFT)) {
                    rotateStep = 2;
                }

                if (key(KEY_RIGHT)){
                    gamestate = GAMESTATE_MOVING;
                    currentRotStep = (currentRotStep + rotateStep) % ROTATION_STEPS;
                    updateRaycasterVectors();
                    needleNeedsUpdate = true;
                }
                if (key(KEY_LEFT)){
                    gamestate = GAMESTATE_MOVING;
                    currentRotStep = (currentRotStep - rotateStep + ROTATION_STEPS) % ROTATION_STEPS;
                    updateRaycasterVectors();
                    needleNeedsUpdate = true;
                }
                if (key(KEY_UP)) {
                    gamestate = GAMESTATE_MOVING;
                    if(worldMap[int(posY)][int(posX + (dirX * moveSpeed) * playerScale)] == false) posX += (dirX * moveSpeed);
                    if(worldMap[int(posY + (dirY * moveSpeed) * playerScale)][int(posX)] == false) posY +=  (dirY * moveSpeed);
                }
                if (key(KEY_DOWN)) {
                    gamestate = GAMESTATE_MOVING;
                    if(worldMap[int(posY)][int(posX - (dirX * moveSpeed) * playerScale)] == false) posX -= (dirX * moveSpeed);
                    if(worldMap[int(posY - (dirY * moveSpeed) * playerScale)][int(posX)] == false) posY -= (dirY * moveSpeed);
                }
                // Strafe Right
                if (key(KEY_D)) {
                    gamestate = GAMESTATE_MOVING;
                    // Perpendicular vector (rotate right 90 deg: x' = -y, y' = x)
                    // But based on analysis: Right is (-dirY, dirX)
                    FpF16<7> strafeX = -dirY;
                    FpF16<7> strafeY = dirX;

                    if(worldMap[int(posY)][int(posX + (strafeX * moveSpeed) * playerScale)] == false) posX += (strafeX * moveSpeed);
                    if(worldMap[int(posY + (strafeY * moveSpeed) * playerScale)][int(posX)] == false) posY += (strafeY * moveSpeed);
                }
                // Strafe Left
                if (key(KEY_A)) {
                    gamestate = GAMESTATE_MOVING;
                    // Perpendicular vector (rotate left 90 deg)
                    // Left is (dirY, -dirX)
                    FpF16<7> strafeX = dirY;
                    FpF16<7> strafeY = -dirX;

                    if(worldMap[int(posY)][int(posX + (strafeX * moveSpeed) * playerScale)] == false) posX += (strafeX * moveSpeed);
                    if(worldMap[int(posY + (strafeY * moveSpeed) * playerScale)][int(posX)] == false) posY += (strafeY * moveSpeed);
                }
                bool f_down = key(KEY_F);
                if (f_down && !f_key_latch) {
                    overlayUpdatesEnabled = !overlayUpdatesEnabled;
                    if (overlayUpdatesEnabled) {
                        draw_ui();
                        needleNeedsUpdate = true;
                    }
                }
                f_key_latch = f_down;

                bool g_down = key(KEY_G);
                if (g_down && !g_key_latch) {
                    movingWallShadingEnabled = !movingWallShadingEnabled;
                }
                g_key_latch = g_down;

                bool q_down = key(KEY_Q);
                if (q_down && !q_key_latch) {
                    if (coarseSidePercent >= 5) {
                        coarseSidePercent = (uint8_t)(coarseSidePercent - 5);
                    } else {
                        coarseSidePercent = 0;
                    }
                }
                q_key_latch = q_down;

                bool e_down = key(KEY_E);
                if (e_down && !e_key_latch) {
                    if (coarseSidePercent <= 45) {
                        coarseSidePercent = (uint8_t)(coarseSidePercent + 5);
                    } else {
                        coarseSidePercent = 50;
                    }
                }
                e_key_latch = e_down;

                if (key(KEY_S)) {
                    render_sprites = !render_sprites;
                }
                if (key(KEY_ESC)) {
                    break;
                }
            }

        }
        if (!scan_active) {
            if (gamestate == GAMESTATE_MOVING) {
                currentStep = movementStep;
                timer = 0;
            } else {
                if (timer < 2) timer++;
                if (timer == 2 && currentStep > 1) {
                    currentStep = 1;
                }
            }
            handleCalculation();
        }

        if (overlayUpdatesEnabled) {
            map_visible = true;
            draw_map();
            draw_player(true);
        } else {
            map_visible = false;
            prevPlayerDotValid = false;
        }
        if (scan_active) {
            for (uint8_t y = 0; y < mapHeight; y++) {
                for (uint8_t x = 0; x < mapWidth; x++) {
                    if (scan_decay[y][x] > 0) {
                        scan_decay[y][x]--;
                    }
                }
            }
            for (uint8_t i = 0; i < numSprites; i++) {
                if (sprite_scan_decay[i] > 0) {
                    sprite_scan_decay[i]--;
                }
            }

            if (scan_frame + 1 >= SCAN_FRAMES + SCAN_DECAY_MAX) {
                scan_active = false;
                scan_frame = 0;
            } else {
                scan_frame++;
            }
        }

        uint16_t frame_end_clock = ria_call_int(RIA_OP_CLOCK);
        uint16_t frame_ticks = frame_end_clock - frame_start_clock;
        if (frame_ticks > 0) {
            fps = (uint8_t)(CLOCK_TICKS_PER_SEC / frame_ticks);
        } else {
            fps = CLOCK_TICKS_PER_SEC;
        }
    }
    return 0;
}
