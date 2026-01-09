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

#include "core_rv32.h"

static const struct device *i3c_dev_controller = DEVICE_DT_GET(DT_NODELABEL(i3c5));

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

int main(void)
{
	if (!device_is_ready(i3c_dev_controller)) {
		__ASSERT(0,"I2C device is not ready\n");
	}
    /*如果在I3C的设备节点中没有注册I2C设备，则I3C总线的时钟计算按照纯I3C总线计算，
        I2C相关的时钟是没有配置的，需要用户调用i3c_configure来配置I2C时钟*/
    struct i3c_config_controller i3c_cfg = 
    {
        .is_secondary = false,
        .scl = 
        {
            .i3c = 12500000,
            .i2c = 400000,
        },
        .supported_hdr = false,
    };
    i3c_configure(i3c_dev_controller, I3C_CONFIG_CONTROLLER, &i3c_cfg);

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

	i2c_transfer(i3c_dev_controller, msg, 2, I2C_DEVICE_ADDR);

	uint16_t reg_addr=0x04;
	i2c_write_read(i3c_dev_controller, I2C_DEVICE_ADDR,
			      &reg_addr, 2,
			      rdata_buf, 10);

	msg[0].buf = wdata_buf;
	msg[0].len = 10U;
	msg[0].flags = I2C_MSG_WRITE;

	msg[1].buf = wdata_buf;
	msg[1].len = 5;
	msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

	i2c_transfer(i3c_dev_controller, msg, 2, I2C_DEVICE_ADDR);

    return 0;
}
