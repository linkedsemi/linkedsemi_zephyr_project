/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <soc.h>
#ifndef DELAY_MS
#define DELAY_MS(ms) k_msleep(ms)
#endif

#define READ_LLI_DIS 0
#define NO_ALIGN 0
LOG_MODULE_REGISTER(flash_spi_ls_example, LOG_LEVEL_INF);
#define TEST_READ_SIZE  4096

#define FLASH_TEST_STACK_SIZE 4096
#define FLASH_TEST_PRIORITY 5
#define START_ADDR 0x00
#define ERASE_SIZE 0X9000
#define FLASH_PAGE_SIZE_MAIN 256
#define FLASH_TEST_255_SIZE  255
#define FLASH_TEST_255_ADDR  (START_ADDR + 0x7000)

static uint8_t write_buf[32896]={0};
static uint8_t read_buf[32896]={0};
static uint8_t test_255_write_buf[FLASH_TEST_255_SIZE];
static uint8_t test_255_read_buf[FLASH_TEST_255_SIZE];
#if NO_ALIGN

#if READ_LLI_DIS
__aligned(4) static uint8_t read_total_big_noalign_buf[sizeof(write_buf) + 8] = {0};
#else
__aligned(4) static uint8_t read_total_big_lli_noalign_buf[sizeof(write_buf)-24564 + 8] = {0};
#endif

#else

#if READ_LLI_DIS
static uint8_t read_total_big_buf[sizeof(write_buf)]={0};
static uint8_t read_total_big_buf1[sizeof(write_buf)-1]={0};
static uint8_t read_total_big_buf2[sizeof(write_buf)-2]={0};
static uint8_t read_total_big_buf3[sizeof(write_buf)-3]={0};
#else
static uint8_t read_total_big_lli_buf[sizeof(write_buf)-24564]={0};
static uint8_t read_total_big_lli_buf1[sizeof(write_buf)-24565]={0};
static uint8_t read_total_big_lli_buf2[sizeof(write_buf)-24566]={0};
static uint8_t read_total_big_lli_buf3[sizeof(write_buf)-24567]={0};
#endif

#endif
int  test_spif(void)
{
	do {
		uint8_t id[3] = { 0 };
		const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
		// const struct device *const flash_dev = DEVICE_DT_GET(DT_ALIAS(spiflash));
		// spi_nor_re_init(flash_dev);
		int rc = flash_read_jedec_id(flash_dev, id);
		DELAY_MS(10);
		if (rc == 0) {

			LOG_INF("jedec-id = [%02x %02x %02x];\n", id[0], id[1], id[2]);
		} else {
		LOG_INF("JEDEC ID read failed: %d\n", rc);
		}
	} while(0);

	return 0;
}

int flash_helper_erase(const struct device *dev, uint32_t addr, uint32_t size)
{
	int ret = flash_erase(dev, addr, size);
	if (ret != 0) {
		DEV_ERR(dev,"Erase Error at 0x%08x (ret: %d)", addr, ret);
	}
	return ret;
}

int flash_helper_write(const struct device *dev, uint32_t addr, const uint8_t *data, uint32_t size)
{
	int ret;
	for (uint32_t offset = 0; offset < size; offset += FLASH_PAGE_SIZE_MAIN) {
		uint32_t chunk = (size - offset < FLASH_PAGE_SIZE_MAIN) ? (size - offset) : FLASH_PAGE_SIZE_MAIN;

		ret = flash_write(dev, addr + offset, &data[offset], chunk);
		if (ret != 0) {
			DEV_ERR(dev,"Write Error at 0x%08x", addr + offset);
		return ret;
		}
	}
	return 0;
}

int flash_helper_read(const struct device *dev, uint32_t addr, uint8_t *dest, uint32_t size)
{
	int ret = flash_read(dev, addr, dest, size);
	if (ret != 0) {
		DEV_ERR(dev,"Read Error at 0x%08x (ret: %d)", addr, ret);
	}
	return ret;
}

