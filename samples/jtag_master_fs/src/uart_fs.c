/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>
#include <stdarg.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/fs/devfs.h>

static int uart_open(struct fs_file_t *zfp, const char *name, fs_mode_t flags)
{
    printk("open %s.\n", name);
    zfp->filep = "uart close";
    return 0;
}

static int uart_close(struct fs_file_t *zfp)
{
    printk("%s.\n", (char *)zfp->filep);
    return 0;
}

static int uart_ioctl(struct fs_file_t *zfp, unsigned long cmd, va_list args)
{
    switch (cmd)
    {
    case 1:
        printk("uart ioctl %ld.\n", cmd);
        break;
    case 2:
        printk("uart ioctl %ld.\n", cmd);
        break;
    }
    return 0;
}

static const struct fs_file_system_t uart_fs = {
    .open = uart_open,
    .close = uart_close,
    .ioctl = uart_ioctl
};

static int uart_fs_init(void)
{
    devfs_register("/dev/uart1", &uart_fs);
    devfs_register("/dev/uart2", &uart_fs);
    devfs_register("/dev/uart/uart3", &uart_fs);
    return 0;
}

SYS_INIT(uart_fs_init, APPLICATION, CONFIG_FILE_SYSTEM_INIT_PRIORITY);
