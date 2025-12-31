/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
// #include <zephyr/drivers/sensor/alipsu/psu.h>
#include "psu.h"

LOG_MODULE_REGISTER(alipsu_app, LOG_LEVEL_INF);

// 定义驱动兼容性字符串
#define DT_DRV_COMPAT ali_psu

// 定义设备标签名称
#define ALIPSU_DEV_NODE DT_NODELABEL(alipsu)

void main(void) {
  const struct device *dev = DEVICE_DT_GET(ALIPSU_DEV_NODE);
  uint16_t word;
  uint8_t retry = 0, len = 0; /* len comes from ali_attr_len[] */
  uint8_t buf[32] = {0};
  int64_t val;
  // float power;

  int ret;
  if (NULL == dev) {
    LOG_ERR("Failed to get ALIPSU device binding");
    return;
  }

  if (!device_is_ready(dev)) {
    LOG_ERR("Device %s is not ready", dev->name);
    return;
  }

  LOG_INF("ALIPSU sensor measurement example started");

  while (retry < 1) {
    
    uint8_t old_page = 0;
    ret = ali_psu_mfr_page_show(dev, &old_page);
    if (ret < 0) {
      LOG_ERR("Failed to read current page: %d", old_page);
      continue;
    }

    ret = ali_psu_mfr_page_store(dev, old_page+1);
    if (ret < 0) {
      LOG_ERR("Failed to store new page: %d", old_page+1);
      continue;
    }

    ret = ali_psu_mfr_page_show(dev, &old_page);
    if (ret < 0) {
      LOG_ERR("Read current updated page: %d", old_page);
      continue;
    }

    ret = ali_psu_fw_version_show(dev, ALI_PS_REG_FW_REV, buf, &len);
    if (ret < 0) {
      LOG_ERR("Failed to show firmware version: %d", ret);
      continue;
    }

    ret = ali_psu_ac_cycle_store(dev);
    if (ret < 0) {
      LOG_ERR("Failed to store AC cycle: %d", ret);
      continue;
    }
    
    ret = ali_psu_bootloader_str_show(dev, ALI_PS_REG_BOOTLOADER_KEY, buf, &len);
    if (ret < 0) {
      LOG_ERR("Failed to show bootloader key: %d", ret);
      continue;
    }

    len = 3;
    ret = ali_psu_bootloader_str_store(dev, ALI_PS_REG_BOOTLOADER_KEY, buf, len);
    if (ret < 0) {
      LOG_ERR("Failed to store bootloader key: %d", ret);
      continue;
    }

    len = 1;
    buf[0] = 0x01;
    ret = ali_psu_bootloader_hex_store(dev, ALI_PS_REG_BOOTLOADER_STATUS, buf, len);
    if (ret < 0) {
      LOG_ERR("Failed to store bootloader status: %d", ret);
      continue;
    }

    ret = ali_psu_bootloader_hex_show(dev, ALI_PS_REG_BOOTLOADER_STATUS, buf, &len);
    if (ret < 0) {
      LOG_ERR("Failed to show bootloader status: %d", ret);
      continue;
    }

    ret = ali_psu_block_show(dev, ALI_PS_REG_MFR_ID, buf, &len);
    if (ret < 0) {
      LOG_ERR("Failed to show MFR ID block: %d", ret);
      continue;
    }

    ret = ali_psu_block_hex_his_show(dev, ALI_PS_REG_MFR_POS_TOTAL, buf, &len);
    if (ret < 0) {
      LOG_ERR("Failed to show block hex MFR_POS_TOTAL history: %d", ret);
      continue;
    }

    ret = ali_psu_sensor_show(dev, ALI_PS_REG_CURR_SHARE_IOUT_READ_FILTER, &val);
    if (ret < 0) {
      LOG_ERR("Failed to show current sensor IOUT_READ_FILTER: %d", ret);
      continue;
    }

    /* Get voltage, power and temperature data */
    ret = ali_psu_sensor_his_show(dev, ALI_PS_REG_READ_VIN, &val);
    if (ret < 0) {
      LOG_ERR("Failed to get voltage: %d", ret);
      continue;
    }

    ret = ali_psu_sensor_his_show(dev, ALI_PS_REG_READ_IIN, &val);
    if (ret < 0) {
      LOG_ERR("Failed to get current: %d", ret);
      continue;
    }

    ret = ali_psu_sensor_his_show(dev, ALI_PS_REG_READ_PIN, &val);
    if (ret < 0) {
      LOG_ERR("Failed to get power: %d", ret);
      continue;
    }

    ret = ali_psu_sensor_his_show(dev, ALI_PS_REG_READ_TEMP1, &val);
    if (ret < 0) {
      LOG_ERR("Failed to get temperature: %d", ret);
      continue;
    }

    LOG_INF("Voltage mV: %lld", val);
    LOG_INF("Current mA: %lld", val);
    LOG_INF("Power vW: %lld ", val);
    LOG_INF("Temperature m°C: %lld", val);

    ret = ali_psu_byte_his_show(dev, ALI_PS_REG_STATUS_INPUT, buf);
    if (ret < 0) {
      LOG_ERR("Failed to show byte history STATUS_INPUT: %d", ret);
      continue;
    }

    ret = ali_psu_word_his_show(dev, ALI_PS_REG_STATUS_WORD, buf);
    if (ret < 0) {
      LOG_ERR("Failed to show word history STATUS_WORD: %d", ret);
      continue;
    }

    ret = ali_psu_word_show(dev, ALI_PS_REG_AUTO_TURN_ON_DELAY_WORD, &word);
    if (ret < 0) {
      LOG_ERR("Failed to show word AUTO_TURN_ON_DELAY_WORD: %d", ret);
      continue;
    }

    ret = ali_psu_word_store(dev, ALI_PS_REG_AUTO_TURN_ON_DELAY_WORD, word);
    if (ret < 0) {
      LOG_ERR("Failed to store word AUTO_TURN_ON_DELAY_WORD: %d", ret);
      continue;
    }

    ret = ali_psu_byte_show(dev, ALI_PS_REG_STATUS_OTHER, &buf[0]);
    if (ret < 0) {
      LOG_ERR("Failed to show byte STATUS_OTHER: %d", ret);
      continue;
    }

    ret = ali_psu_byte_store(dev, ALI_PS_REG_STATUS_OTHER, buf[0]);
    if (ret < 0) {
      LOG_ERR("Failed to store byte STATUS_OTHER: %d", ret);
      continue;
    }

    k_sleep(K_SECONDS(1));
    retry++;
  }
}