int flash_helper_verify(const uint8_t *expected, const uint8_t *actual, uint32_t size)
{
	if (memcmp(expected, actual, size) == 0) {
		return 0;
	}

	for (uint32_t i = 0; i < size; i++) {
		if (expected[i] != actual[i]) {
		LOG_ERR("Mismatch! Offset: 0x%x, Exp: 0x%02x, Act: 0x%02x", i, expected[i], actual[i]);
		break;
		}
	}
	return -EIO;
}

/*
 * Erase one sector, write 255 bytes in a single flash_write(), read back and verify.
 */
static int test_flash_255_byte_write_read(const struct device *dev, uint32_t addr)
{
	int ret;
	const char *test_name = "255-byte single write/read";

	memset(test_255_write_buf, 0, sizeof(test_255_write_buf));
	memset(test_255_read_buf, 0, sizeof(test_255_read_buf));

	for (uint32_t i = 0; i < FLASH_TEST_255_SIZE; i++) {
		test_255_write_buf[i] = (uint8_t)(i);
	}

	ret = flash_helper_erase(dev, addr, TEST_READ_SIZE);
	if (ret != 0) {
		DEV_ERR(dev, "[%s] erase failed at 0x%08x", test_name, addr);
		return ret;
	}

	ret = flash_write(dev, addr, test_255_write_buf, FLASH_TEST_255_SIZE);
	if (ret != 0) {
		DEV_ERR(dev, "[%s] single write failed at 0x%08x", test_name, addr);
		return ret;
	}

	ret = flash_read(dev, addr, test_255_read_buf, FLASH_TEST_255_SIZE);
	if (ret != 0) {
		DEV_ERR(dev, "[%s] read failed at 0x%08x", test_name, addr);
		return ret;
	}

	ret = flash_helper_verify(test_255_write_buf, test_255_read_buf, FLASH_TEST_255_SIZE);
	if (ret != 0) {
		DEV_ERR(dev, "[%s] verify failed at 0x%08x", test_name, addr);
		LOG_HEXDUMP_ERR(test_255_write_buf, FLASH_TEST_255_SIZE, "Expected:");
		LOG_HEXDUMP_ERR(test_255_read_buf, FLASH_TEST_255_SIZE, "Actual:");
		return ret;
	}

	DEV_INF(dev, ">>> %s PASSED at 0x%08x <<<", test_name, addr);
	return 0;
}

static int flash_read_big_and_verify_data(const struct device *dev, uint32_t addr,
                            const uint8_t *expected, uint8_t *actual,
                            size_t len, const char *test_name)
{
	int ret;

	ret = flash_helper_read(dev, addr, actual, len);
	if (ret != 0) {
		DEV_ERR(dev, "[%s] Read operation failed! (ret: %d)", test_name, ret);
		return ret;
	}

	if (memcmp(expected, actual, len) == 0) {
		DEV_INF(dev, ">>> %s PASSED! (Size: %zu) <<<", test_name, len);
		return 0;
	} else {

		DEV_ERR(dev, ">>> %s BULK CHECK FAILED! (Addr: 0x%08x) <<<", test_name, addr);
		LOG_HEXDUMP_ERR(expected, len, "Expected Data:");
		LOG_HEXDUMP_ERR(actual, len, "Actual Read From Flash:");

		return -EIO;
	}
}


