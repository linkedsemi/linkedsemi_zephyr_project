/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 /*
 * hash_update 喂数据，更新中间状态 (可调用 0 次或多次)
 * hash_compute 喂数据 + padding + 输出摘要 (最后调用 1 次，必须调用)
 */

#include <zephyr/crypto/crypto.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys_clock.h>
#include <zephyr/timing/timing.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LOG_LEVEL CONFIG_SHA512_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#if DT_HAS_COMPAT_STATUS_OKAY(linkedsemi_sha512)
#define SHA512_DEV_COMPAT linkedsemi_sha512
#else
#error "You need to enable one crypto device"
#endif

#define SHA512_DIGEST_SIZE  64
#define TEST_TOTAL_SIZE     KB(260)     /* 总数据量 */
#define TEST_STEP_SIZE      KB(4)       /* 分片大小 */

__attribute__((aligned(32))) static uint8_t big_buffer[TEST_TOTAL_SIZE];


static void print_hash(const uint8_t *hash, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        printk("%2.2x ", hash[i]);
        if ((i + 1) % 16 == 0) {
            printk("\n");
        }
    }
    printk("\n");
}

static int test_hash(void)
{
    const struct device *const dev = DEVICE_DT_GET_ONE(SHA512_DEV_COMPAT);
    if (!device_is_ready(dev)) {
        printk("SHA512 device is not ready\n");
        return -1;
    }

    struct hash_ctx ctx;
    struct hash_pkt pkt;
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    uint32_t cpu_freq_hz;
    uint8_t hash_result[SHA512_DIGEST_SIZE];

    cpu_freq_hz = sys_clock_hw_cycles_per_sec();

    memset(big_buffer, 2, sizeof(big_buffer));

    /* 开始会话 */
    ctx.flags = CAP_SYNC_OPS | CAP_SEPARATE_IO_BUFS;
    if (hash_begin_session(dev, &ctx, CRYPTO_HASH_ALGO_SHA512)) {
        printk("Failed to init sha512 session\n");
        return -1;
    }

    printk("\n%s: %d\n\n", __func__, __LINE__);

    /* 分片喂数据，用 cycle 计数器计时 */
    for (int off = 0; off < TEST_TOTAL_SIZE; off += TEST_STEP_SIZE) {
        pkt.in_buf  = (uint8_t *)big_buffer + off;
        pkt.in_len  = MIN(TEST_STEP_SIZE, TEST_TOTAL_SIZE - off);
        pkt.out_buf = hash_result;

        start_time = timing_counter_get();

        if (hash_update(&ctx, &pkt) != 0) {
            printk("SHA512 update error at offset %d\n", off);
            hash_free_session(dev, &ctx);
            return -1;
        }

        end_time = timing_counter_get();

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += pkt.in_len;
    }

    /* 最终计算 (padding + 输出摘要) */
    pkt.in_buf  = (uint8_t *)big_buffer;
    pkt.in_len  = 0;
    pkt.out_buf = hash_result;

    start_time = timing_counter_get();

    if (hash_compute(&ctx, &pkt) != 0) {
        printk("SHA512 compute error\n");
        hash_free_session(dev, &ctx);
        return -1;
    }

    end_time = timing_counter_get();
    total_cycles += timing_cycles_get(&start_time, &end_time);

    hash_free_session(dev, &ctx);

    printk("--------------------------------------------\n");
    printk("SHA512 result:\n");
    print_hash(hash_result, SHA512_DIGEST_SIZE);
    printk("============================================\n");

    /* 计算吞吐率 */
    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_kbs = 0;

    if (total_us > 0) {
        throughput_kbs = (total_bytes * 1000000ULL / 1024) / total_us;
    }

    printk("--------------------------------------------\n");
    printk("SHA512 performance:\n");
    printk("  Total data  : %llu bytes (%llu KB)\n",
           total_bytes, total_bytes / 1024);
    printk("  Total cycles: %llu\n", total_cycles);
    printk("  Total time  : %llu us (%llu ms)\n",
           total_us, total_us / 1000);
    printk("  Throughput  : %llu KB/s (%llu MB/s)\n",
           throughput_kbs, throughput_kbs / 1024);
    printk("--------------------------------------------\n");

    return 0;
}

int main(void)
{
    test_hash();
    return 0;
}
