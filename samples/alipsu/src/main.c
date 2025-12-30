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
  uint16_t voltage, current, temp, power;
  uint8_t retry = 0;
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

    /* Get voltage, power and temperature data */
    ret = ali_psu_word_show(dev, ALI_PS_REG_READ_VIN, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get voltage: %d", ret);
      continue;
    }

    ret = ali_psu_word_show(dev, ALI_PS_REG_READ_IIN, &current);
    if (ret < 0) {
      LOG_ERR("Failed to get current: %d", ret);
      continue;
    }

    ret = ali_psu_word_show(dev, ALI_PS_REG_READ_PIN, &power);
    if (ret < 0) {
      LOG_ERR("Failed to get power: %d", ret);
      continue;
    }

    ret = ali_psu_word_show(dev, ALI_PS_REG_READ_TEMP1, &temp);
    if (ret < 0) {
      LOG_ERR("Failed to get temperature: %d", ret);
      continue;
    }

    // power = (voltage.val1 + voltage.val2 / 1000000.0f) *
    //         (current.val1 + current.val2 / 1000000.0f);

    LOG_INF("Voltage mV: %d", voltage);
    LOG_INF("Current mA: %d", current);
    LOG_INF("Power vW: %d ", power);
    LOG_INF("Temperature m°C: %d", temp);

    k_sleep(K_SECONDS(1));
    retry++;
  }
}