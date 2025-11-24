#include <zephyr/device.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/sd/mmc.h>
#include <zephyr/kernel.h>

#define SECTOR_COUNT 32
#define SECTOR_SIZE  512 /* subsystem should set all cards to 512 byte blocks */
#define BUF_SIZE     (SECTOR_SIZE * SECTOR_COUNT)
static const struct device *const sdhc_dev = DEVICE_DT_GET(DT_ALIAS(sdhc0));
static struct sd_card card;
static uint8_t buf[BUF_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
// static uint8_t check_buf[BUF_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
static uint32_t sector_size;
static uint32_t sector_count;

#define MMC_UNALIGN_OFFSET 1

void test_emmc_func(void)
{
    int ret;
    // int count = 0;

    __ASSERT(device_is_ready(sdhc_dev), "SDHC device is not ready");
    card.bus_width = 8;
    ret = sd_init(sdhc_dev, &card);

    ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
    __ASSERT(0 == ret, "IOCTL sector count read failed");
    LOG_INF("SD card reports sector count of %d\n", sector_count);

    ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
    __ASSERT(0 == ret, "IOCTL sector size read failed");
    LOG_INF("SD card reports sector size of %d\n", sector_size);

    while (true) {
        int block_addr = sector_count / 2;
        for (int i = 0; i < 10; i++) {
            ret = mmc_read_blocks(&card, buf, block_addr, SECTOR_COUNT);
            // count++;
            // if (count > 1000) {
            //     count = 0;
            LOG_INF("%s: buf: %#x %#x %#x %#x\n", __func__, buf[0], buf[1], buf[2], buf[3]);
            // }
            __ASSERT(0 == ret, "Multiple reads from same addr failed");
            k_msleep(100);
        }
    }
}

static void test_emmc_entry(void *dummy1, void *dummy2, void *dummy3)
{
	LOG_INF("%s", __func__);
    test_emmc_func();
}
