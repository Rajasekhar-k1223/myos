#include "vector.h"
#include "kheap.h"
#include "string.h"

static edge_t edges[MAX_EDGES];
static int edge_count = 0;

static float curr_x = 0;
static float curr_y = 0;

void vec_init(void) {
    edge_count = 0;
    curr_x = 0;
    curr_y = 0;
}

void vec_move_to(float x, float y) {
    curr_x = x;
    curr_y = y;
}

static void add_edge(float x0, float y0, float x1, float y1) {
    if (edge_count >= MAX_EDGES) return;
    
    // Ignore horizontal lines
    if ((int)y0 == (int)y1) return;
    
    edge_t* e = &edges[edge_count++];
    if (y0 < y1) {
        e->y_min = y0;
        e->y_max = y1;
        e->x_curr = x0;
        e->inv_slope = (x1 - x0) / (y1 - y0);
        e->direction = 1;
    } else {
        e->y_min = y1;
        e->y_max = y0;
        e->x_curr = x1;
        e->inv_slope = (x0 - x1) / (y0 - y1);
        e->direction = -1;
    }
}

void vec_line_to(float x, float y) {
    add_edge(curr_x, curr_y, x, y);
    curr_x = x;
    curr_y = y;
}

void vec_quad_to(float cx, float cy, float x, float y) {
    // Simple recursive subdivision (De Casteljau)
    // For simplicity, we just use fixed 8 steps
    int steps = 8;
    float start_x = curr_x;
    float start_y = curr_y;
    
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float u = 1.0f - t;
        
        float px = u * u * start_x + 2.0f * u * t * cx + t * t * x;
        float py = u * u * start_y + 2.0f * u * t * cy + t * t * y;
        
        vec_line_to(px, py);
    }
}

// Simple Bubble Sort for AET (since it's mostly sorted anyway)
static void sort_aet(edge_t** aet, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (aet[j]->x_curr > aet[j+1]->x_curr) {
                edge_t* temp = aet[j];
                aet[j] = aet[j+1];
                aet[j+1] = temp;
            }
        }
    }
}

void vec_fill(uint32_t* buffer, int width, int height, uint32_t color) {
    if (edge_count == 0) return;
    
    float min_y = edges[0].y_min;
    float max_y = edges[0].y_max;
    for (int i = 1; i < edge_count; i++) {
        if (edges[i].y_min < min_y) min_y = edges[i].y_min;
        if (edges[i].y_max > max_y) max_y = edges[i].y_max;
    }
    
    int start_y = (int)(min_y + 0.5f);
    int end_y = (int)(max_y + 0.5f);
    if (start_y < 0) start_y = 0;
    if (end_y >= height) end_y = height - 1;
    
    edge_t* aet[MAX_EDGES];
    int aet_count = 0;
    
    for (int y = start_y; y <= end_y; y++) {
        // 1. Add edges that start at this Y
        for (int i = 0; i < edge_count; i++) {
            if ((int)(edges[i].y_min + 0.5f) == y) {
                aet[aet_count++] = &edges[i];
            }
        }
        
        // 2. Remove edges that end at this Y
        for (int i = 0; i < aet_count; ) {
            if ((int)(aet[i]->y_max + 0.5f) <= y) {
                aet[i] = aet[--aet_count];
            } else {
                i++;
            }
        }
        
        // 3. Sort AET by X
        sort_aet(aet, aet_count);
        
        // 4. Fill pixels (Non-Zero Winding Rule)
        int winding = 0;
        int fill_start = -1;
        
        for (int i = 0; i < aet_count; i++) {
            int x_pos = (int)(aet[i]->x_curr + 0.5f);
            winding += aet[i]->direction;
            
            if (winding != 0 && fill_start == -1) {
                fill_start = x_pos;
            } else if (winding == 0 && fill_start != -1) {
                int fill_end = x_pos;
                if (fill_start < 0) fill_start = 0;
                if (fill_end >= width) fill_end = width - 1;
                
                for (int px = fill_start; px < fill_end; px++) {
                    buffer[y * width + px] = color;
                }
                fill_start = -1;
            }
            
            // 5. Update X for next scanline
            aet[i]->x_curr += aet[i]->inv_slope;
        }
    }
}
