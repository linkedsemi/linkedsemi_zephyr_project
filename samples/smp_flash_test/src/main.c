/*
 * Copyright (c) 2025 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(smp);

#define TEST_REGION_OFFSET_CPU0  0x100000
#define TEST_REGION_OFFSET_CPU1  0x101000
#define TEST_REGION_OFFSET_CPU2  0x102000
#define TEST_SECTOR_SIZE         4096
#define TEST_BUF_SIZE            16
#define TEST_LOOP_COUNT          200

static K_THREAD_STACK_DEFINE(cpu0_stack, 2048);
static K_THREAD_STACK_DEFINE(cpu1_stack, 2048);
static K_THREAD_STACK_DEFINE(cpu2_stack, 2048);
static struct k_thread cpu0_thread;
static struct k_thread cpu1_thread;
static struct k_thread cpu2_thread;
static atomic_t start_barrier;
static atomic_t done_counter;

static const struct device *flash_dev;

/* UART4 interrupt storm state */
#define UART4_STORM_BUF_SIZE    64
#define UART4_STORM_STACK_SIZE  1024
#define UART4_STORM_THREAD_PRIO 8

static const struct device *uart4_dev;
static uint8_t uart4_tx_buf[UART4_STORM_BUF_SIZE];
static volatile uint32_t uart4_irq_count;
static struct k_thread uart4_storm_thread;
static K_THREAD_STACK_DEFINE(uart4_storm_stack, UART4_STORM_STACK_SIZE);

static void flash_test_thread(void *p1, void *p2, void *p3)
{
	int thread_id = (int)(intptr_t)p1;
	off_t offset = (off_t)(intptr_t)p2;
	uint8_t write_buf[TEST_BUF_SIZE];
	uint8_t read_buf[TEST_BUF_SIZE];
	char label[32];
	int rc;

	ARG_UNUSED(p3);

	atomic_inc(&start_barrier);
	// while (atomic_get(&start_barrier) < 2) {
	// 	k_yield();
	// }


	// if(arch_curr_cpu()->id == 1)
	// {
	// 	while(1);
	// }
	LOG_INF("Thread starting, offset 0x%x", (unsigned int)offset);

	for (int loop = 0; loop < TEST_LOOP_COUNT; loop++) {
		for (int i = 0; i < TEST_BUF_SIZE; i++) {
			write_buf[i] = (uint8_t)(thread_id + loop * 7 + i * 3);
		}

		rc = flash_erase(flash_dev, offset, TEST_SECTOR_SIZE);
		if (rc != 0) {
			LOG_ERR("[cpu%d] loop%d: erase failed %d", arch_curr_cpu()->id, loop, rc);
			// continue;
		}
		// k_sleep(K_SECONDS(1));
		rc = flash_write(flash_dev, offset, write_buf, TEST_BUF_SIZE);
		if (rc != 0) {
			LOG_ERR("[cpu%d] loop%d: write failed %d", arch_curr_cpu()->id, loop, rc);
			// continue;
		}
		// k_sleep(K_SECONDS(1));
		memset(read_buf, 0, TEST_BUF_SIZE);
		rc = flash_read(flash_dev, offset, read_buf, TEST_BUF_SIZE);
		if (rc != 0) {
			LOG_ERR("[cpu%d] loop%d: read failed %d", arch_curr_cpu()->id, loop, rc);
			// continue;
		}
		// k_sleep(K_SECONDS(1));
		snprintf(label, sizeof(label), "[cpu%d] loop%d write", arch_curr_cpu()->id, loop);
		LOG_HEXDUMP_INF(write_buf, TEST_BUF_SIZE, label);

		snprintf(label, sizeof(label), "[cpu%d] loop%d read", arch_curr_cpu()->id, loop);
		LOG_HEXDUMP_INF(read_buf, TEST_BUF_SIZE, label);
		if (memcmp(write_buf, read_buf, TEST_BUF_SIZE) == 0) {
			LOG_INF("[cpu%d] thread_id:%d, loop%d: PASS", arch_curr_cpu()->id, thread_id, loop);
		} else {
			LOG_ERR("[cpu%d] thread_id:%d, loop%d: FAIL - data mismatch!", arch_curr_cpu()->id, thread_id, loop);
			// while(1);
		}
	}

	atomic_inc(&done_counter);
}

/*
 * UART4 ISR: keep TX FIFO busy to generate a high rate of interrupts,
 * but cap the per-IRQ processing budget so it cannot monopolize the
 * interrupt controller and starve other IRQs.
 */
#define UART4_STORM_IRQ_BUDGET  1

static void uart4_isr(const struct device *dev, void *user_data)
{
	int tx_len;
	int budget = UART4_STORM_IRQ_BUDGET;

	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	while (budget-- > 0 && uart_irq_is_pending(dev)) {
		if (uart_irq_tx_ready(dev)) {
			/* Pump a small batch to keep TX interrupts firing */
			tx_len = uart_fifo_fill(dev, uart4_tx_buf, sizeof(uart4_tx_buf));
			if (tx_len <= 0) {
				break;
			}
		}
	}

	uart4_irq_count++;
}

