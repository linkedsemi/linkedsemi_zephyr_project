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
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include "emmc_partition.h"
#include "stdio.h"

LOG_MODULE_REGISTER(coredump_emmc_test, CONFIG_KERNEL_LOG_LEVEL);

#ifdef CONFIG_FS_FATFS_MULTI_PARTITION
#include <zephyr/fs/fs.h>
#include <ff.h>

static FATFS fs_part1;

static int test_fatfs_read_write(void)
{
	struct fs_file_t fil;
	int ret;
	char write_data[] = "Hello from eMMC FAT32 Partition 1!\n";
	char read_buf[64] = {0};

	LOG_INF("=== FATFS Read/Write Test ===");

	struct fs_mount_t mp1 = {
		.type = FS_FATFS,
		.fs_data = &fs_part1,
		// .mnt_point = EMMC_FATFS_MOUNT_POINT,
		.mnt_point = "/SD2",
	};

	LOG_INF("Mounting partition 1 at %s...", EMMC_FATFS_MOUNT_POINT);
	ret = fs_mount(&mp1);
	if (ret < 0) {
		LOG_ERR("fs_mount failed: %d", ret);
		return ret;
	}
	LOG_INF("Partition 1 mounted successfully");

	/* Write test */
	fs_file_t_init(&fil);
	ret = fs_open(&fil, EMMC_FATFS_MOUNT_POINT "/test.txt", FS_O_CREATE | FS_O_WRITE);
	if (ret < 0) {
		LOG_ERR("fs_open write failed: %d", ret);
		fs_unmount(&mp1);
		return ret;
	}

	ret = fs_write(&fil, write_data, strlen(write_data));
	if (ret < 0) {
		LOG_ERR("fs_write failed: %d", ret);
		fs_close(&fil);
		fs_unmount(&mp1);
		return ret;
	}
	LOG_INF("Wrote %d bytes to %s/test.txt", ret, EMMC_FATFS_MOUNT_POINT);
	fs_sync(&fil);
	fs_close(&fil);

	/* Read test */
	fs_file_t_init(&fil);
	ret = fs_open(&fil, EMMC_FATFS_MOUNT_POINT "/test.txt", FS_O_READ);
	if (ret < 0) {
		LOG_ERR("fs_open read failed: %d", ret);
		fs_unmount(&mp1);
		return ret;
	}

	memset(read_buf, 0, sizeof(read_buf));
	ret = fs_read(&fil, read_buf, sizeof(read_buf) - 1);
	if (ret < 0) {
		LOG_ERR("fs_read failed: %d", ret);
		fs_close(&fil);
		fs_unmount(&mp1);
		return ret;
	}
	read_buf[ret] = '\0';
	LOG_INF("Read %d bytes: %s", ret, read_buf);
	fs_close(&fil);

	/* Unmount */
	fs_unmount(&mp1);

	LOG_INF("=== FATFS Test PASSED ===");
	return 0;
}
#endif /* CONFIG_FS_FATFS_MULTI_PARTITION */

struct k_thread crash_thread;
struct k_thread verify_thread;
K_THREAD_STACK_DEFINE(crash_stack, CONFIG_MAIN_STACK_SIZE);
K_THREAD_STACK_DEFINE(verify_stack, CONFIG_MAIN_STACK_SIZE);

#define THREAD_STATE_TERMINATED BIT(3)

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
static uint8_t coredump_read_buf[5120] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);

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
		k_sleep(K_MSEC(1));

		/* Check if crash thread terminated */
		if (crash_thread.base.thread_state & THREAD_STATE_TERMINATED) {
			LOG_INF("[Thread2] Detected crash_thread terminated");
			break;
		}

		LOG_DBG("[Thread2] Still monitoring... thread_state=0x%x",
			crash_thread.base.thread_state);
	}

	/* Wait for coredump to complete */
	k_sleep(K_MSEC(3));

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

	LOG_INF("[Thread2] Test complete");
	LOG_INF("Pleae test coredump shell, use cmd :'coredump_emmc xxxx'");

#ifdef CONFIG_FS_FATFS_MULTI_PARTITION
	/* Test FATFS read/write after coredump read */
	LOG_INF("[Thread2] Testing FATFS after coredump...");
	test_fatfs_read_write();
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

#ifdef CONFIG_FS_FATFS_MULTI_PARTITION
	/* Test FATFS partition read/write - partition already created in init_emmc_backend */
	test_fatfs_read_write();
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
