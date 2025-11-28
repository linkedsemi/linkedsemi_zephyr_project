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

static int i2c_open(struct fs_file_t *zfp, const char *name, fs_mode_t flags)
{
    printk("open %s.\n", name);
    zfp->filep = (void *)name;
    return 0;
}

static int i2c_close(struct fs_file_t *zfp)
{
    printk("%s close.\n", (char *)zfp->filep);
    return 0;
}

static int i2c_ioctl(struct fs_file_t *zfp, unsigned long cmd, va_list args)
{
    switch (cmd)
    {
    case 1:
        printk("i2c ioctl %ld.\n", cmd);
        break;
    case 2:
        printk("i2c ioctl %ld.\n", cmd);
        break;
    }
    return 0;
}

static int i2c_read(struct fs_file_t *filp, void *dest, size_t nbytes)
{
    int len = snprintk(dest, nbytes, "hello %s", (const char *)filp->filep);
    return len;
}

static ssize_t i2c_write(struct fs_file_t *zfp, const void *src, size_t nbytes)
{
    printk("%s, %d\n", (const char *)src, nbytes);
    return nbytes;
}

static const struct fs_file_system_t i2c_fs = {
    .open = i2c_open,
    .read = i2c_read,
    .write = i2c_write,
    .close = i2c_close,
    .ioctl = i2c_ioctl
};

static int i2c_fs_init(void)
{
    devfs_register("/dev/i2c1", &i2c_fs);
    devfs_register("/dev/i2c2", &i2c_fs);
    devfs_register("/dev/i2c/i2c3", &i2c_fs);
    return 0;
}
SYS_INIT(i2c_fs_init, APPLICATION, CONFIG_FILE_SYSTEM_INIT_PRIORITY);