static void uart4_storm_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uart4_dev = DEVICE_DT_GET(DT_NODELABEL(uart4));
	if (!device_is_ready(uart4_dev)) {
		LOG_ERR("UART4 device not ready");
		return;
	}

	/* Fill pattern once; ISR will keep recycling it. */
	for (int i = 0; i < sizeof(uart4_tx_buf); i++) {
		uart4_tx_buf[i] = (uint8_t)(i & 0xFFU);
	}

	uart_irq_callback_user_data_set(uart4_dev, uart4_isr, NULL);
	// uart_irq_rx_enable(uart4_dev);
	uart_irq_tx_enable(uart4_dev);

	LOG_INF("UART4 interrupt storm started (irq_count=%u)",
		(unsigned int)uart4_irq_count);

	while (1) {
		/* Replenish TX FIFO periodically to sustain the storm. */
		(void)uart_fifo_fill(uart4_dev, uart4_tx_buf, sizeof(uart4_tx_buf));
		k_sleep(K_MSEC(5));
	}
}

/* Watchdog as a periodic IRQ source (priority 4) for multi-level nesting. */
#define WDT_IRQ_PERIOD_MS       1

static const struct device *wdt_dev;
static int wdt_channel_id = -1;

static void wdt_callback(const struct device *dev, int channel_id)
{
	ARG_UNUSED(channel_id);
	wdt_feed(dev, wdt_channel_id);
}

static int wdt_irq_init(void)
{
	struct wdt_timeout_cfg cfg = {
		.flags = WDT_FLAG_RESET_CPU_CORE,
		.window.min = 0,
		.window.max = WDT_IRQ_PERIOD_MS,
		.callback = wdt_callback,
	};

	wdt_dev = DEVICE_DT_GET(DT_NODELABEL(sec_iwdg));
	if (!device_is_ready(wdt_dev)) {
		LOG_ERR("WDT device not ready");
		return -ENODEV;
	}

	wdt_channel_id = wdt_install_timeout(wdt_dev, &cfg);
	if (wdt_channel_id < 0) {
		LOG_ERR("WDT install timeout failed: %d", wdt_channel_id);
		return wdt_channel_id;
	}

	int ret = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
	if (ret != 0) {
		LOG_ERR("WDT setup failed: %d", ret);
		return ret;
	}

	LOG_INF("WDT periodic IRQ started (period=%d ms)", WDT_IRQ_PERIOD_MS);
	return 0;
}

int main(void)
{
	flash_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("Flash device not ready");
		return 0;
	}

	if (wdt_irq_init() != 0) {
		LOG_ERR("Watchdog IRQ init failed");
		return 0;
	}

	LOG_INF("SMP Flash Test");
	LOG_INF("==========================");

	atomic_set(&start_barrier, 0);
	atomic_set(&done_counter, 0);

	k_tid_t tid = k_thread_create(&cpu0_thread, cpu0_stack, K_THREAD_STACK_SIZEOF(cpu0_stack),
			flash_test_thread, (void *)0, (void *)TEST_REGION_OFFSET_CPU0, NULL,
			K_PRIO_PREEMPT(10), 0, K_FOREVER);
	k_thread_name_set(&cpu0_thread, "cpu0_flash");
	// k_thread_cpu_mask_clear(tid);
	// k_thread_cpu_mask_enable(tid, 0);
	k_tid_t cpu1_thread_tid = k_thread_create(&cpu1_thread, cpu1_stack, K_THREAD_STACK_SIZEOF(cpu1_stack),
			flash_test_thread, (void *)1, (void *)TEST_REGION_OFFSET_CPU1, NULL,
			K_PRIO_PREEMPT(10), 0, K_FOREVER);

	k_tid_t cpu2_thread_tid = k_thread_create(&cpu2_thread, cpu2_stack, K_THREAD_STACK_SIZEOF(cpu2_stack),
			flash_test_thread, (void *)2, (void *)TEST_REGION_OFFSET_CPU2, NULL,
			K_PRIO_PREEMPT(10), 0, K_FOREVER);

	k_thread_name_set(&cpu1_thread, "cpu1_flash");
	k_thread_name_set(&cpu2_thread, "cpu1_flash2");

	/* Start UART4 interrupt storm on CPU0 to stress nested IRQs while Flash test runs. */
	k_tid_t uart4_tid = k_thread_create(&uart4_storm_thread, uart4_storm_stack,
					    K_THREAD_STACK_SIZEOF(uart4_storm_stack),
					    uart4_storm_thread_fn, NULL, NULL, NULL,
					    K_PRIO_PREEMPT(UART4_STORM_THREAD_PRIO), 0, K_FOREVER);
	k_thread_name_set(&uart4_storm_thread, "uart4_storm");
	// k_thread_cpu_mask_clear(uart4_tid);
	// k_thread_cpu_mask_enable(uart4_tid, 0);

	// k_thread_cpu_mask_clear(cpu1_thread_tid);
	// k_thread_cpu_mask_enable(cpu1_thread_tid, 1);
	// k_thread_cpu_mask_clear(cpu2_thread_tid);
	// k_thread_cpu_mask_enable(cpu2_thread_tid, 1);

	k_thread_start(tid);
	k_thread_start(cpu1_thread_tid);
	k_thread_start(cpu2_thread_tid);
	k_thread_start(uart4_tid);


	while (atomic_get(&done_counter) < 3) {
		k_sleep(K_MSEC(100));
	}

	LOG_INF("All tests done");
	return 0;
}
