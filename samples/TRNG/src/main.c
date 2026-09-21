/*
 * APB TRNG（trng0）熵驱动 API 验证。
 *
 * 测试对象：zephyr/drivers/entropy/entropy_ls_apb_trng.c
 * 设备节点由 app.overlay 的 chosen 指向 trng0（linkedsemi,ls-apb-trng @0x400a2000）。
 *
 * 测试流程参考 linkedsemi_zephyr_project/samples/trng_test/src/main.c，
 * 但那份样例用的是另一块 IP（trng1 = linkedsemi,ls-dwtrng，DWC/NIST 数字后处理），
 * 与本工程的环形振荡器 TRNG 不是同一套硬件，所以只借用它的测试流程与写法：
 * 设备获取方式、main 签名、输出接口都按本工程约定改写。
 *
 * 覆盖四条调用路径：
 *   1. entropy_get_entropy                 —— 线程上下文，阻塞
 *   2. entropy_get_entropy_isr(..., 0)     —— ISR 安全，非阻塞，池空返回 -ENODATA
 *   3. entropy_get_entropy_isr(..., BUSYWAIT) —— ISR 安全，忙等轮询补齐
 *   4. 连续多次非阻塞调用                  —— 观察池子的填充/耗竭行为
 *
 * 注意：k_pipe 池子大小 CONFIG_ENTROPY_LS_APB_TRNG_POOL_SIZE 默认 64 字节，
 *       单次非阻塞调用最多只能取到池子大小，TEST 2 请求 1024 字节时取不满属预期。
 *
 * 编译：
 *   source ~/zephyr_work/linkedsemi_zephyr_project/.venv/bin/activate
 *   west build -p always -b lsqsh_evb/lsqsh/cpu1 \
 *     /home/peter/zephyr_work/register-code/TRNG \
 *     --build-dir /home/peter/zephyr_work/build/build_TRNG
 *
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/sys/printk.h>

/* 各测试的请求长度与 hex dump 上限 */
#define LEN_BIG   2048u
#define LEN_ISR   1024u
#define LEN_SMALL 16u
#define SMALL_CNT 16
#define DUMP_MAX  2048u

/*
 * 12 KB 的采样 buffer：main 线程栈只有 CONFIG_MAIN_STACK_SIZE=10240 字节，
 * 必须放 .bss，否则直接爆栈。
 */
static uint8_t trng_buf[12288];

/* 十六进制打印，最多打印 max_show 字节，超出部分只报总长度 */
static void dump_hex_limited(const uint8_t *data, size_t len, size_t max_show)
{
	size_t show = len < max_show ? len : max_show;

	for (size_t i = 0; i < show; ++i) {
		printk("%02x%s", data[i], ((i + 1) % 16 == 0) ? "\n" : " ");
	}
	if (show % 16 != 0) {
		printk("\n");
	}
	if (len > show) {
		printk("... (%zu bytes shown of %zu)\n", show, len);
	}
}

/*
 * 相邻 16-bit 样本的重复计数。
 * 熵源正常时相邻样本重复的概率是 1/65536，repeats 应该接近 0；
 * 出现大段重复说明读出的不是新样本（历史上踩过这个坑）。
 */
static int check_repeats(const uint8_t *data, size_t len)
{
	int repeats = 0;

	for (size_t i = 2; i < len; i += 2) {
		if (data[i] == data[i - 2] && data[i + 1] == data[i - 1]) {
			repeats++;
		}
	}
	return repeats;
}

int main(void)
{
	const struct device *trng = DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
	int rc;

	if (!device_is_ready(trng)) {
		printk("TRNG device not ready\n");
		return 0;
	}

	printk("TRNG device testing start (APB TRNG: %s)\n", trng->name);
	printk("k_pipe pool = %d bytes (non-busy ISR read is capped by it)\n",
	       CONFIG_ENTROPY_LS_APB_TRNG_POOL_SIZE);

	/* 先让中断驱动的 ISR 往池子里填一点数据 */
	k_sleep(K_MSEC(10));

	/* ---- TEST 1: 线程态阻塞读取 ---- */
	printk("\n[TEST 1] thread get_entropy blocking: %u bytes\n", LEN_BIG);
	rc = entropy_get_entropy(trng, trng_buf, LEN_BIG);
	if (rc != 0) {
		printk("get_entropy failed: %d\n", rc);
	} else {
		printk("get_entropy ok, repeats=%d\n",
		       check_repeats(trng_buf, LEN_BIG));
		dump_hex_limited(trng_buf, LEN_BIG, DUMP_MAX);
	}

	/* ---- TEST 2: ISR 非阻塞单次调用 ---- */
	printk("\n[TEST 2] ISR get_entropy_isr non-busy single call: request %u bytes\n",
	       LEN_ISR);
	rc = entropy_get_entropy_isr(trng, trng_buf, LEN_ISR, 0);
	if (rc < 0) {
		printk("get_entropy_isr(non-busy) %s: %d\n",
		       (rc == -ENODATA) ? "pipe empty" : "failed", rc);
	} else {
		printk("get_entropy_isr(non-busy) got %d/%u bytes\n", rc, LEN_ISR);
		dump_hex_limited(trng_buf, (size_t)rc, DUMP_MAX);
	}

	/* ---- TEST 3: ISR 忙等读取 ---- */
	printk("\n[TEST 3] ISR get_entropy_isr BUSYWAIT: %u bytes\n", LEN_BIG);
	rc = entropy_get_entropy_isr(trng, trng_buf, LEN_BIG, ENTROPY_BUSYWAIT);
	if (rc < 0) {
		printk("get_entropy_isr(busywait) failed: %d\n", rc);
	} else {
		printk("get_entropy_isr(busywait) returned %d bytes (expect %u), repeats=%d\n",
		       rc, LEN_BIG, check_repeats(trng_buf, (size_t)rc));
		dump_hex_limited(trng_buf, (size_t)rc, DUMP_MAX);
	}

	/* ---- TEST 4: 连续多次非阻塞调用 ---- */
	printk("\n[TEST 4] multiple non-busy calls (%uB x %d)\n", LEN_SMALL,
	       SMALL_CNT);
	for (int i = 0; i < SMALL_CNT; i++) {
		rc = entropy_get_entropy_isr(trng, trng_buf, LEN_SMALL, 0);
		if (rc < 0) {
			printk("call %d: %s (%d)\n", i + 1,
			       (rc == -ENODATA) ? "pipe empty" : "failed", rc);
		} else {
			printk("call %d returned %d bytes\n", i + 1, rc);
		}
		k_sleep(K_MSEC(20));
	}

	/* ---- TEST 5: 再跑一遍阻塞接口，确认长时间运行后仍可用 ---- */
	printk("\n[TEST 5] thread get_entropy blocking: %u bytes\n", LEN_BIG);
	rc = entropy_get_entropy(trng, trng_buf, LEN_BIG);
	if (rc != 0) {
		printk("get_entropy failed: %d\n", rc);
	} else {
		printk("get_entropy ok, repeats=%d\n",
		       check_repeats(trng_buf, LEN_BIG));
		dump_hex_limited(trng_buf, LEN_BIG, DUMP_MAX);
	}

	printk("\nTRNG tests done.\n");
	return 0;
}
