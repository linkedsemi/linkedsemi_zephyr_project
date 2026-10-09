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
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(flash_calib_sample, LOG_LEVEL_INF);

#define FLASH_NODE DT_NODELABEL(flash2)
#define QSPI_NODE  DT_PARENT(FLASH_NODE)

static const struct device *const flash_dev = DEVICE_DT_GET(FLASH_NODE);
static const struct device *const qspi_dev = DEVICE_DT_GET(QSPI_NODE);

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

static int check_calib_value(const char *stage)
{
	struct qspi_timing_calib_value value;
	int rc;

	rc = qspi_timing_calibration_value(NULL, &value);
	if (rc != -EINVAL) {
		LOG_ERR("[%s] calib value(NULL dev) expected -EINVAL, got %d",
			stage, rc);
		return -EIO;
	}

	rc = qspi_timing_calibration_value(qspi_dev, NULL);
	if (rc != -EINVAL) {
		LOG_ERR("[%s] calib value(NULL out) expected -EINVAL, got %d",
			stage, rc);
		return -EIO;
	}

	rc = qspi_timing_calibration_value(qspi_dev, &value);
	if (rc != 0) {
		LOG_ERR("[%s] calib value query failed: %d", stage, rc);
		return rc;
	}

	if (value.cap_neg > 1U || value.cap_dly > 3U) {
		LOG_ERR("[%s] calib value out of range: cap_neg=%u cap_dly=%u",
			stage, value.cap_neg, value.cap_dly);
		return -EIO;
	}

	LOG_INF("[%s] calib value: cap_neg=%u cap_dly=%u",
		stage, value.cap_neg, value.cap_dly);
	return 0;
}

int main(void)
{
	int status;
	int rc;

	if (!device_is_ready(flash_dev)) {
		LOG_ERR("flash device not ready");
		return 0;
	}

	if (!device_is_ready(qspi_dev)) {
		LOG_ERR("QSPI device not ready");
		return 0;
	}

	/*
	 * JEDEC ID read triggers qspi_ls timing calibration. The reference and
	 * the sweep both use RDID, not a data pattern stored in flash.
	 */
	rc = read_jedec_id();
	if (rc != 0) {
		LOG_ERR("========== calibration sample FAILED (%d) ==========", rc);
		return 0;
	}

	status = check_calib_status("read calibration status");
	LOG_INF("current calib status: %s (%d), is QSPI_TIMING_CALIB_SUCCESS: %s",
		status < 0 ? "query failed" : calib_status_str(status), status,
		status == QSPI_TIMING_CALIB_SUCCESS ? "yes" : "no");
	if (status != QSPI_TIMING_CALIB_SUCCESS &&
	    status != QSPI_TIMING_CALIB_UNUSED) {
		LOG_ERR("========== calibration sample FAILED (status %d) ==========",
			status);
		return status < 0 ? status : -EIO;
	}

	rc = check_calib_value("read calibration value");
	if (rc != 0) {
		LOG_ERR("========== calibration sample FAILED (%d) ==========", rc);
		return rc;
	}

	LOG_INF("========== ALL TEST PASSED ==========");
	return 0;
}