void main(void)
{
	const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(flash2));
	uint8_t id[3] = { 0 };
	int rc = 0;

	rc = flash_read_jedec_id(flash_dev, id);
	if (rc == 0) {
		printf("jedec-id = [%02x %02x %02x];\n", id[0], id[1], id[2]);
	} else {
		printf("JEDEC ID read failed: %d\n", rc);
	}
	test_spif();

	LOG_INF("start erase 0x%x len 0x%x", START_ADDR, ERASE_SIZE);
	int ret=0;
	uint32_t current_addr = START_ADDR;
	memset(write_buf, 0, sizeof(write_buf));
	memset(read_buf, 0, sizeof(read_buf));
	/*Only channels 0 and 1 have lli*/
	#if NO_ALIGN

	#if READ_LLI_DIS
	memset(read_total_big_noalign_buf, 0, sizeof(read_total_big_noalign_buf));
	#else
	memset(read_total_big_lli_noalign_buf, 0, sizeof(read_total_big_lli_noalign_buf));
	#endif

	#else

	#if READ_LLI_DIS
	memset(read_total_big_buf, 0, sizeof(read_total_big_buf));
	memset(read_total_big_buf1, 0, sizeof(read_total_big_buf1));
	memset(read_total_big_buf2, 0, sizeof(read_total_big_buf2));
	memset(read_total_big_buf3, 0, sizeof(read_total_big_buf3));
	#else
	memset(read_total_big_lli_buf, 0, sizeof(read_total_big_lli_buf));
	memset(read_total_big_lli_buf1, 0, sizeof(read_total_big_lli_buf1));
	memset(read_total_big_lli_buf2, 0, sizeof(read_total_big_lli_buf2));
	memset(read_total_big_lli_buf3, 0, sizeof(read_total_big_lli_buf3));
	#endif

	#endif
	flash_helper_erase(flash_dev, START_ADDR, ERASE_SIZE);
#if 0
	ret = test_flash_255_byte_write_read(flash_dev, FLASH_TEST_255_ADDR);
	if (ret != 0) {
		goto test_end;
	}
