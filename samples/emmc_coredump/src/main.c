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
 * 1. Clear FATFS test directory and coredump data
 * 2. FATFS read/write test (before coredump)
 * 3. Thread 1 crashes and triggers coredump to eMMC
 * 4. In exception handler, coredump data is stored to eMMC
 * 5. Thread 2 detects crash and reads coredump from eMMC
 * 6. Thread 2 verifies FATFS data is NOT corrupted by coredump
 * 7. Thread 2 performs additional FATFS read/write to verify FATFS still works
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

/* Test file names */
#define TEST_FILE_1 "before.txt"
#define TEST_FILE_2 "after.txt"
#define TEST_DATA_1 "Hello from eMMC FAT32 - Data before crash!"
#define TEST_DATA_2 "FATFS still works after coredump!"

/**
 * @brief Clear all test files in the FATFS partition
 */
static int clear_fatfs_test_files(struct fs_mount_t *mountp)
{
	struct fs_dir_t dir;
	struct fs_dirent entry;
	int ret;

	LOG_INF("Clearing FATFS test files in %s...", mountp->mnt_point);

	ret = fs_opendir(&dir, mountp->mnt_point);
	if (ret < 0) {
		LOG_ERR("fs_opendir failed: %d", ret);
		return ret;
	}

	while (1) {
		ret = fs_readdir(&dir, &entry);
		if (ret < 0) {
			LOG_ERR("fs_readdir failed: %d", ret);
			fs_closedir(&dir);
			return ret;
		}

		/* Check if end of directory */
		if (entry.type == FS_DIR_ENTRY_DIR && entry.name[0] == 0) {
			break;
		}

		/* Skip . and .. entries */
		if (entry.name[0] == '.' &&
		    (entry.name[1] == 0 || (entry.name[1] == '.' && entry.name[2] == 0))) {
			continue;
		}

		/* Build full path */
		char file_path[64];
		snprintk(file_path, sizeof(file_path), "%s/%s", mountp->mnt_point, entry.name);

		if (entry.type == FS_DIR_ENTRY_FILE) {
			LOG_INF("Deleting file: %s", file_path);
			ret = fs_unlink(file_path);
			if (ret < 0) {
				LOG_ERR("Failed to delete %s: %d", file_path, ret);
			}
		} else if (entry.type == FS_DIR_ENTRY_DIR) {
			LOG_INF("Skipping directory: %s", file_path);
		}
	}

	fs_closedir(&dir);
	LOG_INF("FATFS test files cleared");
	return 0;
}

/**
 * @brief Clear coredump data from eMMC
 */
static void clear_coredump_data(void)
{
	int ret;

	LOG_INF("Clearing coredump data from eMMC...");

	ret = coredump_cmd(COREDUMP_CMD_ERASE_STORED_DUMP, NULL);
	if (ret == 0) {
		LOG_INF("Coredump data erased successfully");
	} else {
		LOG_ERR("Failed to erase coredump: %d", ret);
	}
}

/**
 * @brief Write test data to a specific file
 */
static int write_test_file(struct fs_mount_t *mountp, const char *filename, const char *data)
{
	struct fs_file_t fil;
	char file_path[64];
	char short_name[16];
	int ret;

	fs_file_t_init(&fil);

	/* Truncate filename to 12 chars (8.3 SFN limit) to avoid LFN issues */
	strncpy(short_name, filename, 12);
	short_name[12] = '\0';

	snprintk(file_path, sizeof(file_path), "%s/%s", EMMC_FATFS_MOUNT_POINT, short_name);
	ret = fs_open(&fil, file_path, FS_O_CREATE | FS_O_WRITE);
	if (ret < 0) {
		LOG_ERR("fs_open write failed for %s: %d", filename, ret);
		return ret;
	}

	ret = fs_write(&fil, data, strlen(data));
	if (ret < 0) {
		LOG_ERR("fs_write failed for %s: %d", filename, ret);
		fs_close(&fil);
		return ret;
	}

	LOG_INF("Wrote %d bytes to %s", ret, filename);
	fs_sync(&fil);
	fs_close(&fil);

	return 0;
}

/**
 * @brief Read and verify test data from a specific file
 */
