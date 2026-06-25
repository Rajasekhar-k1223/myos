#include "string.h"
#include <stdint.h>

/* ── Memory ─────────────────────────────────────────────────────────────── */

void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
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
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
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

char* strstr(const char* hay, const char* needle) {
    if (!*needle) return (char*)hay;
    for (; *hay; hay++) {
        const char* h = hay, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)hay;
    }
    return 0;
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
