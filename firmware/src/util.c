#include "util.h"
#include "hal.h"

void u_memcpy(void *d, const void *s, size_t n) {
    uint8_t *dp = d; const uint8_t *sp = s;
    while (n--) *dp++ = *sp++;
}
void u_memset(void *d, int v, size_t n) {
    uint8_t *dp = d;
    while (n--) *dp++ = (uint8_t)v;
}
int u_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}
size_t u_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int u_strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b) return (uint8_t)*a - (uint8_t)*b;
        if (!*a) return 0;
    }
    return 0;
}

static void emit(char *out, size_t n, size_t *pos, char c) {
    if (*pos + 1 < n) out[*pos] = c;
    (*pos)++;
}

int u_vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(out, n, &pos, *fmt); continue; }
        fmt++;
        char pad = ' '; int width = 0, lng = 0;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng++; fmt++; }
        char buf[24]; int len = 0, neg = 0;
        unsigned long long v = 0;
        switch (*fmt) {
        case 'd': {
            long long s = lng >= 2 ? va_arg(ap, long long) : lng ? va_arg(ap, long) : va_arg(ap, int);
            if (s < 0) { neg = 1; v = (unsigned long long)(-s); } else v = (unsigned long long)s;
            do { buf[len++] = (char)('0' + v % 10); v /= 10; } while (v);
            break;
        }
        case 'u': case 'x': {
            v = lng >= 2 ? va_arg(ap, unsigned long long) : lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            unsigned base = *fmt == 'x' ? 16 : 10;
            do { buf[len++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            int sl = (int)u_strlen(s);
            for (int i = sl; i < width; i++) emit(out, n, &pos, ' ');
            while (*s) emit(out, n, &pos, *s++);
            continue;
        }
        case 'c': emit(out, n, &pos, (char)va_arg(ap, int)); continue;
        case '%': emit(out, n, &pos, '%'); continue;
        default: continue;
        }
        if (neg && pad == '0') emit(out, n, &pos, '-');
        for (int i = len + neg; i < width; i++) emit(out, n, &pos, pad);
        if (neg && pad == ' ') emit(out, n, &pos, '-');
        while (len) emit(out, n, &pos, buf[--len]);
    }
    if (n) out[pos < n ? pos : n - 1] = 0;
    return (int)pos;
}

int u_snprintf(char *out, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = u_vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return r;
}

void con_printf(const char *fmt, ...) {
    char buf[160];
    va_list ap; va_start(ap, fmt);
    u_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    for (char *p = buf; *p; p++) {
        if (*p == '\n') uart_putc('\r');
        uart_putc(*p);
    }
}
