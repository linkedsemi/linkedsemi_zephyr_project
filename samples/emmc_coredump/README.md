# eMMC FATFS Multi-Partition & Coredump

## 概述

**Board**：LinkedSemi LSQSH_EVB

**背景**：芯片异常崩溃时需记录现场信息（Coredump），同时用户也需要使用 eMMC 存储数据。通过 MBR 分区将 eMMC 划分为：
- Partition 1：FAT32 文件系统，供用户数据存取
- Partition 2：Coredump 专用区，通过裸扇区访问

## 分区布局

```raw
+------------------+------------------------+------------------------+------------------------+
|     Sector 0     |     Sector 1-2047      |    Sector 2048+        |   Sector N+            |
|       MBR        |     Reserved (1MB)     |   Partition 1 (FAT32)  |   Partition 2 (Coredump)|
+------------------+------------------------+------------------------+------------------------+
```

| 分区 | 用途 | 访问方式 |
|------|------|----------|
| Partition 1 | FAT32 用户数据 | FATFS 挂载 |
| Partition 2 | Coredump 存储 | `disk_access` 裸扇区 |

## FatFs 多分区原理

### 核心数据结构

```c
// FF_VOLUME_STRS - volume ID 字符串表（Kconfig 配置）
// index:       0      1      2     3     4      5      6       7
#define FF_VOLUME_STRS  "RAM", "NAND", "CF", "SD", "SD2", "USB", "USB2", "USB3"

// VolToPart[] - pdrv 到物理驱动器的映射
// 格式: {pd, pt} - pd=驱动器编号, pt=分区号(0=整个磁盘, 1-4=指定分区)
PARTITION VolToPart[FF_VOLUMES] = {
    {0, 0}, {1, 0}, {2, 0}, {3, 0},
    {4, 1},  // pdrv 4 → 驱动器4, 分区1 (eMMC)
    {5, 0}, {6, 0}, {7, 0}
};
```

### 路径解析流程

`fs_open("/SD2:/test.txt")` 解析过程：
1. 匹配 `FF_VOLUME_STRS` 中 "SD2" → pdrv = 4
2. `VolToPart[4]` = `{4, 1}` → 驱动器4, 分区1

## 设备树配置

```dts
&mmc_disk1 {
    disk-name = "SD2";  /* zephyr实现中，该名字必须与fatfs接口的mount point名字一致， unix格式下可以自定义名字，其他情况必须为SD2 */
    emmc_parts: emmc-partitions {
        compatible = "linkedsemi,emmc-partitions";
        mbr-area-size = <0x100000>;        /* 1MB */
        user-partition-size = <...>;       /* FAT32 分区大小 (sectors) */
        coredump-partition-size = <...>;   /* Coredump 分区大小 (sectors) */
    };
};
```

## Kconfig 配置

```kconfig
# 必需
CONFIG_DISK_ACCESS=y
CONFIG_DISK_DRIVER_MMC=y
CONFIG_DEBUG_COREDUMP_BACKEND_EMMC=y

# FATFS
CONFIG_FS_FATFS_MULTI_PARTITION=y        #如果fatfs要使用emmc，则需要利用分区功能划分一块区域给coredump专用
CONFIG_FILE_SYSTEM=y
CONFIG_FAT_FILESYSTEM_ELM=y
CONFIG_FS_FATFS_LFN=y                    # 长文件名支持
CONFIG_FAT_FILESYSTEM_SHELL=y

# 可选：
# Unix 风格挂载点
CONFIG_ZEPHYR_FATFS_UNIX_MNTPOINT=y
CONFIG_ZEPHYR_FATFS_SD2_ALIAS_UNIX="SD2"  # 必须与 device tree disk-name 一致
```

## 代码使用

```c
#include "emmc_partition.h"

void main(void)
{
    /* 初始化 eMMC（检测 disk-name 和 VolToPart[4]） */
    if (init_coredump_emmc_backend() != 0) {
        LOG_ERR("init failed");
        return;
    }

    /* FATFS 挂载 */
    struct fs_mount_t mp = {
        .type = FS_FATFS,
        .mnt_point = EMMC_FATFS_MOUNT_POINT,  /* "/SD2" 或 "/SD2:" */
    };
    fs_mount(&mp);

    /* 文件操作 */
    fs_open(EMMC_FATFS_MOUNT_POINT "/test.txt", FS_O_CREATE | FS_O_WRITE, &fil);
    fs_write(&fil, buf, len);
}
```

## 注意事项

1. `TEST_HW_STACK_PROTECTION` 与 Coredump 冲突，不可同时开启， 启用coredump后，对应固件的ram区域不能存在硬件内存保护的功能。
2. `disk-name` 必须与 `CONFIG_ZEPHYR_FATFS_SD2_ALIAS_UNIX` 匹配
3. `VolToPart[4]` 必须是 `{4, 1}`，否则初始化失败
4. FATFS 只能访问 Partition 1，Partition 2 只能通过 `emmc_get_coredump_info()` 访问