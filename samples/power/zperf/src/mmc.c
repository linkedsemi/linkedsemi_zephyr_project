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

void get_max_temp(void)
{
    int ret;
    int count = 0;

    //zassert_true(device_is_ready(sdhc_dev), "SDHC device is not ready");
    ret = sd_init(sdhc_dev, &card);

    ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
    //zassert_equal(ret, 0, "IOCTL sector count read failed");
    printk("SD card reports sector count of %d\n", sector_count);

    ret = mmc_ioctl(&card, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
    //zassert_equal(ret, 0, "IOCTL sector size read failed");
    printk("SD card reports sector size of %d\n", sector_size);

    while (true) {
        int block_addr = sector_count / 2;
        for (int i = 0; i < 10; i++) {
            ret = mmc_read_blocks(&card, buf, block_addr, SECTOR_COUNT);
            count++;
            if (count > 10000) {
                count = 0;
                printk("buf: %#x %#x %#x %#x\n", buf[0], buf[1], buf[2], buf[3]);
            }
            //zassert_equal(ret, 0, "Multiple reads from same addr failed");
        }
    }
}

static void monitor_temperature_func(void *dummy1, void *dummy2, void *dummy3)
{
    get_max_temp();
}
