#ifndef LIB_PRINTF_PRINTF_H
#define LIB_PRINTF_PRINTF_H

#include <stdarg.h>
#include <stdint.h>

typedef void (*printf_putc_fn)(void *ctx, char c);

void kvprintf_cb(printf_putc_fn out, void *ctx, const char *fmt, va_list ap);

#endif
