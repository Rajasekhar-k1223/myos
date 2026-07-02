#include "mathf.h"

#define K_PI    3.14159265358979323846f
#define K_TWO_PI (2.0f * K_PI)
#define K_PI_2  1.57079632679489661923f

float fabsf(float x) { return x < 0.0f ? -x : x; }
double fabs(double x) { return x < 0.0 ? -x : x; }

float floorf(float x) {
    int i = (int)x;
    if (x < 0.0f && (float)i != x) i -= 1;
    return (float)i;
}

float ceilf(float x) {
    int i = (int)x;
    if (x > 0.0f && (float)i != x) i += 1;
    return (float)i;
}

/* Double-precision floor/ceil go through int truncation directly (not via
 * floorf/ceilf) so values outside float's ~7-digit precision still floor/
 * ceil correctly. */
double floor(double x) {
    long i = (long)x;
    if (x < 0.0 && (double)i != x) i -= 1;
    return (double)i;
}

double ceil(double x) {
    long i = (long)x;
    if (x > 0.0 && (double)i != x) i += 1;
    return (double)i;
}

float fmodf(float x, float y) {
    if (y == 0.0f) return 0.0f;
    float q = x / y;
    float iq = (float)(int)q; /* truncate toward zero, matches fmod's semantics */
    return x - iq * y;
}

float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; int i; } conv;
    conv.f = x;
    conv.i = 0x5f3759df - (conv.i >> 1); /* fast inverse-sqrt seed */
    float y = conv.f;
    y = y * (1.5f - 0.5f * x * y * y); /* Newton-Raphson refinement */
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y; /* x * (1/sqrt(x)) = sqrt(x) */
}

double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double y = (double)sqrtf((float)x); /* float-precision seed */
    y = 0.5 * (y + x / y);              /* double-precision Newton refinement */
    y = 0.5 * (y + x / y);
    return y;
}

/* Bhaskara I sine approximation (~0.0016 max absolute error), with range
 * reduction to [-PI, PI] first. Good enough for icon-scale SVG path math. */
float sinf(float x) {
    x = fmodf(x, K_TWO_PI);
    if (x > K_PI) x -= K_TWO_PI;
    else if (x < -K_PI) x += K_TWO_PI;

    int neg = 0;
    if (x < 0.0f) { x = -x; neg = 1; }
    float pi_minus_x = K_PI - x;
    float result = (16.0f * x * pi_minus_x) / (5.0f * K_PI * K_PI - 4.0f * x * pi_minus_x);
    return neg ? -result : result;
}

float cosf(float x) {
    return sinf(x + K_PI_2);
}

/* Double-precision cos/fmod go through the float versions -- acceptable
 * precision loss for fontstash's stb_truetype glyph-geometry math, which
 * isn't pixel-critical. */
double cos(double x) { return (double)cosf((float)x); }
double fmod(double x, double y) { return (double)fmodf((float)x, (float)y); }

float tanf(float x) {
    float c = cosf(x);
    if (fabsf(c) < 1e-6f) c = (c < 0.0f) ? -1e-6f : 1e-6f; /* avoid blowing up near +-PI/2 */
    return sinf(x) / c;
}

float roundf(float x) {
    return (x >= 0.0f) ? floorf(x + 0.5f) : ceilf(x - 0.5f);
}

/* 5th-order minimax polynomial for atan(z), |z| <= 1 (~0.0015 rad max error). */
static float atan_poly(float z) {
    float z2 = z * z;
    return z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
}

float atan2f(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return K_PI_2;
        if (y < 0.0f) return -K_PI_2;
        return 0.0f;
    }
    float z = y / x;
    if (fabsf(z) < 1.0f) {
        float a = atan_poly(z);
        if (x > 0.0f) return a;
        return (y >= 0.0f) ? a + K_PI : a - K_PI;
    } else {
        float a = atan_poly(1.0f / z);
        return (y > 0.0f) ? (K_PI_2 - a) : (-K_PI_2 - a);
    }
}

/* acos(x) = atan2(sqrt(1-x^2), x), a standard identity -- reuses the
 * already-verified atan2f/sqrtf above instead of a separate approximation. */
float acosf(float x) {
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    return atan2f(sqrtf(1.0f - x * x), x);
}

double acos(double x) { return (double)acosf((float)x); }

/* Exact for integer exponents only (exponentiation by squaring) — see
 * mathf.h: nanosvg's only call site is pow(10.0, integer). */
double pow(double base, double exp) {
    int n = (int)exp;
    int neg = n < 0;
    if (neg) n = -n;
    double result = 1.0;
    double b = base;
    while (n) {
        if (n & 1) result *= b;
        b *= b;
        n >>= 1;
    }
    return neg ? 1.0 / result : result;
}

/* Range reduction (x = n*ln2 + r, |r| <= ln2/2) + 5th-order Taylor series
 * for exp(r), then exp(x) = exp(r) * 2^n via direct IEEE-754 exponent bit
 * manipulation for the power-of-2 multiply -- standard, real technique. */
float expf(float x) {
    if (x > 80.0f) return 3.4e38f;   /* avoid overflowing float range */
    if (x < -80.0f) return 0.0f;

    const float ln2 = 0.6931471805599453f;
    int n = (int)(x / ln2 + (x >= 0.0f ? 0.5f : -0.5f));
    float r = x - (float)n * ln2;

    float y = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f/6.0f + r * (1.0f/24.0f + r * (1.0f/120.0f)))));

    union { float f; int i; } u;
    u.f = y;
    u.i += (n << 23); /* multiply by 2^n via the IEEE-754 exponent field */
    return u.f;
}

float  fminf(float a, float b) { return a < b ? a : b; }
float  fmaxf(float a, float b) { return a > b ? a : b; }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
