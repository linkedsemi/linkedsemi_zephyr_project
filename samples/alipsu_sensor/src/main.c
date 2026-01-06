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
#include "psu.h"
// #include <zephyr/drivers/sensor/alipsu/psu.h>

LOG_MODULE_REGISTER(alipsu_sensor_app, LOG_LEVEL_INF);

// 定义驱动兼容性字符串
#define DT_DRV_COMPAT ali_psu

// 定义设备标签名称
#define ALIPSU_DEV_NODE1 DT_NODELABEL(alipsu1)
#define ALIPSU_DEV_NODE2 DT_NODELABEL(alipsu2)

void main(void) {
  const struct device *dev1 = DEVICE_DT_GET(ALIPSU_DEV_NODE1);
  const struct device *dev2 = DEVICE_DT_GET(ALIPSU_DEV_NODE2);
  struct sensor_value voltage, current, temp, power, fan;
  uint8_t retry = 0;
  int ret;

  if (NULL == dev1) {
    LOG_ERR("Failed to get ALIPSU1 device binding");
    return;
  }

  if (!device_is_ready(dev1)) {
    LOG_ERR("Device %s is not ready", dev1->name);
    return;
  }

  if (NULL == dev2) {
    LOG_ERR("Failed to get ALIPSU2 device binding");
    return;
  }

  if (!device_is_ready(dev2)) {
    LOG_ERR("Device %s is not ready", dev2->name);
    return;
  }

  LOG_INF("ALIPSU1 sensor measurement example started");

  uint8_t old_page = 0;
  ret = ali_psu_mfr_page_show(dev1, &old_page);
  if (ret < 0) {
    LOG_ERR("Failed to read current page: %d", old_page);
  }

  /* example: switch to page 1 */
  ret = ali_psu_mfr_page_store(dev1, old_page+1);
  if (ret < 0) {
    LOG_ERR("Failed to store new page to alipsu1: %d", old_page+1);
  }

  ret = ali_psu_mfr_page_show(dev1, &old_page);
  if (ret < 0) {
    LOG_ERR("Read current updated page: %d", old_page);
  }

  while (retry < 1) {
    /* Fetch all sensor data - alipsu specific sensor driver */
    ret = sensor_sample_fetch(dev1); /* general sensor api */
    if (ret < 0) {
      LOG_ERR("Failed to fetch samples: %d", ret);
      continue;
    }

    /* Get voltage, power and temperature data */
    voltage.val2 = 0; /* vin */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get input voltage: %d", ret);
    }
    LOG_INF("Input Voltage mV: %d", voltage.val1);

    voltage.val2 = 1; /* vin1 */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get input voltage1: %d", ret);
    }
    LOG_INF("Input Voltage1 mV: %d", voltage.val1);

    voltage.val2 = 2; /* vout */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get output voltage: %d", ret);
    }
    LOG_INF("Output Voltage mV: %d", voltage.val1);

    current.val2 = 0; /* iin */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_CURRENT, &current);
    if (ret < 0) {
      LOG_ERR("Failed to get input current: %d", ret);
    }
    LOG_INF("Input Current mV: %d", current.val1);

    current.val2 = 1; /* iout */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_CURRENT, &current);
    if (ret < 0) {
      LOG_ERR("Failed to get output current: %d", ret);
    }
    LOG_INF("Output Current mV: %d", current.val1);

    ret = sensor_channel_get(dev1, SENSOR_CHAN_POWER, &power);
    if (ret < 0) {
      LOG_ERR("Failed to get input power: %d", ret);
    }
    LOG_INF("Input Power uW: %d", power.val1);

    ret = sensor_channel_get(dev1, SENSOR_CHAN_GAUGE_TEMP, &temp);
    if (ret < 0) {
      LOG_ERR("Failed to get temperature: %d", ret);
    }
    LOG_INF("Temperature m°C: %d", temp.val1);

    fan.val2 = 0; /* fan1 */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_RPM, &fan);
    if (ret < 0) {
      LOG_ERR("Failed to get fan1: %d", ret);
    }
    LOG_INF("Fan1 RPM mHz: %d", fan.val1);

    fan.val2 = 1; /* fan2 */
    ret = sensor_channel_get(dev1, SENSOR_CHAN_RPM, &fan);
    if (ret < 0) {
      LOG_ERR("Failed to get fan2: %d", ret);
    }
    LOG_INF("Fan2 RPM mHz: %d", fan.val1);

    k_sleep(K_SECONDS(1));
    retry++;
  }

  LOG_INF("ALIPSU2 sensor measurement example started");

  ret = ali_psu_mfr_page_show(dev2, &old_page);
  if (ret < 0) {
    LOG_ERR("Failed to read current page: %d", old_page);
  }

  /* example: switch to page 1 */
  ret = ali_psu_mfr_page_store(dev2, old_page+1);
  if (ret < 0) {
    LOG_ERR("Failed to store new page to alipsu2: %d", old_page+1);
  }

  ret = ali_psu_mfr_page_show(dev2, &old_page);
  if (ret < 0) {
    LOG_ERR("Read current updated page: %d", old_page);
  }

  while (retry < 1) {
    /* Fetch all sensor data - alipsu specific sensor driver */
    ret = sensor_sample_fetch(dev2); /* general sensor api */
    if (ret < 0) {
      LOG_ERR("Failed to fetch samples: %d", ret);
      continue;
    }

    /* Get voltage, power and temperature data */
    voltage.val2 = 0; /* vin */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get input voltage: %d", ret);
    }
    LOG_INF("Input Voltage mV: %d", voltage.val1);

    voltage.val2 = 1; /* vin1 */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get input voltage1: %d", ret);
    }
    LOG_INF("Input Voltage1 mV: %d", voltage.val1);

    voltage.val2 = 2; /* vout */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_VOLTAGE, &voltage);
    if (ret < 0) {
      LOG_ERR("Failed to get output voltage: %d", ret);
    }
    LOG_INF("Output Voltage mV: %d", voltage.val1);

    current.val2 = 0; /* iin */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_CURRENT, &current);
    if (ret < 0) {
      LOG_ERR("Failed to get input current: %d", ret);
    }
    LOG_INF("Input Current mV: %d", current.val1);

    current.val2 = 1; /* iout */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_CURRENT, &current);
    if (ret < 0) {
      LOG_ERR("Failed to get output current: %d", ret);
    }
    LOG_INF("Output Current mV: %d", current.val1);

    ret = sensor_channel_get(dev2, SENSOR_CHAN_POWER, &power);
    if (ret < 0) {
      LOG_ERR("Failed to get input power: %d", ret);
    }
    LOG_INF("Input Power uW: %d", power.val1);

    ret = sensor_channel_get(dev2, SENSOR_CHAN_GAUGE_TEMP, &temp);
    if (ret < 0) {
      LOG_ERR("Failed to get temperature: %d", ret);
    }
    LOG_INF("Temperature m°C: %d", temp.val1);

    fan.val2 = 0; /* fan1 */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_RPM, &fan);
    if (ret < 0) {
      LOG_ERR("Failed to get fan1: %d", ret);
    }
    LOG_INF("Fan1 RPM mHz: %d", fan.val1);

    fan.val2 = 1; /* fan2 */
    ret = sensor_channel_get(dev2, SENSOR_CHAN_RPM, &fan);
    if (ret < 0) {
      LOG_ERR("Failed to get fan2: %d", ret);
    }
    LOG_INF("Fan2 RPM mHz: %d", fan.val1);

    k_sleep(K_SECONDS(1));
    retry++;
  }
}