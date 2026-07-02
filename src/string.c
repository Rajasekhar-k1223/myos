#include "string.h"
#include <stdint.h>

/* ── Memory ─────────────────────────────────────────────────────────────── */

void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
    uint32_t* d32 = (uint32_t*)dest;
    const uint32_t* s32 = (const uint32_t*)src;
    size_t n32 = n / 4;
    for (size_t i = 0; i < n32; i++) d32[i] = s32[i];
    
    uint8_t* d8 = (uint8_t*)dest + n32 * 4;
    const uint8_t* s8 = (const uint8_t*)src + n32 * 4;
    size_t n8 = n % 4;
    for (size_t i = 0; i < n8; i++) d8[i] = s8[i];
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s)
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    else
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint32_t c32 = ((uint32_t)(uint8_t)c * 0x01010101);
    uint32_t* p32 = (uint32_t*)s;
    size_t n32 = n / 4;
    for (size_t i = 0; i < n32; i++) p32[i] = c32;
    
    uint8_t* p8 = (uint8_t*)s + n32 * 4;
    size_t n8 = n % 4;
    for (size_t i = 0; i < n8; i++) p8[i] = (uint8_t)c;
    return s;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* p = (const uint8_t*)a;
    const uint8_t* q = (const uint8_t*)b;
    for (size_t i = 0; i < n; i++)
        if (p[i] != q[i]) return p[i] - q[i];
    return 0;
}

/* ── String inspection ──────────────────────────────────────────────────── */

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n--) {
        if (*s1 != *s2) return *(const unsigned char*)s1 - *(const unsigned char*)s2;
        if (*s1 == '\0') return 0;
        s1++; s2++;
    }
    return 0;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (const char* p = haystack; *p; p++) {
        if (strncmp(p, needle, strlen(needle)) == 0) return (char*)p;
    }
    return 0;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char*)s;
    return (char*)last;
}

/* ── String building ────────────────────────────────────────────────────── */

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dest;
}

/* ── Conversion ─────────────────────────────────────────────────────────── */

long strtol(const char* str, char** endptr, int base) {
    while (*str == ' ' || *str == '\t') str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    if (base == 0) {
        if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) { base = 16; str += 2; }
        else if (*str == '0') { base = 8; str++; }
        else base = 10;
    } else if (base == 16 && *str == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    long result = 0;
    while (*str) {
        int d;
        if (*str >= '0' && *str <= '9') d = *str - '0';
        else if (*str >= 'a' && *str <= 'z') d = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') d = *str - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return result * sign;
}

long long strtoll(const char* str, char** endptr, int base) {
    while (*str == ' ' || *str == '\t') str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') str++;
    if (base == 0) {
        if (*str == '0' && (str[1] == 'x' || str[1] == 'X')) { base = 16; str += 2; }
        else if (*str == '0') { base = 8; str++; }
        else base = 10;
    } else if (base == 16 && *str == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    long long result = 0;
    while (*str) {
        int d;
        if (*str >= '0' && *str <= '9') d = *str - '0';
        else if (*str >= 'a' && *str <= 'z') d = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') d = *str - 'A' + 10;
        else break;
        if (d >= base) break;
        result = result * base + d;
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return result * sign;
}

/* Writes number n in given base into buf. Returns buf. */
char* utoa(unsigned int n, char* buf, int base) {
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    while (n > 0) { tmp[i++] = digits[n % base]; n /= base; }
    int j;
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[j] = '\0';
    return buf;
}

char* itoa(int n, char* buf, int base) {
    if (base == 10 && n < 0) {
        buf[0] = '-';
        utoa((unsigned int)(-n), buf + 1, base);
    } else {
        utoa((unsigned int)n, buf, base);
    }
    return buf;
}

/* ── Sorting ────────────────────────────────────────────────────────────── */

static void qsort_swap(uint8_t* a, uint8_t* b, size_t size) {
    while (size--) {
        uint8_t t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static void qsort_rec(uint8_t* base, size_t lo, size_t hi, size_t size,
                       int (*compar)(const void*, const void*)) {
    while (lo < hi) {
        /* Median-of-three pivot to avoid O(n^2) worst case on already-sorted
         * (or nearly-sorted) input, which scanline edge lists often are. */
        size_t mid = lo + (hi - lo) / 2;
        if (compar(base + mid * size, base + lo * size) < 0) qsort_swap(base + mid * size, base + lo * size, size);
        if (compar(base + hi * size, base + lo * size) < 0)  qsort_swap(base + hi  * size, base + lo * size, size);
        if (compar(base + hi * size, base + mid * size) < 0) qsort_swap(base + hi  * size, base + mid * size, size);
        qsort_swap(base + mid * size, base + hi * size, size); /* pivot to end */

        uint8_t* pivot = base + hi * size;
        size_t store = lo;
        for (size_t i = lo; i < hi; i++) {
            if (compar(base + i * size, pivot) < 0) {
                qsort_swap(base + i * size, base + store * size, size);
                store++;
            }
        }
        qsort_swap(base + store * size, pivot, size);

        /* Recurse into the smaller half, loop on the larger to bound stack depth. */
        if (store > lo && store - lo < hi - store) {
            if (store > 0) qsort_rec(base, lo, store - 1, size, compar);
            lo = store + 1;
        } else {
            if (store + 1 < hi) qsort_rec(base, store + 1, hi, size, compar);
            if (store == 0) break;
            hi = store - 1;
        }
    }
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*)) {
    if (nmemb < 2 || size == 0) return;
    qsort_rec((uint8_t*)base, 0, nmemb - 1, size, compar);
}
