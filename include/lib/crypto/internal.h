#ifndef LIB_CRYPTO_INTERNAL_H
#define LIB_CRYPTO_INTERNAL_H

#include <stdint.h>

static inline int ct_equal(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++)
        d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

#endif
