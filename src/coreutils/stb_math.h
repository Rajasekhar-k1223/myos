#pragma once
#include <stddef.h>
#include <stdint.h>

/* Basic memory and string operations from our libc */
extern void* malloc(unsigned int size);
extern void free(void* ptr);
extern void* memcpy(void* dest, const void* src, unsigned int n);
extern void* memset(void* s, int c, unsigned int n);
extern int strlen(const char* s);

#define STBTT_malloc(x,u)  ((void)(u), malloc(x))
#define STBTT_free(x,u)    ((void)(u), free(x))
#define STBTT_assert(x)    do{}while(0)
#define STBTT_strlen(x)    strlen(x)
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset

/* Basic Math Functions for stb_truetype */
static inline int my_ifloor(float x) {
    int i = (int)x;
    return (x < 0 && x != (float)i) ? i - 1 : i;
}

static inline int my_iceil(float x) {
    int i = (int)x;
    return (x > 0 && x != (float)i) ? i + 1 : i;
}

static inline float my_fabs(float x) {
    return (x < 0) ? -x : x;
}

static inline float my_sqrt(float x) {
    if (x <= 0) return 0;
    float z = x;
    for (int i = 0; i < 10; i++) {
        z = z - (z * z - x) / (2 * z);
    }
    return z;
}

static inline float my_pow(float x, float y) {
    if (y == 2.0f) return x * x;
    if (y == 0.0f) return 1.0f;
    return x;
}

static inline float my_fmod(float x, float y) {
    if (y == 0) return 0;
    int quotient = (int)(x / y);
    return x - quotient * y;
}

static inline float my_cos(float x) {
    float x2 = x * x;
    return 1.0f - (x2 / 2.0f) + ((x2 * x2) / 24.0f) - ((x2 * x2 * x2) / 720.0f);
}

static inline float my_acos(float x) {
    if (x <= -1.0f) return 3.14159265f;
    if (x >= 1.0f) return 0.0f;
    return 1.57079632f - x;
}

#define STBTT_ifloor(x)   my_ifloor(x)
#define STBTT_iceil(x)    my_iceil(x)
#define STBTT_fabs(x)     my_fabs(x)
#define STBTT_sqrt(x)     my_sqrt(x)
#define STBTT_pow(x,y)    my_pow(x,y)
#define STBTT_fmod(x,y)   my_fmod(x,y)
#define STBTT_cos(x)      my_cos(x)
#define STBTT_acos(x)     my_acos(x)
