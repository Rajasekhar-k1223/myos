#ifndef VECTOR_H
#define VECTOR_H

#include <stdint.h>

#define MAX_EDGES 4096
#define MAX_POINTS 4096

typedef struct {
    float y_min, y_max;
    float x_curr;
    float inv_slope;
    int direction; // 1 for down, -1 for up
} edge_t;

void vec_init(void);
void vec_move_to(float x, float y);
void vec_line_to(float x, float y);
void vec_quad_to(float cx, float cy, float x, float y);
void vec_fill(uint32_t* buffer, int width, int height, uint32_t color);

#endif
