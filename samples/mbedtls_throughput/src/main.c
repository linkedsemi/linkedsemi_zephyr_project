/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>
#include <zephyr/timing/timing.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/aes.h"
#if defined(MBEDTLS_ECDSA_C)
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#endif
#if defined(MBEDTLS_RSA_C)
#include "mbedtls/rsa.h"
#include "rsa_test_key.h"
#include <zephyr/drivers/entropy.h>
#include <zephyr/device.h>
#include "ls_otbn_config.h"
#endif
#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
#include "mbedtls/sm4_alt.h"
#endif

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#define SHA512_DIGEST_SIZE  64
#define SHA256_DIGEST_SIZE  32
#define SM3_DIGEST_SIZE     32
#define TEST_TOTAL_SIZE     KB(256)     /* 总数据量 */

__attribute__((aligned(32))) static uint8_t big_buffer[TEST_TOTAL_SIZE + 1];

static const uint32_t step_sizes[] = {
    KB(1), KB(2), KB(4), KB(8),
    KB(16), KB(32), KB(64), KB(128), KB(256)
};

static int test_mbedtls_sha256(uint32_t step_size)
{
    mbedtls_sha256_context sha;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SHA256_DIGEST_SIZE];

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sha256_init(&sha);

    start_time = timing_counter_get();

    if (mbedtls_sha256_starts(&sha, false) != 0) {
        printk("mbedtls SHA256 starts error\n");
        mbedtls_sha256_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sha256_update(&sha, (uint8_t *)big_buffer + off, len) != 0) {
            printk("mbedtls SHA256 update error at offset %d\n", off);
            mbedtls_sha256_free(&sha);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sha256_finish(&sha, hash_result) != 0) {
        printk("mbedtls SHA256 finish error\n");
        mbedtls_sha256_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha256_free(&sha);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}

static int test_mbedtls_sha512(uint32_t step_size)
{
    mbedtls_sha512_context sha;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SHA512_DIGEST_SIZE];

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sha512_init(&sha);

    start_time = timing_counter_get();

    if (mbedtls_sha512_starts(&sha, false) != 0) {
        printk("mbedtls SHA512 starts error\n");
        mbedtls_sha512_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sha512_update(&sha, (uint8_t *)big_buffer + off, len) != 0) {
            printk("mbedtls SHA512 update error at offset %d\n", off);
            mbedtls_sha512_free(&sha);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sha512_finish(&sha, hash_result) != 0) {
        printk("mbedtls SHA512 finish error\n");
        mbedtls_sha512_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha512_free(&sha);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}

static int test_mbedtls_sha256_unaligned(uint32_t step_size)
{
    mbedtls_sha256_context sha;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SHA256_DIGEST_SIZE];
    const uint8_t *input = big_buffer + 1;

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sha256_init(&sha);

    start_time = timing_counter_get();

    if (mbedtls_sha256_starts(&sha, false) != 0) {
        printk("mbedtls SHA256 unaligned starts error\n");
        mbedtls_sha256_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sha256_update(&sha, input + off, len) != 0) {
            printk("mbedtls SHA256 unaligned update error at offset %d\n", off);
            mbedtls_sha256_free(&sha);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sha256_finish(&sha, hash_result) != 0) {
        printk("mbedtls SHA256 unaligned finish error\n");
        mbedtls_sha256_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha256_free(&sha);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}

static int test_mbedtls_sha512_unaligned(uint32_t step_size)
{
    mbedtls_sha512_context sha;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SHA512_DIGEST_SIZE];
    const uint8_t *input = big_buffer + 1;

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sha512_init(&sha);

    start_time = timing_counter_get();

    if (mbedtls_sha512_starts(&sha, false) != 0) {
        printk("mbedtls SHA512 unaligned starts error\n");
        mbedtls_sha512_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sha512_update(&sha, input + off, len) != 0) {
            printk("mbedtls SHA512 unaligned update error at offset %d\n", off);
            mbedtls_sha512_free(&sha);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sha512_finish(&sha, hash_result) != 0) {
        printk("mbedtls SHA512 unaligned finish error\n");
        mbedtls_sha512_free(&sha);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sha512_free(&sha);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}

#if defined(CONFIG_MBEDTLS_SHA224_SHA256_SM3_LINKEDSEMI_HARDWARE_ALT) || \
    defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
static int test_mbedtls_sm3(uint32_t step_size)
{
    mbedtls_sha256_context sm3;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SM3_DIGEST_SIZE];

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sm3_init(&sm3);

    start_time = timing_counter_get();

    if (mbedtls_sm3_starts(&sm3) != 0) {
        printk("mbedtls SM3 starts error\n");
        mbedtls_sm3_free(&sm3);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sm3_update(&sm3, (uint8_t *)big_buffer + off, len) != 0) {
            printk("mbedtls SM3 update error at offset %d\n", off);
            mbedtls_sm3_free(&sm3);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sm3_finish(&sm3, hash_result) != 0) {
        printk("mbedtls SM3 finish error\n");
        mbedtls_sm3_free(&sm3);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sm3_free(&sm3);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}

