/*
 * wolfSSL AES-GCM hardware throughput benchmark
 *
 * Throughput is measured the same way as samples/crypto/throughput:
 *   - walk a large buffer in fixed-size steps (TEST_TOTAL_SIZE / TEST_STEP_SIZE)
 *   - time each wc_AesGcmEncrypt / wc_AesGcmDecrypt call with the Zephyr
 *     timing API (k_cycle_get_64 / timing_cycles_get) and accumulate
 *     total_cycles + total_bytes
 *   - convert cycles -> time via sys_clock_hw_cycles_per_sec() and derive
 *     throughput: (total_bytes * 1e6 / 1024) / total_us
 *
 * The active cipher path is selected by Kconfig:
 *   CONFIG_WOLFSSL_LINKEDSEMI_HARDWARE_AES_ALT=y -> Linkedsemi AES engine
 *   otherwise                                       -> pure wolfSSL software
 *
 * For AES-128/256-GCM (wc_AesGcmEncrypt/Decrypt) and AES-128/256-ECB
 * (wc_AesEcbEncrypt/Decrypt, gated on HAVE_AES_ECB) the sample first runs an
 * encrypt-then-decrypt round trip to confirm correctness, then reports
 * encrypt/decrypt throughput.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/util.h>
#include <zephyr/timing/timing.h>
#include <string.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/aes.h>

/* ---- AES-GCM parameters ------------------------------------------------ */
#define GCM_IV_SZ     12   /* 标准 96-bit nonce                  */
#define GCM_TAG_SZ    16   /* 完整 128-bit 认证标签              */

/* ---- benchmark workload (matches samples/crypto/throughput) ------------ */
#define TEST_TOTAL_SIZE   KB(400)   /* 总数据量                          */
#define TEST_STEP_SIZE    KB(32)     /* 分片大小（每次一次 GCM 调用）     */

/* Active cipher path label, reported at startup. */
#ifdef CONFIG_WOLFSSL_LINKEDSEMI_HARDWARE_AES_ALT
#define AES_PATH_LABEL "Linkedsemi hardware"
#else
#define AES_PATH_LABEL "software"
#endif

/* 12-byte nonce reused across all one-shot calls. */
static const uint8_t g_iv[GCM_IV_SZ] = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
    0xde, 0xca, 0xf8, 0x88
};

/* 大输入缓冲区（明文源），以及每个分片的密文/解密输出缓冲区。 */
__attribute__((aligned(32))) static uint8_t big_buffer[TEST_TOTAL_SIZE];
__attribute__((aligned(32))) static uint8_t cipher_buf[TEST_STEP_SIZE + GCM_TAG_SZ];
__attribute__((aligned(32))) static uint8_t dec_buf[TEST_STEP_SIZE];
static uint8_t g_tag[GCM_TAG_SZ];

enum bench_dir {
    BENCH_ENC,
    BENCH_DEC,
};

/* ------------------------------------------------------------------------
 * 按累计的 cycles / bytes 输出性能块，公式与 samples/crypto/throughput 一致。
 * ------------------------------------------------------------------------ */
static void print_perf(const char *name, const char *what,
                       uint64_t total_cycles, uint64_t total_bytes)
{
    uint32_t cpu_freq_hz = sys_clock_hw_cycles_per_sec();
    uint64_t total_ns = (total_cycles * 1000000000ULL) / cpu_freq_hz;
    uint64_t total_us = total_ns / 1000;
    uint64_t throughput_kbs = 0;

    if (total_us > 0) {
        throughput_kbs = (total_bytes * 1000000ULL / 1024) / total_us;
    }

    printk("--------------------------------------------\n");
    printk("%s %s throughput\n", name, what);
    printk("  Total data  : %llu bytes (%llu KB)\n",
           (unsigned long long)total_bytes, (unsigned long long)(total_bytes / 1024));
    printk("  Total cycles: %llu\n", (unsigned long long)total_cycles);
    printk("  Total time  : %llu us (%llu ms)\n",
           (unsigned long long)total_us, (unsigned long long)(total_us / 1000));
    printk("  Throughput  : %llu KB/s (%llu MB/s)\n",
           (unsigned long long)throughput_kbs,
           (unsigned long long)(throughput_kbs / 1024));
    printk("--------------------------------------------\n");
}

