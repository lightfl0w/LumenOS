#include "lib/printf/printf.h"

static void emit(printf_putc_fn out, void *ctx, char c) {
    out(ctx, c);
}

static void print_unsigned(printf_putc_fn out, void *ctx, uint32_t v, int base, int upper,
                           int width, int pad0, int hexPrefix) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *digits = upper ? up : lo;
    char buf[33];
    int n = 0;

    if (v == 0) {
        buf[n++] = '0';
    } else {
        while (v) {
            buf[n++] = digits[v % base];
            v /= base;
        }
    }

    int body = (hexPrefix ? 2 : 0) + n;
    int pad = (width > body) ? (width - body) : 0;

    if (pad0) {
        if (hexPrefix) {
            emit(out, ctx, '0');
            emit(out, ctx, upper ? 'X' : 'x');
        }
        while (pad--)
            emit(out, ctx, '0');
    } else {
        while (pad--)
            emit(out, ctx, ' ');
        if (hexPrefix) {
            emit(out, ctx, '0');
            emit(out, ctx, upper ? 'X' : 'x');
        }
    }
    while (n--)
        emit(out, ctx, buf[n]);
}

static void print_signed(printf_putc_fn out, void *ctx, int v, int width, int pad0) {
    unsigned int uv = (unsigned int)v;
    int neg = 0;
    char buf[12];
    int n = 0;

    if (v < 0) {
        neg = 1;
        uv = 0u - uv;
    }
    if (uv == 0) {
        buf[n++] = '0';
    } else {
        while (uv) {
            buf[n++] = '0' + uv % 10;
            uv /= 10;
        }
    }

    int body = neg + n;
    int pad = (width > body) ? (width - body) : 0;

    if (pad0) {
        if (neg)
            emit(out, ctx, '-');
        while (pad--)
            emit(out, ctx, '0');
    } else {
        while (pad--)
            emit(out, ctx, ' ');
        if (neg)
            emit(out, ctx, '-');
    }
    while (n--)
        emit(out, ctx, buf[n]);
}

void kvprintf_cb(printf_putc_fn out, void *ctx, const char *fmt, va_list ap) {
    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            emit(out, ctx, *fmt);
            continue;
        }
        ++fmt;

        int pad0 = 0, hex_pre = 0, width = 0;
        for (;;) {
            if (*fmt == '0') {
                pad0 = 1;
                ++fmt;
            } else if (*fmt == '#') {
                hex_pre = 1;
                ++fmt;
            } else {
                break;
            }
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }
        if (*fmt == 'l')
            ++fmt;
        switch (*fmt) {
        case 'd': {
            int v = va_arg(ap, int);
            print_signed(out, ctx, v, width, pad0);
            break;
        }
        case 'u': {
            uint32_t v = va_arg(ap, uint32_t);
            print_unsigned(out, ctx, v, 10, 0, width, pad0, 0);
            break;
        }
        case 'x': {
            uint32_t v = va_arg(ap, uint32_t);
            print_unsigned(out, ctx, v, 16, 0, width, pad0, hex_pre);
            break;
        }
        case 'X': {
            uint32_t v = va_arg(ap, uint32_t);
            print_unsigned(out, ctx, v, 16, 1, width, pad0, hex_pre);
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            emit(out, ctx, (char)c);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == 0)
                s = "(null)";
            while (*s)
                emit(out, ctx, *s++);
            break;
        }
        case 'o': {
            uint32_t v = va_arg(ap, uint32_t);
            print_unsigned(out, ctx, v, 8, 0, width, pad0, 0);
            break;
        }
        case '%': {
            emit(out, ctx, '%');
            break;
        }
        case '\0':
            --fmt;
            break;
        default:
            emit(out, ctx, '%');
            emit(out, ctx, *fmt);
            break;
        }
        if (*fmt == 0)
            break;
    }
}
