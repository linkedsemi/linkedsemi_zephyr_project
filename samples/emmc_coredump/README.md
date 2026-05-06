# eMMC FATFS Multi-Partition & Coredump

## 概述

**Board**：LinkedSemi LSQSH_EVB

**背景**：芯片运行时可能发生异常崩溃，Coredump 功能用于记录崩溃时的寄存器、栈等现场信息。由于异常处理代码运行在受限上下文中，必须绕过复杂的文件系统层直接写入存储。同时，用户也需要使用 eMMC 存储普通数据。因此需要通过 MBR 分区将存储划分为：
- Partition 1：FAT32 文件系统，供用户日常数据存取
- Partition 2：Coredump 专用区，仅通过裸扇区访问，与 FATFS 完全隔离

**实现方案**：
- 使用 MBR 分区表将 eMMC 分为两个分区
- 分区信息静态定义在设备树中
- Partition 1 通过 FATFS 访问，Partition 2 通过裸扇区访问

## 分区布局

```raw
+------------------+------------------------+------------------------+------------------------+
|     Sector 0     |     Sector 1-2047      |    Sector 2048+        |   Sector N+            |
|       MBR        |     Reserved (1MB)     |   Partition 1 (FAT32)  |   Partition 2 (Coredump)|
+------------------+------------------------+------------------------+------------------------+
```

| 分区 | 用途 | 访问方式 | 大小 |
|------|------|----------|------|
| Partition 1 | FAT32 用户数据 | `fs_mount` 挂载到 `/4:` | ~7.27 GB |
| Partition 2 | Coredump 存储 | `disk_access` 裸扇区访问 | 10 MB |

## 设备树配置

在 board overlay 文件中配置分区信息：

```dts
&mmc_disk1 {
    emmc_parts: emmc-partitions {
        compatible = "linkedsemi,emmc-partitions";
        mbr-area-size = <0x100000>;                    /* 1MB: MBR + Reserved */
        user-partition-size = <...>;                   /* FAT32 分区大小 (sectors) */
        coredump-partition-size = <0xA00000>;         /* 10MB coredump 区 (sectors) */
    };
};
```

**分区大小计算**：
- `user-partition-size` = (总容量 - 1MB - 10MB) / 512
- 总容量从 `DISK_IOCTL_GET_SECTOR_COUNT` 获取或根据实际 eMMC 型号确定

## Kconfig 必要配置

```kconfig
# ====================
# eMMC 磁盘驱动 (必需)
# ====================
CONFIG_DISK_ACCESS=y
CONFIG_DISK_DRIVER_MMC=y

# ====================
# eMMC Coredump 后端 (必需)
# ====================
CONFIG_DEBUG_COREDUMP_BACKEND_EMMC=y
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_THREADS=y
CONFIG_DEBUG_COREDUMP_MEMORY_DUMP_LINKER_RAM=y

# ====================
# FATFS 多分区 (可选，需要 FAT32 用户区时启用)
# ====================
CONFIG_FS_FATFS_MULTI_PARTITION=y
CONFIG_FILE_SYSTEM=y
CONFIG_FAT_FILESYSTEM_ELM=y
CONFIG_FILE_SYSTEM_SHELL=y    # FS Shell (fs ls, fs mount 等)

# ====================
# Shell 支持 (可选，用于调试)
# ====================
CONFIG_SHELL=y
CONFIG_DEBUG_COREDUMP_SHELL=y
```

## 代码移植

eMMC 分区功能已作为 SDK 库实现：
- 公共接口：`zephyr/soc/linkedsemi/lsqsh/coredump/emmc_partition.h`
- 实现代码：`zephyr/soc/linkedsemi/lsqsh/coredump/emmc_partition.c`

使用时只需 `#include "emmc_partition.h"` 即可。

### 1. 设备树配置

确保 board overlay 中有 `mmc_disk1` 节点和 `emmc-partitions` 子节点，参考上文"设备树配置"。

### 2. 包含头文件并调用

```c
#include "emmc_partition.h"

int main(void)
{
    /* 初始化 eMMC 并创建分区表 */
    if (init_emmc_backend() != 0) {
        LOG_ERR("eMMC init failed");
        return -1;
    }

    /* 后续 FATFS mount、coredump 等操作 */
    ...
}
```

### 3. FATFS 挂载 (Partition 1)

分区后，FATFS 不再使用 `SD2` 裸设备名访问，而是通过挂载点访问：

```c
#include "emmc_partition.h"

static FATFS fs_part1;

struct fs_mount_t mp1 = {
    .type = FS_FATFS,
    .fs_data = &fs_part1,
    .mnt_point = EMMC_FATFS_MOUNT_POINT,  /* "/4:" */
};

/* 挂载 */
ret = fs_mount(&mp1);
if (ret < 0) {
    LOG_ERR("fs_mount failed: %d", ret);
    return ret;
}

/* 文件操作 */
ret = fs_open(&fil, EMMC_FATFS_MOUNT_POINT "/test.txt", FS_O_CREATE | FS_O_WRITE);
```

### 4. 依赖关系

```raw
CONFIG_DEBUG_COREDUMP_BACKEND_EMMC
├── VolToPart[]           (FATFS 多分区映射)
├── emmc_get_coredump_info()   (获取 coredump 分区位置)
├── emmc_create_partition()    (创建 MBR 分区表)
└── init_emmc_backend()       (初始化入口)
    └── emmc_create_partition() (依赖 CONFIG_FS_FATFS_MULTI_PARTITION)
```

## Shell 命令

### FS Shell (FATFS 操作)
```raw
fs mount fat /4:          # 挂载 Partition 1 (如已自动挂载可跳过)
fs ls /4:                 # 列出文件
fs read /4:/TEST.TXT      # 读取文件
```

### Coredump Shell
```raw
coredump_emmc find    # 查询是否有存储的 coredump
coredump_emmc print   # 打印 coredump 数据
coredump_emmc verify  # 验证 coredump 完整性
coredump_emmc erase   # 清除存储的 coredump
coredump_emmc error   # 查看错误信息
```

## Coredump 数据解析

```raw
#CD:BEGIN#
#CD:<hex data>
...
#CD:END#
```
```bash
# 1. 日志转 bin 文件
python zephyr/scripts/coredump/coredump_serial_log_parser.py coredump.log coredump.bin

# 2. 启动 GDB 服务器
python zephyr/scripts/coredump/coredump_gdbserver.py build/zephyr/zephyr.elf coredump.bin -v

# 3. 连接 GDB 查看调用栈
riscv64-unknown-elf-gdb build/zephyr/zephyr.elf
(gdb) target remote localhost:1234
(gdb) bt
```

## 注意事项

1. `TEST_HW_STACK_PROTECTION` 与 Coredump 冲突，不可同时开启
2. 开启 Coredump 后，线程需预留足够栈空间（异常处理在当前线程栈执行）
3. FATFS 操作无法访问 Partition 2（coredump 区），只能通过 coredump 接口访问
4. 分区后 FATFS 必须使用挂载点 `/4:` 访问，不可直接使用 `SD2` 设备名