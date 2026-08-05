/*
 * Copyright (c) 2025 Linkedsemi Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <ls_hal_adcv2.h>
#include <reg_adcv2_type.h>
#include <reg_sec_pmu_rg.h>
#include <reg_base_addr.h>
#include <field_manipulate.h>
#include <ls_hal_otp_ctrl.h>
#include <ls_msp_otp_ctrl.h>
#include <platform.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define ADC_TEMPER_CHANNEL 13

/*
 *  adc = -4.5807 * T + 2330.7
 * => T = (2330.7 - adc) / 4.5807
 */
static int16_t adc_raw_to_celsius(uint16_t adc_value)
{
	return (int16_t)((2330.7f - (float)adc_value) / 4.5807f);
}

static void ADC_Temper_Channel_setCfg(const struct device *adc)
{
	struct adc_channel_cfg channel_config = {
		.channel_id = ADC_TEMPER_CHANNEL,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_SAMPLETIME_15CYCLES,
	};

	adc_channel_setup(adc, &channel_config);
}

int main(void)
{
	printf("start adc temper test! %s\n", CONFIG_BOARD_TARGET);


	const struct device *const adc = DEVICE_DT_GET(DT_ALIAS(testadc));

	if (!device_is_ready(adc)) {
		__ASSERT(0, "adc device is not ready");
	}

	uint16_t *buffer;
	size_t buf_size = 40 * sizeof(uint16_t);

	buffer = k_malloc(buf_size);
	if (!buffer) {
		printk("buffer alloc failed\n");
		return -ENOMEM;
	}

	struct adc_sequence sequence = {
		.buffer = buffer,
		.buffer_size = buf_size,
		.options = NULL,
	};

	ADC_Temper_Channel_setCfg(adc);

	printk("ADC built-in temperature sensor (channel %d)\n", ADC_TEMPER_CHANNEL);

	for (uint8_t testcount = 0; testcount < 20; testcount++) {
		int ret;
		uint16_t adc_raw;
		int16_t temperature_c;

		memset(buffer, 0, buf_size);
		ret = adc_read(adc, &sequence);
		if (ret < 0) {
			printk("adc_read failed: %d\n", ret);
			continue;
		}

		adc_raw = buffer[0];
		temperature_c = adc_raw_to_celsius(adc_raw);

		printk("testcount = %d, adc_raw = %u, temperature = %d C\n",
		       testcount, adc_raw, temperature_c);

		k_msleep(500);
	}

	k_free(buffer);

	return 0;
}
