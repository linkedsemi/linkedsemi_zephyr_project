/*
 * Copyright (c) 2016 Intel Corporation.
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_msc.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <stdio.h>

LOG_MODULE_REGISTER(main);

static struct fs_mount_t usb2_fs_mnt;
static struct fs_mount_t usb1_fs_mnt;
static FATFS fat_fs_usb2;
static FATFS fat_fs_usb1;

USBD_DEFINE_MSC_LUN(usb2, "USB2", "Zephyr", "RAMDisk", "0.00");
USBD_DEFINE_MSC_LUN(usb1, "USB", "Zephyr", "RAMDisk", "0.00");

extern struct usbd_context *usbd_init_usb2(usbd_msg_cb_t msg_cb);
extern struct usbd_context *usbd_init_usb1(usbd_msg_cb_t msg_cb);

static int enable_usb_device_next(void)
{
	int err;
 	struct usbd_context *usb_ctx = usbd_init_usb2(NULL);
	if (usb_ctx == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	err = usbd_enable(usb_ctx);
	if (err) {
		LOG_ERR("Failed to enable device support");
		return err;
	}

	usb_ctx = usbd_init_usb1(NULL);
	if (usb_ctx == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	err = usbd_enable(usb_ctx);
	if (err) {
		LOG_ERR("Failed to enable device support");
		return err;
	}

	LOG_DBG("USB device support enabled");
	return 0;
}

static void setup_disk(struct fs_mount_t *fs_mnt, FATFS *fat_fs, const char *mnt_point)
{
	struct fs_mount_t *mp = fs_mnt;
	struct fs_dir_t dir;
	struct fs_statvfs sbuf;
	int rc;

	fs_dir_t_init(&dir);

	if (!IS_ENABLED(CONFIG_FILE_SYSTEM_LITTLEFS) &&
	    !IS_ENABLED(CONFIG_FAT_FILESYSTEM_ELM)) {
		LOG_INF("No file system selected");
		return;
	}

	mp->type = FS_FATFS;
	mp->fs_data = fat_fs;
	mp->mnt_point = mnt_point;

	rc = fs_mount(mp);
	if (rc < 0) {
		LOG_ERR("Failed to mount filesystem");
		return;
	}

	/* Allow log messages to flush to avoid interleaved output */
	k_sleep(K_MSEC(50));

	printk("Mount %s: %d\n", mp->mnt_point, rc);

	rc = fs_statvfs(mp->mnt_point, &sbuf);
	if (rc < 0) {
		printk("FAIL: statvfs: %d\n", rc);
		return;
	}

	printk("%s: bsize = %lu ; frsize = %lu ;"
	       " blocks = %lu ; bfree = %lu\n",
	       mp->mnt_point,
	       sbuf.f_bsize, sbuf.f_frsize,
	       sbuf.f_blocks, sbuf.f_bfree);

	rc = fs_opendir(&dir, mp->mnt_point);
	printk("%s opendir: %d\n", mp->mnt_point, rc);

	if (rc < 0) {
		LOG_ERR("Failed to open directory");
	}

	while (rc >= 0) {
		struct fs_dirent ent = { 0 };

		rc = fs_readdir(&dir, &ent);
		if (rc < 0) {
			LOG_ERR("Failed to read directory entries");
			break;
		}
		if (ent.name[0] == 0) {
			printk("End of files\n");
			break;
		}
		printk("  %c %u %s\n",
		       (ent.type == FS_DIR_ENTRY_FILE) ? 'F' : 'D',
		       ent.size,
		       ent.name);
	}

	(void)fs_closedir(&dir);

	return;
}

int main(void)
{
	int ret;

	setup_disk(&usb2_fs_mnt, &fat_fs_usb2, "/usb2:");
	setup_disk(&usb1_fs_mnt, &fat_fs_usb1, "/usb:");

	ret = enable_usb_device_next();

	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return 0;
	}

	LOG_INF("The device is put in USB mass storage mode.\n");
	return 0;
}
