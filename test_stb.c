#include <stdint.h>
void* kmalloc(uint32_t size) { return 0; }
void kfree(void* ptr) {}
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc(x,u)  ((void)(u),kmalloc(x))
#define STBTT_free(x,u)    ((void)(u),kfree(x))
#include "stb_truetype.h"
int main() { return 0; }
