#include "lib/rand/rand.h"

#include "arch/cpu.h"
#include "lib/crypto/chacha20.h"

#define CHACHA_RESEED_BYTES (1u << 20)

static int has_rdrand;
static int has_rdseed;

static uint32_t ckey[8];
static uint32_t cnonce[2];
static uint32_t ccounter;
static uint8_t cbuf[64];
static uint32_t cbuf_pos = 64;
static uint32_t bytes_left;

static uint64_t entropy_pool;

static int hw_rdrand(uint64_t *out) {
    if (!has_rdrand)
        return 0;
    for (int i = 0; i < 10; i++) {
        if (arch_hw_rand(out))
            return 1;
    }
    return 0;
}

static int hw_rdseed(uint64_t *out) {
    if (!has_rdseed)
        return 0;
    for (int i = 0; i < 10; i++) {
        if (arch_hw_seed(out))
            return 1;
    }
    return 0;
}

static uint64_t mix(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static void entropy_add(uint64_t v) {
    entropy_pool ^= v;
    mix(&entropy_pool);
}

static uint64_t entropy_get(void) {
    uint64_t r;
    if (hw_rdseed(&r))
        entropy_add(r);
    if (hw_rdrand(&r))
        entropy_add(r);
    uint64_t t0 = cpu_rdtsc();
    for (volatile int i = 0; i < 8; i++)
        cpu_pause();
    entropy_add(cpu_rdtsc() ^ t0);
    entropy_add((uint64_t)(uintptr_t)&r);
    return mix(&entropy_pool);
}

static void csprng_block(void) {
    uint8_t nonce[12];
    nonce[0] = (uint8_t)cnonce[0];
    nonce[1] = (uint8_t)(cnonce[0] >> 8);
    nonce[2] = (uint8_t)(cnonce[0] >> 16);
    nonce[3] = (uint8_t)(cnonce[0] >> 24);
    nonce[4] = (uint8_t)cnonce[1];
    nonce[5] = (uint8_t)(cnonce[1] >> 8);
    nonce[6] = (uint8_t)(cnonce[1] >> 16);
    nonce[7] = (uint8_t)(cnonce[1] >> 24);
    nonce[8] = 0;
    nonce[9] = 0;
    nonce[10] = 0;
    nonce[11] = 0;
    chacha20_block(ckey, ccounter++, nonce, cbuf);
    cbuf_pos = 0;
}

static void csprng_reseed(void) {
    for (int i = 0; i < 4; i++) {
        uint64_t v = entropy_get();
        ckey[i * 2] = (uint32_t)v;
        ckey[i * 2 + 1] = (uint32_t)(v >> 32);
    }
    uint64_t n = entropy_get();
    cnonce[0] = (uint32_t)n;
    cnonce[1] = (uint32_t)(n >> 32);
    ccounter = 0;
    cbuf_pos = 64;
    bytes_left = CHACHA_RESEED_BYTES;
}

static uint8_t csprng_byte(void) {
    if (bytes_left == 0)
        csprng_reseed();
    if (cbuf_pos >= 64)
        csprng_block();
    bytes_left--;
    return cbuf[cbuf_pos++];
}

void rand_init(void) {
    uint32_t a, b, c, d;
    arch_cpuid(1, 0, &a, &b, &c, &d);
    has_rdrand = (int)((c >> 30) & 1u);
    arch_cpuid(7, 0, &a, &b, &c, &d);
    has_rdseed = (int)((b >> 18) & 1u);

    entropy_pool = cpu_rdtsc();
    for (int i = 0; i < 4; i++)
        csprng_reseed();
}

uint64_t rand_u64(void) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)csprng_byte() << (i * 8);
    return v;
}

uint32_t rand_u32(void) {
    return (uint32_t)(rand_u64() >> 32);
}

void rand_bytes(void *buf, uint32_t len) {
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++)
        p[i] = csprng_byte();
}
