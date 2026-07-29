# mbedtls_hash 测试说明

本 sample 用于在 LinkedSemi QSH 系列芯片上，对 mbedtls 硬件加速哈希算法做全面正确性验证，并给出不同输入长度 / 地址对齐 / update 切分方式下的吞吐参考。

## 覆盖算法

| 算法 | 硬件加速单元 | 备注 |
|---|---|---|
| SHA-224 | LSSHA | 与 SHA-256 共用硬件 |
| SHA-256 | LSSHA | 支持 DMA 路径 |
| SM3     | LSSHA | 与 SHA-256 共用硬件 |
| SHA-384 | LS_SHA512 | 128 B block |
| SHA-512 | LS_SHA512 | 128 B block |

## 测试内容

### 1. 长度覆盖

- `0 .. 128` 字节穷举（129 个长度）。
- 27 个边界长度：128/256/512/1024/2048/4096/8192/16384 附近的 ±1、±2 等关键点。
- 合计 **156 个长度**。

### 2. 基地址偏移

测试数据从 16 个不同偏移开始，覆盖 cache-line / word 对齐与非对齐：

```
0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63
```

### 3. Update 模式

每种算法、每个长度、每个偏移都会跑以下模式，结果与“单次 update 参考值”比对：

| 模式 | 说明 |
|---|---|
| `RANDOM64` | 随机 chunk，范围 1 .. 64 B |
| `RANDOM512` | 随机 chunk，范围 1 .. 512 B |
| `RANDOM8192` | 随机 chunk，范围 1 .. 8192 B |
| `ZERO_SPLICE` | 随机 chunk 中插入 0 长度 update |
| `ALTERNATING` | 1 B 与 1024 B 交替 chunk |
| `BOUNDARY` | 按 block 边界步进（SHA-256/224/SM3 为 64 B；SHA-384/512 为 128 B） |
| `LARGE_STEPS` | 大 step 步进，含 8127/8128/8129/8192/16384 |
| `TINY` | 全 1 B chunk（仅长度 ≤512） |
| `ALIGNED_DMA` | 对齐大 chunk，强制走 DMA/硬件路径 |
| `LARGE_CPU` | 故意使用非对齐指针，走 CPU copy 基线 |
| `SINGLE_ALIGNED_DMA` | 单次 update，对齐 buffer |
| `MIXED_DMA_CPU` | DMA 大 chunk 与 CPU 小 chunk 混合 |
| `BULK_SINGLE_DMA` | 256 KB 对齐数据一次性 update，测量最大硬件吞吐 |

## 如何构建

```bash
west build -p auto -b lsqsh_evb@1os/lsqsh/cpu1 linkedsemi_zephyr_project/samples/mbedtls_hash
```

## DMA 与对齐说明

### SHA-256 / SHA-224 / SM3

- 标准 `mbedtls_sha256_update` 会自动选择：
  - 输入 **32 B cache-line 对齐** 且 **长度 ≥ 64 B** → 走 DMA 路径。
  - 否则 → CPU copy。
- 多次 `update` 之间 DMA 与 CPU copy 可任意混合。

### SHA-384 / SHA-512

- `mbedtls_sha512_update` 要求源地址在 **SRAM** 且 **4 B 对齐**，才能直接交给 LS_SHA512 硬件。
- 如果不满足，会先把数据拷贝到内部对齐 buffer 再走硬件。
- 因此 SHA-384/512 不需要 32 B 对齐，4 B 对齐即可达到最佳吞吐。

## 测试结果示例

> 测试环境：`lsqsh_evb@1os/lsqsh/cpu1`，RAM 中运行，硬件加速开启。
> 所有模式均打印 `all PASS`。

### 综合测试平均吞吐

| 算法 | 总字节数 | 总时间 (us) | 平均吞吐 (MB/s) |
|---|---|---|---|
| SHA-256 | 22328064 | 2259817 | 9.88 |
| SHA-224 | 22328064 | 2257657 | 9.88 |
| SM3     | 22328064 | 2293514 | 9.73 |
| SHA-384 | 22328064 | 2496911 | 8.94 |
| SHA-512 | 22328064 | 2505387 | 8.91 |

### BULK_SINGLE_DMA（256 KB 一次性 update）

| 算法 | 字节数 | 时间 (us) | 吞吐 (MB/s) |
|---|---|---|---|
| SHA-256 | 262144 | 3369 | 77.81 |
| SHA-384 | 262144 | 787  | 333.09 |
| SHA-512 | 262144 | 787  | 333.09 |

### 典型 per-mode 吞吐对比

| 模式 | SHA-256 (MB/s) | SHA-512 (MB/s) |
|---|---|---|
| RANDOM64 | 10.08 | 5.62 |
| RANDOM512 | 10.13 | 7.17 |
| ALIGNED_DMA | 21.55 | 24.61 |
| LARGE_CPU | 10.71 | 15.07 |
| SINGLE_ALIGNED_DMA | 22.25 | 25.99 |
| MIXED_DMA_CPU | 21.60 | 20.03 |
| BULK_SINGLE_DMA | 77.81 | 333.09 |

## 为什么综合测试平均吞吐远低于 BULK_SINGLE_DMA？

综合测试把大量小 chunk、非对齐偏移、CPU fallback、DMA/CPU 混合等场景累加在一起，每次 update 都要付出启动、中断、同步、block 边界处理等固定开销，所以平均只有约 **10 MB/s**。

`BULK_SINGLE_DMA` 是 256 KB 对齐数据一次性 update，硬件可以连续处理整段数据，几乎没有额外开销，因此能体现出硬件的真实峰值：

- SHA-256 约 **78 MB/s**，与 `mbedtls_throughput` 中 `hardware_ram` 256 KB DMA 的 **73.5 MB/s** 接近。
- SHA-512 约 **333 MB/s**，与 `mbedtls_throughput` 中 `hardware_ram` 256 KB 的 **317.25 MB/s** 接近；而非对齐时只有约 **29 MB/s**，可见对齐对 SHA-512 同样关键。

## 推荐用法

1. **追求最高吞吐**：
   - 尽量使用**单次大 buffer update**（如 ≥ 8 KB，越大越好）。
   - SHA-256/224/SM3 的 buffer 建议 **32 B 对齐**。
   - SHA-384/512 的 buffer 建议至少 **4 B 对齐**。
   - buffer 必须放在 SRAM。

2. **必须分片时**：
   - 使用较大的 chunk（≥ 8 KB），减少 update 次数。
   - 保持每片 chunk 的地址对齐，避免触发 CPU copy 或内部 buffer 拷贝。

3. **小数据 / 非对齐场景**：
   - 功能仍然正确，但吞吐会明显下降（如 1 B chunk 只有约 1.5 ~ 2.3 MB/s）。
   - 若业务只关心哈希结果而不追求速度，可直接使用标准 `mbedtls_xxx_update`。

## 参考

- 本目录：`linkedsemi_zephyr_project/samples/mbedtls_hash/`
- 吞吐对比原始数据：`linkedsemi_zephyr_project/samples/mbedtls_throughput/test_info/qsh_crypto_throughput.md`
