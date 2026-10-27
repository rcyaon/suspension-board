#ifndef UTIL_H
#define UTIL_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* small printf: %d %u %x %s %c with l/ll and width/zero-pad */
int u_vsnprintf(char *out, size_t n, const char *fmt, va_list ap);
int u_snprintf(char *out, size_t n, const char *fmt, ...);
void con_printf(const char *fmt, ...);

void u_memcpy(void *d, const void *s, size_t n);
void u_memset(void *d, int v, size_t n);
int u_memcmp(const void *a, const void *b, size_t n);
size_t u_strlen(const char *s);
int u_strncmp(const char *a, const char *b, size_t n);

static inline void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put_le32(uint8_t *p, uint32_t v) { put_le16(p, (uint16_t)v); put_le16(p + 2, (uint16_t)(v >> 16)); }
static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t get_le32(const uint8_t *p) { return get_le16(p) | ((uint32_t)get_le16(p + 2) << 16); }

#endif
