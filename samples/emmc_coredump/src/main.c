/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 LinkedSemi Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief eMMC coredump backend test
 *
 * This test demonstrates:
 * 1. Thread 1 crashes and triggers coredump to eMMC
 * 2. In exception handler, coredump data is stored to eMMC
 * 3. Thread 2 detects crash and reads coredump from eMMC
 * 4. Thread 2 prints the coredump data to verify integrity
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/debug/coredump.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include "stdio.h"
#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
/* Test buffer for eMMC bare-metal read/write test */
#define EMMC_TEST_OFFSET    0
#define EMMC_TEST_SIZE      512
static uint8_t emmc_test_write_buf[EMMC_TEST_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
static uint8_t emmc_test_read_buf[EMMC_TEST_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);

/* Forward declarations for HAL functions */
extern uint32_t hal_mmc_write_blocks(uint32_t mapbase, const uint8_t *wbuf, uint32_t start_block, uint32_t num_blocks);
extern uint32_t hal_mmc_read_blocks(uint32_t mapbase, uint8_t *rbuf, uint32_t start_block, uint32_t num_blocks);
#endif

LOG_MODULE_REGISTER(coredump_emmc_test, CONFIG_KERNEL_LOG_LEVEL);

struct k_thread crash_thread;
struct k_thread verify_thread;
K_THREAD_STACK_DEFINE(crash_stack, CONFIG_MAIN_STACK_SIZE);
K_THREAD_STACK_DEFINE(verify_stack, CONFIG_MAIN_STACK_SIZE);

/* Synchronization between threads - use flag instead of k_event */
static volatile bool crash_event_flag;
static volatile uint32_t crash_count;

/* Pattern that will be written to memory before crash for verification */
#define TEST_PATTERN_VALUE 0xDEADBEEF
static volatile uint32_t test_memory_area[16] __noinit;

/* Flag to track if we should read coredump (set after crash) */
static volatile bool crash_occurred = false;

#define THREAD_STATE_TERMINATED BIT(3)

/**
 * @brief Test eMMC using Zephyr disk access API (disk_access_read/disk_access_write)
 *
 * This tests the standard Zephyr disk access functions which go through
 * the SD/MMC driver stack.
 * @return true if test passed, false if failed
 */
static bool test_emmc_zephyr_rw(void)
{
#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
	int ret;
	int i;

	printf("========================================\n");
	printf("eMMC Zephyr Disk Access R/W Test\n");
	printf("========================================\n");

	/* Prepare test data */
	memset(emmc_test_write_buf, 0, sizeof(emmc_test_write_buf));
	for (i = 0; i < EMMC_TEST_SIZE; i++) {
		emmc_test_write_buf[i] = (uint8_t)(i & 0xFF);
	}

	printf("Test: Writing %d bytes to eMMC at block %d...\n", EMMC_TEST_SIZE, EMMC_TEST_OFFSET / 512);
	printf("[DEBUG] write_buf[0..3] = 0x%02X 0x%02X 0x%02X 0x%02X\n",
	       emmc_test_write_buf[0], emmc_test_write_buf[1],
	       emmc_test_write_buf[2], emmc_test_write_buf[3]);

	/* Write using disk access */
	ret = disk_access_write("SD2", emmc_test_write_buf, EMMC_TEST_OFFSET / 512, 1);
	if (ret != 0) {
		printf("ERROR: disk_access_write failed: %d\n", ret);
		return false;
	}
	printf("disk_access_write succeeded!\n");

	/* Read using disk access */
	memset(emmc_test_read_buf, 0, sizeof(emmc_test_read_buf));
	ret = disk_access_read("SD2", emmc_test_read_buf, EMMC_TEST_OFFSET / 512, 1);
	if (ret != 0) {
		printf("ERROR: disk_access_read failed: %d\n", ret);
		return false;
	}
	printf("disk_access_read succeeded!\n");

	printf("[DEBUG] read_buf[0..3] = 0x%02X 0x%02X 0x%02X 0x%02X\n",
	       emmc_test_read_buf[0], emmc_test_read_buf[1],
	       emmc_test_read_buf[2], emmc_test_read_buf[3]);

	/* Verify data */
	printf("Verifying data...\n");
	for (i = 0; i < EMMC_TEST_SIZE; i++) {
		if (emmc_test_read_buf[i] != emmc_test_write_buf[i]) {
			printf("ERROR: Data mismatch at byte %d: wrote 0x%02X, read 0x%02X\n",
				i, emmc_test_write_buf[i], emmc_test_read_buf[i]);
			while(1);
		}
	}

	printf("========================================\n");
	printf("eMMC Zephyr Disk Access R/W Test PASSED!\n");
	printf("========================================\n");
	return true;
#else
	printf("eMMC Zephyr disk access test skipped - CONFIG_DEBUG_COREDUMP_EMMC not enabled\n");
	return false;
#endif
}

/* Turn off optimizations to prevent the compiler from optimizing this away
 * due to the null pointer dereference.
 */
__no_optimization void func_3(uint32_t *addr)
{
	LOG_INF("func_3");
	*addr = 0;
	while(1);
}

__no_optimization void func_2(uint32_t *addr)
{
	LOG_INF("func_2");
	func_3(addr);
}

__no_optimization void func_1(uint32_t *addr)
{
	LOG_INF("func_1");
	func_2(addr);
}

/**
 * @brief Thread 1: Triggers crash and generates coredump
 */
static void crash_entry(void *p1, void *p2, void *p3)
{
	printk("Coredump: %s\n", CONFIG_BOARD);

	func_1(0);

	/* Should never reach here */
	LOG_ERR("[Thread1] ERROR: Function returned after crash!");
}

/**
 * @brief Print coredump data in hexadecimal format with coredump log prefix
 */
static void print_coredump_hex(const uint8_t *data, size_t len)
{
	char line_buf[128];
	size_t i, j;

	/* Print with COREDUMP_PREFIX_STR so it can be parsed by coredump_serial_log_parser.py */
	for (i = 0; i < len; i += 16) {
		int offset = 0;

		offset += snprintk(line_buf + offset, sizeof(line_buf) - offset, "%s", COREDUMP_PREFIX_STR);

		for (j = 0; j < 16 && (i + j) < len; j++) {
			offset += snprintk(line_buf + offset, sizeof(line_buf) - offset,
					  "%02x", data[i + j]);
		}

		LOG_INF("%s", line_buf);
	}
}

/* Static buffer for reading coredump - no malloc allowed */
static uint8_t coredump_read_buf[5120];

/**
 * @brief Thread 2: Monitors for crash and reads coredump from eMMC
 */
static void verify_entry(void *p1, void *p2, void *p3)
{
	int ret;
	ssize_t dump_size;
	struct coredump_cmd_copy_arg copy_arg;
	size_t offset = 0;

	LOG_INF("[Thread2] Starting - waiting for crash in Thread1...");

	while (1) {
		/* Use busy wait instead of k_sleep to avoid scheduler issues during crash handling */
		// k_busy_wait(1000000);  /* 1 second */
		k_sleep(K_MSEC(1)); 
		/* Check if crash occurred via flag */
		if (crash_event_flag) {
			LOG_INF("[Thread2] Crash event detected!");
			break;
		}

		/* Alternative: check if crash thread terminated */
		if (crash_thread.base.thread_state & THREAD_STATE_TERMINATED) {
			LOG_INF("[Thread2] Detected crash_thread terminated");
			crash_occurred = true;
			break;
		}

		LOG_DBG("[Thread2] Still monitoring... thread_state=0x%x",
			crash_thread.base.thread_state);
	}

	/* Wait a bit for coredump to complete */
	// k_busy_wait(2000000);  /* 2 seconds - wait for coredump to complete */
	k_sleep(K_MSEC(3)); 

	//Ensure that the coredump storage space in the SD card has not been erased.
	test_emmc_zephyr_rw();

	LOG_INF("[Thread2] Attempting to read coredump from eMMC...");

	/* Check if coredump is available */
	ret = coredump_query(COREDUMP_QUERY_HAS_STORED_DUMP, NULL);
	if (ret != 1) {
		LOG_ERR("[Thread2] No stored coredump found (query returned %d)", ret);
		LOG_ERR("[Thread2] This may indicate the eMMC backend is not working");
		return;
	}

	dump_size = coredump_query(COREDUMP_QUERY_GET_STORED_DUMP_SIZE, NULL);
	if (dump_size <= 0) {
		LOG_ERR("[Thread2] Invalid coredump size: %d", dump_size);
		return;
	}

	LOG_INF("[Thread2] Found stored coredump, size: %d bytes", dump_size);

	/* Read and print coredump data */
	LOG_INF("[Thread2] Reading coredump data from eMMC...");
	offset = 0;
	/* Print BEGIN marker for coredump log parser */
	LOG_INF("%s%s", COREDUMP_PREFIX_STR, COREDUMP_BEGIN_STR);
	while (offset < dump_size) {
		size_t to_read = MIN(sizeof(coredump_read_buf), dump_size - offset);

		copy_arg.offset = offset;
		copy_arg.buffer = coredump_read_buf;
		copy_arg.length = to_read;

		ret = coredump_cmd(COREDUMP_CMD_COPY_STORED_DUMP, &copy_arg);
		if (ret <= 0) {
			LOG_ERR("[Thread2] Failed to read coredump at offset %zu (ret=%d)",
				offset, ret);
			break;
		}

		/* Print all coredump chunks in hex with #CD: prefix for log parser */
		LOG_INF("[Thread2] --- Coredump data at offset %zu ---", offset);
		print_coredump_hex(coredump_read_buf, ret);

		offset += ret;
	}

	/* Print END marker for coredump log parser */
	LOG_INF("%s%s", COREDUMP_PREFIX_STR, COREDUMP_END_STR);
	if (offset == (size_t)dump_size) {
		LOG_INF("[Thread2] Coredump read complete: %zu bytes", offset);
	} else {
		LOG_ERR("[Thread2] Coredump read incomplete: %zu / %d bytes", offset, dump_size);
	}

	/* Erase coredump after reading */
	// ret = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	// if (ret == 0) {
	// 	LOG_INF("[Thread2] Coredump erased after reading");
	// } else {
	// 	LOG_ERR("[Thread2] Failed to erase coredump: %d", ret);
	// }

	if(test_emmc_zephyr_rw() == true)
	{
		LOG_INF("[Thread2] Test complete");
		LOG_INF("Pleae test coredump shell, use cmd :'coredump_emmc xxxx'");
	}

}

/**
 * @brief Initialize eMMC coredump backend
 *
 * Uses Zephyr's disk access layer to initialize the eMMC card.
 * After this, the card is in TRAN state and ready for read/write.
 *
 * @return 0 if successful, negative errno on error
 */
static int init_emmc_backend(void)
{
#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
	int ret;

	LOG_INF("Initializing eMMC coredump backend...");

	/* Initialize eMMC via Zephyr's disk access layer.
	 * This calls the SDHC driver which initializes the card
	 * (CMD0, CMD1, CMD2, CMD3, CMD7) and puts it in TRAN state.
	 */
	static const char *disk_pdrv = "SD2";
	ret = disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_INIT, NULL);
	if (ret != 0) {
		LOG_ERR("DISK_IOCTL_CTRL_INIT failed: %d", ret);
		return ret;
	}
	LOG_INF("eMMC disk initialized successfully");
	LOG_INF("eMMC coredump backend initialized");

	return 0;
#else
	LOG_INF("eMMC coredump backend not enabled (CONFIG_DEBUG_COREDUMP_EMMC)");
	return -ENODEV;
#endif
}

