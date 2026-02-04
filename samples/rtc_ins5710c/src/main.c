/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Linkedsemi Corporation
 * 
 * @file main.c
 * @brief INS5710C RTC Sample Application
 * @author ZouWei <wzou@linkedsemi.com>
 * @date 2026-02-01
 * 
 * @note Function demonstration:
 *       1. Initialize RTC and verify device ID
 *       2. Set initial time
 *       3. Cyclically read time and display
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rtc_ins5710c_main, LOG_LEVEL_INF);
#define DT_DRV_COMPAT DT_NODELABEL(ins5710c)
static const struct device *const rtc_dev = DEVICE_DT_GET(DT_DRV_COMPAT);

/**
* @brief Print RTC time
*/
static void print_time(const struct rtc_time *timeptr, const char *prefix)
{
    LOG_INF("%s: %04d-%02d-%02d %02d:%02d:%02d  Week:%d",
            prefix,
            timeptr->tm_year + 1900,  /* tm_year starts from 1900 */
            timeptr->tm_mon + 1,       /* tm_mon ranges from 0-11 */
            timeptr->tm_mday,
            timeptr->tm_hour,
            timeptr->tm_min,
            timeptr->tm_sec,
            timeptr->tm_wday);
}

/**
 * @brief Set default time
 */
static void set_default_time(struct rtc_time *timeptr)
{
    /* Set to 2024-01-30 12:00:00 Tuesday */
    timeptr->tm_year = 2024 - 1900;  /* 124 = 2024 */
    timeptr->tm_mon = 0;              /* 0 = January */
    timeptr->tm_mday = 30;
    timeptr->tm_hour = 12;
    timeptr->tm_min = 0;
    timeptr->tm_sec = 0;
    timeptr->tm_wday = 2;             /* 2 = Tuesday */
    timeptr->tm_yday = 29;            /* Day of the year */
    timeptr->tm_isdst = -1;
}

int main(void)
{
    struct rtc_time time_set;
    struct rtc_time time_get;
    int ret;
    int count = 0;
    int temp_count = 0;
    
    LOG_INF("========================================");
    LOG_INF("    INS5710C RTC Sample Application");
    LOG_INF("    Build: " __DATE__ " " __TIME__);
    LOG_INF("========================================");
    
    /* ========== 1. Get RTC device ========== */
    LOG_INF("Step 1: Initializing RTC device...");
    if (!device_is_ready(rtc_dev)) {
        LOG_ERR("RTC device not ready!");
        return -ENODEV;
    }
    LOG_INF("RTC device ready: %s", rtc_dev->name);

    /* ========== 2. Set initial time ========== */
    LOG_INF("Step 2: Setting initial time...");
    set_default_time(&time_set);
    print_time(&time_set, "Setting");
    
    ret = rtc_set_time(rtc_dev, &time_set);
    if (ret < 0) {
        LOG_ERR("Failed to set time: %d", ret);
    } else {
        LOG_INF("Time set successfully");
    }
    
    /* Verify setting */
    ret = rtc_get_time(rtc_dev, &time_get);
    if (ret == 0) {
        print_time(&time_get, "Verified");
    }

    /* ========== 3. Main loop ========== */
    LOG_INF("Step 3: Starting main loop ...");
    LOG_INF("");
    
    while (1) {
        /* Read current time */
        ret = rtc_get_time(rtc_dev, &time_get);
        if (ret < 0) {
            LOG_ERR("Failed to get time: %d", ret);
            k_msleep(1000);
            continue;
        }
        
        /* Display time every second */
        if (count % 1 == 0) {
            print_time(&time_get, "Current");
        }
        
        /* Minute mark */
        if (time_get.tm_sec == 0) {
            LOG_INF("  >>> Minute mark <<<");
        }
        
        count++;
        temp_count++;
        k_msleep(1000);
    }
    
    return 0;
}