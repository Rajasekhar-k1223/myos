#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

void terminal_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
void vec_init() {}
void vec_move_to() {}
void vec_line_to() {}
void vec_quad_to() {}
void vec_fill() {}

void* tar_get_file(const char* name, size_t* out_size) {
    FILE* f = fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *out_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = malloc(*out_size);
    fread(data, 1, *out_size, f);
    fclose(f);
    return data;
}

void ttf_init(uint8_t* data);
uint16_t ttf_get_glyph_index(uint16_t charcode);

int main() {
    size_t sz;
    void* data = tar_get_file("initrd/font.ttf", &sz);
    if (!data) return 1;
    ttf_init(data);
    printf("Glyph index for 'A': %d\n", ttf_get_glyph_index('A'));
    return 0;
}
