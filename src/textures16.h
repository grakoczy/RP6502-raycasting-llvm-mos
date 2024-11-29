#ifndef TEXTURE_DATA_H
#define TEXTURE_DATA_H

#include <stdint.h>

#define texWidth 16
#define texHeight 16
#define NUM_TEXTURES 8
uint8_t texture[NUM_TEXTURES][texWidth * texHeight] = {
    {  // Texture greystone16x16.png
        241, 59, 243, 249, 249, 249, 246, 245, 247, 246, 59, 243, 245, 243, 241, 241, 
        249, 245, 102, 145, 247, 247, 243, 243, 247, 246, 238, 248, 248, 248, 248, 248, 
        8, 239, 241, 145, 245, 246, 242, 237, 239, 241, 239, 246, 245, 102, 8, 8, 
        237, 237, 238, 248, 246, 246, 242, 59, 250, 248, 241, 239, 238, 238, 237, 237, 
        248, 247, 239, 245, 245, 8, 241, 241, 247, 8, 239, 248, 243, 250, 249, 249, 
        246, 245, 238, 8, 242, 238, 240, 8, 246, 8, 238, 249, 242, 248, 246, 245, 
        8, 242, 237, 145, 247, 59, 250, 248, 247, 246, 59, 249, 239, 242, 241, 242, 
        102, 247, 243, 145, 246, 239, 249, 246, 245, 102, 240, 145, 241, 245, 8, 240, 
        145, 247, 8, 249, 247, 239, 145, 247, 245, 8, 240, 248, 8, 247, 247, 242, 
        248, 247, 243, 145, 246, 239, 243, 243, 59, 238, 238, 240, 240, 241, 59, 239, 
        248, 247, 242, 248, 246, 239, 242, 248, 248, 248, 249, 7, 7, 250, 249, 242, 
        246, 8, 238, 246, 246, 239, 242, 250, 247, 247, 248, 246, 246, 246, 247, 239, 
        241, 241, 238, 238, 238, 236, 239, 246, 8, 245, 245, 245, 245, 245, 102, 239, 
        248, 249, 249, 248, 248, 247, 246, 246, 239, 238, 238, 238, 237, 237, 237, 238, 
        245, 8, 8, 8, 243, 243, 8, 8, 237, 248, 248, 243, 248, 247, 246, 242, 
        238, 237, 237, 236, 236, 236, 236, 236, 236, 238, 238, 240, 241, 241, 241, 238
    },
    {  // Texture greystone16x16dark.png
        239, 238, 240, 245, 245, 102, 242, 241, 243, 242, 238, 240, 242, 240, 238, 238, 
        102, 242, 241, 8, 243, 243, 240, 240, 243, 242, 236, 8, 8, 8, 8, 8, 
        59, 237, 239, 8, 242, 242, 239, 236, 237, 239, 237, 242, 242, 241, 59, 59, 
        236, 235, 236, 8, 242, 242, 239, 238, 245, 8, 239, 237, 236, 236, 236, 235, 
        8, 243, 237, 242, 242, 241, 238, 239, 243, 59, 237, 8, 240, 245, 102, 102, 
        242, 242, 236, 59, 239, 236, 238, 59, 242, 59, 236, 102, 239, 8, 242, 241, 
        59, 239, 236, 102, 243, 238, 245, 8, 243, 242, 238, 102, 237, 239, 238, 240, 
        241, 243, 240, 8, 242, 237, 102, 242, 241, 241, 238, 8, 239, 242, 241, 238, 
        8, 243, 59, 102, 243, 237, 8, 243, 242, 59, 238, 8, 59, 243, 243, 239, 
        8, 243, 240, 8, 242, 237, 240, 240, 238, 236, 236, 238, 238, 239, 238, 237, 
        8, 243, 239, 8, 242, 237, 239, 8, 8, 8, 102, 246, 245, 245, 102, 239, 
        242, 59, 237, 243, 242, 237, 240, 245, 243, 243, 8, 243, 243, 242, 243, 237, 
        238, 239, 236, 236, 236, 235, 237, 242, 59, 242, 241, 242, 242, 241, 241, 237, 
        8, 102, 102, 8, 8, 243, 242, 242, 237, 236, 236, 236, 236, 236, 236, 236, 
        242, 59, 59, 59, 240, 240, 59, 59, 236, 8, 8, 240, 8, 243, 242, 239, 
        236, 235, 235, 235, 235, 235, 235, 235, 235, 237, 236, 238, 239, 239, 238, 237
    },
    {  // Texture redbrick16x16.png
        95, 88, 88, 239, 95, 88, 95, 95, 88, 238, 95, 95, 88, 88, 88, 238, 
        88, 88, 88, 238, 124, 88, 88, 88, 88, 238, 88, 88, 88, 88, 88, 52, 
        238, 52, 52, 237, 238, 52, 52, 52, 52, 238, 238, 238, 52, 237, 52, 237, 
        124, 88, 88, 88, 239, 124, 88, 88, 88, 88, 88, 88, 88, 238, 124, 124, 
        1, 1, 1, 52, 238, 88, 1, 1, 1, 1, 1, 1, 1, 237, 88, 1, 
        95, 88, 88, 88, 88, 88, 1, 239, 88, 88, 88, 1, 1, 88, 88, 238, 
        88, 88, 88, 88, 88, 88, 1, 95, 124, 88, 88, 88, 88, 88, 88, 52, 
        239, 52, 52, 237, 52, 52, 52, 238, 1, 52, 52, 52, 52, 52, 52, 237, 
        88, 124, 1, 95, 88, 88, 88, 88, 88, 88, 95, 124, 88, 88, 88, 88, 
        1, 1, 52, 239, 1, 1, 1, 1, 1, 1, 239, 88, 88, 88, 1, 1, 
        95, 88, 1, 1, 1, 1, 1, 1, 239, 88, 95, 88, 95, 1, 1, 238, 
        88, 88, 88, 88, 88, 88, 88, 1, 238, 88, 1, 88, 88, 88, 88, 52, 
        238, 52, 52, 238, 237, 52, 52, 52, 238, 1, 52, 237, 237, 52, 52, 237, 
        88, 88, 88, 88, 238, 88, 88, 88, 88, 88, 88, 52, 95, 88, 88, 88, 
        1, 1, 1, 1, 237, 88, 1, 1, 1, 1, 1, 52, 1, 1, 1, 1, 
        238, 238, 238, 238, 238, 239, 238, 238, 238, 238, 237, 237, 239, 238, 238, 238
    },
    {  // Texture redbrick16x16dark.png
        52, 52, 52, 237, 52, 52, 52, 52, 52, 236, 52, 52, 52, 52, 52, 236, 
        1, 52, 52, 236, 1, 52, 52, 52, 52, 236, 52, 52, 52, 52, 52, 52, 
        236, 52, 52, 236, 236, 52, 52, 52, 235, 236, 236, 236, 236, 235, 52, 235, 
        1, 52, 52, 52, 237, 1, 52, 52, 52, 52, 52, 52, 52, 236, 1, 1, 
        52, 52, 52, 52, 237, 52, 52, 52, 52, 52, 52, 52, 52, 236, 52, 52, 
        52, 52, 52, 52, 52, 52, 52, 237, 52, 52, 52, 52, 52, 52, 52, 236, 
        52, 52, 52, 52, 52, 52, 52, 237, 1, 52, 52, 52, 52, 52, 52, 52, 
        237, 52, 52, 236, 236, 52, 235, 237, 52, 52, 235, 52, 52, 52, 52, 235, 
        1, 1, 52, 237, 52, 52, 52, 52, 52, 52, 237, 1, 52, 52, 52, 52, 
        52, 52, 235, 237, 52, 52, 52, 52, 52, 52, 237, 52, 52, 52, 52, 52, 
        52, 52, 52, 52, 52, 52, 52, 52, 237, 52, 52, 52, 237, 52, 52, 236, 
        52, 52, 52, 52, 52, 52, 52, 52, 236, 52, 52, 52, 52, 52, 52, 52, 
        237, 52, 52, 236, 235, 52, 52, 52, 236, 52, 52, 236, 236, 52, 52, 235, 
        52, 52, 52, 52, 236, 1, 52, 52, 52, 52, 52, 52, 237, 52, 52, 52, 
        52, 52, 52, 52, 235, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 52, 
        236, 236, 236, 236, 236, 237, 236, 236, 236, 236, 236, 236, 237, 236, 236, 236
    },
    {  // Texture bluestone16x16.png
        18, 19, 18, 18, 18, 17, 19, 19, 18, 19, 19, 19, 19, 4, 19, 18, 
        18, 19, 18, 18, 17, 233, 19, 19, 18, 18, 4, 17, 17, 4, 19, 17, 
        4, 18, 18, 17, 17, 233, 19, 19, 19, 18, 17, 18, 4, 4, 19, 17, 
        17, 17, 17, 17, 17, 233, 20, 19, 19, 4, 17, 18, 4, 17, 17, 233, 
        19, 19, 18, 4, 19, 18, 19, 19, 18, 17, 233, 4, 17, 17, 18, 20, 
        19, 19, 4, 17, 19, 17, 4, 4, 17, 17, 17, 17, 4, 18, 4, 19, 
        19, 19, 4, 17, 4, 17, 19, 20, 20, 19, 19, 17, 19, 19, 4, 19, 
        19, 18, 17, 17, 4, 4, 19, 19, 19, 18, 17, 17, 19, 19, 17, 19, 
        18, 4, 17, 17, 18, 17, 19, 19, 4, 17, 17, 233, 19, 18, 17, 18, 
        4, 17, 17, 233, 18, 17, 17, 4, 17, 17, 17, 17, 19, 18, 17, 18, 
        17, 18, 18, 4, 18, 17, 19, 20, 19, 18, 18, 4, 18, 4, 17, 17, 
        19, 20, 19, 19, 17, 17, 19, 20, 19, 18, 17, 17, 17, 17, 17, 17, 
        19, 20, 19, 18, 19, 4, 19, 19, 20, 18, 17, 17, 19, 20, 19, 18, 
        19, 20, 19, 17, 19, 4, 19, 19, 18, 4, 4, 233, 19, 19, 18, 17, 
        19, 18, 18, 17, 19, 17, 19, 19, 18, 17, 17, 233, 19, 19, 18, 17, 
        4, 17, 17, 17, 4, 17, 18, 18, 17, 17, 17, 17, 18, 4, 17, 17
    },
    {  // Texture bluestone16x16dark.png
        17, 4, 4, 4, 17, 17, 18, 18, 4, 4, 4, 4, 4, 17, 18, 17, 
        17, 18, 4, 4, 17, 232, 4, 4, 4, 4, 17, 17, 17, 17, 18, 17, 
        17, 4, 17, 17, 17, 232, 18, 18, 18, 17, 17, 17, 17, 17, 4, 17, 
        17, 17, 17, 233, 233, 232, 18, 4, 4, 17, 233, 17, 17, 17, 17, 232, 
        18, 18, 4, 17, 18, 17, 18, 4, 17, 17, 232, 17, 17, 17, 17, 18, 
        4, 4, 17, 17, 4, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 18, 
        4, 18, 17, 17, 17, 233, 4, 19, 19, 18, 4, 17, 18, 18, 17, 18, 
        4, 4, 17, 233, 17, 17, 4, 18, 18, 4, 17, 17, 18, 4, 17, 18, 
        4, 17, 17, 233, 4, 17, 4, 4, 17, 17, 17, 232, 18, 4, 17, 4, 
        17, 17, 233, 232, 4, 17, 17, 17, 17, 17, 17, 233, 4, 17, 17, 4, 
        17, 4, 4, 17, 17, 17, 18, 19, 18, 4, 4, 17, 4, 17, 233, 17, 
        18, 19, 18, 4, 17, 17, 4, 19, 18, 17, 17, 17, 17, 233, 233, 17, 
        18, 18, 4, 17, 4, 17, 4, 18, 19, 4, 17, 233, 4, 19, 18, 17, 
        18, 19, 4, 17, 4, 17, 4, 4, 17, 17, 17, 233, 4, 18, 17, 17, 
        4, 4, 17, 17, 4, 17, 4, 18, 17, 17, 17, 232, 4, 18, 17, 17, 
        17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17
    },
    {  // Texture colorstone16x16.png
        242, 239, 237, 237, 237, 237, 238, 239, 240, 59, 239, 239, 59, 240, 240, 241, 
        240, 59, 238, 237, 238, 238, 240, 239, 237, 237, 242, 59, 239, 239, 239, 239, 
        239, 59, 240, 239, 59, 240, 238, 236, 236, 236, 241, 240, 239, 239, 239, 239, 
        239, 241, 240, 242, 59, 237, 236, 236, 236, 237, 242, 239, 239, 239, 239, 238, 
        239, 240, 239, 239, 239, 236, 236, 236, 236, 238, 240, 239, 239, 239, 238, 238, 
        237, 238, 240, 239, 240, 237, 236, 237, 239, 240, 241, 242, 239, 238, 238, 240, 
        236, 237, 241, 240, 241, 239, 238, 240, 239, 237, 239, 242, 59, 240, 59, 238, 
        236, 236, 59, 242, 239, 240, 241, 239, 238, 237, 239, 240, 240, 241, 239, 237, 
        236, 238, 59, 240, 241, 242, 239, 238, 237, 237, 240, 238, 238, 238, 238, 236, 
        239, 242, 59, 240, 240, 243, 239, 238, 237, 238, 240, 58, 237, 238, 240, 238, 
        242, 241, 240, 240, 239, 240, 239, 238, 237, 240, 239, 58, 237, 238, 239, 238, 
        240, 240, 240, 240, 239, 239, 59, 240, 238, 241, 239, 238, 237, 238, 238, 236, 
        242, 241, 240, 239, 239, 238, 238, 238, 240, 238, 238, 240, 238, 239, 240, 239, 
        238, 239, 239, 239, 239, 238, 236, 236, 238, 236, 235, 238, 241, 59, 239, 240, 
        237, 237, 238, 240, 238, 237, 236, 238, 237, 235, 235, 235, 238, 240, 239, 239, 
        237, 238, 240, 240, 238, 236, 237, 238, 236, 235, 235, 235, 235, 239, 240, 238
    },
    {  // Texture colorstone16x16dark.png
        239, 237, 236, 236, 235, 235, 236, 237, 238, 238, 236, 237, 238, 237, 237, 238, 
        237, 238, 236, 235, 236, 236, 238, 236, 235, 235, 239, 238, 237, 237, 237, 237, 
        237, 238, 237, 237, 238, 237, 236, 235, 235, 235, 238, 237, 236, 237, 236, 237, 
        237, 238, 237, 238, 238, 235, 235, 235, 235, 235, 239, 237, 237, 237, 236, 236, 
        237, 238, 237, 237, 237, 235, 235, 235, 235, 236, 238, 237, 236, 236, 236, 236, 
        235, 236, 238, 237, 237, 235, 235, 235, 236, 237, 238, 239, 236, 236, 236, 237, 
        235, 235, 238, 237, 238, 236, 236, 237, 236, 236, 237, 239, 238, 238, 238, 236, 
        235, 235, 238, 239, 237, 237, 238, 237, 236, 235, 236, 238, 237, 238, 237, 235, 
        235, 236, 238, 238, 238, 239, 237, 236, 236, 235, 237, 236, 236, 236, 236, 235, 
        237, 238, 238, 237, 237, 239, 237, 236, 236, 236, 238, 236, 236, 236, 237, 236, 
        239, 238, 237, 237, 237, 237, 237, 236, 236, 237, 237, 236, 236, 236, 237, 236, 
        237, 237, 237, 237, 237, 236, 238, 237, 236, 238, 237, 236, 236, 236, 236, 234, 
        239, 238, 237, 237, 236, 236, 236, 236, 238, 236, 236, 237, 236, 237, 237, 237, 
        236, 237, 236, 236, 237, 236, 235, 235, 236, 235, 234, 236, 238, 238, 237, 238, 
        236, 235, 236, 237, 236, 235, 235, 236, 235, 234, 234, 234, 236, 237, 237, 236, 
        236, 236, 237, 238, 236, 235, 235, 236, 235, 234, 234, 234, 234, 236, 237, 236
    },
};

#endif // TEXTURE_DATA_H
