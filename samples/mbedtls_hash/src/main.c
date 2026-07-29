#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include <zephyr/timing/timing.h>

#define MAX_TOTAL_LEN 16384
#define MAX_BUF       (MAX_TOTAL_LEN + 64)
#define THROUGHPUT_TOTAL_SIZE KB(256)
#define SHA512_BLOCK_SIZE 128

static __attribute__((aligned(32))) uint8_t s_base[MAX_BUF];
static __attribute__((aligned(32))) uint8_t s_aligned[MAX_BUF];
static __attribute__((aligned(32))) uint8_t s_throughput[THROUGHPUT_TOTAL_SIZE + 1];
static uint8_t s_ref[64];
static uint8_t s_multi[64];

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int hash_ref(int is224, bool sm3, const uint8_t *data, size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
        if (ret == 0) {
            ret = mbedtls_sm3_update(&ctx, data, len);
        }
        if (ret == 0) {
            ret = mbedtls_sm3_finish(&ctx, out);
        }
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
        if (ret == 0) {
            ret = mbedtls_sha256_update(&ctx, data, len);
        }
        if (ret == 0) {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_random_max(int is224, bool sm3, const uint8_t *data,
                                 size_t len, uint8_t *out,
                                 uint32_t seed, size_t max_chunk)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0; ret == 0 && off < len; ) {
        uint32_t r = xorshift32(&seed);
        size_t chunk = (r % max_chunk) + 1;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, chunk);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, chunk);
        }
        off += chunk;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_random(int is224, bool sm3, const uint8_t *data,
                             size_t len, uint8_t *out, uint32_t seed)
{
    return hash_multi_random_max(is224, sm3, data, len, out, seed, 512);
}

static int hash_multi_random_zero_splice(int is224, bool sm3, const uint8_t *data,
                                          size_t len, uint8_t *out,
                                          uint32_t seed, size_t max_chunk)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0; ret == 0 && off < len; ) {
        uint32_t r = xorshift32(&seed);
        size_t chunk = (r % max_chunk) + 1;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        /* Occasionally insert a zero-length update between real chunks. */
        if ((r & 0x100U) && off > 0) {
            if (sm3) {
                ret = mbedtls_sm3_update(&ctx, data + off, 0);
            } else {
                ret = mbedtls_sha256_update(&ctx, data + off, 0);
            }
        }

        if (ret == 0) {
            if (sm3) {
                ret = mbedtls_sm3_update(&ctx, data + off, chunk);
            } else {
                ret = mbedtls_sha256_update(&ctx, data + off, chunk);
            }
        }
        off += chunk;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_alternating(int is224, bool sm3, const uint8_t *data,
                                  size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0, toggle = 0; ret == 0 && off < len; toggle ^= 1) {
        size_t chunk = toggle ? 1 : 1024;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, chunk);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, chunk);
        }
        off += chunk;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_boundary(int is224, bool sm3, const uint8_t *data,
                               size_t len, uint8_t *out)
{
    static const size_t steps[] = {
        1, 63, 64, 65, 127, 128, 129,
        255, 256, 257, 511, 512, 513,
        1023, 1024, 1025, 2047, 2048, 2049
    };
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0, s = 0; ret == 0 && off < len; ) {
        size_t chunk = steps[s % ARRAY_SIZE(steps)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, chunk);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, chunk);
        }
        off += chunk;
        s++;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_large_steps(int is224, bool sm3, const uint8_t *data,
                                  size_t len, uint8_t *out)
{
    /* Step sizes chosen to exercise _dma internal splitting (8128 B max) and
     * all common block / cache-line boundaries.
     */
    static const size_t steps[] = {
        1, 63, 64, 65, 127, 128, 129,
        255, 256, 257, 511, 512, 513,
        1023, 1024, 1025,
        2047, 2048, 2049,
        4095, 4096, 4097,
        8127, 8128, 8129,
        8191, 8192, 8193,
        16383, 16384
    };
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0, s = 0; ret == 0 && off < len; ) {
        size_t chunk = steps[s % ARRAY_SIZE(steps)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, chunk);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, chunk);
        }
        off += chunk;
        s++;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

static int hash_multi_tiny(int is224, bool sm3, const uint8_t *data,
                           size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0; ret == 0 && off < len; off++) {
        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, 1);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, 1);
        }
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

/* Large, 32-byte-aligned chunks so the unified update can take the DMA path.
 * 8192 B exceeds the 8128 B _dma transfer limit, so it also exercises the
 * internal split into multiple DMA transfers plus a small tail copy.
 */
