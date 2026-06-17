/*
 * OTBN hash alignment test
 *
 * Two independent paths are exercised:
 *   - wolfssl (wc_Sha256 / wc_Sha224 / wc_Sha384 / wc_Sha512 / wc_Sm3)
 *   - mbedtls (mbedtls_sha256 / mbedtls_sha512 / mbedtls_sm3)
 *
 * Whether each path uses OTBN hardware is controlled by Kconfig:
 *   CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA224_SHA256_ALT
 *   CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA384_SHA512_ALT
 *   CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SM3_ALT
 *   CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT
 *   CONFIG_MBEDTLS_SHA384_SHA512_LINKEDSEMI_OTBN_ALT
 *
 * The test focuses on boundary lengths where padding/block handling is
 * most likely to fail.
 */

#include <zephyr/kernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <wolfssl/wolfcrypt/sha256.h>
#include <wolfssl/wolfcrypt/sha512.h>
#include <wolfssl/wolfcrypt/sm3.h>
#include <wolfssl/wolfcrypt/types.h>
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"

#define TEST_SHA224_SIZE 28
#define TEST_SHA256_SIZE 32
#define TEST_SM3_SIZE    32
#define TEST_SHA384_SIZE 48
#define TEST_SHA512_SIZE 64
#define TEST_MAX_DIGEST_SIZE TEST_SHA512_SIZE

/* Unified error check for *_test_once() helpers: log the failing call,
 * jump to the common cleanup label, and return the error code. */
#define TEST_ONCE_CHECK(_algo, _ret) do { \
    if ((_ret) != 0) { \
        printk("%s_test_once error: ret=%d at line %d\n", (_algo), (_ret), __LINE__); \
        while(1);\
        goto _test_once_err; \
    } \
} while (0)

static uint8_t plaintext_buf[KB(11)];
static uint8_t result_a[TEST_MAX_DIGEST_SIZE];
static uint8_t result_b[TEST_MAX_DIGEST_SIZE];

static const uint32_t test_lengths[] = {
    0,
    1,
    55,   /* SHA-256: one padding block case */
    56,   /* SHA-256: two padding blocks case */
    57,
    63,
    64,   /* exact one block */
    65,   /* one block + one byte */
    119,  /* SHA-512: one padding block case */
    120,  /* SHA-512: two padding blocks case */
    121,
    127,
    128,  /* exact one block */
    129,
    255,
    256,
    257,
    511,
    512,
    513,
    1000,
    1023,
    1024,
    1025,
    2048,
    4096,
    10240,
};
#define NUM_TEST_LENGTHS (sizeof(test_lengths) / sizeof(test_lengths[0]))

static inline void fill_plaintext(uint32_t len)
{
    if (len > 0) {
        memset(plaintext_buf, 0x5a, len);
    }
}

static inline void print_hex(const uint8_t *ptr, uint32_t len)
{
    while (len-- != 0) {
        //printk("%2.2x", *ptr++);
    }
}

static inline int compare_results(const char *name, uint32_t len,
                                   const uint8_t *a, const uint8_t *b,
                                   uint32_t digest_size)
{
    if (memcmp(a, b, digest_size) != 0) {
        printk("%s len=%u compare FAIL\n", name, len);
        printk("  wolfssl: "); print_hex(a, digest_size); //printk("\n");
        printk("  mbedtls: "); print_hex(b, digest_size); //printk("\n");
        while(1);
        return -1;
    }
    printk("%s len=%u compare pass\n", name, len);
    return 0;
}

/* ===================================================================
 * SHA-224
 * =================================================================== */