#else
	for (uint32_t len = 1; len <= FLASH_PAGE_SIZE_MAIN; len++) {
		uint32_t offset = current_addr - START_ADDR;
		if ((current_addr + len) > (START_ADDR + ERASE_SIZE)) {
			DEV_ERR(flash_dev,"Test reached boundary of erased area at addr %u len: %u",current_addr, len);
			break;
		}

		// In each round, the first byte is equal to the current length.
		for (uint32_t i = 0; i < len; i++) {
			write_buf[offset+i] = (uint8_t)((len + i) & 0xFF);
		}

		// 0-len write
		ret = flash_helper_write(flash_dev, current_addr, &write_buf[offset], len);
		if (ret != 0) {
			DEV_ERR(flash_dev,"Write failed at addr 0x%x, len %u", current_addr, len);
			goto test_end;
		}

		ret = flash_helper_read(flash_dev, current_addr, &read_buf[offset], len);
		if (ret != 0) {
			DEV_ERR(flash_dev,"Read failed at addr 0x%x, len %u", current_addr, len);
			goto test_end;
		}

		if (flash_helper_verify(&write_buf[offset], &read_buf[offset], len) != 0) {
			// DEV_ERR(dev,"Failed at write length: %u", len);
			DEV_ERR(flash_dev, "verify Error Length: %u, Flash Addr: 0x%08x, Offset: 0x%x", len, current_addr, offset);
			uint32_t error_count = 0; // error cnt
			for (uint32_t i = 0; i < len; i++) {
					if (write_buf[offset + i] != read_buf[offset + i]) {
						DEV_ERR(flash_dev, "Mismatch at i=%d (Addr: 0x%x): Exp 0x%02x, Act 0x%02x",
							i, current_addr + i, write_buf[offset + i], read_buf[offset + i]);
							error_count++;

						if (error_count >= 10) {
							DEV_ERR(flash_dev, "!TO many data no pass.");
							break;
						}
					}
			}
			LOG_HEXDUMP_ERR(&write_buf[offset], len, "Expected Data Buffer:");
			LOG_HEXDUMP_ERR(&read_buf[offset], len, "Actual Read From Flash:");
			goto test_end;
		}

		current_addr += len;
	}

	uint32_t total_tested_size = current_addr - START_ADDR;
	DEV_INF(flash_dev,"Performing final bulk verification of %u bytes...", total_tested_size);
	if (memcmp(write_buf, read_buf, total_tested_size) == 0) {
		DEV_INF(flash_dev,">>> FINAL BULK CHECK PASSED! <<<");
	} else {
		DEV_ERR(flash_dev,">>> FINAL BULK CHECK FAILED! <<<");
		ret = -EIO;
	}

	#if NO_ALIGN

	#if READ_LLI_DIS
	size_t base_len = sizeof(write_buf); // L is raw write_buf length
	// extra poll 3 addr (+1, +2, +3)
	for (int off = 1; off <= 3; off++) {
		uint8_t *current_dst = read_total_big_noalign_buf + off;

		// inside poll 4 read length (L, L-1, L-2, L-3)
		for (int l_off = 0; l_off < 4; l_off++) {
			size_t current_len = base_len - l_off;

			memset(read_total_big_noalign_buf, 0, sizeof(read_total_big_noalign_buf));
			ret = flash_helper_read(flash_dev, START_ADDR, current_dst, current_len);

			if (ret == 0 && memcmp(write_buf, current_dst, current_len) == 0) {
				DEV_INF(flash_dev, "PASSED: Offset +%d, Len L-%d (%zu)", off, l_off, current_len);
			} else {
				DEV_ERR(flash_dev, "FAILED: Offset +%d, Len L-%d (%zu)!", off, l_off, current_len);

				DEV_ERR(flash_dev, "Fail Addr: %p", (void*)current_dst);
				goto test_end;
			}
		}
	}
	#else
	size_t base_len = sizeof(write_buf)-24564;
	for (int off = 1; off <= 3; off++) {
		uint8_t *current_dst = read_total_big_lli_noalign_buf + off;

		for (int l_off = 0; l_off < 4; l_off++) {
			size_t current_len = base_len - l_off;

			memset(read_total_big_lli_noalign_buf, 0, sizeof(read_total_big_lli_noalign_buf));

			ret = flash_helper_read(flash_dev, START_ADDR, current_dst, current_len);

			if (ret == 0 && memcmp(write_buf, current_dst, current_len) == 0) {
				DEV_INF(flash_dev, "PASSED: Offset +%d, Len L-%d (%zu)", off, l_off, current_len);
			} else {
				DEV_ERR(flash_dev, "FAILED: Offset +%d, Len L-%d (%zu)!", off, l_off, current_len);

				DEV_ERR(flash_dev, "Fail Addr: %p", (void*)current_dst);
				goto test_end;
			}
		}
	}
	#endif

	#else
	#if READ_LLI_DIS
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_buf,sizeof(read_total_big_buf),"read_total_big_buf");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_buf1,sizeof(read_total_big_buf1),"read_total_big_buf1");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf1");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_buf2,sizeof(read_total_big_buf2),"read_total_big_buf2");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf2");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_buf3,sizeof(read_total_big_buf3),"read_total_big_buf3");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf3");goto test_end;}
	#else
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_lli_buf,sizeof(read_total_big_lli_buf),"read_total_big_lli_buf");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_lli_buf1,sizeof(read_total_big_lli_buf1),"read_total_big_lli_buf1");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf1");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_lli_buf2,sizeof(read_total_big_lli_buf2),"read_total_big_lli_buf2");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf2");goto test_end;}
	ret = flash_read_big_and_verify_data(flash_dev,START_ADDR,write_buf,read_total_big_lli_buf3,sizeof(read_total_big_lli_buf3),"read_total_big_lli_buf3");
	if (ret != 0) {DEV_ERR(flash_dev,"Read failed read_total_big_buf3");goto test_end;}
	#endif
	#endif
#endif
test_end:
	if (ret != 0) {
		DEV_ERR(flash_dev,"Flash Test Failed!");
	} else {
		DEV_INF(flash_dev,"========== All Flash Tests Passed! ==========");
	}
//     flash_test(flash_dev);
}