static int test_mbedtls_sm3_unaligned(uint32_t step_size)
{
    mbedtls_sha256_context sm3;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SM3_DIGEST_SIZE];
    uint8_t *input = big_buffer + 1;

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    mbedtls_sm3_init(&sm3);

    start_time = timing_counter_get();

    if (mbedtls_sm3_starts(&sm3) != 0) {
        printk("mbedtls SM3 unaligned starts error\n");
        mbedtls_sm3_free(&sm3);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    for (int off = 0; off < TEST_TOTAL_SIZE; off += step_size) {
        size_t len = MIN(step_size, TEST_TOTAL_SIZE - off);

        start_time = timing_counter_get();

        if (mbedtls_sm3_update(&sm3, input + off, len) != 0) {
            printk("mbedtls SM3 unaligned update error at offset %d\n", off);
            mbedtls_sm3_free(&sm3);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += len;
    }

    start_time = timing_counter_get();

    if (mbedtls_sm3_finish(&sm3, hash_result) != 0) {
        printk("mbedtls SM3 unaligned finish error\n");
        mbedtls_sm3_free(&sm3);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    mbedtls_sm3_free(&sm3);

    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_mbs_x100 = 0;

    if (total_us > 0) {
        throughput_mbs_x100 = (total_bytes * 1000000ULL * 100ULL) /
                              (total_us * 1024ULL * 1024ULL);
    }

    printk("  %5u   %7llu   %4llu.%02llu\n",
           step_size / 1024, total_us,
           throughput_mbs_x100 / 100ULL, throughput_mbs_x100 % 100ULL);

    return 0;
}
#endif /* SM3 hardware/OTBN alt */

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE   16
#define SM4_BLOCK_SIZE 16
#define SM4_KEY_SIZE   16
#define SM4_MAX_CHUNK  4096    /* SM4 hw alt: max 256 blocks (256*16 B) per call */
#define CIPHER_RUNS    5

static const uint8_t aes_key[AES_KEY_SIZE] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66
};

static const uint8_t aes256_key[32] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50
};

static const uint8_t aes_iv[AES_BLOCK_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

static const uint8_t sm4_key[SM4_KEY_SIZE] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
};

static const uint8_t sm4_iv[SM4_BLOCK_SIZE] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

static const uint8_t aes_xts_key[32] = {
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50
};

static const uint8_t aes_xts_data_unit[AES_BLOCK_SIZE] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

