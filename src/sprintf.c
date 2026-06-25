#include "string.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

static void _reverse(char* s, int len) {
    for (int i = 0, j = len - 1; i < j; i++, j--) {
        char t = s[i]; s[i] = s[j]; s[j] = t;
    }
}

/* Write unsigned n in base into buf. Returns char count (no NUL). */
static int _uint_to_buf(unsigned int n, char* buf, int base, int upper) {
    const char* lo = "0123456789abcdef";
    const char* hi = "0123456789ABCDEF";
    const char* d  = upper ? hi : lo;
    if (n == 0) { buf[0] = '0'; return 1; }
    int i = 0;
    while (n) { buf[i++] = d[n % base]; n /= base; }
    _reverse(buf, i);
    return i;
}

/* ── vsnprintf ──────────────────────────────────────────────────────────── */

int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    size_t pos = 0;

#define PUT(c) do { if (size && pos + 1 < size) buf[pos] = (c); pos++; } while (0)

    while (*fmt) {
        if (*fmt != '%') { PUT(*fmt++); continue; }
        fmt++; /* skip '%' */

        /* ── flags ── */
        int left  = 0;
        int zero  = 0;
        int plus  = 0;
        int space = 0;
        int alt   = 0;
        for (;;) {
            if (*fmt == '-')      { left  = 1; fmt++; }
            else if (*fmt == '0') { zero  = 1; fmt++; }
            else if (*fmt == '+') { plus  = 1; fmt++; }
            else if (*fmt == ' ') { space = 1; fmt++; }
            else if (*fmt == '#') { alt   = 1; fmt++; }
            else break;
        }
        (void)plus; (void)space; (void)alt;

        /* ── width ── */
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        /* ── precision ── */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }

        /* ── conversion ── */
        char  tmp[32];
        int   tlen = 0;
        int   neg  = 0;
        char  pad  = (zero && !left) ? '0' : ' ';

        switch (*fmt) {

        case 'd': case 'i': {
            int v = va_arg(ap, int);
            unsigned int u = (v < 0) ? (neg = 1, (unsigned int)(-v)) : (unsigned int)v;
            tlen = _uint_to_buf(u, tmp, 10, 0);
            if (neg) { memmove(tmp + 1, tmp, tlen + 1); tmp[0] = '-'; tlen++; }
            break;
        }
        case 'u':
            tlen = _uint_to_buf(va_arg(ap, unsigned int), tmp, 10, 0);
            break;
        case 'o':
            tlen = _uint_to_buf(va_arg(ap, unsigned int), tmp, 8, 0);
            break;
        case 'x':
            tlen = _uint_to_buf(va_arg(ap, unsigned int), tmp, 16, 0);
            break;
        case 'X':
            tlen = _uint_to_buf(va_arg(ap, unsigned int), tmp, 16, 1);
            break;
        case 'p': {
            unsigned int v = va_arg(ap, unsigned int);
            tmp[0] = '0'; tmp[1] = 'x';
            tlen = 2 + _uint_to_buf(v, tmp + 2, 16, 0);
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            tlen = 1;
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            tlen = (int)strlen(s);
            if (prec >= 0 && tlen > prec) tlen = prec;
            int pad_n = (width > tlen) ? width - tlen : 0;
            if (!left) for (int i = 0; i < pad_n; i++) PUT(' ');
            for (int i = 0; i < tlen; i++) PUT(s[i]);
            if ( left) for (int i = 0; i < pad_n; i++) PUT(' ');
            fmt++;
            continue;
        }
        case '%':
            PUT('%');
            fmt++;
            continue;
        default:
            PUT('%'); PUT(*fmt);
            fmt++;
            continue;
        }

        tmp[tlen] = '\0';
        int pad_n = (width > tlen) ? width - tlen : 0;
        if (!left) for (int i = 0; i < pad_n; i++) PUT(pad);
        for (int i = 0; i < tlen; i++) PUT(tmp[i]);
        if ( left) for (int i = 0; i < pad_n; i++) PUT(' ');
        fmt++;
    }

    if (size) buf[(pos < size) ? pos : size - 1] = '\0';
    return (int)pos;

#undef PUT
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int snprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return r;
}
