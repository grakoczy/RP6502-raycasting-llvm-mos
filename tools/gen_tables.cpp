#include <stdio.h>
#include <stdint.h>
#include <math.h>

#define ROTATION_STEPS 32
#define WINDOW_WIDTH 96

// Helper to convert double to FpF16<7> raw format
int16_t to_fp_raw(double val) {
    return (int16_t)(val * 128.0);
}

int main() {
    FILE* f = fopen("tables.bin", "wb");
    
    if (!f) {
        printf("Error: Could not open tables.bin for writing\n");
        return 1;
    }

    for (int i = 0; i < ROTATION_STEPS; i++) {
        // Calculate absolute angle for this step to avoid cumulative errors
        double angle = (double)i * (2.0 * M_PI / (double)ROTATION_STEPS);
        
        // Player vectors (starting looking Up: 0, -1)
        double pDirX = sin(angle);
        double pDirY = -cos(angle);
        double pPlaneX = -pDirY * 0.66; 
        double pPlaneY = pDirX * 0.66;

        int16_t step_vectors[4];
        step_vectors[0] = to_fp_raw(pDirX);
        step_vectors[1] = to_fp_raw(pDirY);
        step_vectors[2] = to_fp_raw(pPlaneX);
        step_vectors[3] = to_fp_raw(pPlaneY);

        // Debug output
        if (i < 10) {
            printf("Generated Step %d: dirX=%d dirY=%d planeX=%d planeY=%d\n",
                   i, step_vectors[0], step_vectors[1], step_vectors[2], step_vectors[3]);
        }

        int16_t row_rayDirX[WINDOW_WIDTH];
        int16_t row_rayDirY[WINDOW_WIDTH];
        int16_t row_deltaDistX[WINDOW_WIDTH];
        int16_t row_deltaDistY[WINDOW_WIDTH];

        for (int x = 0; x < WINDOW_WIDTH; x++) {
            double cameraX = 2.0 * (double)x / (double)WINDOW_WIDTH - 1.0;
            double rayDirX = pDirX + pPlaneX * cameraX;
            double rayDirY = pDirY + pPlaneY * cameraX;

            // Delta distance with clamping to prevent overflow
            double dDX = (fabs(rayDirX) < 0.0001) ? 32767.0 : fabs(1.0 / rayDirX);
            double dDY = (fabs(rayDirY) < 0.0001) ? 32767.0 : fabs(1.0 / rayDirY);
            
            // Clamp to prevent overflow in int16_t
            if (dDX > 255.0) dDX = 255.0;
            if (dDY > 255.0) dDY = 255.0;

            row_rayDirX[x] = to_fp_raw(rayDirX);
            row_rayDirY[x] = to_fp_raw(rayDirY);
            row_deltaDistX[x] = to_fp_raw(dDX);
            row_deltaDistY[x] = to_fp_raw(dDY);
        }

        // Write the 4 Ray Tables (768 bytes total)
        size_t written = 0;
        written += fwrite(row_rayDirX, 2, WINDOW_WIDTH, f);
        written += fwrite(row_rayDirY, 2, WINDOW_WIDTH, f);
        written += fwrite(row_deltaDistX, 2, WINDOW_WIDTH, f);
        written += fwrite(row_deltaDistY, 2, WINDOW_WIDTH, f);
        
        // Write the player vectors for THIS step (8 bytes)
        written += fwrite(step_vectors, 2, 4, f);
        
        if (written != (WINDOW_WIDTH * 4 + 4)) {
            printf("Warning: Step %d wrote %zu elements instead of %d\n", 
                   i, written, WINDOW_WIDTH * 4 + 4);
        }
    }

    long file_size = ftell(f);
    printf("\nTotal file size: %ld bytes (expected: %d bytes)\n", 
           file_size, ROTATION_STEPS * (WINDOW_WIDTH * 2 * 4 + 8));

    fclose(f);
    return 0;
}