static int sha224_test_once(uint32_t len)
{
    wc_Sha224 ctx_w;
    mbedtls_sha256_context ctx_m;
    int ret;

    // fill_plaintext(len);

    ret = wc_InitSha224_ex(&ctx_w, NULL, INVALID_DEVID);
    TEST_ONCE_CHECK("SHA-224(wolf)", ret);
    ret = wc_Sha224Update(&ctx_w, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-224(wolf)", ret);
    ret = wc_Sha224Final(&ctx_w, result_a);
    TEST_ONCE_CHECK("SHA-224(wolf)", ret);

    mbedtls_sha256_init(&ctx_m);
    ret = mbedtls_sha256_starts(&ctx_m, 1);
    TEST_ONCE_CHECK("SHA-224(mbed)", ret);
    ret = mbedtls_sha256_update(&ctx_m, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-224(mbed)", ret);
    ret = mbedtls_sha256_finish(&ctx_m, result_b);
    TEST_ONCE_CHECK("SHA-224(mbed)", ret);

    ret = compare_results("SHA-224", len, result_a, result_b, TEST_SHA224_SIZE);

_test_once_err:
    wc_Sha224Free(&ctx_w);
    mbedtls_sha256_free(&ctx_m);
    return ret;
}

/* ===================================================================
 * SHA-256
 * =================================================================== */
static int sha256_test_once(uint32_t len)
{
    wc_Sha256 ctx_w;
    mbedtls_sha256_context ctx_m;
    int ret;

    // fill_plaintext(len);

    ret = wc_InitSha256_ex(&ctx_w, NULL, INVALID_DEVID);
    TEST_ONCE_CHECK("SHA-256(wolf)", ret);
    ret = wc_Sha256Update(&ctx_w, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-256(wolf)", ret);
    ret = wc_Sha256Final(&ctx_w, result_a);
    TEST_ONCE_CHECK("SHA-256(wolf)", ret);

    mbedtls_sha256_init(&ctx_m);
    ret = mbedtls_sha256_starts(&ctx_m, 0);
    TEST_ONCE_CHECK("SHA-256(mbed)", ret);
    ret = mbedtls_sha256_update(&ctx_m, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-256(mbed)", ret);
    ret = mbedtls_sha256_finish(&ctx_m, result_b);
    TEST_ONCE_CHECK("SHA-256(mbed)", ret);

    ret = compare_results("SHA-256", len, result_a, result_b, TEST_SHA256_SIZE);

_test_once_err:
    wc_Sha256Free(&ctx_w);
    mbedtls_sha256_free(&ctx_m);
    return ret;
}

#if defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
/* ===================================================================
 * SM3
 * =================================================================== */
static int sm3_test_once(uint32_t len)
{
    wc_Sm3 ctx_w;
    mbedtls_sha256_context ctx_m;
    int ret;

    // fill_plaintext(len);

    ret = wc_InitSm3(&ctx_w, NULL, INVALID_DEVID);
    TEST_ONCE_CHECK("SM3(wolf)", ret);
    ret = wc_Sm3Update(&ctx_w, plaintext_buf, len);
    TEST_ONCE_CHECK("SM3(wolf)", ret);
    ret = wc_Sm3Final(&ctx_w, result_a);
    TEST_ONCE_CHECK("SM3(wolf)", ret);

    mbedtls_sm3_init(&ctx_m);
    ret = mbedtls_sm3_starts(&ctx_m);
    TEST_ONCE_CHECK("SM3(mbed)", ret);
    ret = mbedtls_sm3_update(&ctx_m, plaintext_buf, len);
    TEST_ONCE_CHECK("SM3(mbed)", ret);
    ret = mbedtls_sm3_finish(&ctx_m, result_b);
    TEST_ONCE_CHECK("SM3(mbed)", ret);

    ret = compare_results("SM3", len, result_a, result_b, TEST_SM3_SIZE);

_test_once_err:
    wc_Sm3Free(&ctx_w);
    mbedtls_sm3_free(&ctx_m);
    return ret;
}
#endif
/* ===================================================================
 * SHA-384
 * =================================================================== */
static int sha384_test_once(uint32_t len)
{
    wc_Sha384 ctx_w;
    mbedtls_sha512_context ctx_m;
    int ret;

    // fill_plaintext(len);

    ret = wc_InitSha384_ex(&ctx_w, NULL, INVALID_DEVID);
    TEST_ONCE_CHECK("SHA-384(wolf)", ret);
    ret = wc_Sha384Update(&ctx_w, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-384(wolf)", ret);
    ret = wc_Sha384Final(&ctx_w, result_a);
    TEST_ONCE_CHECK("SHA-384(wolf)", ret);

    mbedtls_sha512_init(&ctx_m);
    ret = mbedtls_sha512_starts(&ctx_m, 1);
    TEST_ONCE_CHECK("SHA-384(mbed)", ret);
    ret = mbedtls_sha512_update(&ctx_m, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-384(mbed)", ret);
    ret = mbedtls_sha512_finish(&ctx_m, result_b);
    TEST_ONCE_CHECK("SHA-384(mbed)", ret);

    ret = compare_results("SHA-384", len, result_a, result_b, TEST_SHA384_SIZE);

_test_once_err:
    wc_Sha384Free(&ctx_w);
    mbedtls_sha512_free(&ctx_m);
    return ret;
}

/* ===================================================================
 * SHA-512
 * =================================================================== */
static int sha512_test_once(uint32_t len)
{
    wc_Sha512 ctx_w;
    mbedtls_sha512_context ctx_m;
    int ret;

    // fill_plaintext(len);

    ret = wc_InitSha512_ex(&ctx_w, NULL, INVALID_DEVID);
    TEST_ONCE_CHECK("SHA-512(wolf)", ret);
    ret = wc_Sha512Update(&ctx_w, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-512(wolf)", ret);
    ret = wc_Sha512Final(&ctx_w, result_a);
    TEST_ONCE_CHECK("SHA-512(wolf)", ret);

    mbedtls_sha512_init(&ctx_m);
    ret = mbedtls_sha512_starts(&ctx_m, 0);
    TEST_ONCE_CHECK("SHA-512(mbed)", ret);
    ret = mbedtls_sha512_update(&ctx_m, plaintext_buf, len);
    TEST_ONCE_CHECK("SHA-512(mbed)", ret);
    ret = mbedtls_sha512_finish(&ctx_m, result_b);
    TEST_ONCE_CHECK("SHA-512(mbed)", ret);

    ret = compare_results("SHA-512", len, result_a, result_b, TEST_SHA512_SIZE);

_test_once_err:
    wc_Sha512Free(&ctx_w);
    mbedtls_sha512_free(&ctx_m);
    return ret;
}

static int interleaved_compare(const char *name,
                                const uint8_t *result_a,
                                const uint8_t *result_b,
                                const uint8_t *ref_wa,
                                const uint8_t *ref_wb,
                                const uint8_t *ref_ma,
                                const uint8_t *ref_mb,
                                uint32_t digest_size)
{
    if (memcmp(result_a, ref_wa, digest_size) != 0 ||
        memcmp(result_b, ref_wb, digest_size) != 0 ||
        memcmp(result_a, ref_ma, digest_size) != 0 ||
        memcmp(result_b, ref_mb, digest_size) != 0) {
        //printk("Interleaved %s compare FAIL\n", name);
        //printk("  wolfssl interleaved a: "); print_hex(result_a, digest_size); //printk("\n");
        //printk("  wolfssl sequential a:  "); print_hex(ref_wa, digest_size); //printk("\n");
        //printk("  mbedtls sequential a:  "); print_hex(ref_ma, digest_size); //printk("\n");
        //printk("  wolfssl interleaved b: "); print_hex(result_b, digest_size); //printk("\n");
        //printk("  wolfssl sequential b:  "); print_hex(ref_wb, digest_size); //printk("\n");
        //printk("  mbedtls sequential b:  "); print_hex(ref_mb, digest_size); //printk("\n");
        return -1;
    }
    //printk("Interleaved %s compare pass\n", name);
    return 0;
}

/* ===================================================================
 * Interleaved update test: two SHA-224 contexts updated alternately,
 * compared against both wolfssl and mbedtls sequential references.
 * =================================================================== */
static int interleaved_sha224_test(void)
{
    wc_Sha224 ctx_wa, ctx_wb;
    mbedtls_sha256_context ctx_ma, ctx_mb;
    uint8_t ref_wa[TEST_SHA224_SIZE];
    uint8_t ref_wb[TEST_SHA224_SIZE];
    uint8_t ref_ma[TEST_SHA224_SIZE];
    uint8_t ref_mb[TEST_SHA224_SIZE];
    const byte *data_a = (const byte *)"abcdef";
    const byte *data_b = (const byte *)"123456";
    int ret;

    wc_InitSha224_ex(&ctx_wa, NULL, INVALID_DEVID);
    wc_InitSha224_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha224Update(&ctx_wa, data_a, 6);
        if (ret != 0) return ret;
        ret = wc_Sha224Update(&ctx_wb, data_b, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha224Final(&ctx_wa, result_a);
    if (ret != 0) return ret;
    ret = wc_Sha224Final(&ctx_wb, result_b);
    if (ret != 0) return ret;

    wc_InitSha224_ex(&ctx_wa, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha224Update(&ctx_wa, data_a, 6);
    wc_Sha224Final(&ctx_wa, ref_wa);

    wc_InitSha224_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha224Update(&ctx_wb, data_b, 6);
    wc_Sha224Final(&ctx_wb, ref_wb);

    mbedtls_sha256_init(&ctx_ma);
    mbedtls_sha256_starts(&ctx_ma, 1);
    for (int i = 0; i < 20; i++) mbedtls_sha256_update(&ctx_ma, data_a, 6);
    mbedtls_sha256_finish(&ctx_ma, ref_ma);
    mbedtls_sha256_free(&ctx_ma);

    mbedtls_sha256_init(&ctx_mb);
    mbedtls_sha256_starts(&ctx_mb, 1);
    for (int i = 0; i < 20; i++) mbedtls_sha256_update(&ctx_mb, data_b, 6);
    mbedtls_sha256_finish(&ctx_mb, ref_mb);
    mbedtls_sha256_free(&ctx_mb);

    return interleaved_compare("SHA-224", result_a, result_b,
                               ref_wa, ref_wb, ref_ma, ref_mb,
                               TEST_SHA224_SIZE);
}

/* ===================================================================
 * Interleaved update test: two SHA-256 contexts updated alternately,
 * compared against both wolfssl and mbedtls sequential references.
 * =================================================================== */
static int interleaved_sha256_test(void)
{
    wc_Sha256 ctx_wa, ctx_wb;
    mbedtls_sha256_context ctx_ma, ctx_mb;
    uint8_t ref_wa[TEST_SHA256_SIZE];
    uint8_t ref_wb[TEST_SHA256_SIZE];
    uint8_t ref_ma[TEST_SHA256_SIZE];
    uint8_t ref_mb[TEST_SHA256_SIZE];
    const byte *data_a = (const byte *)"abcdef";
    const byte *data_b = (const byte *)"123456";
    int ret;

    wc_InitSha256_ex(&ctx_wa, NULL, INVALID_DEVID);
    wc_InitSha256_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha256Update(&ctx_wa, data_a, 6);
        if (ret != 0) return ret;
        ret = wc_Sha256Update(&ctx_wb, data_b, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha256Final(&ctx_wa, result_a);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx_wb, result_b);
    if (ret != 0) return ret;

    wc_InitSha256_ex(&ctx_wa, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx_wa, data_a, 6);
    wc_Sha256Final(&ctx_wa, ref_wa);

    wc_InitSha256_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx_wb, data_b, 6);
    wc_Sha256Final(&ctx_wb, ref_wb);

    mbedtls_sha256_init(&ctx_ma);
    mbedtls_sha256_starts(&ctx_ma, 0);
    for (int i = 0; i < 20; i++) mbedtls_sha256_update(&ctx_ma, data_a, 6);
    mbedtls_sha256_finish(&ctx_ma, ref_ma);
    mbedtls_sha256_free(&ctx_ma);

    mbedtls_sha256_init(&ctx_mb);
    mbedtls_sha256_starts(&ctx_mb, 0);
    for (int i = 0; i < 20; i++) mbedtls_sha256_update(&ctx_mb, data_b, 6);
    mbedtls_sha256_finish(&ctx_mb, ref_mb);
    mbedtls_sha256_free(&ctx_mb);

    return interleaved_compare("SHA-256", result_a, result_b,
                               ref_wa, ref_wb, ref_ma, ref_mb,
                               TEST_SHA256_SIZE);
}

#if  defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
/* ===================================================================
 * Interleaved update test: two SM3 contexts updated alternately,
 * compared against both wolfssl and mbedtls sequential references.
 * =================================================================== */
static int interleaved_sm3_test(void)
{
    wc_Sm3 ctx_wa, ctx_wb;
    mbedtls_sha256_context ctx_ma, ctx_mb;
    uint8_t ref_wa[TEST_SM3_SIZE];
    uint8_t ref_wb[TEST_SM3_SIZE];
    uint8_t ref_ma[TEST_SM3_SIZE];
    uint8_t ref_mb[TEST_SM3_SIZE];
    const byte *data_a = (const byte *)"abcdef";
    const byte *data_b = (const byte *)"123456";
    int ret;

    wc_InitSm3(&ctx_wa, NULL, INVALID_DEVID);
    wc_InitSm3(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) {
        ret = wc_Sm3Update(&ctx_wa, data_a, 6);
        if (ret != 0) return ret;
        ret = wc_Sm3Update(&ctx_wb, data_b, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sm3Final(&ctx_wa, result_a);
    if (ret != 0) return ret;
    ret = wc_Sm3Final(&ctx_wb, result_b);
    if (ret != 0) return ret;

    wc_InitSm3(&ctx_wa, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sm3Update(&ctx_wa, data_a, 6);
    wc_Sm3Final(&ctx_wa, ref_wa);

    wc_InitSm3(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sm3Update(&ctx_wb, data_b, 6);
    wc_Sm3Final(&ctx_wb, ref_wb);

    mbedtls_sm3_init(&ctx_ma);
    mbedtls_sm3_starts(&ctx_ma);
    for (int i = 0; i < 20; i++) mbedtls_sm3_update(&ctx_ma, data_a, 6);
    mbedtls_sm3_finish(&ctx_ma, ref_ma);
    mbedtls_sm3_free(&ctx_ma);

    mbedtls_sm3_init(&ctx_mb);
    mbedtls_sm3_starts(&ctx_mb);
    for (int i = 0; i < 20; i++) mbedtls_sm3_update(&ctx_mb, data_b, 6);
    mbedtls_sm3_finish(&ctx_mb, ref_mb);
    mbedtls_sm3_free(&ctx_mb);

    return interleaved_compare("SM3", result_a, result_b,
                               ref_wa, ref_wb, ref_ma, ref_mb,
                               TEST_SM3_SIZE);
}
#endif
/* ===================================================================
 * Interleaved update test: two SHA-384 contexts updated alternately,
 * compared against both wolfssl and mbedtls sequential references.
 * =================================================================== */
static int interleaved_sha384_test(void)
{
    wc_Sha384 ctx_wa, ctx_wb;
    mbedtls_sha512_context ctx_ma, ctx_mb;
    uint8_t ref_wa[TEST_SHA384_SIZE];
    uint8_t ref_wb[TEST_SHA384_SIZE];
    uint8_t ref_ma[TEST_SHA384_SIZE];
    uint8_t ref_mb[TEST_SHA384_SIZE];
    const byte *data_a = (const byte *)"abcdef";
    const byte *data_b = (const byte *)"123456";
    int ret;

    wc_InitSha384_ex(&ctx_wa, NULL, INVALID_DEVID);
    wc_InitSha384_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha384Update(&ctx_wa, data_a, 6);
        if (ret != 0) return ret;
        ret = wc_Sha384Update(&ctx_wb, data_b, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha384Final(&ctx_wa, result_a);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx_wb, result_b);
    if (ret != 0) return ret;

    wc_InitSha384_ex(&ctx_wa, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha384Update(&ctx_wa, data_a, 6);
    wc_Sha384Final(&ctx_wa, ref_wa);

    wc_InitSha384_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha384Update(&ctx_wb, data_b, 6);
    wc_Sha384Final(&ctx_wb, ref_wb);

    mbedtls_sha512_init(&ctx_ma);
    mbedtls_sha512_starts(&ctx_ma, 1);
    for (int i = 0; i < 20; i++) mbedtls_sha512_update(&ctx_ma, data_a, 6);
    mbedtls_sha512_finish(&ctx_ma, ref_ma);
    mbedtls_sha512_free(&ctx_ma);

    mbedtls_sha512_init(&ctx_mb);
    mbedtls_sha512_starts(&ctx_mb, 1);
    for (int i = 0; i < 20; i++) mbedtls_sha512_update(&ctx_mb, data_b, 6);
    mbedtls_sha512_finish(&ctx_mb, ref_mb);
    mbedtls_sha512_free(&ctx_mb);

    return interleaved_compare("SHA-384", result_a, result_b,
                               ref_wa, ref_wb, ref_ma, ref_mb,
                               TEST_SHA384_SIZE);
}

/* ===================================================================
 * Interleaved update test: two SHA-512 contexts updated alternately,
 * compared against both wolfssl and mbedtls sequential references.
 * =================================================================== */
static int interleaved_sha512_test(void)
{
    wc_Sha512 ctx_wa, ctx_wb;
    mbedtls_sha512_context ctx_ma, ctx_mb;
    uint8_t ref_wa[TEST_SHA512_SIZE];
    uint8_t ref_wb[TEST_SHA512_SIZE];
    uint8_t ref_ma[TEST_SHA512_SIZE];
    uint8_t ref_mb[TEST_SHA512_SIZE];
    const byte *data_a = (const byte *)"abcdef";
    const byte *data_b = (const byte *)"123456";
    int ret;

    wc_InitSha512_ex(&ctx_wa, NULL, INVALID_DEVID);
    wc_InitSha512_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha512Update(&ctx_wa, data_a, 6);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx_wb, data_b, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha512Final(&ctx_wa, result_a);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx_wb, result_b);
    if (ret != 0) return ret;

    wc_InitSha512_ex(&ctx_wa, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx_wa, data_a, 6);
    wc_Sha512Final(&ctx_wa, ref_wa);

    wc_InitSha512_ex(&ctx_wb, NULL, INVALID_DEVID);
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx_wb, data_b, 6);
    wc_Sha512Final(&ctx_wb, ref_wb);

    mbedtls_sha512_init(&ctx_ma);
    mbedtls_sha512_starts(&ctx_ma, 0);
    for (int i = 0; i < 20; i++) mbedtls_sha512_update(&ctx_ma, data_a, 6);
    mbedtls_sha512_finish(&ctx_ma, ref_ma);
    mbedtls_sha512_free(&ctx_ma);

    mbedtls_sha512_init(&ctx_mb);
    mbedtls_sha512_starts(&ctx_mb, 0);
    for (int i = 0; i < 20; i++) mbedtls_sha512_update(&ctx_mb, data_b, 6);
    mbedtls_sha512_finish(&ctx_mb, ref_mb);
    mbedtls_sha512_free(&ctx_mb);

    return interleaved_compare("SHA-512", result_a, result_b,
                               ref_wa, ref_wb, ref_ma, ref_mb,
                               TEST_SHA512_SIZE);
}

/* ===================================================================
 * wolfSSL auxiliary API consistency tests
 * (wc_*Copy / wc_*GetHash / wc_*FinalRaw)
 * These exercise the same port functions regardless of whether wolfSSL
 * is using software or OTBN hardware.
 * =================================================================== */

#define API_TEST_CHUNK1  "abcdef"
#define API_TEST_CHUNK2  "123456"
#define API_TEST_LEN1    6
#define API_TEST_LEN2    6

static int sha224_api_test(void)
{
#ifdef WOLFSSL_SHA224
    wc_Sha224 ctx, ctx_ref, ctx_copy;
    uint8_t digest1[TEST_SHA224_SIZE];
    uint8_t digest2[TEST_SHA224_SIZE];
    int ret;

    /* Reference digest for chunk1+chunk2 */
    ret = wc_InitSha224_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx_ref, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha224Final(&ctx_ref, digest2);
    if (ret != 0) return ret;

    /* GetHash: must return digest so far without destroying context */
    ret = wc_InitSha224_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha224GetHash(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha224Final(&ctx, digest1);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA224_SIZE) != 0) {
        //printk("SHA-224 GetHash test FAIL\n");
        return -1;
    }
    //printk("SHA-224 GetHash test pass\n");

    /* Copy: copied context must produce identical digest */
    ret = wc_InitSha224_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha224Copy(&ctx, &ctx_copy);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha224Update(&ctx_copy, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha224Final(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha224Final(&ctx_copy, digest2);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA224_SIZE) != 0) {
        //printk("SHA-224 Copy test FAIL\n");
        return -1;
    }
    //printk("SHA-224 Copy test pass\n");

    wc_Sha224Free(&ctx);
    wc_Sha224Free(&ctx_ref);
    wc_Sha224Free(&ctx_copy);
#else
    //printk("SHA-224 API test skipped\n");
    return 1;
#endif
    return 0;
}

static int sha256_api_test(void)
{
    wc_Sha256 ctx, ctx_ref, ctx_copy;
    uint8_t digest1[TEST_SHA256_SIZE];
    uint8_t digest2[TEST_SHA256_SIZE];
    uint8_t raw1[TEST_SHA256_SIZE];
    uint8_t raw2[TEST_SHA256_SIZE];
    int ret;

    ret = wc_InitSha256_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx_ref, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx_ref, digest2);
    if (ret != 0) return ret;

    ret = wc_InitSha256_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha256GetHash(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx, digest1);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA256_SIZE) != 0) {
        //printk("SHA-256 GetHash test FAIL\n");
        return -1;
    }
    //printk("SHA-256 GetHash test pass\n");

    ret = wc_InitSha256_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha256Copy(&ctx, &ctx_copy);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx_copy, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx_copy, digest2);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA256_SIZE) != 0) {
        //printk("SHA-256 Copy test FAIL\n");
        return -1;
    }
    //printk("SHA-256 Copy test pass\n");

    ret = wc_InitSha256_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha256FinalRaw(&ctx, raw1);
    if (ret != 0) return ret;
    ret = wc_InitSha256_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha256Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha256FinalRaw(&ctx_ref, raw2);
    if (ret != 0) return ret;
    if (memcmp(raw1, raw2, TEST_SHA256_SIZE) != 0) {
        //printk("SHA-256 FinalRaw test FAIL\n");
        return -1;
    }
    //printk("SHA-256 FinalRaw test pass\n");

    wc_Sha256Free(&ctx);
    wc_Sha256Free(&ctx_ref);
    wc_Sha256Free(&ctx_copy);
    return 0;
}

static int sha384_api_test(void)
{
#ifdef WOLFSSL_SHA384
    wc_Sha384 ctx, ctx_ref, ctx_copy;
    uint8_t digest1[TEST_SHA384_SIZE];
    uint8_t digest2[TEST_SHA384_SIZE];
    uint8_t raw1[WC_SHA512_DIGEST_SIZE];
    uint8_t raw2[WC_SHA512_DIGEST_SIZE];
    int ret;

    ret = wc_InitSha384_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx_ref, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx_ref, digest2);
    if (ret != 0) return ret;

    ret = wc_InitSha384_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha384GetHash(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx, digest1);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA384_SIZE) != 0) {
        //printk("SHA-384 GetHash test FAIL\n");
        return -1;
    }
    //printk("SHA-384 GetHash test pass\n");

    ret = wc_InitSha384_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha384Copy(&ctx, &ctx_copy);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx_copy, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx_copy, digest2);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA384_SIZE) != 0) {
        //printk("SHA-384 Copy test FAIL\n");
        return -1;
    }
    //printk("SHA-384 Copy test pass\n");

    ret = wc_InitSha384_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha384FinalRaw(&ctx, raw1);
    if (ret != 0) return ret;
    ret = wc_InitSha384_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha384Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha384FinalRaw(&ctx_ref, raw2);
    if (ret != 0) return ret;
    if (memcmp(raw1, raw2, TEST_SHA384_SIZE) != 0) {
        //printk("SHA-384 FinalRaw test FAIL\n");
        return -1;
    }
    //printk("SHA-384 FinalRaw test pass\n");

    wc_Sha384Free(&ctx);
    wc_Sha384Free(&ctx_ref);
    wc_Sha384Free(&ctx_copy);
