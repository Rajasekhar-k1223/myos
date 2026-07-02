#ifndef GPU_H
#define GPU_H
#include <stdint.h>
typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  bus, dev, fn;
    uint32_t vram_base;
    uint32_t vram_size;
    int      is_hardware_accelerated;
    char     name[64];
} gpu_device_t;
void gpu_init(void);
void gpu_draw_rect(int x, int y, int w, int h, uint32_t color32);
void gpu_execute_shader(const char* shader_code);
#endif
