#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define GPU_VENDOR_INTEL 1
#define GPU_VENDOR_AMD   2
#define GPU_VENDOR_NVIDIA 3

typedef struct {
    int vendor_id;
    int is_hardware_accelerated;
} gpu_device_t;

void gpu_init(void);
void gpu_draw_rect(int x, int y, int w, int h, uint32_t color32);
void gpu_execute_shader(const char* shader_code);

#endif
