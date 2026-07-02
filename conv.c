#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: %s <input> <output> <width> <height>\n", argv[0]);
        return 1;
    }
    int w, h, channels;
    unsigned char *img = stbi_load(argv[1], &w, &h, &channels, 3);
    if (!img) {
        printf("Failed to load %s\n", argv[1]);
        return 2;
    }
    
    int new_w = atoi(argv[3]);
    int new_h = atoi(argv[4]);
    unsigned char *resized = malloc(new_w * new_h * 3);
    stbir_resize_uint8_linear(img, w, h, 0, resized, new_w, new_h, 0, (stbir_pixel_layout)3);
    
    stbi_write_bmp(argv[2], new_w, new_h, 3, resized);
    
    free(img);
    free(resized);
    printf("Converted %s to %s (%dx%d)\n", argv[1], argv[2], new_w, new_h);
    return 0;
}