static uint64_t throughput_from_cycles(uint64_t cycles, uint64_t bytes)
{
    uint32_t cpu_freq_hz = sys_clock_hw_cycles_per_sec();
    uint64_t total_ns = (cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;

    if (total_us == 0) {
        return 0;
    }
    return (bytes * 1000000ULL * 100ULL) /
           (total_us * 1024ULL * 1024ULL);
}

static int test_mbedtls_aes_ecb_avg_with_key(const uint8_t *key,
                                              unsigned int keybits,
                                              const char *name,
                                              int mode)
{
    mbedtls_aes_context aes;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    int key_ret;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));

        mbedtls_aes_init(&aes);
        if (mode == MBEDTLS_AES_ENCRYPT) {
            key_ret = mbedtls_aes_setkey_enc(&aes, key, keybits);
        } else {
            key_ret = mbedtls_aes_setkey_dec(&aes, key, keybits);
        }
        if (key_ret != 0) {
            printk("mbedtls %s ECB %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        for (size_t i = 0; i < TEST_TOTAL_SIZE; i += AES_BLOCK_SIZE) {
            if (mbedtls_aes_crypt_ecb(&aes, mode,
                                      big_buffer + i,
                                      big_buffer + i) != 0) {
                printk("mbedtls %s ECB %s error\n", name, mode_str);
                mbedtls_aes_free(&aes);
                return -1;
            }
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s ECB %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_ecb_avg(void)
{
    return test_mbedtls_aes_ecb_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_ecb_decrypt_avg(void)
{
    return test_mbedtls_aes_ecb_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes256_ecb_avg(void)
{
    return test_mbedtls_aes_ecb_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_ecb_decrypt_avg(void)
{
    return test_mbedtls_aes_ecb_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes_cbc_avg_with_key(const uint8_t *key,
                                              unsigned int keybits,
                                              const char *name,
                                              int mode)
{
    mbedtls_aes_context aes;
    uint8_t iv[AES_BLOCK_SIZE];
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    int key_ret;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, aes_iv, AES_BLOCK_SIZE);

        mbedtls_aes_init(&aes);
        if (mode == MBEDTLS_AES_ENCRYPT) {
            key_ret = mbedtls_aes_setkey_enc(&aes, key, keybits);
        } else {
            key_ret = mbedtls_aes_setkey_dec(&aes, key, keybits);
        }
        if (key_ret != 0) {
            printk("mbedtls %s CBC %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_cbc(&aes, mode, TEST_TOTAL_SIZE, iv,
                                  big_buffer, big_buffer) != 0) {
            printk("mbedtls %s CBC %s error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s CBC %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_cbc_avg(void)
{
    return test_mbedtls_aes_cbc_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_cbc_decrypt_avg(void)
{
    return test_mbedtls_aes_cbc_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes256_cbc_avg(void)
{
    return test_mbedtls_aes_cbc_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_cbc_decrypt_avg(void)
{
    return test_mbedtls_aes_cbc_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes_ctr_avg_with_key(const uint8_t *key,
                                              unsigned int keybits,
                                              const char *name,
                                              int mode)
{
    mbedtls_aes_context aes;
    uint8_t nonce_counter[AES_BLOCK_SIZE];
    uint8_t stream_block[AES_BLOCK_SIZE];
    size_t nc_off;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(nonce_counter, aes_iv, AES_BLOCK_SIZE);
        memset(stream_block, 0, sizeof(stream_block));
        nc_off = 0;

        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key, keybits) != 0) {
            printk("mbedtls %s CTR %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_ctr(
                &aes, TEST_TOTAL_SIZE, &nc_off,
                nonce_counter, stream_block,
                big_buffer, big_buffer) != 0) {
            printk("mbedtls %s CTR %s error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s CTR %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_ctr_avg(void)
{
    return test_mbedtls_aes_ctr_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_ctr_decrypt_avg(void)
{
    return test_mbedtls_aes_ctr_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_ctr_avg(void)
{
    return test_mbedtls_aes_ctr_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_ctr_decrypt_avg(void)
{
    return test_mbedtls_aes_ctr_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
static int test_mbedtls_sm4_ctr_avg(void)
{
    mbedtls_sm4_context sm4;
    uint8_t iv[SM4_BLOCK_SIZE];
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, sm4_iv, SM4_BLOCK_SIZE);

        mbedtls_sm4_init(&sm4);
        if (mbedtls_sm4_setkey(sm4_key) != 0) {
            printk("mbedtls SM4 CTR encrypt setkey error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }
        if (mbedtls_sm4_setiv(&sm4, iv) != 0) {
            printk("mbedtls SM4 CTR encrypt setiv error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_sm4_ctr_crypto(
                &sm4, big_buffer, big_buffer, TEST_TOTAL_SIZE) != 0) {
            printk("mbedtls SM4 CTR encrypt error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_sm4_free(&sm4);
    }

    printk("  SM4 CTR encrypt:%4llu.%02llu MB/s (%d runs)\n",
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_sm4_ctr_decrypt_avg(void)
{
    mbedtls_sm4_context sm4;
    uint8_t iv[SM4_BLOCK_SIZE];
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, sm4_iv, SM4_BLOCK_SIZE);

        mbedtls_sm4_init(&sm4);
        if (mbedtls_sm4_setkey(sm4_key) != 0) {
            printk("mbedtls SM4 CTR decrypt setkey error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }
        if (mbedtls_sm4_setiv(&sm4, iv) != 0) {
            printk("mbedtls SM4 CTR decrypt setiv error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_sm4_ctr_crypto(
                &sm4, big_buffer, big_buffer, TEST_TOTAL_SIZE) != 0) {
            printk("mbedtls SM4 CTR decrypt error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_sm4_free(&sm4);
    }

    printk("  SM4 CTR decrypt:%4llu.%02llu MB/s (%d runs)\n",
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_sm4_ecb_encrypt_avg(void)
{
    mbedtls_sm4_context sm4;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));

        mbedtls_sm4_init(&sm4);
        if (mbedtls_sm4_setkey(sm4_key) != 0) {
            printk("mbedtls SM4 ECB encrypt setkey error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }

        start_time = timing_counter_get();
        for (size_t i = 0; i < TEST_TOTAL_SIZE; i += SM4_MAX_CHUNK) {
            size_t chunk = MIN(SM4_MAX_CHUNK, TEST_TOTAL_SIZE - i);
            if (mbedtls_sm4_ecb_encrypt(&sm4, big_buffer + i,
                                        big_buffer + i, chunk) != 0) {
                printk("mbedtls SM4 ECB encrypt error\n");
                mbedtls_sm4_free(&sm4);
                return -1;
            }
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_sm4_free(&sm4);
    }

    printk("  SM4 ECB encrypt:%4llu.%02llu MB/s (%d runs)\n",
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_sm4_ecb_decrypt_avg(void)
{
    mbedtls_sm4_context sm4;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));

        mbedtls_sm4_init(&sm4);
        if (mbedtls_sm4_setkey(sm4_key) != 0) {
            printk("mbedtls SM4 ECB decrypt setkey error\n");
            mbedtls_sm4_free(&sm4);
            return -1;
        }

        start_time = timing_counter_get();
        for (size_t i = 0; i < TEST_TOTAL_SIZE; i += SM4_MAX_CHUNK) {
            size_t chunk = MIN(SM4_MAX_CHUNK, TEST_TOTAL_SIZE - i);
            if (mbedtls_sm4_ecb_decrypt(&sm4, big_buffer + i, big_buffer + i, chunk) != 0) {
                printk("mbedtls SM4 ECB decrypt error\n");
                mbedtls_sm4_free(&sm4);
                return -1;
            }
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_sm4_free(&sm4);
    }

    printk("  SM4 ECB decrypt:%4llu.%02llu MB/s (%d runs)\n",
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}
#endif /* CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT */

static int test_mbedtls_aes_cfb128_avg_with_key(const uint8_t *key,
                                                 unsigned int keybits,
                                                 const char *name,
                                                 int mode)
{
    mbedtls_aes_context aes;
    uint8_t iv[AES_BLOCK_SIZE];
    size_t iv_off;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, aes_iv, AES_BLOCK_SIZE);
        iv_off = 0;

        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key, keybits) != 0) {
            printk("mbedtls %s CFB128 %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_cfb128(
                &aes, mode, TEST_TOTAL_SIZE,
                &iv_off, iv, big_buffer, big_buffer) != 0) {
            printk("mbedtls %s CFB128 %s error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s CFB128 %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_cfb128_avg(void)
{
    return test_mbedtls_aes_cfb128_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                                "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_cfb128_decrypt_avg(void)
{
    return test_mbedtls_aes_cfb128_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                                "AES-128", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes256_cfb128_avg(void)
{
    return test_mbedtls_aes_cfb128_avg_with_key(aes256_key, 256,
                                                "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_cfb128_decrypt_avg(void)
{
    return test_mbedtls_aes_cfb128_avg_with_key(aes256_key, 256,
                                                "AES-256", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes_cfb8_avg_with_key(const uint8_t *key,
                                               unsigned int keybits,
                                               const char *name,
                                               int mode)
{
    mbedtls_aes_context aes;
    uint8_t iv[AES_BLOCK_SIZE];
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, aes_iv, AES_BLOCK_SIZE);

        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key, keybits) != 0) {
            printk("mbedtls %s CFB8 %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_cfb8(
                &aes, mode, TEST_TOTAL_SIZE,
                iv, big_buffer, big_buffer) != 0) {
            printk("mbedtls %s CFB8 %s error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s CFB8 %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_cfb8_avg(void)
{
    return test_mbedtls_aes_cfb8_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                              "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_cfb8_decrypt_avg(void)
{
    return test_mbedtls_aes_cfb8_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                              "AES-128", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes256_cfb8_avg(void)
{
    return test_mbedtls_aes_cfb8_avg_with_key(aes256_key, 256,
                                              "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_cfb8_decrypt_avg(void)
{
    return test_mbedtls_aes_cfb8_avg_with_key(aes256_key, 256,
                                              "AES-256", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes_ofb_avg_with_key(const uint8_t *key,
                                              unsigned int keybits,
                                              const char *name,
                                              int mode)
{
    mbedtls_aes_context aes;
    uint8_t iv[AES_BLOCK_SIZE];
    size_t iv_off;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));
        memcpy(iv, aes_iv, AES_BLOCK_SIZE);
        iv_off = 0;

        mbedtls_aes_init(&aes);
        if (mbedtls_aes_setkey_enc(&aes, key, keybits) != 0) {
            printk("mbedtls %s OFB %s setkey error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_ofb(
                &aes, TEST_TOTAL_SIZE, &iv_off,
                iv, big_buffer, big_buffer) != 0) {
            printk("mbedtls %s OFB %s error\n", name, mode_str);
            mbedtls_aes_free(&aes);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_free(&aes);
    }

    printk("  %s OFB %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_ofb_avg(void)
{
    return test_mbedtls_aes_ofb_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_ofb_decrypt_avg(void)
{
    return test_mbedtls_aes_ofb_avg_with_key(aes_key, AES_KEY_SIZE * 8,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_ofb_avg(void)
{
    return test_mbedtls_aes_ofb_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_ofb_decrypt_avg(void)
{
    return test_mbedtls_aes_ofb_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

#if defined(MBEDTLS_CIPHER_MODE_XTS)
static int test_mbedtls_aes_xts_avg_with_key(const uint8_t *key,
                                              unsigned int keybits,
                                              const char *name,
                                              int mode)
{
    mbedtls_aes_xts_context xts;
    timing_t start_time, end_time;
    uint64_t tp_sum_x100 = 0;
    int key_ret;
    const char *mode_str = (mode == MBEDTLS_AES_ENCRYPT) ? "encrypt" : "decrypt";

    for (int run = 0; run < CIPHER_RUNS; run++) {
        memset(big_buffer, 2, sizeof(big_buffer));

        mbedtls_aes_xts_init(&xts);
        if (mode == MBEDTLS_AES_ENCRYPT) {
            key_ret = mbedtls_aes_xts_setkey_enc(&xts, key, keybits);
        } else {
            key_ret = mbedtls_aes_xts_setkey_dec(&xts, key, keybits);
        }
        if (key_ret != 0) {
            printk("mbedtls %s XTS %s setkey error\n", name, mode_str);
            mbedtls_aes_xts_free(&xts);
            return -1;
        }

        start_time = timing_counter_get();
        if (mbedtls_aes_crypt_xts(
                &xts, mode, TEST_TOTAL_SIZE,
                aes_xts_data_unit, big_buffer, big_buffer) != 0) {
            printk("mbedtls %s XTS %s error\n", name, mode_str);
            mbedtls_aes_xts_free(&xts);
            return -1;
        }
        end_time = timing_counter_get();

        tp_sum_x100 += throughput_from_cycles(
            timing_cycles_get(&start_time, &end_time), TEST_TOTAL_SIZE);

        mbedtls_aes_xts_free(&xts);
    }

    printk("  %s XTS %s:%4llu.%02llu MB/s (%d runs)\n",
           name, mode_str,
           (tp_sum_x100 / CIPHER_RUNS) / 100ULL,
           (tp_sum_x100 / CIPHER_RUNS) % 100ULL,
           CIPHER_RUNS);
    return 0;
}

static int test_mbedtls_aes_xts_avg(void)
{
    return test_mbedtls_aes_xts_avg_with_key(aes_xts_key, 256,
                                             "AES-128", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes_xts_decrypt_avg(void)
{
    return test_mbedtls_aes_xts_avg_with_key(aes_xts_key, 256,
                                             "AES-128", MBEDTLS_AES_DECRYPT);
}

static int test_mbedtls_aes256_xts_avg(void)
{
    return test_mbedtls_aes_xts_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_ENCRYPT);
}

static int test_mbedtls_aes256_xts_decrypt_avg(void)
{
    return test_mbedtls_aes_xts_avg_with_key(aes256_key, 256,
                                             "AES-256", MBEDTLS_AES_DECRYPT);
}
#endif /* MBEDTLS_CIPHER_MODE_XTS */

#if defined(MBEDTLS_ECDSA_C)
#define ECDSA_PERF_LOOPS    5

static int ls_mbedtls_get_random(void *null, unsigned char *buf, size_t size)
{
    (void)null;

    for (size_t i = 0; i < size; i++) {
        buf[i] = (unsigned char)(0x55 * i);
    }
    return 0;
}

static int ls_mbedtls_get_fixed(void *null, unsigned char *buf, size_t size)
{
    (void)null;
    memset(buf, 0x55, size);
    return 0;
}

static int test_mbedtls_ecdsa_curve(mbedtls_ecp_group_id curve, const char *name)
{
    mbedtls_ecdsa_context ctx;
    mbedtls_mpi r, s;
    uint8_t hash[32];
    int err = 0;
    int64_t start, end;
    uint32_t ms;

    memset(hash, 0x12, sizeof(hash));

    mbedtls_ecdsa_init(&ctx);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    err = mbedtls_ecp_group_load(&ctx.private_grp, curve);
    if (err != 0) {
        printk("  %s group load error: %d\n", name, err);
        goto exit;
    }

    err = mbedtls_ecdsa_genkey(&ctx, curve, ls_mbedtls_get_random, NULL);
    if (err != 0) {
        printk("  %s keygen init error: %d\n", name, err);
        goto exit;
    }

    start = k_uptime_get();
    for (int i = 0; i < ECDSA_PERF_LOOPS; i++) {
        err = mbedtls_ecdsa_genkey(&ctx, curve, ls_mbedtls_get_random, NULL);
        if (err != 0) {
            printk("  %s keygen error: %d\n", name, err);
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  %s keygen:%5d ops/s (%d loops, %d ms)\n",
           name, (int)(ECDSA_PERF_LOOPS * 1000ULL / ms),
           ECDSA_PERF_LOOPS, ms);

    start = k_uptime_get();
    for (int i = 0; i < ECDSA_PERF_LOOPS; i++) {
        err = mbedtls_ecdsa_sign(&ctx.private_grp, &r, &s,
                                 &ctx.private_d, hash, sizeof(hash),
                                 ls_mbedtls_get_fixed, NULL);
        if (err != 0) {
            printk("  %s sign error: %d\n", name, err);
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  %s sign:  %5d ops/s (%d loops, %d ms)\n",
           name, (int)(ECDSA_PERF_LOOPS * 1000ULL / ms),
           ECDSA_PERF_LOOPS, ms);

    start = k_uptime_get();
    for (int i = 0; i < ECDSA_PERF_LOOPS; i++) {
        err = mbedtls_ecdsa_verify(&ctx.private_grp, hash, sizeof(hash),
                                   &ctx.private_Q, &r, &s);
        if (err != 0) {
            printk("  %s verify error: %d\n", name, err);
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  %s verify:%5d ops/s (%d loops, %d ms)\n",
           name, (int)(ECDSA_PERF_LOOPS * 1000ULL / ms),
           ECDSA_PERF_LOOPS, ms);

exit:
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecdsa_free(&ctx);
    return err;
}

static int test_mbedtls_ecdsa_p256(void)
{
    return test_mbedtls_ecdsa_curve(MBEDTLS_ECP_DP_SECP256R1, "ECDSA P-256");
}

static int test_mbedtls_ecdsa_p384(void)
{
    return test_mbedtls_ecdsa_curve(MBEDTLS_ECP_DP_SECP384R1, "ECDSA P-384");
}

#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
static int test_mbedtls_ecdsa_sm2(void)
{
    return test_mbedtls_ecdsa_curve(MBEDTLS_ECP_DP_SM2, "SM2");
}
#endif /* CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT */
#endif /* CONFIG_MBEDTLS_ECDSA_C */

#if defined(MBEDTLS_RSA_C)
#define RSA_PERF_LOOPS 10

static const struct device *trng_dev;

/* RNG profiling counters (valid while rng_profiling is true) */
static volatile int rng_profiling;

static uint32_t sw_rng_call_cnt;
static uint32_t sw_rng_byte_cnt;
static uint64_t sw_rng_cycles;

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
static uint32_t otbn_rnd_cnt;
static uint32_t otbn_urnd_cnt;
static uint64_t otbn_rnd_cycles;
static uint64_t otbn_urnd_cycles;
#endif

static int trng_init(void)
{
    if (trng_dev != NULL) {
        return 0;
    }

    trng_dev = DEVICE_DT_GET(DT_NODELABEL(trng1));
    if (!device_is_ready(trng_dev)) {
        printk("TRNG device not ready\n");
        trng_dev = NULL;
        return -ENODEV;
    }

    return 0;
}

static void rng_profiling_reset(void)
{
    sw_rng_call_cnt = 0;
    sw_rng_byte_cnt = 0;
    sw_rng_cycles = 0;
#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
    otbn_rnd_cnt = 0;
    otbn_urnd_cnt = 0;
    otbn_rnd_cycles = 0;
    otbn_urnd_cycles = 0;
#endif
}

static void rng_profiling_print(void)
{
    uint32_t cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    printk("    SW RNG: calls=%u bytes=%u cycles=%llu time=%llu ms\n",
           sw_rng_call_cnt, sw_rng_byte_cnt, sw_rng_cycles,
           (sw_rng_cycles * 1000ULL) / cpu_freq_hz);
#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
    printk("    OTBN RND: calls=%u cycles=%llu time=%llu ms\n",
           otbn_rnd_cnt, otbn_rnd_cycles,
           (otbn_rnd_cycles * 1000ULL) / cpu_freq_hz);
    printk("    OTBN URND: calls=%u cycles=%llu time=%llu ms\n",
           otbn_urnd_cnt, otbn_urnd_cycles,
           (otbn_urnd_cycles * 1000ULL) / cpu_freq_hz);
#endif
}

static int rsa_get_rng(void *ctx, unsigned char *buf, size_t len)
{
    int rc;
    timing_t start_time, end_time;

    (void)ctx;

    if (trng_init() != 0) {
        return -ENODEV;
    }

    if (rng_profiling) {
        start_time = timing_counter_get();
    }

    rc = entropy_get_entropy_isr(trng_dev, buf, len, ENTROPY_BUSYWAIT);

    if (rng_profiling) {
        end_time = timing_counter_get();
        sw_rng_cycles += timing_cycles_get(&start_time, &end_time);
        sw_rng_call_cnt++;
        sw_rng_byte_cnt += (uint32_t)len;
    }

    if (rc < 0) {
        return rc;
    }

    /* PKCS#1 v1.5 padding requires non-zero random bytes. */
    for (size_t i = 0; i < len; i++) {
        buf[i] |= 0x01U;
    }
    return 0;
}

static int rsa_keygen_get_rng(void *ctx, unsigned char *buf, size_t len)
{
    int rc;
    timing_t start_time, end_time;

    (void)ctx;

    if (trng_init() != 0) {
        return -ENODEV;
    }

    if (rng_profiling) {
        start_time = timing_counter_get();
    }

    rc = entropy_get_entropy_isr(trng_dev, buf, len, ENTROPY_BUSYWAIT);

    if (rng_profiling) {
        end_time = timing_counter_get();
        sw_rng_cycles += timing_cycles_get(&start_time, &end_time);
        sw_rng_call_cnt++;
        sw_rng_byte_cnt += (uint32_t)len;
    }

    return (rc < 0) ? rc : 0;
}

#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
static uint32_t otbn_trng_get(void)
{
    uint32_t val = 0;
    timing_t start_time, end_time;

    if (trng_dev == NULL) {
        (void)trng_init();
    }

    if (trng_dev == NULL) {
        return 0;
    }

    if (rng_profiling) {
        start_time = timing_counter_get();
    }

    (void)entropy_get_entropy_isr(trng_dev, (uint8_t *)&val,
                                  sizeof(val), ENTROPY_BUSYWAIT);

    if (rng_profiling) {
        end_time = timing_counter_get();
        otbn_rnd_cycles += timing_cycles_get(&start_time, &end_time);
        otbn_rnd_cnt++;
    }

    return val;
}

static uint32_t otbn_prng_get(void)
{
    uint32_t val = 0;
    timing_t start_time, end_time;

    if (trng_dev == NULL) {
        (void)trng_init();
    }

    if (trng_dev == NULL) {
        return 0;
    }

    if (rng_profiling) {
        start_time = timing_counter_get();
    }

    (void)entropy_get_entropy_isr(trng_dev, (uint8_t *)&val,
                                  sizeof(val), ENTROPY_BUSYWAIT);

    if (rng_profiling) {
        end_time = timing_counter_get();
        otbn_urnd_cycles += timing_cycles_get(&start_time, &end_time);
        otbn_urnd_cnt++;
    }

    return val;
}
#endif /* CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT */

static int test_mbedtls_rsa_2048(void)
{
    mbedtls_rsa_context rsa;
    const unsigned char msg[] = "rsa throughput test";
    unsigned char hash[32];
    unsigned char enc[256];
    unsigned char dec[256];
    unsigned char sig[256];
    size_t dec_len;
    int64_t start, end;
    uint32_t ms;
    int err;
    int ret = 0;

    memset(hash, 0x12, sizeof(hash));

    mbedtls_rsa_init(&rsa);

    err = mbedtls_rsa_import_raw(&rsa,
                                 rsa2048_n, sizeof(rsa2048_n),
                                 NULL, 0,
                                 NULL, 0,
                                 rsa2048_d, sizeof(rsa2048_d),
                                 rsa2048_e, sizeof(rsa2048_e));
    if (err != 0) {
        printk("  RSA-2048 key import error: %d\n", err);
        ret = err;
        goto exit;
    }

    err = mbedtls_rsa_complete(&rsa);
    if (err != 0) {
        printk("  RSA-2048 key complete error: %d\n", err);
        ret = err;
        goto exit;
    }

    /* Public-key encrypt (uses OTBN F4 mode). */
    start = k_uptime_get();
    for (int i = 0; i < RSA_PERF_LOOPS; i++) {
        err = mbedtls_rsa_pkcs1_encrypt(&rsa, rsa_get_rng, NULL,
                                        sizeof(msg) - 1, msg, enc);
        if (err != 0) {
            printk("  RSA-2048 encrypt error: %d\n", err);
            ret = err;
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  RSA-2048 encrypt:%5d ops/s (%d loops, %d ms)\n",
           (int)(RSA_PERF_LOOPS * 1000ULL / ms), RSA_PERF_LOOPS, ms);

    /* Private-key decrypt (uses OTBN modexp with D). */
    start = k_uptime_get();
    for (int i = 0; i < RSA_PERF_LOOPS; i++) {
        err = mbedtls_rsa_pkcs1_decrypt(&rsa, rsa_get_rng, NULL,
                                        &dec_len, enc, dec, sizeof(dec));
        if (err != 0) {
            printk("  RSA-2048 decrypt error: %d\n", err);
            ret = err;
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  RSA-2048 decrypt:%5d ops/s (%d loops, %d ms)\n",
           (int)(RSA_PERF_LOOPS * 1000ULL / ms), RSA_PERF_LOOPS, ms);

    /* Private-key sign (uses OTBN modexp with D). */
    start = k_uptime_get();
    for (int i = 0; i < RSA_PERF_LOOPS; i++) {
        err = mbedtls_rsa_pkcs1_sign(&rsa, rsa_get_rng, NULL,
                                     MBEDTLS_MD_SHA256, 32, hash, sig);
        if (err != 0) {
            printk("  RSA-2048 sign error: %d\n", err);
            ret = err;
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  RSA-2048 sign:   %5d ops/s (%d loops, %d ms)\n",
           (int)(RSA_PERF_LOOPS * 1000ULL / ms), RSA_PERF_LOOPS, ms);

    /* Public-key verify (uses OTBN F4 mode). */
    start = k_uptime_get();
    for (int i = 0; i < RSA_PERF_LOOPS; i++) {
        err = mbedtls_rsa_pkcs1_verify(&rsa, MBEDTLS_MD_SHA256, 32, hash, sig);
        if (err != 0) {
            printk("  RSA-2048 verify error: %d\n", err);
            ret = err;
            goto exit;
        }
    }
    end = k_uptime_get();
    ms = (uint32_t)(end - start);
    if (ms == 0) {
        ms = 1;
    }
    printk("  RSA-2048 verify: %5d ops/s (%d loops, %d ms)\n",
           (int)(RSA_PERF_LOOPS * 1000ULL / ms), RSA_PERF_LOOPS, ms);

exit:
    mbedtls_rsa_free(&rsa);
    return ret;
}

#if defined(MBEDTLS_GENPRIME)
#define RSA_KEYGEN_LOOPS 10

static int test_mbedtls_rsa_keygen(void)
{
    mbedtls_rsa_context rsa;
    int64_t start, end;
    uint32_t ms;
    int err;
    int ret = 0;

    printk("  RSA-2048 keygen (%d loops)\n", RSA_KEYGEN_LOOPS);

    for (int i = 0; i < RSA_KEYGEN_LOOPS; i++) {
        mbedtls_rsa_init(&rsa);
        rng_profiling_reset();
        rng_profiling = 1;
        start = k_uptime_get();
        err = mbedtls_rsa_gen_key(&rsa, rsa_keygen_get_rng, NULL, 2048, 65537);
        end = k_uptime_get();
        rng_profiling = 0;
        if (err != 0) {
            printk("  RSA-2048 keygen loop %d error: %d\n", i + 1, err);
            ret = err;
            goto exit;
        }
        ms = (uint32_t)(end - start);
        if (ms == 0) {
            ms = 1;
        }
        printk("  RSA-2048 keygen loop %d: %d ms\n", i + 1, ms);
        rng_profiling_print();
        mbedtls_rsa_free(&rsa);
    }

exit:
    mbedtls_rsa_free(&rsa);
    return ret;
}
#endif /* MBEDTLS_GENPRIME */
#endif /* MBEDTLS_RSA_C */

int main(void)
{
    printk("\nmbedtls SHA256 throughput (total %d KB)\n", TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sha256(step_sizes[i]);
    }

    printk("\nmbedtls SHA512 throughput (total %d KB)\n", TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sha512(step_sizes[i]);
    }

    printk("\nmbedtls SHA256 unaligned throughput (total %d KB, +1 offset)\n",
           TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sha256_unaligned(step_sizes[i]);
    }

    printk("\nmbedtls SHA512 unaligned throughput (total %d KB, +1 offset)\n",
           TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sha512_unaligned(step_sizes[i]);
    }

#if defined(CONFIG_MBEDTLS_SHA224_SHA256_SM3_LINKEDSEMI_HARDWARE_ALT) || \
    defined(CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT)
    printk("\nmbedtls SM3 throughput (total %d KB)\n", TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sm3(step_sizes[i]);
    }

    printk("\nmbedtls SM3 unaligned throughput (total %d KB, +1 offset)\n",
           TEST_TOTAL_SIZE / 1024);
    printk("Step(KB)   Time(us)   MB/s\n");
    for (size_t i = 0; i < ARRAY_SIZE(step_sizes); i++) {
        test_mbedtls_sm3_unaligned(step_sizes[i]);
    }
#else
    printk("\nSM3 hardware/OTBN alt is not enabled, skipped.\n");
#endif

    printk("\nmbedtls cipher throughput (total %d KB, %d runs)\n",
           TEST_TOTAL_SIZE / 1024, CIPHER_RUNS);
    test_mbedtls_aes_ecb_avg();
    test_mbedtls_aes_ecb_decrypt_avg();
    test_mbedtls_aes256_ecb_avg();
    test_mbedtls_aes256_ecb_decrypt_avg();
    test_mbedtls_aes_cbc_avg();
    test_mbedtls_aes_cbc_decrypt_avg();
    test_mbedtls_aes256_cbc_avg();
    test_mbedtls_aes256_cbc_decrypt_avg();
    test_mbedtls_aes_ctr_avg();
    test_mbedtls_aes_ctr_decrypt_avg();
    test_mbedtls_aes256_ctr_avg();
    test_mbedtls_aes256_ctr_decrypt_avg();
    test_mbedtls_aes_cfb128_avg();
    test_mbedtls_aes_cfb128_decrypt_avg();
    test_mbedtls_aes256_cfb128_avg();
    test_mbedtls_aes256_cfb128_decrypt_avg();
    test_mbedtls_aes_cfb8_avg();
    test_mbedtls_aes_cfb8_decrypt_avg();
    test_mbedtls_aes256_cfb8_avg();
    test_mbedtls_aes256_cfb8_decrypt_avg();
    test_mbedtls_aes_ofb_avg();
    test_mbedtls_aes_ofb_decrypt_avg();
    test_mbedtls_aes256_ofb_avg();
    test_mbedtls_aes256_ofb_decrypt_avg();
#if defined(MBEDTLS_CIPHER_MODE_XTS)
    test_mbedtls_aes_xts_avg();
    test_mbedtls_aes_xts_decrypt_avg();
    test_mbedtls_aes256_xts_avg();
    test_mbedtls_aes256_xts_decrypt_avg();
#endif
#if defined(CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT)
    test_mbedtls_sm4_ecb_encrypt_avg();
    test_mbedtls_sm4_ecb_decrypt_avg();
    test_mbedtls_sm4_ctr_avg();
    test_mbedtls_sm4_ctr_decrypt_avg();
#endif

#if defined(MBEDTLS_ECDSA_C)
    printk("\nmbedtls ECDSA throughput (%d loops)\n", ECDSA_PERF_LOOPS);
    test_mbedtls_ecdsa_p384();
    test_mbedtls_ecdsa_p256();
#if defined(CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT)
    test_mbedtls_ecdsa_sm2();
#endif
#endif

#if defined(MBEDTLS_RSA_C)
#if defined(CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT)
    ls_otbn_random_callback_register(otbn_trng_get, otbn_prng_get);
#endif
    printk("\nmbedtls RSA-2048 throughput (%d loops)\n", RSA_PERF_LOOPS);
    test_mbedtls_rsa_2048();
#if defined(MBEDTLS_GENPRIME)
    test_mbedtls_rsa_keygen();
#endif
#endif

    return 0;
}