int main(void)
{
	LOG_INF("========================================");
	LOG_INF("eMMC Coredump Test Starting...");
	LOG_INF("========================================");
	LOG_INF("Board: %s", CONFIG_BOARD);

	/* Initialize eMMC backend if enabled */
	if (init_emmc_backend() != 0) {
		LOG_ERR("Failed to initialize eMMC backend");
		LOG_ERR("Please ensure CONFIG_DEBUG_COREDUMP_EMMC=y in prj.conf");
		LOG_ERR("abort coredump test");
		return 0;
	}

#ifdef CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
	test_emmc_zephyr_rw();
#endif

	LOG_INF("Creating crash thread (Thread1)...");
	k_thread_create(&crash_thread, crash_stack, CONFIG_MAIN_STACK_SIZE,
		       crash_entry, NULL, NULL, NULL,
		       -1, IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, K_NO_WAIT);

	LOG_INF("Creating monitor thread (Thread2)...");
	k_thread_create(&verify_thread, verify_stack, CONFIG_MAIN_STACK_SIZE,
		       verify_entry, NULL, NULL, NULL,
		       -1, IS_ENABLED(CONFIG_USERSPACE) ? K_USER : 0, K_NO_WAIT);

	LOG_INF("Main thread exiting - test running in background");

	return 0;
}
