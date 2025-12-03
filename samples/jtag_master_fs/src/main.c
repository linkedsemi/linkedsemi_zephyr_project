/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/jtag.h>
#include <zephyr/posix/sys/ioctl.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/posix/sys/ioctl.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/dirent.h>
#include <zephyr/shell/shell.h>
#include "ioctl.h"
#include "jtagm_fs.h"
#include <string.h>

static int jtag_shift_ir(int fd, uint32_t ir_value, int ir_len)
{
    struct jtag_xfer xfer = {0};

    xfer.type = JTAG_SIR_XFER;
    xfer.length = ir_len;
    xfer.direction = JTAG_WRITE_XFER;
    xfer.tdio = (uintptr_t)&ir_value;
    xfer.endstate = JTAG_STATE_IDLE;

    return ioctl(fd, JTAG_IOCXFER, &xfer);
}

static int jtag_shift_dr_read32(int fd, uint32_t *out)
{
    uint32_t dummy = 0;
    struct jtag_xfer xfer = {0};

    xfer.type = JTAG_SDR_XFER;
    xfer.length = 32;
    xfer.direction = JTAG_READ_XFER;
    xfer.tdio = (uintptr_t)&dummy;
    xfer.endstate = JTAG_STATE_IDLE;

    int ret = ioctl(fd, JTAG_IOCXFER, &xfer);
    if (ret < 0)
        return ret;

    *out = dummy;
    return 0;
}

static int jtag_set_tap_state(int fd, enum jtag_tapstate tap_state)
{
    struct jtag_tap_state state = {
        .endstate = tap_state,
        .from = JTAG_STATE_IDLE,
        .reset = JTAG_NO_RESET,
        .tck = 0
    };

    int ret = ioctl(fd, JTAG_SIOCSTATE, &state);
    if (ret < 0)
        return ret;

    return 0;
}

static enum jtag_tapstate jtag_get_tap_state(int fd)
{
    enum jtag_tapstate tap_state;

    int ret = ioctl(fd, JTAG_GIOCSTATUS, &tap_state);
    if (ret < 0)
        return ret;

    return tap_state;
}

static int jtag_set_freq(int fd, uint32_t freq)
{
    int ret = ioctl(fd, JTAG_SIOCFREQ, &freq);
    if (ret < 0)
        return ret;

    return 0;
}

static int jtag_get_freq(int fd)
{
    uint32_t freq;
    int ret = ioctl(fd, JTAG_GIOCFREQ, &freq);
    if (ret < 0)
        return ret;

    return freq;
}

static int gd32_read_id_code(const struct shell *shell, size_t argc, char **argv, void *data)
{
    static uint8_t gd32_id_code[4] = {0x3d,0x56,0x0,0x10};  //GD32 id-code：0x1000563d
    enum jtag_tapstate state;
    uint32_t id_code;
    int fd = open("/dev/jtag/jtag3", O_RDWR);

    if (fd > 0)
    {
        printk("cur freq: %d\n", jtag_get_freq(fd));
        printk("set freq: %d\n", 5000000);
        jtag_set_freq(fd, 5000000);
        printk("get freq: %d\n", jtag_get_freq(fd));

        jtag_set_tap_state(fd, JTAG_STATE_IDLE);
        state = jtag_get_tap_state(fd);

        jtag_shift_ir(fd, 1, 5);

        jtag_set_tap_state(fd, JTAG_STATE_SHIFTDR);
        jtag_shift_dr_read32(fd, &id_code);
        if (memcmp(gd32_id_code, &id_code, 4) == 0)
            printk("read id code ok.\n");

        close(fd);
    }

    return 0;
}
SHELL_CMD_REGISTER(gd32_read_id_code, NULL, "gd32_read_id_code", gd32_read_id_code);


static int i2c_fs_test(const struct shell *shell, size_t argc, char **argv, void *data)
{
    int fd = open("/dev/i2c1", O_RDWR);
    char read_buf[16];

    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);

        write(fd, "write i2c1", strlen("write i2c1"));
        int len = read(fd, read_buf, 16);
        printk("read len: %d, read buf: %s\n", len, read_buf);

        close(fd);
    }

    fd = open("/dev/i2c2", O_RDWR);
    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);

        write(fd, "write i2c2", strlen("write i2c2"));
        int len = read(fd, read_buf, 16);
        printk("read len: %d, read buf: %s\n", len, read_buf);

        close(fd);
    }

    fd = open("/dev/i2c/i2c3", O_RDWR);
    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);

        write(fd, "write i2c3", strlen("write i2c3"));
        int len = read(fd, read_buf, 16);
        printk("read len: %d, read buf: %s\n", len, read_buf);

        close(fd);
    }

    return 0;
}
SHELL_CMD_REGISTER(i2c_fs_test, NULL, "i2c_fs_test", i2c_fs_test);

static int uart_fs_test(const struct shell *shell, size_t argc, char **argv, void *data)
{
    int fd = open("/dev/uart1", O_RDWR);

    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);
        close(fd);
    }

    fd = open("/dev/uart2", O_RDWR);
    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);
        close(fd);
    }

    fd = open("/dev/uart/uart3", O_RDWR);
    if (fd > 0)
    {
        ioctl(fd, 1);
        ioctl(fd, 2);
        close(fd);
    }

    return 0;
}
SHELL_CMD_REGISTER(uart_fs_test, NULL, "uart_fs_test", uart_fs_test);

int main(void)
{
    return 0;
}
 