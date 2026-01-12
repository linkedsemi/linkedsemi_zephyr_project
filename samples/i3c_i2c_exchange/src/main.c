/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <stdlib.h>
#include <stdio.h>
#include <zephyr/drivers/i3c/ccc.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/drivers/i2c.h>
#include "core_rv32.h"
#include <zephyr/drivers/pinctrl.h>

static const struct device *i3c5 = DEVICE_DT_GET(DT_NODELABEL(i3c5));
static const struct device *i2c5 = DEVICE_DT_GET(DT_NODELABEL(i2c5));

/* pin configuration for test device */
PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(i3c5));
// static const struct pinctrl_dev_config *i2c5_pcfg = DT_PINCTRL_IDX_TO_NAME_TOKEN(DT_NODELABEL(i3c5), 1);
static const struct pinctrl_dev_config *i3c5_pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(i3c5));

#define I2C_DEVICE_ADDR 0x50
#define BUF_SIZE 256
uint8_t wdata_buf[BUF_SIZE];
uint8_t rdata_buf[BUF_SIZE];
static void write_read_compare(const struct device *const i2c,uint16_t dev_addr,const uint8_t *wdata,uint8_t *rdata,uint16_t len)
{
	i2c_burst_write(i2c,dev_addr,0,wdata,len);

	i2c_burst_read(i2c,dev_addr,0,rdata,len);

	if(memcmp(wdata,rdata,len))
	{
		printf("len: %d\n", len);
		printf("wdata: ");
		for(uint32_t i = 0; i < len; i++)
		{
			printf("%x ", wdata[i]);
		}
		printf("\n\n");
		printf("len: %d\n", len);
		printf("rdata: ");
		for(uint32_t i = 0; i < len; i++)
		{
			printf("%x ", rdata[i]);
		}
		printf("\n\n");
		__ASSERT(0,"wdata rdata not match\n");
    }
}

#define PINCTRL_STATE_PINMUX_I2C PINCTRL_STATE_PRIV_START

int main(void)
{
	const struct pinctrl_state *i3c_pcfg_state = &i3c5_pcfg->states[0];
	const struct pinctrl_state *i2c_pcfg_state = &i3c5_pcfg->states[1];
	if(i3c_pcfg_state->id != PINCTRL_STATE_DEFAULT)
	{
		__ASSERT(0,"i3c_default_pcfg id must be default\n");
	}
	if(i2c_pcfg_state->id != PINCTRL_STATE_PINMUX_I2C)
	{
		__ASSERT(0,"i3c_to_i2c_pcfg id must be I3C_PINCTRL_STATE_I2C_PINMUX\n");
	}


	if (!device_is_ready(i3c5)) {
		__ASSERT(0,"I3C device is not ready\n");
	}

	if (!device_is_ready(i2c5)) {
		__ASSERT(0,"I2C device is not ready\n");
	}
	/*切换成默认的I3C pinmux*/
	pinctrl_apply_state(i3c5_pcfg, PINCTRL_STATE_DEFAULT);


	i3c_ccc_do_rstdaa_all(i3c5);

	/*切换成I2C pinmux*/
	pinctrl_apply_state(i3c5_pcfg, PINCTRL_STATE_PINMUX_I2C);
    for(uint16_t i = 0; i < BUF_SIZE; i++)
    {
        wdata_buf[i] = i;
    }
	struct i2c_msg msg[2];
	msg[0].buf = rdata_buf;
	msg[0].len = 1;
	msg[0].flags = I2C_MSG_READ;

	msg[1].buf = rdata_buf;
	msg[1].len = 10;
	msg[1].flags = I2C_MSG_READ | I2C_MSG_STOP;

	i2c_transfer(i2c5, msg, 2, I2C_DEVICE_ADDR);

	uint16_t reg_addr=0x04;
	i2c_write_read(i2c5, I2C_DEVICE_ADDR,
			      &reg_addr, 2,
			      rdata_buf, 10);

	msg[0].buf = wdata_buf;
	msg[0].len = 10U;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = wdata_buf;
	msg[1].len = 5;
	msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	i2c_transfer(i2c5, msg, 2, I2C_DEVICE_ADDR);

    /*切换成默认的I3C pinmux*/
	pinctrl_apply_state(i3c5_pcfg, PINCTRL_STATE_DEFAULT);

	i3c_ccc_do_rstdaa_all(i3c5);
    return 0;
}