/* ------------------------------------------------------------------------
 * Correctness: encrypt then decrypt a few representative sizes, verify the
 * round trip. Returns 0 on success, non-zero on failure.
 * ------------------------------------------------------------------------ */
static int aesgcm_correctness(Aes *aes)
{
    static const uint32_t sizes[] = { 16, 64, TEST_STEP_SIZE };
    int ret;

    for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        uint32_t len = sizes[i];

        for (uint32_t j = 0; j < len; j++) {
            big_buffer[j] = (uint8_t)(j & 0xffu);
        }

        ret = wc_AesGcmEncrypt(aes, cipher_buf, big_buffer, len,
                               g_iv, GCM_IV_SZ, g_tag, GCM_TAG_SZ, NULL, 0);
        if (ret != 0) {
            printk("  len=%6u: encrypt FAIL (ret=%d)\n", len, ret);
            return ret;
        }

        ret = wc_AesGcmDecrypt(aes, dec_buf, cipher_buf, len,
                               g_iv, GCM_IV_SZ, g_tag, GCM_TAG_SZ, NULL, 0);
        if (ret != 0) {
            printk("  len=%6u: decrypt FAIL (ret=%d)\n", len, ret);
            return ret;
        }
        if (memcmp(dec_buf, big_buffer, len) != 0) {
            printk("  len=%6u: round-trip mismatch\n", len);
            return -1;
        }

        printk("  len=%6u: encrypt+decrypt OK\n", len);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * Walk big_buffer in TEST_STEP_SIZE chunks. For each chunk call the one-shot
 * GCM API once and time only that call, accumulating cycles + bytes.
 *
 * For decrypt the valid ciphertext+tag is produced first (untimed), and the
 * decrypted output is verified against the plaintext (also untimed), so the
 * measurement captures the decrypt path only.
 *
 * Returns 0 on success, non-zero on failure.
 * ------------------------------------------------------------------------ */
static int aesgcm_run(Aes *aes, enum bench_dir dir,
                      uint64_t *out_cycles, uint64_t *out_bytes)
{
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    int ret;

    for (int off = 0; off < TEST_TOTAL_SIZE; off += TEST_STEP_SIZE) {
        uint32_t chunk = MIN(TEST_STEP_SIZE, TEST_TOTAL_SIZE - off);
        const uint8_t *in = big_buffer + off;

        if (dir == BENCH_ENC) {
            start_time = k_cycle_get_64();

            ret = wc_AesGcmEncrypt(aes, cipher_buf, in, chunk,
                                   g_iv, GCM_IV_SZ, g_tag, GCM_TAG_SZ,
                                   NULL, 0);

            end_time = k_cycle_get_64();
            if (ret != 0) {
                printk("AES-GCM encrypt error at offset %d (ret=%d)\n", off, ret);
                return ret;
            }
        } else {
            /* 产生有效的密文+tag 供解密使用（不计入耗时） */
            ret = wc_AesGcmEncrypt(aes, cipher_buf, in, chunk,
                                   g_iv, GCM_IV_SZ, g_tag, GCM_TAG_SZ,
                                   NULL, 0);
            if (ret != 0) {
                printk("AES-GCM encrypt error at offset %d (ret=%d)\n", off, ret);
                return ret;
            }

            start_time = k_cycle_get_64();

            ret = wc_AesGcmDecrypt(aes, dec_buf, cipher_buf, chunk,
                                   g_iv, GCM_IV_SZ, g_tag, GCM_TAG_SZ,
                                   NULL, 0);

            end_time = k_cycle_get_64();
            if (ret != 0) {
                printk("AES-GCM decrypt error at offset %d (ret=%d)\n", off, ret);
                return ret;
            }
            /* 校验解密结果（不计入耗时） */
            if (memcmp(dec_buf, in, chunk) != 0) {
                printk("AES-GCM decrypt mismatch at offset %d\n", off);
                return -1;
            }
        }

        total_cycles += end_time - start_time;
        total_bytes += chunk;
    }

    *out_cycles = total_cycles;
    *out_bytes = total_bytes;
    return 0;
}

/* AES-GCM: set key, run correctness + encrypt/decrypt throughput. */
static int run_aesgcm_key_size(const char *name, const uint8_t *key, uint32_t key_sz)
{
    Aes aes[1];
    uint64_t total_cycles, total_bytes;
    int ret;

    printk("\n========== %s ==========\n", name);

    ret = wc_AesInit(aes, NULL, INVALID_DEVID);
    if (ret != 0) {
        printk("wc_AesInit failed (%d)\n", ret);
        return ret;
    }

    ret = wc_AesGcmSetKey(aes, key, key_sz);
    if (ret != 0) {
        printk("wc_AesGcmSetKey failed (%d)\n", ret);
        wc_AesFree(aes);
        return ret;
    }

    printk("\n--- %s correctness check ---\n", name);
    ret = aesgcm_correctness(aes);
    if (ret != 0) {
        printk("%s correctness check FAILED\n", name);
        wc_AesFree(aes);
        return ret;
    }
    printk("%s correctness check PASSED\n", name);

    /* 填充大缓冲区用于吞吐率测试 */
    memset(big_buffer, 2, TEST_TOTAL_SIZE);

    if (aesgcm_run(aes, BENCH_ENC, &total_cycles, &total_bytes) != 0) {
        wc_AesFree(aes);
        return -1;
    }
    print_perf(name, "encrypt", total_cycles, total_bytes);

    if (aesgcm_run(aes, BENCH_DEC, &total_cycles, &total_bytes) != 0) {
        wc_AesFree(aes);
        return -1;
    }
    print_perf(name, "decrypt", total_cycles, total_bytes);

    wc_AesFree(aes);
    return 0;
}

#ifdef HAVE_AES_ECB
/* ------------------------------------------------------------------------
 * AES-ECB correctness: encrypt then decrypt a few block-aligned sizes, verify
 * the round trip. Returns 0 on success, non-zero on failure.
 * ------------------------------------------------------------------------ */
static int aesecb_correctness(Aes *enc, Aes *dec)
{
    static const uint32_t sizes[] = { 16, 64, TEST_STEP_SIZE };
    int ret;

    for (uint32_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        uint32_t len = sizes[i];

        for (uint32_t j = 0; j < len; j++) {
            big_buffer[j] = (uint8_t)(j & 0xffu);
        }

        ret = wc_AesEcbEncrypt(enc, cipher_buf, big_buffer, len);
        if (ret != 0) {
            printk("  len=%6u: ECB encrypt FAIL (ret=%d)\n", len, ret);
            return ret;
        }

        ret = wc_AesEcbDecrypt(dec, dec_buf, cipher_buf, len);
        if (ret != 0) {
            printk("  len=%6u: ECB decrypt FAIL (ret=%d)\n", len, ret);
            return ret;
        }
        if (memcmp(dec_buf, big_buffer, len) != 0) {
            printk("  len=%6u: ECB round-trip mismatch\n", len);
            return -1;
        }

        printk("  len=%6u: ECB encrypt+decrypt OK\n", len);
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * AES-ECB throughput: walk big_buffer in TEST_STEP_SIZE chunks (each a whole
 * number of AES blocks) and time each one-shot ECB call. ECB output never
 * expands, so cipher_buf / dec_buf (sized for a step) are reused.
 * ------------------------------------------------------------------------ */
static int aesecb_run(Aes *aes, enum bench_dir dir,
                      uint64_t *out_cycles, uint64_t *out_bytes)
{
    timing_t start_time, end_time;
    uint64_t total_cycles = 0;
    uint64_t total_bytes = 0;
    int ret;

    for (int off = 0; off < TEST_TOTAL_SIZE; off += TEST_STEP_SIZE) {
        uint32_t chunk = MIN(TEST_STEP_SIZE, TEST_TOTAL_SIZE - off);
        const uint8_t *in = big_buffer + off;

        start_time = timing_counter_get();

        if (dir == BENCH_ENC) {
            ret = wc_AesEcbEncrypt(aes, cipher_buf, in, chunk);
        } else {
            ret = wc_AesEcbDecrypt(aes, dec_buf, in, chunk);
        }

        end_time = timing_counter_get();
        if (ret != 0) {
            printk("AES-ECB %s error at offset %d (ret=%d)\n",
                   dir == BENCH_ENC ? "encrypt" : "decrypt", off, ret);
            return ret;
        }

        total_cycles += timing_cycles_get(&start_time, &end_time);
        total_bytes += chunk;
    }

    *out_cycles = total_cycles;
    *out_bytes = total_bytes;
    return 0;
}

/* AES-ECB: set enc/dec keys, run correctness + encrypt/decrypt throughput. */
static int run_aesecb_key_size(const char *name, const uint8_t *key, uint32_t key_sz)
{
    Aes enc[1], dec[1];
    uint64_t total_cycles, total_bytes;
    int ret;

    printk("\n========== %s ==========\n", name);

    ret = wc_AesInit(enc, NULL, INVALID_DEVID);
    if (ret != 0) {
        printk("wc_AesInit(enc) failed (%d)\n", ret);
        return ret;
    }
    ret = wc_AesInit(dec, NULL, INVALID_DEVID);
    if (ret != 0) {
        printk("wc_AesInit(dec) failed (%d)\n", ret);
        wc_AesFree(enc);
        return ret;
    }

    ret = wc_AesSetKey(enc, key, key_sz, NULL, AES_ENCRYPTION);
    if (ret != 0) {
        printk("wc_AesSetKey(enc) failed (%d)\n", ret);
        wc_AesFree(enc);
        wc_AesFree(dec);
        return ret;
    }
    ret = wc_AesSetKey(dec, key, key_sz, NULL, AES_DECRYPTION);
    if (ret != 0) {
        printk("wc_AesSetKey(dec) failed (%d)\n", ret);
        wc_AesFree(enc);
        wc_AesFree(dec);
        return ret;
    }

    printk("\n--- %s correctness check ---\n", name);
    ret = aesecb_correctness(enc, dec);
    if (ret != 0) {
        printk("%s correctness check FAILED\n", name);
        wc_AesFree(enc);
        wc_AesFree(dec);
        return ret;
    }
    printk("%s correctness check PASSED\n", name);

    memset(big_buffer, 2, TEST_TOTAL_SIZE);

    if (aesecb_run(enc, BENCH_ENC, &total_cycles, &total_bytes) != 0) {
        wc_AesFree(enc);
        wc_AesFree(dec);
        return -1;
    }
    print_perf(name, "encrypt", total_cycles, total_bytes);

    if (aesecb_run(dec, BENCH_DEC, &total_cycles, &total_bytes) != 0) {
        wc_AesFree(enc);
        wc_AesFree(dec);
        return -1;
    }
    print_perf(name, "decrypt", total_cycles, total_bytes);

    wc_AesFree(enc);
    wc_AesFree(dec);
    return 0;
}
#endif /* HAVE_AES_ECB */

int main(void)
{
    static const uint8_t key128[16] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08
    };
    static const uint8_t key256[32] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08
    };

    int failures = 0;

    printk("==========================================\n");
    printk("wolfSSL AES Throughput Test (GCM + ECB)\n");
    printk("==========================================\n");
    printk("AES path: %s\n", AES_PATH_LABEL);

    if (run_aesgcm_key_size("AES-128-GCM", key128, sizeof(key128)) != 0) {
        failures++;
    }
#ifdef WOLFSSL_AES_256
    if (run_aesgcm_key_size("AES-256-GCM", key256, sizeof(key256)) != 0) {
        failures++;
    }
#endif

#ifdef HAVE_AES_ECB
    if (run_aesecb_key_size("AES-128-ECB", key128, sizeof(key128)) != 0) {
        failures++;
    }
#ifdef WOLFSSL_AES_256
    if (run_aesecb_key_size("AES-256-ECB", key256, sizeof(key256)) != 0) {
        failures++;
    }
#endif
#endif /* HAVE_AES_ECB */

    printk("\n==========================================\n");
    if (failures == 0) {
        printk("All AES throughput tests PASSED\n");
    } else {
        printk("%d AES test(s) FAILED\n", failures);
    }
    printk("==========================================\n");

    return 0;
}
