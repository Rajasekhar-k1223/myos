#pragma once

/*
 * Minimal real (non-stubbed) libm replacement for this freestanding kernel.
 * -nostdlib means there's no libm to link against, so anything that needs
 * actual trig/sqrt has to bring its own. These use the standard libm names
 * (not a k_/custom_ prefix) because src/nanosvg.h and src/nanosvgrast.h
 * call sqrtf/sinf/cosf/etc. directly with no override-macro hook — unlike
 * stb_truetype.h/stb_image.h, which were written to be portable via
 * STBTT_ and STBI_ macros. The double-precision overloads (floor/ceil/cos/
 * acos/fmod) exist because fontstash.h's *own* bundled stb_truetype.h
 * instance (src/fontstash.h, used by src/nvg_backend.c) falls back to
 * those exact double-precision names when nothing overrides STBTT_ifloor/
 * STBTT_cos/etc, unlike src/ttf.c's instance which overrides them all.
 *
 * pow() here is exact only for integer exponents (exponentiation by
 * squaring) — that covers nanosvg's only use (decimal-scaling in its
 * number parser, always pow(10.0, integer)), NOT a general real-valued pow.
 */

float  sqrtf(float x);
double sqrt(double x);
float  fabsf(float x);
double fabs(double x);
float  floorf(float x);
double floor(double x);
float  ceilf(float x);
double ceil(double x);
float  roundf(float x);
float  sinf(float x);
float  cosf(float x);
double cos(double x);
float  tanf(float x);
float  acosf(float x);
double acos(double x);
float  atan2f(float y, float x);
float  fmodf(float x, float y);
double fmod(double x, double y);
double pow(double base, double exp);
float  expf(float x);
float  fminf(float a, float b);
float  fmaxf(float a, float b);
double fmin(double a, double b);
double fmax(double a, double b);