static int hash_multi_aligned_dma(int is224, bool sm3, const uint8_t *data,
                                  size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;
    const size_t chunk = 8192;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0; ret == 0 && off < len; ) {
        size_t c = chunk;
        size_t remain = len - off;

        if (c > remain) {
            c = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, c);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, c);
        }
        off += c;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

/* Large, deliberately unaligned chunks to force the CPU copy fallback and
 * provide a baseline for comparing DMA vs CPU throughput.
 */
static int hash_multi_large_cpu(int is224, bool sm3, const uint8_t *data,
                                size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;
    const size_t chunk = 8192;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0; ret == 0 && off < len; ) {
        size_t c = chunk;
        size_t remain = len - off;

        if (c > remain) {
            c = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, c);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, c);
        }
        off += c;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

/* Deliberately mix large aligned chunks (DMA) with small/odd-sized chunks
 * (CPU copy/tail) within a single hash session.
 */
static int hash_mixed_dma_cpu(int is224, bool sm3, const uint8_t *data,
                              size_t len, uint8_t *out)
{
    /* Chunk sizes chosen so cumulative offset repeatedly switches between
     * cache-line aligned and unaligned, forcing DMA/CPU alternation.
     */
    static const size_t pattern[] = {
        8192, 63, 8192, 65, 1024, 1, 512, 7, 4096, 31, 2048, 1,
        8128, 64, 8128, 65, 128, 127, 128, 1
    };
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }

    for (size_t off = 0, p = 0; ret == 0 && off < len; ) {
        size_t chunk = pattern[p % ARRAY_SIZE(pattern)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if (sm3) {
            ret = mbedtls_sm3_update(&ctx, data + off, chunk);
        } else {
            ret = mbedtls_sha256_update(&ctx, data + off, chunk);
        }
        off += chunk;
        p++;
    }

    if (ret == 0) {
        if (sm3) {
            ret = mbedtls_sm3_finish(&ctx, out);
        } else {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

/* Single update with a fully aligned buffer to measure the DMA fast path
 * without any per-update CPU refill/tail overhead.
 */
static int hash_single_aligned_dma(int is224, bool sm3, const uint8_t *data,
                                   size_t len, uint8_t *out)
{
    mbedtls_sha256_context ctx;
    int ret;

    mbedtls_sha256_init(&ctx);

    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
        if (ret == 0) {
            ret = mbedtls_sm3_update(&ctx, data, len);
        }
        if (ret == 0) {
            ret = mbedtls_sm3_finish(&ctx, out);
        }
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
        if (ret == 0) {
            ret = mbedtls_sha256_update(&ctx, data, len);
        }
        if (ret == 0) {
            ret = mbedtls_sha256_finish(&ctx, out);
        }
    }

    mbedtls_sha256_free(&ctx);
    return ret;
}

/* SHA-512/SHA-384 helper functions (same modes as SHA-256 but with 128-byte
 * block boundaries and 48/64-byte digests).
 */

static int hash_ref_sha512(int is384, const uint8_t *data, size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);
    if (ret == 0) {
        ret = mbedtls_sha512_update(&ctx, data, len);
    }
    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }
    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_random_max_sha512(int is384, const uint8_t *data,
                                        size_t len, uint8_t *out,
                                        uint32_t seed, size_t max_chunk)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0; ret == 0 && off < len; ) {
        uint32_t r = xorshift32(&seed);
        size_t chunk = (r % max_chunk) + 1;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        off += chunk;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_random_sha512(int is384, const uint8_t *data,
                                    size_t len, uint8_t *out, uint32_t seed)
{
    return hash_multi_random_max_sha512(is384, data, len, out, seed, 512);
}

static int hash_multi_random_zero_splice_sha512(int is384, const uint8_t *data,
                                                size_t len, uint8_t *out,
                                                uint32_t seed, size_t max_chunk)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0; ret == 0 && off < len; ) {
        uint32_t r = xorshift32(&seed);
        size_t chunk = (r % max_chunk) + 1;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        if ((r & 0x100U) && off > 0) {
            ret = mbedtls_sha512_update(&ctx, data + off, 0);
        }

        if (ret == 0) {
            ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        }
        off += chunk;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_alternating_sha512(int is384, const uint8_t *data,
                                          size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0, toggle = 0; ret == 0 && off < len; toggle ^= 1) {
        size_t chunk = toggle ? 1 : 1024;
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        off += chunk;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_boundary_sha512(int is384, const uint8_t *data,
                                       size_t len, uint8_t *out)
{
    /* Step sizes relevant to the 128-byte SHA-512 block size. */
    static const size_t steps[] = {
        1, 127, 128, 129,
        255, 256, 257,
        511, 512, 513,
        1023, 1024, 1025,
        2047, 2048, 2049
    };
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0, s = 0; ret == 0 && off < len; ) {
        size_t chunk = steps[s % ARRAY_SIZE(steps)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        off += chunk;
        s++;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_large_steps_sha512(int is384, const uint8_t *data,
                                          size_t len, uint8_t *out)
{
    static const size_t steps[] = {
        1, 127, 128, 129,
        255, 256, 257,
        511, 512, 513,
        1023, 1024, 1025,
        2047, 2048, 2049,
        4095, 4096, 4097,
        8127, 8128, 8129,
        8191, 8192, 8193,
        16383, 16384
    };
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0, s = 0; ret == 0 && off < len; ) {
        size_t chunk = steps[s % ARRAY_SIZE(steps)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        off += chunk;
        s++;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_tiny_sha512(int is384, const uint8_t *data,
                                   size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0; ret == 0 && off < len; off++) {
        ret = mbedtls_sha512_update(&ctx, data + off, 1);
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_aligned_dma_sha512(int is384, const uint8_t *data,
                                          size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;
    const size_t chunk = 8192;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0; ret == 0 && off < len; ) {
        size_t c = chunk;
        size_t remain = len - off;

        if (c > remain) {
            c = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, c);
        off += c;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_multi_large_cpu_sha512(int is384, const uint8_t *data,
                                        size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;
    const size_t chunk = 8192;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0; ret == 0 && off < len; ) {
        size_t c = chunk;
        size_t remain = len - off;

        if (c > remain) {
            c = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, c);
        off += c;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_mixed_dma_cpu_sha512(int is384, const uint8_t *data,
                                      size_t len, uint8_t *out)
{
    static const size_t pattern[] = {
        8192, 63, 8192, 65, 1024, 1, 512, 7, 4096, 31, 2048, 1,
        8128, 64, 8128, 65, 128, 127, 128, 1
    };
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);

    for (size_t off = 0, p = 0; ret == 0 && off < len; ) {
        size_t chunk = pattern[p % ARRAY_SIZE(pattern)];
        size_t remain = len - off;

        if (chunk > remain) {
            chunk = remain;
        }

        ret = mbedtls_sha512_update(&ctx, data + off, chunk);
        off += chunk;
        p++;
    }

    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }

    mbedtls_sha512_free(&ctx);
    return ret;
}

static int hash_single_aligned_dma_sha512(int is384, const uint8_t *data,
                                           size_t len, uint8_t *out)
{
    mbedtls_sha512_context ctx;
    int ret;

    mbedtls_sha512_init(&ctx);
    ret = mbedtls_sha512_starts(&ctx, is384);
    if (ret == 0) {
        ret = mbedtls_sha512_update(&ctx, data, len);
    }
    if (ret == 0) {
        ret = mbedtls_sha512_finish(&ctx, out);
    }
    mbedtls_sha512_free(&ctx);
    return ret;
}

/* One-shot 256 KB update on an aligned buffer: measures the DMA fast path at
 * the largest practical single-call size.
 */
static int hash_bulk_single_dma(int is224, bool sm3, uint8_t *out,
                                uint64_t *out_us)
{
    mbedtls_sha256_context ctx;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    int ret;

    memset(s_throughput, 2, sizeof(s_throughput));

    mbedtls_sha256_init(&ctx);

    start_time = timing_counter_get();
    if (sm3) {
        ret = mbedtls_sm3_starts(&ctx);
    } else {
        ret = mbedtls_sha256_starts(&ctx, is224);
    }
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    start_time = timing_counter_get();
    if (sm3) {
        ret = mbedtls_sm3_update(&ctx, s_throughput, THROUGHPUT_TOTAL_SIZE);
    } else {
        ret = mbedtls_sha256_update(&ctx, s_throughput, THROUGHPUT_TOTAL_SIZE);
    }
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    start_time = timing_counter_get();
    if (sm3) {
        ret = mbedtls_sm3_finish(&ctx, out);
    } else {
        ret = mbedtls_sha256_finish(&ctx, out);
    }
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha256_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha256_free(&ctx);

    *out_us = total_cycles / timing_freq_get_mhz();
    return 0;
}

static int hash_bulk_single_dma_sha512(int is384, uint8_t *out,
                                        uint64_t *out_us)
{
    mbedtls_sha512_context ctx;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    int ret;

    memset(s_throughput, 2, sizeof(s_throughput));

    mbedtls_sha512_init(&ctx);

    start_time = timing_counter_get();
    ret = mbedtls_sha512_starts(&ctx, is384);
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha512_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    start_time = timing_counter_get();
    ret = mbedtls_sha512_update(&ctx, s_throughput, THROUGHPUT_TOTAL_SIZE);
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha512_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    start_time = timing_counter_get();
    ret = mbedtls_sha512_finish(&ctx, out);
    end_time = timing_counter_get();
    if (ret != 0) {
        mbedtls_sha512_free(&ctx);
        return ret;
    }
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha512_free(&ctx);

    *out_us = total_cycles / timing_freq_get_mhz();
    return 0;
}

enum {
    MODE_RANDOM64,
    MODE_RANDOM512,
    MODE_RANDOM8192,
    MODE_ZERO_SPLICE,
    MODE_ALTERNATING,
    MODE_BOUNDARY,
    MODE_LARGE_STEPS,
    MODE_TINY,
    MODE_ALIGNED_DMA,
    MODE_LARGE_CPU,
    MODE_SINGLE_ALIGNED_DMA,
    MODE_MIXED_DMA_CPU,
    MODE_COUNT
};

static const char *mode_names[MODE_COUNT] = {
    [MODE_RANDOM64]    = "RANDOM64",
    [MODE_RANDOM512]   = "RANDOM512",
    [MODE_RANDOM8192]  = "RANDOM8192",
    [MODE_ZERO_SPLICE] = "ZERO_SPLICE",
    [MODE_ALTERNATING] = "ALTERNATING",
    [MODE_BOUNDARY]    = "BOUNDARY",
    [MODE_LARGE_STEPS] = "LARGE_STEPS",
    [MODE_TINY]        = "TINY",
    [MODE_ALIGNED_DMA] = "ALIGNED_DMA",
    [MODE_LARGE_CPU]   = "LARGE_CPU",
    [MODE_SINGLE_ALIGNED_DMA] = "SINGLE_ALIGNED_DMA",
    [MODE_MIXED_DMA_CPU] = "MIXED_DMA_CPU",
};

static void print_suite_speed(const char *name, uint64_t bytes, uint64_t us);

static int run_test_suite(const char *name, int is224, bool sm3,
                          uint64_t *out_bytes)
{
    /* Exhaustive small lengths + representative block/cache-line/DMA-split
     * boundaries up to MAX_TOTAL_LEN.
     */
    static size_t lengths[256];
    static const int offsets[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
        31, 32, 33, 63
    };
    const size_t digest_len = sm3 ? 32 : (is224 ? 28 : 32);
    uint64_t total_bytes = 0;
    uint64_t mode_bytes[MODE_COUNT] = {0};
    uint64_t mode_us[MODE_COUNT] = {0};
    size_t num_lengths = 0;
    uint64_t t0;
    int ret;

    /* 0..128 exhaustive. */
    for (size_t i = 0; i <= 128; i++) {
        lengths[num_lengths++] = i;
    }

    /* Boundaries that matter for 64 B blocks, 32 B cache lines, and 8128 B
     * _dma max transfer.
     */
    static const size_t extra_lengths[] = {
        129, 255, 256, 257,
        511, 512, 513,
        1023, 1024, 1025,
        2047, 2048, 2049,
        4095, 4096, 4097,
        8127, 8128, 8129,
        8191, 8192, 8193,
        12287, 12288, 12289,
        16383, 16384
    };
    for (size_t i = 0; i < ARRAY_SIZE(extra_lengths); i++) {
        lengths[num_lengths++] = extra_lengths[i];
    }

    /* Deterministic pseudo-random fill, reproducible across runs. */
    for (size_t i = 0; i < sizeof(s_base); i++) {
        s_base[i] = (uint8_t)(i ^ 0xA5 ^ (i >> 7));
        s_aligned[i] = s_base[i];
    }

    printk("\n=== %s comprehensive update test ===\n", name);

    for (size_t li = 0; li < num_lengths; li++) {
        size_t total = lengths[li];

        for (size_t oi = 0; oi < ARRAY_SIZE(offsets); oi++) {
            int off = offsets[oi];
            const uint8_t *data = s_base + off;

            ret = hash_ref(is224, sm3, data, total, s_ref);
            if (ret != 0) {
                printk("%s REF FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return ret;
            }
            total_bytes += total;

            /* Random chunks: small (1..64). */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_max(is224, sm3, data, total, s_multi,
                                        0x11111111U ^ (uint32_t)total ^ (uint32_t)off, 64);
            mode_us[MODE_RANDOM64] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM64 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM64] += total;

            /* Random chunks: medium (1..512). */
            t0 = k_cycle_get_64();
            ret = hash_multi_random(is224, sm3, data, total, s_multi,
                                    0x12345678U ^ (uint32_t)total ^ (uint32_t)off);
            mode_us[MODE_RANDOM512] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM512 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM512] += total;

            /* Random chunks: large (1..8192), exercises _dma trans_count > 0. */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_max(is224, sm3, data, total, s_multi,
                                        0x87654321U ^ (uint32_t)total ^ (uint32_t)off, 8192);
            mode_us[MODE_RANDOM8192] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM8192 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM8192] += total;

            /* Random chunks with zero-length updates interspersed. */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_zero_splice(is224, sm3, data, total, s_multi,
                                                0xA5A5A5A5U ^ (uint32_t)total ^ (uint32_t)off,
                                                512);
            mode_us[MODE_ZERO_SPLICE] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s ZERO_SPLICE FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_ZERO_SPLICE] += total;

            /* Alternating 1-byte and 1024-byte chunks. */
            t0 = k_cycle_get_64();
            ret = hash_multi_alternating(is224, sm3, data, total, s_multi);
            mode_us[MODE_ALTERNATING] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s ALTERNATING FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_ALTERNATING] += total;

            /* Deterministic boundary steps. */
            t0 = k_cycle_get_64();
            ret = hash_multi_boundary(is224, sm3, data, total, s_multi);
            mode_us[MODE_BOUNDARY] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s BOUNDARY FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_BOUNDARY] += total;

            /* Large step sizes including 8128/8129/8192. */
            t0 = k_cycle_get_64();
            ret = hash_multi_large_steps(is224, sm3, data, total, s_multi);
            mode_us[MODE_LARGE_STEPS] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s LARGE_STEPS FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_LARGE_STEPS] += total;

            /* 1-byte chunks for smaller lengths. */
            if (total <= 512) {
                t0 = k_cycle_get_64();
                ret = hash_multi_tiny(is224, sm3, data, total, s_multi);
                mode_us[MODE_TINY] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
                if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                    printk("%s TINY FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }
                mode_bytes[MODE_TINY] += total;
            }

            /* Large aligned chunks to force the DMA path.  Use the dedicated
             * aligned buffer and compute the reference over that same buffer.
             */
            {
                t0 = k_cycle_get_64();
                ret = hash_multi_aligned_dma(is224, sm3, s_aligned, total, s_multi);
                mode_us[MODE_ALIGNED_DMA] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

                if (ret != 0) {
                    printk("%s ALIGNED_DMA FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }

                if (hash_ref(is224, sm3, s_aligned, total, s_ref) != 0 ||
                    memcmp(s_ref, s_multi, digest_len) != 0) {
                    printk("%s ALIGNED_DMA FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }

                mode_bytes[MODE_ALIGNED_DMA] += total;
            }

        }

        /* CPU-only baseline: use a deliberately unaligned pointer so the
         * unified update never selects the DMA path.  Reference is computed
         * over the same unaligned buffer so the digest compares correctly.
         */
        {
            const uint8_t *cpu_data = s_base + 1;

            ret = hash_ref(is224, sm3, cpu_data, total, s_ref);
            if (ret != 0) {
                printk("%s REF_CPU FAIL len=%zu ret=%d\n", name, total, ret);
                return ret;
            }

            t0 = k_cycle_get_64();
            ret = hash_multi_large_cpu(is224, sm3, cpu_data, total, s_multi);
            mode_us[MODE_LARGE_CPU] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s LARGE_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }
            mode_bytes[MODE_LARGE_CPU] += total;
        }

        /* Single update with fully aligned buffer: measures the DMA fast path
         * without per-update refill/tail overhead.  Use the dedicated aligned
         * buffer and compute the reference over that same buffer.
         */
        if (total > 0) {
            t0 = k_cycle_get_64();
            ret = hash_single_aligned_dma(is224, sm3, s_aligned, total, s_multi);
            mode_us[MODE_SINGLE_ALIGNED_DMA] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

            if (ret != 0) {
                printk("%s SINGLE_ALIGNED_DMA FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            if (hash_ref(is224, sm3, s_aligned, total, s_ref) != 0 ||
                memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s SINGLE_ALIGNED_DMA FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            mode_bytes[MODE_SINGLE_ALIGNED_DMA] += total;
        }

        /* Mix large aligned chunks (DMA) and small/odd chunks (CPU copy) in
         * one hash session.
         */
        if (total > 0) {
            t0 = k_cycle_get_64();
            ret = hash_mixed_dma_cpu(is224, sm3, s_aligned, total, s_multi);
            mode_us[MODE_MIXED_DMA_CPU] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

            if (ret != 0) {
                printk("%s MIXED_DMA_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            if (hash_ref(is224, sm3, s_aligned, total, s_ref) != 0 ||
                memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s MIXED_DMA_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            mode_bytes[MODE_MIXED_DMA_CPU] += total;
        }
    }

    printk("%s all PASS (%zu lengths x %zu offsets + CPU baseline)\n",
           name, num_lengths, ARRAY_SIZE(offsets));

    printk("  %s per-mode throughput:\n", name);
    for (int m = 0; m < MODE_COUNT; m++) {
        if (mode_bytes[m] == 0) {
            continue;
        }
        printk("    ");
        print_suite_speed(mode_names[m], mode_bytes[m], mode_us[m]);
    }

    *out_bytes = total_bytes;
    for (int m = 0; m < MODE_COUNT; m++) {
        *out_bytes += mode_bytes[m];
    }
    return 0;
}

static int run_test_suite_sha512(const char *name, int is384, uint64_t *out_bytes)
{
    /* 0..128 exhaustive plus boundaries relevant to 128-byte blocks. */
    static size_t lengths[256];
    static const int offsets[] = {
        0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
        31, 32, 33, 63
    };
    const size_t digest_len = is384 ? 48 : 64;
    uint64_t total_bytes = 0;
    uint64_t mode_bytes[MODE_COUNT] = {0};
    uint64_t mode_us[MODE_COUNT] = {0};
    size_t num_lengths = 0;
    uint64_t t0;
    int ret;

    for (size_t i = 0; i <= 128; i++) {
        lengths[num_lengths++] = i;
    }

    /* Boundaries around 128/256/512/1024/2048/4096/8192/16384. */
    static const size_t extra_lengths[] = {
        129, 255, 256, 257,
        511, 512, 513,
        1023, 1024, 1025,
        2047, 2048, 2049,
        4095, 4096, 4097,
        8127, 8128, 8129,
        8191, 8192, 8193,
        12287, 12288, 12289,
        16383, 16384
    };
    for (size_t i = 0; i < ARRAY_SIZE(extra_lengths); i++) {
        lengths[num_lengths++] = extra_lengths[i];
    }

    for (size_t i = 0; i < sizeof(s_base); i++) {
        s_base[i] = (uint8_t)(i ^ 0xA5 ^ (i >> 7));
        s_aligned[i] = s_base[i];
    }

    printk("\n=== %s comprehensive update test ===\n", name);

    for (size_t li = 0; li < num_lengths; li++) {
        size_t total = lengths[li];

        for (size_t oi = 0; oi < ARRAY_SIZE(offsets); oi++) {
            int off = offsets[oi];
            const uint8_t *data = s_base + off;

            ret = hash_ref_sha512(is384, data, total, s_ref);
            if (ret != 0) {
                printk("%s REF FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return ret;
            }
            total_bytes += total;

            /* Random chunks: small (1..64). */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_max_sha512(is384, data, total, s_multi,
                                               0x11111111U ^ (uint32_t)total ^ (uint32_t)off, 64);
            mode_us[MODE_RANDOM64] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM64 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM64] += total;

            /* Random chunks: medium (1..512). */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_sha512(is384, data, total, s_multi,
                                           0x12345678U ^ (uint32_t)total ^ (uint32_t)off);
            mode_us[MODE_RANDOM512] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM512 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM512] += total;

            /* Random chunks: large (1..8192). */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_max_sha512(is384, data, total, s_multi,
                                               0x87654321U ^ (uint32_t)total ^ (uint32_t)off, 8192);
            mode_us[MODE_RANDOM8192] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s RANDOM8192 FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_RANDOM8192] += total;

            /* Random chunks with zero-length updates interspersed. */
            t0 = k_cycle_get_64();
            ret = hash_multi_random_zero_splice_sha512(is384, data, total, s_multi,
                                                       0xA5A5A5A5U ^ (uint32_t)total ^ (uint32_t)off,
                                                       512);
            mode_us[MODE_ZERO_SPLICE] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s ZERO_SPLICE FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_ZERO_SPLICE] += total;

            /* Alternating 1-byte and 1024-byte chunks. */
            t0 = k_cycle_get_64();
            ret = hash_multi_alternating_sha512(is384, data, total, s_multi);
            mode_us[MODE_ALTERNATING] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s ALTERNATING FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_ALTERNATING] += total;

            /* 128-byte-block boundary steps. */
            t0 = k_cycle_get_64();
            ret = hash_multi_boundary_sha512(is384, data, total, s_multi);
            mode_us[MODE_BOUNDARY] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s BOUNDARY FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_BOUNDARY] += total;

            /* Large step sizes including 8128/8129/8192. */
            t0 = k_cycle_get_64();
            ret = hash_multi_large_steps_sha512(is384, data, total, s_multi);
            mode_us[MODE_LARGE_STEPS] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s LARGE_STEPS FAIL len=%zu off=%d ret=%d\n",
                       name, total, off, ret);
                return -1;
            }
            mode_bytes[MODE_LARGE_STEPS] += total;

            /* 1-byte chunks for smaller lengths. */
            if (total <= 512) {
                t0 = k_cycle_get_64();
                ret = hash_multi_tiny_sha512(is384, data, total, s_multi);
                mode_us[MODE_TINY] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
                if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                    printk("%s TINY FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }
                mode_bytes[MODE_TINY] += total;
            }

            /* Large aligned chunks to force the DMA path. */
            {
                t0 = k_cycle_get_64();
                ret = hash_multi_aligned_dma_sha512(is384, s_aligned, total, s_multi);
                mode_us[MODE_ALIGNED_DMA] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

                if (ret != 0) {
                    printk("%s ALIGNED_DMA FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }

                if (hash_ref_sha512(is384, s_aligned, total, s_ref) != 0 ||
                    memcmp(s_ref, s_multi, digest_len) != 0) {
                    printk("%s ALIGNED_DMA FAIL len=%zu off=%d ret=%d\n",
                           name, total, off, ret);
                    return -1;
                }

                mode_bytes[MODE_ALIGNED_DMA] += total;
            }
        }

        /* CPU-only baseline: deliberately unaligned pointer. */
        {
            const uint8_t *cpu_data = s_base + 1;

            ret = hash_ref_sha512(is384, cpu_data, total, s_ref);
            if (ret != 0) {
                printk("%s REF_CPU FAIL len=%zu ret=%d\n", name, total, ret);
                return ret;
            }

            t0 = k_cycle_get_64();
            ret = hash_multi_large_cpu_sha512(is384, cpu_data, total, s_multi);
            mode_us[MODE_LARGE_CPU] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);
            if (ret != 0 || memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s LARGE_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }
            mode_bytes[MODE_LARGE_CPU] += total;
        }

        /* Single update with fully aligned buffer. */
        if (total > 0) {
            t0 = k_cycle_get_64();
            ret = hash_single_aligned_dma_sha512(is384, s_aligned, total, s_multi);
            mode_us[MODE_SINGLE_ALIGNED_DMA] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

            if (ret != 0) {
                printk("%s SINGLE_ALIGNED_DMA FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            if (hash_ref_sha512(is384, s_aligned, total, s_ref) != 0 ||
                memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s SINGLE_ALIGNED_DMA FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            mode_bytes[MODE_SINGLE_ALIGNED_DMA] += total;
        }

        /* Mix large aligned chunks (DMA) and small/odd chunks (CPU copy). */
        if (total > 0) {
            t0 = k_cycle_get_64();
            ret = hash_mixed_dma_cpu_sha512(is384, s_aligned, total, s_multi);
            mode_us[MODE_MIXED_DMA_CPU] += k_cyc_to_us_floor64(k_cycle_get_64() - t0);

            if (ret != 0) {
                printk("%s MIXED_DMA_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            if (hash_ref_sha512(is384, s_aligned, total, s_ref) != 0 ||
                memcmp(s_ref, s_multi, digest_len) != 0) {
                printk("%s MIXED_DMA_CPU FAIL len=%zu ret=%d\n",
                       name, total, ret);
                return -1;
            }

            mode_bytes[MODE_MIXED_DMA_CPU] += total;
        }
    }

    printk("%s all PASS (%zu lengths x %zu offsets + CPU baseline)\n",
           name, num_lengths, ARRAY_SIZE(offsets));

    printk("  %s per-mode throughput:\n", name);
    for (int m = 0; m < MODE_COUNT; m++) {
        if (mode_bytes[m] == 0) {
            continue;
        }
        printk("    ");
        print_suite_speed(mode_names[m], mode_bytes[m], mode_us[m]);
    }

    *out_bytes = total_bytes;
    for (int m = 0; m < MODE_COUNT; m++) {
        *out_bytes += mode_bytes[m];
    }
    return 0;
}

static void print_bulk_single_dma(const char *name, int is224, bool sm3)
{
    uint8_t out[32];
    uint64_t us;
    int err;

    err = hash_bulk_single_dma(is224, sm3, out, &us);
    if (err != 0) {
        printk("%s BULK_SINGLE_DMA FAIL\n", name);
        return;
    }

    printk("\n%s BULK_SINGLE_DMA (256 KB one-shot): ", name);
    print_suite_speed("", THROUGHPUT_TOTAL_SIZE, us);
}

static void print_suite_speed(const char *name, uint64_t bytes, uint64_t us)
{
    uint32_t mbps_int = 0;
    uint32_t mbps_frac = 0;

    if (us != 0) {
        mbps_int = (uint32_t)(bytes / us);
        mbps_frac = (uint32_t)((bytes % us) * 100ULL / us);
    }

    printk("%s: %llu bytes, %llu us -> %u.%02u MB/s\n",
           name, (unsigned long long)bytes, (unsigned long long)us,
           mbps_int, mbps_frac);
}

static void print_bulk_single_dma_sha512(const char *name, int is384)
{
    uint8_t out[64];
    uint64_t us;
    int err;

    err = hash_bulk_single_dma_sha512(is384, out, &us);
    if (err != 0) {
        printk("%s BULK_SINGLE_DMA FAIL\n", name);
        return;
    }

    printk("\n%s BULK_SINGLE_DMA (256 KB one-shot): ", name);
    print_suite_speed("", THROUGHPUT_TOTAL_SIZE, us);
}

int main(void)
{
    int ret = 0;
    uint64_t bytes;
    uint64_t total_bytes = 0;
    uint64_t total_us = 0;
    uint64_t start;
    uint64_t us;

    start = k_cycle_get_64();
    ret |= run_test_suite("SHA-256", 0, false, &bytes);
    us = k_cyc_to_us_floor64(k_cycle_get_64() - start);
    print_suite_speed("SHA-256", bytes, us);
    total_bytes += bytes;
    total_us += us;

    start = k_cycle_get_64();
    ret |= run_test_suite("SHA-224", 1, false, &bytes);
    us = k_cyc_to_us_floor64(k_cycle_get_64() - start);
    print_suite_speed("SHA-224", bytes, us);
    total_bytes += bytes;
    total_us += us;

    start = k_cycle_get_64();
    ret |= run_test_suite("SM3", 0, true, &bytes);
    us = k_cyc_to_us_floor64(k_cycle_get_64() - start);
    print_suite_speed("SM3", bytes, us);
    total_bytes += bytes;
    total_us += us;

    start = k_cycle_get_64();
    ret |= run_test_suite_sha512("SHA-384", 1, &bytes);
    us = k_cyc_to_us_floor64(k_cycle_get_64() - start);
    print_suite_speed("SHA-384", bytes, us);
    total_bytes += bytes;
    total_us += us;

    start = k_cycle_get_64();
    ret |= run_test_suite_sha512("SHA-512", 0, &bytes);
    us = k_cyc_to_us_floor64(k_cycle_get_64() - start);
    print_suite_speed("SHA-512", bytes, us);
    total_bytes += bytes;
    total_us += us;

    print_bulk_single_dma("SHA-256", 0, false);
    print_bulk_single_dma_sha512("SHA-384", 1);
    print_bulk_single_dma_sha512("SHA-512", 0);

    if (ret == 0) {
        uint32_t avg_int = 0;
        uint32_t avg_frac = 0;

        if (total_us != 0) {
            avg_int = (uint32_t)(total_bytes / total_us);
            avg_frac = (uint32_t)((total_bytes % total_us) * 100ULL / total_us);
        }

        printk("\n*** ALL HASH TESTS PASSED ***\n");
        printk("Average hash throughput: %u.%02u MB/s\n", avg_int, avg_frac);
    } else {
        printk("\n*** HASH TESTS FAILED ***\n");
    }

    return ret ? -1 : 0;
}
