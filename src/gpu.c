#include "gpu.h"
#include "kernel.h"
#include "vesa.h"

static gpu_device_t current_gpu;

void gpu_init(void) {
    terminal_printf("[GPU] Hardware Abstraction Layer initialized.\n");
    /* Mock fallback to VESA software rendering */
    current_gpu.vendor_id = 0;
    current_gpu.is_hardware_accelerated = 0;
}

void gpu_draw_rect(int x, int y, int w, int h, uint32_t color32) {
    if (current_gpu.is_hardware_accelerated) {
        /* In reality, we'd write to the GPU command ring buffer here */
    } else {
        /* Fallback to CPU VESA drawing */
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                vesa_putpixel(x + j, y + i, color32);
            }
        }
    }
}

void gpu_execute_shader(const char* shader_code) {
    if (current_gpu.is_hardware_accelerated) {
        terminal_printf("[GPU] Executing hardware shader...\n");
    } else {
        terminal_printf("[GPU] WARNING: Software shader execution not supported.\n");
    }
}