#else
    //printk("SHA-384 API test skipped\n");
    return 1;
#endif
    return 0;
}

static int sha512_api_test(void)
{
#ifdef WOLFSSL_SHA512
    wc_Sha512 ctx, ctx_ref, ctx_copy;
    uint8_t digest1[TEST_SHA512_SIZE];
    uint8_t digest2[TEST_SHA512_SIZE];
    uint8_t raw1[TEST_SHA512_SIZE];
    uint8_t raw2[TEST_SHA512_SIZE];
    int ret;

    ret = wc_InitSha512_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx_ref, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx_ref, digest2);
    if (ret != 0) return ret;

    ret = wc_InitSha512_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha512GetHash(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx, digest1);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA512_SIZE) != 0) {
        //printk("SHA-512 GetHash test FAIL\n");
        return -1;
    }
    //printk("SHA-512 GetHash test pass\n");

    ret = wc_InitSha512_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha512Copy(&ctx, &ctx_copy);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx_copy, (const byte *)API_TEST_CHUNK2, API_TEST_LEN2);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx, digest1);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx_copy, digest2);
    if (ret != 0) return ret;
    if (memcmp(digest1, digest2, TEST_SHA512_SIZE) != 0) {
        //printk("SHA-512 Copy test FAIL\n");
        return -1;
    }
    //printk("SHA-512 Copy test pass\n");

    ret = wc_InitSha512_ex(&ctx, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha512FinalRaw(&ctx, raw1);
    if (ret != 0) return ret;
    ret = wc_InitSha512_ex(&ctx_ref, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_Sha512Update(&ctx_ref, (const byte *)API_TEST_CHUNK1, API_TEST_LEN1);
    if (ret != 0) return ret;
    ret = wc_Sha512FinalRaw(&ctx_ref, raw2);
    if (ret != 0) return ret;
    if (memcmp(raw1, raw2, TEST_SHA512_SIZE) != 0) {
        //printk("SHA-512 FinalRaw test FAIL\n");
        return -1;
    }
    //printk("SHA-512 FinalRaw test pass\n");

    wc_Sha512Free(&ctx);
    wc_Sha512Free(&ctx_ref);
    wc_Sha512Free(&ctx_copy);
#else
    //printk("SHA-512 API test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-224 and SHA-256 contexts updated
 * alternately must not corrupt each other (they share the same OTBN
 * SHA-256 firmware but use independent contexts).
 * =================================================================== */
static int cross_algo_224_256_test(void)
{
#ifdef WOLFSSL_SHA224
    wc_Sha224 ctx224;
    wc_Sha256 ctx256;
    uint8_t ref224[TEST_SHA224_SIZE];
    uint8_t ref256[TEST_SHA256_SIZE];
    uint8_t out224[TEST_SHA224_SIZE];
    uint8_t out256[TEST_SHA256_SIZE];
    const byte *data224 = (const byte *)"abcdef";
    const byte *data256 = (const byte *)"123456";
    int ret;

    /* Interleaved updates */
    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha224Update(&ctx224, data224, 6);
        if (ret != 0) return ret;
        ret = wc_Sha256Update(&ctx256, data256, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha224Final(&ctx224, out224);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx256, out256);
    if (ret != 0) return ret;

    /* Sequential references */
    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha224Update(&ctx224, data224, 6);
    wc_Sha224Final(&ctx224, ref224);

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx256, data256, 6);
    wc_Sha256Final(&ctx256, ref256);

    if (memcmp(out224, ref224, TEST_SHA224_SIZE) != 0 ||
        memcmp(out256, ref256, TEST_SHA256_SIZE) != 0) {
        //printk("Cross-algo SHA-224/256 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-224/256 interference test pass\n");
#else
    //printk("Cross-algo SHA-224/256 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-256 and SHA-512 contexts updated
 * alternately (different firmware families).
 * =================================================================== */
static int cross_algo_256_512_test(void)
{
#if defined(WOLFSSL_SHA512)
    wc_Sha256 ctx256;
    wc_Sha512 ctx512;
    uint8_t ref256[TEST_SHA256_SIZE];
    uint8_t ref512[TEST_SHA512_SIZE];
    uint8_t out256[TEST_SHA256_SIZE];
    uint8_t out512[TEST_SHA512_SIZE];
    const byte *data256 = (const byte *)"abcdef";
    const byte *data512 = (const byte *)"123456";
    int ret;

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha256Update(&ctx256, data256, 6);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx512, data512, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha256Final(&ctx256, out256);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx512, out512);
    if (ret != 0) return ret;

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx256, data256, 6);
    wc_Sha256Final(&ctx256, ref256);

    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx512, data512, 6);
    wc_Sha512Final(&ctx512, ref512);

    if (memcmp(out256, ref256, TEST_SHA256_SIZE) != 0 ||
        memcmp(out512, ref512, TEST_SHA512_SIZE) != 0) {
        //printk("Cross-algo SHA-256/512 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-256/512 interference test pass\n");
#else
    //printk("Cross-algo SHA-256/512 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-384 and SHA-512 contexts updated
 * alternately (same firmware family, different IVs).
 * =================================================================== */
static int cross_algo_384_512_test(void)
{
#if defined(WOLFSSL_SHA384) && defined(WOLFSSL_SHA512)
    wc_Sha384 ctx384;
    wc_Sha512 ctx512;
    uint8_t ref384[TEST_SHA384_SIZE];
    uint8_t ref512[TEST_SHA512_SIZE];
    uint8_t out384[TEST_SHA384_SIZE];
    uint8_t out512[TEST_SHA512_SIZE];
    const byte *data384 = (const byte *)"abcdef";
    const byte *data512 = (const byte *)"123456";
    int ret;

    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha384Update(&ctx384, data384, 6);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx512, data512, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha384Final(&ctx384, out384);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx512, out512);
    if (ret != 0) return ret;

    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha384Update(&ctx384, data384, 6);
    wc_Sha384Final(&ctx384, ref384);

    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx512, data512, 6);
    wc_Sha512Final(&ctx512, ref512);

    if (memcmp(out384, ref384, TEST_SHA384_SIZE) != 0 ||
        memcmp(out512, ref512, TEST_SHA512_SIZE) != 0) {
        //printk("Cross-algo SHA-384/512 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-384/512 interference test pass\n");
#else
    //printk("Cross-algo SHA-384/512 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-224 and SHA-512 contexts updated
 * alternately (smallest vs largest block size).
 * =================================================================== */
static int cross_algo_224_512_test(void)
{
#if defined(WOLFSSL_SHA224) && defined(WOLFSSL_SHA512)
    wc_Sha224 ctx224;
    wc_Sha512 ctx512;
    uint8_t ref224[TEST_SHA224_SIZE];
    uint8_t ref512[TEST_SHA512_SIZE];
    uint8_t out224[TEST_SHA224_SIZE];
    uint8_t out512[TEST_SHA512_SIZE];
    const byte *data224 = (const byte *)"abcdef";
    const byte *data512 = (const byte *)"123456";
    int ret;

    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha224Update(&ctx224, data224, 6);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx512, data512, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha224Final(&ctx224, out224);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx512, out512);
    if (ret != 0) return ret;

    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha224Update(&ctx224, data224, 6);
    wc_Sha224Final(&ctx224, ref224);

    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx512, data512, 6);
    wc_Sha512Final(&ctx512, ref512);

    if (memcmp(out224, ref224, TEST_SHA224_SIZE) != 0 ||
        memcmp(out512, ref512, TEST_SHA512_SIZE) != 0) {
        //printk("Cross-algo SHA-224/512 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-224/512 interference test pass\n");
#else
    //printk("Cross-algo SHA-224/512 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-224 and SHA-384 contexts updated
 * alternately.
 * =================================================================== */
static int cross_algo_224_384_test(void)
{
#if defined(WOLFSSL_SHA224) && defined(WOLFSSL_SHA384)
    wc_Sha224 ctx224;
    wc_Sha384 ctx384;
    uint8_t ref224[TEST_SHA224_SIZE];
    uint8_t ref384[TEST_SHA384_SIZE];
    uint8_t out224[TEST_SHA224_SIZE];
    uint8_t out384[TEST_SHA384_SIZE];
    const byte *data224 = (const byte *)"abcdef";
    const byte *data384 = (const byte *)"123456";
    int ret;

    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha224Update(&ctx224, data224, 6);
        if (ret != 0) return ret;
        ret = wc_Sha384Update(&ctx384, data384, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha224Final(&ctx224, out224);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx384, out384);
    if (ret != 0) return ret;

    ret = wc_InitSha224_ex(&ctx224, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha224Update(&ctx224, data224, 6);
    wc_Sha224Final(&ctx224, ref224);

    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha384Update(&ctx384, data384, 6);
    wc_Sha384Final(&ctx384, ref384);

    if (memcmp(out224, ref224, TEST_SHA224_SIZE) != 0 ||
        memcmp(out384, ref384, TEST_SHA384_SIZE) != 0) {
        //printk("Cross-algo SHA-224/384 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-224/384 interference test pass\n");
#else
    //printk("Cross-algo SHA-224/384 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SHA-256 and SHA-384 contexts updated
 * alternately.
 * =================================================================== */
static int cross_algo_256_384_test(void)
{
#if defined(WOLFSSL_SHA384)
    wc_Sha256 ctx256;
    wc_Sha384 ctx384;
    uint8_t ref256[TEST_SHA256_SIZE];
    uint8_t ref384[TEST_SHA384_SIZE];
    uint8_t out256[TEST_SHA256_SIZE];
    uint8_t out384[TEST_SHA384_SIZE];
    const byte *data256 = (const byte *)"abcdef";
    const byte *data384 = (const byte *)"123456";
    int ret;

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sha256Update(&ctx256, data256, 6);
        if (ret != 0) return ret;
        ret = wc_Sha384Update(&ctx384, data384, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sha256Final(&ctx256, out256);
    if (ret != 0) return ret;
    ret = wc_Sha384Final(&ctx384, out384);
    if (ret != 0) return ret;

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx256, data256, 6);
    wc_Sha256Final(&ctx256, ref256);

    ret = wc_InitSha384_ex(&ctx384, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha384Update(&ctx384, data384, 6);
    wc_Sha384Final(&ctx384, ref384);

    if (memcmp(out256, ref256, TEST_SHA256_SIZE) != 0 ||
        memcmp(out384, ref384, TEST_SHA384_SIZE) != 0) {
        //printk("Cross-algo SHA-256/384 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SHA-256/384 interference test pass\n");
#else
    //printk("Cross-algo SHA-256/384 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SM3 and SHA-256 contexts updated alternately.
 * =================================================================== */
static int cross_algo_sm3_256_test(void)
{
#if defined(WOLFSSL_SM3)
    wc_Sm3 ctx_sm3;
    wc_Sha256 ctx256;
    uint8_t ref_sm3[TEST_SM3_SIZE];
    uint8_t ref256[TEST_SHA256_SIZE];
    uint8_t out_sm3[TEST_SM3_SIZE];
    uint8_t out256[TEST_SHA256_SIZE];
    const byte *data_sm3 = (const byte *)"abcdef";
    const byte *data256 = (const byte *)"123456";
    int ret;

    ret = wc_InitSm3(&ctx_sm3, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sm3Update(&ctx_sm3, data_sm3, 6);
        if (ret != 0) return ret;
        ret = wc_Sha256Update(&ctx256, data256, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sm3Final(&ctx_sm3, out_sm3);
    if (ret != 0) return ret;
    ret = wc_Sha256Final(&ctx256, out256);
    if (ret != 0) return ret;

    ret = wc_InitSm3(&ctx_sm3, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sm3Update(&ctx_sm3, data_sm3, 6);
    wc_Sm3Final(&ctx_sm3, ref_sm3);

    ret = wc_InitSha256_ex(&ctx256, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha256Update(&ctx256, data256, 6);
    wc_Sha256Final(&ctx256, ref256);

    if (memcmp(out_sm3, ref_sm3, TEST_SM3_SIZE) != 0 ||
        memcmp(out256, ref256, TEST_SHA256_SIZE) != 0) {
        //printk("Cross-algo SM3/SHA-256 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SM3/SHA-256 interference test pass\n");
#else
    //printk("Cross-algo SM3/SHA-256 interference test skipped\n");
    return 1;
#endif
    return 0;
}

/* ===================================================================
 * Cross-algo interference: SM3 and SHA-512 contexts updated alternately.
 * =================================================================== */
static int cross_algo_sm3_512_test(void)
{
#if defined(WOLFSSL_SM3) && defined(WOLFSSL_SHA512)
    wc_Sm3 ctx_sm3;
    wc_Sha512 ctx512;
    uint8_t ref_sm3[TEST_SM3_SIZE];
    uint8_t ref512[TEST_SHA512_SIZE];
    uint8_t out_sm3[TEST_SM3_SIZE];
    uint8_t out512[TEST_SHA512_SIZE];
    const byte *data_sm3 = (const byte *)"abcdef";
    const byte *data512 = (const byte *)"123456";
    int ret;

    ret = wc_InitSm3(&ctx_sm3, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) {
        ret = wc_Sm3Update(&ctx_sm3, data_sm3, 6);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx512, data512, 6);
        if (ret != 0) return ret;
    }
    ret = wc_Sm3Final(&ctx_sm3, out_sm3);
    if (ret != 0) return ret;
    ret = wc_Sha512Final(&ctx512, out512);
    if (ret != 0) return ret;

    ret = wc_InitSm3(&ctx_sm3, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sm3Update(&ctx_sm3, data_sm3, 6);
    wc_Sm3Final(&ctx_sm3, ref_sm3);

    ret = wc_InitSha512_ex(&ctx512, NULL, INVALID_DEVID);
    if (ret != 0) return ret;
    for (int i = 0; i < 20; i++) wc_Sha512Update(&ctx512, data512, 6);
    wc_Sha512Final(&ctx512, ref512);

    if (memcmp(out_sm3, ref_sm3, TEST_SM3_SIZE) != 0 ||
        memcmp(out512, ref512, TEST_SHA512_SIZE) != 0) {
        //printk("Cross-algo SM3/SHA-512 interference test FAIL\n");
        return -1;
    }
    //printk("Cross-algo SM3/SHA-512 interference test pass\n");
#else
    //printk("Cross-algo SM3/SHA-512 interference test skipped\n");
    return 1;
#endif
    return 0;
}

#ifdef LITTLE_ENDIAN_ORDER
static void bswap32_buf(const uint8_t *in, uint8_t *out, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 4) {
        out[i]     = in[i + 3];
        out[i + 1] = in[i + 2];
        out[i + 2] = in[i + 1];
        out[i + 3] = in[i];
    }
}

static void bswap64_buf(const uint8_t *in, uint8_t *out, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 8) {
        out[i]     = in[i + 7];
        out[i + 1] = in[i + 6];
        out[i + 2] = in[i + 5];
        out[i + 3] = in[i + 4];
        out[i + 4] = in[i + 3];
        out[i + 5] = in[i + 2];
        out[i + 6] = in[i + 1];
        out[i + 7] = in[i];
    }
}
#endif

/* ===================================================================
 * Transform API test (wc_Sha256Transform / wc_Sha512Transform).
 * These interfaces are only available when OPENSSL_EXTRA or HAVE_CURL
 * is enabled.  The public API expects the block in host-endian word
 * order, so on little-endian hosts we byte-swap before feeding OTBN.
 * =================================================================== */
static int transform_api_test(void)
{
#if !defined(WOLFSSL_KCAPI_HASH) && !defined(WOLFSSL_AFALG_HASH) && \
    (defined(OPENSSL_EXTRA) || defined(HAVE_CURL))
    int ret;

    /* SHA-256 Transform vs Update */
    {
        wc_Sha256 ctx_t, ctx_u;
        uint8_t block[WC_SHA256_BLOCK_SIZE];
        uint8_t host_block[WC_SHA256_BLOCK_SIZE];
        uint8_t raw_t[TEST_SHA256_SIZE];
        uint8_t raw_u[TEST_SHA256_SIZE];

        fill_plaintext(sizeof(block));
        memcpy(block, plaintext_buf, sizeof(block));
#ifdef LITTLE_ENDIAN_ORDER
        bswap32_buf(block, host_block, sizeof(block));
#else
        memcpy(host_block, block, sizeof(block));
#endif

        ret = wc_InitSha256_ex(&ctx_t, NULL, INVALID_DEVID);
        if (ret != 0) return ret;
        ret = wc_Sha256Transform(&ctx_t, host_block);
        if (ret != 0) return ret;
        ret = wc_Sha256FinalRaw(&ctx_t, raw_t);
        if (ret != 0) return ret;

        ret = wc_InitSha256_ex(&ctx_u, NULL, INVALID_DEVID);
        if (ret != 0) return ret;
        ret = wc_Sha256Update(&ctx_u, block, sizeof(block));
        if (ret != 0) return ret;
        ret = wc_Sha256FinalRaw(&ctx_u, raw_u);
        if (ret != 0) return ret;

        if (memcmp(raw_t, raw_u, TEST_SHA256_SIZE) != 0) {
            //printk("SHA-256 Transform test FAIL\n");
            return -1;
        }
        //printk("SHA-256 Transform test pass\n");
    }

    /* SHA-512 Transform vs Update */
    {
        wc_Sha512 ctx_t, ctx_u;
        uint8_t block[WC_SHA512_BLOCK_SIZE];
        uint8_t host_block[WC_SHA512_BLOCK_SIZE];
        uint8_t raw_t[TEST_SHA512_SIZE];
        uint8_t raw_u[TEST_SHA512_SIZE];

        fill_plaintext(sizeof(block));
        memcpy(block, plaintext_buf, sizeof(block));
#ifdef LITTLE_ENDIAN_ORDER
        bswap64_buf(block, host_block, sizeof(block));
#else
        memcpy(host_block, block, sizeof(block));
#endif

        ret = wc_InitSha512_ex(&ctx_t, NULL, INVALID_DEVID);
        if (ret != 0) return ret;
        ret = wc_Sha512Transform(&ctx_t, host_block);
        if (ret != 0) return ret;
        ret = wc_Sha512FinalRaw(&ctx_t, raw_t);
        if (ret != 0) return ret;

        ret = wc_InitSha512_ex(&ctx_u, NULL, INVALID_DEVID);
        if (ret != 0) return ret;
        ret = wc_Sha512Update(&ctx_u, block, sizeof(block));
        if (ret != 0) return ret;
        ret = wc_Sha512FinalRaw(&ctx_u, raw_u);
        if (ret != 0) return ret;

        if (memcmp(raw_t, raw_u, TEST_SHA512_SIZE) != 0) {
            //printk("SHA-512 Transform test FAIL\n");
            return -1;
        }
        //printk("SHA-512 Transform test pass\n");
    }

    return 0;
#else
    //printk("Transform API test skipped\n");
    return 1;
#endif
}

/* ===================================================================
 * Test runner helper: negative = fail, 0 = pass, positive = skipped.
 * =================================================================== */
static int run_test(int (*test_fn)(void), int *failures, int *skips)
{
    int ret = test_fn();
    if (ret < 0) {
        (*failures)++;
        while(1);
    } else if (ret > 0) {
        (*skips)++;
    }
    return ret;
}

/* ===================================================================
 * Entry point
 * =================================================================== */
int main(void)
{
    int failures = 0;
    int skips = 0;
    fill_plaintext(10240);
    int64_t start_ms = k_uptime_get();

    printk("hash alignment test start\n");

#if defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA224_SHA256_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA384_SHA512_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SM3_ALT)
    printk("wolfSSL hash path: OTBN hardware\n");
#else
    printk("wolfSSL hash path: software\n");
#endif
#ifdef CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT
    printk("mbedtls SHA-256/SM3 path: OTBN hardware\n");
#else
    printk("mbedtls SHA-256/SM3 path: software\n");
#endif

#ifdef CONFIG_MBEDTLS_SHA384_SHA512_LINKEDSEMI_OTBN_ALT
    printk("mbedtls SHA-384/SHA-512 path: OTBN hardware\n");
#else
    printk("mbedtls SHA-384/SHA-512 path: software\n");
#endif

    for (uint32_t i = 0; i < NUM_TEST_LENGTHS; i++) {
        uint32_t len = test_lengths[i];
//         if (sha224_test_once(len) != 0) failures++;
//         if (sha256_test_once(len) != 0) failures++;
// #if defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
//         if (sm3_test_once(len) != 0) failures++;
// #endif
        if (sha384_test_once(len) != 0) failures++;
        if (sha512_test_once(len) != 0) failures++;
    }

#if defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA224_SHA256_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA384_SHA512_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SM3_ALT)
    printk(" ls hardware hash does not sopport interleaved test\n");
#else
    if (interleaved_sha224_test() != 0) failures++;
    if (interleaved_sha256_test() != 0) failures++;
#if defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
    if (interleaved_sm3_test() != 0) failures++;
#endif
    if (interleaved_sha384_test() != 0) failures++;
    if (interleaved_sha512_test() != 0) failures++;
#endif

    run_test(sha224_api_test, &failures, &skips);
    run_test(sha256_api_test, &failures, &skips);
    run_test(sha384_api_test, &failures, &skips);
    run_test(sha512_api_test, &failures, &skips);
    run_test(transform_api_test, &failures, &skips);
#if defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA224_SHA256_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SHA384_SHA512_ALT) || \
    defined(CONFIG_WOLFSSL_LINKEDSEMI_OTBN_SM3_ALT)
    printk(" ls hardware hash does not sopport interleaved test\n");
#else
    run_test(cross_algo_224_256_test, &failures, &skips);
    run_test(cross_algo_224_384_test, &failures, &skips);
    run_test(cross_algo_256_384_test, &failures, &skips);
    run_test(cross_algo_256_512_test, &failures, &skips);
    run_test(cross_algo_384_512_test, &failures, &skips);
    run_test(cross_algo_224_512_test, &failures, &skips);
#if defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
    run_test(cross_algo_sm3_256_test, &failures, &skips);
    run_test(cross_algo_sm3_512_test, &failures, &skips);
#endif
#endif

    int64_t elapsed_ms = k_uptime_get() - start_ms;
    printk("\n---------tests ended---------\n");

    if (failures == 0 && skips == 0) {
        printk("All tests passed\n");
    } else {
        printk("%d test(s) failed\n", failures);
        printk("%d test(s) skipped\n",skips);
    }
    printk("total test time: %lld.%03lld s\n", elapsed_ms / 1000, elapsed_ms % 1000);
    return 0;
}
