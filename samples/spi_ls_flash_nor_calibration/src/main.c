/*
 * Copyright (c) 2026 LinkedSemi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/qspi_ls.h>
#include <zephyr/drivers/spi_nor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flash_calib_sample, LOG_LEVEL_INF);

#define FLASH_NODE      DT_NODELABEL(flash2)
#define QSPI_NODE       DT_PARENT(FLASH_NODE)
#define CALIB_READ_LEN  DT_PROP(QSPI_NODE, timing_calibration_data_len)

BUILD_ASSERT(CALIB_READ_LEN > 0, "timing-calibration-data-len must be > 0");
BUILD_ASSERT(CALIB_READ_LEN <= 4096, "sample buffer limited to 4096 bytes");

static const struct device *const flash_dev = DEVICE_DT_GET(FLASH_NODE);
static const struct device *const qspi_dev = DEVICE_DT_GET(QSPI_NODE);

static uint8_t read_buf[CALIB_READ_LEN];
static uint8_t write_buf[CALIB_READ_LEN];

static const char *calib_status_str(int status)
{
	switch (status) {
	case QSPI_TIMING_CALIB_UNUSED:
		return "unused";
	case QSPI_TIMING_CALIB_SUCCESS:
		return "success";
	case QSPI_TIMING_CALIB_FAILED:
		return "failed";
	default:
		return "unknown";
	}
}

static int read_jedec_id(void)
{
	uint8_t id[3];
	int rc = flash_read_jedec_id(flash_dev, id);

	if (rc == 0) {
		LOG_INF("JEDEC ID = [%02x %02x %02x]", id[0], id[1], id[2]);
	} else {
		LOG_ERR("JEDEC ID read failed: %d", rc);
	}

	return rc;
}

static int get_flash_size(uint64_t *size_out)
{
	const struct flash_parameters *params = flash_get_parameters(flash_dev);

	if (params == NULL || params->flash_size == 0U) {
		LOG_ERR("flash size unavailable");
		return -EINVAL;
	}

	*size_out = params->flash_size;
	return 0;
}

static int read_calib_size(off_t offset, uint8_t *buf, const char *tag)
{
	int rc = flash_read(flash_dev, offset, buf, CALIB_READ_LEN);

	if (rc != 0) {
		LOG_ERR("[%s] flash read 0x%lx len %u failed: %d",
			tag, (long)offset, CALIB_READ_LEN, rc);
		return rc;
	}

	LOG_INF("[%s] flash read 0x%lx len %u ok, first bytes: %02x %02x %02x %02x",
		tag, (long)offset, CALIB_READ_LEN,
		buf[0], buf[1], buf[2], buf[3]);
	return 0;
}

static void fill_from_flash_end(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		buf[i] = (uint8_t)(0xA5U ^ (i & 0xFFU) ^ ((i >> 8) & 0xFFU));
	}
}

static int write_flash(off_t offset, const uint8_t *data)
{
	size_t write_block = flash_get_write_block_size(flash_dev);
	int rc;

	rc = flash_erase(flash_dev, offset, CALIB_READ_LEN);
	if (rc != 0) {
		LOG_ERR("flash erase 0x%lx len %u failed: %d",
			(long)offset, CALIB_READ_LEN, rc);
		return rc;
	}

	for (size_t done = 0; done < CALIB_READ_LEN; done += write_block) {
		size_t chunk = MIN(write_block, CALIB_READ_LEN - done);

		rc = flash_write(flash_dev, offset + done, &data[done], chunk);
		if (rc != 0) {
			LOG_ERR("flash write 0x%lx len %zu failed: %d",
				(long)(offset + done), chunk, rc);
			return rc;
		}
	}

	LOG_INF("flash write 0x%lx len %u", (long)offset, CALIB_READ_LEN);
	return 0;
}

static int write_from_flash_end(off_t offset)
{
	fill_from_flash_end(write_buf, CALIB_READ_LEN);
	return write_flash(offset, write_buf);
}

static int erase_flash_end(off_t offset)
{
	int rc = flash_erase(flash_dev, offset, CALIB_READ_LEN);

	if (rc != 0) {
		LOG_ERR("flash erase 0x%lx len %u failed: %d",
			(long)offset, CALIB_READ_LEN, rc);
		return rc;
	}

	LOG_INF("flash erase end 0x%lx len %u", (long)offset, CALIB_READ_LEN);
	return 0;
}

static int check_calib_status(const char *stage)
{
	int status = qspi_timing_calibration_status(qspi_dev);

	if (status < 0) {
		LOG_ERR("[%s] calib status query failed: %d", stage, status);
		return status;
	}

	LOG_INF("[%s] calib status: %s (%d)", stage,
		calib_status_str(status), status);
	return status;
}

static int run_calibration(void)
{
	uint64_t flash_size;
	off_t end_off;
	int status;
	int rc;
	int ret = 0;

	rc = get_flash_size(&flash_size);
	if (rc != 0) {
		return rc;
	}

	if (flash_size < CALIB_READ_LEN) {
		LOG_ERR("flash size %llu < calib len %u",
			(unsigned long long)flash_size, CALIB_READ_LEN);
		return -EINVAL;
	}

	end_off = (off_t)(flash_size - CALIB_READ_LEN);
	LOG_INF("flash size 0x%llx, end offset 0x%lx len %u",
		(unsigned long long)flash_size, (long)end_off, CALIB_READ_LEN);

	status = check_calib_status("boot");
	rc = read_calib_size(end_off, read_buf, "boot");
	if (rc != 0) {
		ret = rc;
		goto out;
	}

	if (status != QSPI_TIMING_CALIB_FAILED) {
		ret = (status == QSPI_TIMING_CALIB_SUCCESS) ? 0 : -EIO;
		goto out;
	}

	LOG_WRN("calib failed, write flash end and re-init");

	rc = write_from_flash_end(end_off);
	if (rc != 0) {
		ret = rc;
		goto out;
	}

	rc = spi_nor_re_init(flash_dev);
	if (rc != 0) {
		LOG_ERR("spi_nor_re_init failed: %d", rc);
		ret = rc;
		goto out;
	}

	status = check_calib_status("after re-init");
	rc = read_calib_size(end_off, read_buf, "after re-init");
	if (rc != 0) {
		ret = rc;
		goto out;
	}

	if (status == QSPI_TIMING_CALIB_SUCCESS) {
		LOG_INF("calib recovered after re-init");
		ret = 0;
	} else {
		LOG_ERR("calib still failed after re-init");
		ret = -EIO;
	}

out:
	rc = erase_flash_end(end_off);
	if (rc != 0 && ret == 0) {
		ret = rc;
	}

	return ret;
}

int main(void)
{
	int rc;

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("flash device not ready");
		return 0;
	}

	if (!device_is_ready(qspi_dev)) {
		LOG_ERR("QSPI device not ready");
		return 0;
	}

	rc = read_jedec_id();
	if (rc != 0) {
		return 0;
	}

	rc = run_calibration();
	if (rc == 0) {
		LOG_INF("========== calibration sample PASSED ==========");
	} else {
		LOG_ERR("========== calibration sample FAILED (%d) ==========", rc);
	}

	return 0;
}