static int read_verify_file(struct fs_mount_t *mountp, const char *filename,
			     const char *expected_data, bool verify_content)
{
	struct fs_file_t fil;
	char file_path[64];
	char short_name[16];
	char read_buf[128] = {0};
	int ret;

	fs_file_t_init(&fil);

	/* Truncate filename to 12 chars (8.3 SFN limit) to avoid LFN issues */
	strncpy(short_name, filename, 12);
	short_name[12] = '\0';

	snprintk(file_path, sizeof(file_path), "%s/%s", EMMC_FATFS_MOUNT_POINT, short_name);
	ret = fs_open(&fil, file_path, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("fs_open read failed for %s: %d", filename, ret);
		return ret;
	}

	ret = fs_read(&fil, read_buf, sizeof(read_buf) - 1);
	fs_close(&fil);

	if (ret < 0) {
		LOG_ERR("fs_read failed for %s: %d", filename, ret);
		return ret;
	}

	read_buf[ret] = '\0';
	LOG_INF("Read %d bytes from %s: %s", ret, filename, read_buf);

	if (verify_content && expected_data != NULL) {
		if (strcmp(read_buf, expected_data) != 0) {
			LOG_ERR("Data mismatch! Expected: %s, Got: %s", expected_data, read_buf);
			return -1;
		}
		LOG_INF("Data verified OK for %s", filename);
	}

	return 0;
}

/**
 * @brief FATFS read/write test
 */
static int test_fatfs_read_write(const char *filename, const char *write_data)
{
	struct fs_mount_t mp1 = {
		.type = FS_FATFS,
		.fs_data = &fs_part1,
		.mnt_point = EMMC_FATFS_MOUNT_POINT,
	};
	struct fs_file_t fil;
	char file_path[64];
	char short_name[16];
	int ret;
	char read_buf[128];

	LOG_INF("=== FATFS Read/Write Test ===");
	LOG_INF("File: %s, Data: %s", filename, write_data);

	/* Mount */
	ret = fs_mount(&mp1);
	if (ret < 0) {
		LOG_ERR("fs_mount failed: %d", ret);
		return ret;
	}
	LOG_INF("Partition mounted successfully");

	/* Build file path with snprintk */

	/* Truncate filename to 12 chars (8.3 SFN limit) to avoid LFN issues */
	strncpy(short_name, filename, 12);
	short_name[12] = '\0';

	snprintk(file_path, sizeof(file_path), "%s/%s", EMMC_FATFS_MOUNT_POINT, short_name);
	LOG_INF("Opening path: %s", file_path);

	/* Write test */
	fs_file_t_init(&fil);
	ret = fs_open(&fil, file_path, FS_O_CREATE | FS_O_WRITE);
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
	LOG_INF("Wrote %d bytes", ret);
	fs_sync(&fil);
	fs_close(&fil);

	/* Read test */
	fs_file_t_init(&fil);
	ret = fs_open(&fil, file_path, FS_O_READ);
	if (ret < 0) {
		LOG_ERR("fs_open read failed: %d", ret);
		fs_unmount(&mp1);
		return ret;
	}

	memset(read_buf, 0, sizeof(read_buf));
	ret = fs_read(&fil, read_buf, sizeof(read_buf) - 1);
	fs_close(&fil);
	fs_unmount(&mp1);

	if (ret < 0) {
		LOG_ERR("fs_read failed: %d", ret);
		return ret;
	}
	read_buf[ret] = '\0';
	LOG_INF("Read %d bytes: %s", ret, read_buf);

	/* Verify */
	if (strcmp(read_buf, write_data) != 0) {
		LOG_ERR("Data mismatch!");
		return -1;
	}

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
	LOG_INF("Please test coredump shell, use cmd :'coredump_emmc xxxx'");

#ifdef CONFIG_FS_FATFS_MULTI_PARTITION
	/* Test FATFS after coredump */
	LOG_INF("[Thread2] Testing FATFS after coredump...");
	test_fatfs_read_write(TEST_FILE_2, TEST_DATA_2);
#endif

}
static int test_fatfs_read_write22222(void);
int main(void)
{
	LOG_INF("========================================");
	LOG_INF("eMMC Coredump Test Starting...");
	LOG_INF("========================================");
	LOG_INF("Board: %s", CONFIG_BOARD);

	/* Initialize eMMC backend if enabled */
	if (init_coredump_emmc_backend() != 0) {
		LOG_ERR("Failed to initialize eMMC backend");
		LOG_ERR("Please ensure CONFIG_DEBUG_COREDUMP_EMMC=y in prj.conf");
		LOG_ERR("abort coredump test");
		return 0;
	}
#ifdef CONFIG_FS_FATFS_MULTI_PARTITION
	/* Test FATFS read/write - before crash */
	test_fatfs_read_write(TEST_FILE_1, TEST_DATA_1);
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





