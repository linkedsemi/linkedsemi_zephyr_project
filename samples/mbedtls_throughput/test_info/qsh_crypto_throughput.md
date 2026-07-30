# mbedtls throughput 性能对比

对比六种运行方式。

- **software_ram**：RAM + software
- **software_xip**：XIP + software
- **hardware_ram**：RAM + hardware
- **hardware_xip**：XIP + hardware
- **otbn_ram**：RAM + OTBN
- **otbn_xip**：XIP + OTBN


## SHA256 throughput (32 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     6.06 |     1.95 |    34.77 |     9.58 |     4.42 |     3.19 |
|         2 |     6.08 |     1.96 |    49.43 |    18.70 |     4.69 |     3.77 |
|         4 |     6.08 |     1.97 |    62.62 |    31.22 |     4.69 |     4.10 |
|         8 |     6.08 |     1.97 |    59.85 |    32.45 |     4.71 |     4.04 |
|        16 |     6.09 |     1.97 |    66.27 |    39.41 |     4.71 |     4.29 |
|        32 |     6.09 |     1.97 |    70.06 |    44.43 |     4.72 |     4.11 |
|        64 |     6.09 |     1.97 |    72.15 |    47.71 |     4.72 |     4.00 |
|       128 |     6.09 |     1.97 |    73.12 |    49.16 |     4.72 |     4.05 |
|       256 |     6.09 |     1.97 |    73.46 |    51.52 |     4.72 |     4.38 |

## SHA256 throughput (non-32 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     6.09 |     1.94 |    12.30 |     3.80 |     4.40 |     3.29 |
|         2 |     6.10 |     1.97 |    12.31 |     3.80 |     4.67 |     3.89 |
|         4 |     6.10 |     1.97 |    12.32 |     3.81 |     4.68 |     4.01 |
|         8 |     6.10 |     1.97 |    12.33 |     3.81 |     4.72 |     4.16 |
|        16 |     6.10 |     1.97 |    12.34 |     3.81 |     4.74 |     4.24 |
|        32 |     6.11 |     1.97 |    12.34 |     3.81 |     4.74 |     4.28 |
|        64 |     6.11 |     1.97 |    12.34 |     3.81 |     4.74 |     4.30 |
|       128 |     6.11 |     1.97 |    12.35 |     3.81 |     4.74 |     4.31 |
|       256 |     6.11 |     1.97 |    12.35 |     3.81 |     4.74 |     4.32 |

## SHA512 throughput (4 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     4.25 |     0.77 |    65.82 |    26.29 |     5.84 |     3.19 |
|         2 |     4.27 |     1.02 |   128.07 |    41.21 |     6.38 |     3.86 |
|         4 |     4.27 |     1.12 |   198.25 |    74.25 |     6.49 |     3.98 |
|         8 |     4.28 |     1.02 |   245.33 |   140.92 |     6.54 |     4.12 |
|        16 |     4.28 |     1.16 |   278.08 |   173.97 |     6.57 |     4.19 |
|        32 |     4.28 |     1.16 |   297.97 |   204.08 |     6.58 |     4.23 |
|        64 |     4.28 |     1.13 |   309.02 |   217.77 |     6.59 |     4.25 |
|       128 |     4.28 |     1.13 |   314.86 |   225.42 |     6.59 |     4.26 |
|       256 |     4.28 |     0.88 |   317.66 |   234.74 |     6.59 |     4.26 |

## SHA512 throughput (non-4 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     4.26 |     1.14 |    29.01 |     8.71 |     5.84 |     2.99 |
|         2 |     4.28 |     1.04 |    29.42 |    12.12 |     6.38 |     3.77 |
|         4 |     4.28 |     1.03 |    29.69 |    12.23 |     6.48 |     3.94 |
|         8 |     4.29 |     1.02 |    29.81 |    12.53 |     6.54 |     4.09 |
|        16 |     4.29 |     1.18 |    29.90 |    12.62 |     6.56 |     4.18 |
|        32 |     4.29 |     0.92 |    29.93 |    12.59 |     6.58 |     4.23 |
|        64 |     4.29 |     1.12 |    29.96 |    12.66 |     6.59 |     4.25 |
|       128 |     4.29 |     1.00 |    29.97 |    12.76 |     6.59 |     4.26 |
|       256 |     4.29 |     1.17 |    30.01 |    12.67 |     6.59 |     4.27 |

## SM3 throughput (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |        - |        - |    34.88 |     9.58 |     1.90 |     1.63 |
|         2 |        - |        - |    49.47 |    18.85 |     1.95 |     1.78 |
|         4 |        - |        - |    62.67 |    32.75 |     1.96 |     1.80 |
|         8 |        - |        - |    59.88 |    33.52 |     1.96 |     1.83 |
|        16 |        - |        - |    66.45 |    40.85 |     1.97 |     1.85 |
|        32 |        - |        - |    70.22 |    46.46 |     1.97 |     1.86 |
|        64 |        - |        - |    72.27 |    50.00 |     1.97 |     1.87 |
|       128 |        - |        - |    73.22 |    52.00 |     1.97 |     1.87 |
|       256 |        - |        - |    73.59 |    53.01 |     1.97 |     1.87 |

## SM3 unaligned throughput (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |        - |        - |    12.29 |     3.80 |     1.90 |     1.64 |
|         2 |        - |        - |    12.31 |     3.80 |     1.95 |     1.79 |
|         4 |        - |        - |    12.32 |     3.81 |     1.96 |     1.80 |
|         8 |        - |        - |    12.33 |     3.81 |     1.96 |     1.84 |
|        16 |        - |        - |    12.34 |     3.81 |     1.96 |     1.86 |
|        32 |        - |        - |    12.34 |     3.81 |     1.96 |     1.86 |
|        64 |        - |        - |    12.34 |     3.81 |     1.96 |     1.86 |
|       128 |        - |        - |    12.35 |     3.81 |     1.96 |     1.86 |
|       256 |        - |        - |    12.35 |     3.81 |     1.96 |     1.87 |

## Cipher throughput (MB/s)

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| AES-128 ECB encrypt              |     7.94 |     1.62 |    13.22 |    11.74 |        - |        - |
| AES-128 ECB decrypt              |     7.96 |     2.05 |    10.51 |     9.61 |        - |        - |
| AES-256 ECB encrypt              |     6.38 |     0.85 |    10.18 |     9.18 |        - |        - |
| AES-256 ECB decrypt              |     6.41 |     1.59 |     6.83 |     6.35 |        - |        - |
| AES-128 CBC encrypt              |     7.32 |     1.94 |    12.85 |     9.49 |        - |        - |
| AES-128 CBC decrypt              |     6.07 |     1.08 |    10.26 |     8.00 |        - |        - |
| AES-256 CBC encrypt              |     5.97 |     1.52 |    10.69 |     8.26 |        - |        - |
| AES-256 CBC decrypt              |     5.10 |     0.83 |     7.04 |     5.90 |        - |        - |
| AES-128 CTR encrypt              |     7.10 |     1.79 |    16.03 |    15.77 |        - |        - |
| AES-256 CTR encrypt              |     5.83 |     1.42 |    12.80 |    12.64 |        - |        - |
| AES-128 CFB128 encrypt           |     6.54 |     1.72 |    10.36 |     6.67 |        - |        - |
| AES-128 CFB128 decrypt           |     7.00 |     1.63 |    10.57 |     7.25 |        - |        - |
| AES-256 CFB128 encrypt           |     5.44 |     1.38 |     8.48 |     5.85 |        - |        - |
| AES-256 CFB128 decrypt           |     5.76 |     1.38 |     8.55 |     6.18 |        - |        - |
| AES-128 CFB8 encrypt             |     0.33 |     0.06 |     0.61 |     0.30 |        - |        - |
| AES-128 CFB8 decrypt             |     0.42 |     0.06 |     0.49 |     0.38 |        - |        - |
| AES-256 CFB8 encrypt             |     0.27 |     0.04 |     0.50 |     0.25 |        - |        - |
| AES-256 CFB8 decrypt             |     0.34 |     0.04 |     0.50 |     0.33 |        - |        - |
| AES-128 OFB encrypt              |     7.10 |     1.85 |    10.93 |     7.50 |        - |        - |
| AES-256 OFB encrypt              |     5.83 |     1.46 |     8.75 |     6.40 |        - |        - |
| AES-128 XTS encrypt              |     5.33 |     1.69 |     7.66 |     3.32 |        - |        - |
| AES-128 XTS decrypt              |     6.42 |     1.36 |     6.67 |     3.85 |        - |        - |
| AES-256 XTS encrypt              |     5.30 |     1.69 |     7.66 |     4.15 |        - |        - |
| AES-256 XTS decrypt              |     6.41 |     1.69 |     6.67 |     3.85 |        - |        - |
| SM4 ECB encrypt                  |        - |        - |    41.92 |    41.87 |        - |        - |
| SM4 ECB decrypt                  |        - |        - |    41.92 |    41.85 |        - |        - |
| SM4 CTR encrypt                  |        - |        - |    11.13 |     4.56 |        - |        - |
| SM4 CTR decrypt                  |        - |        - |    11.13 |     4.57 |        - |        - |

## ECDSA performance (ops/s)

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| ECDSA P-384 keygen               |      4.1 |      0.9 |        - |        - |    192.3 |    185.2 |
| ECDSA P-384 sign                 |      4.0 |      1.2 |        - |        - |    178.6 |    172.4 |
| ECDSA P-384 verify               |      0.9 |      0.3 |        - |        - |    263.2 |    250.0 |
| ECDSA P-256 keygen               |      8.5 |      2.5 |        - |        - |    416.7 |    454.5 |
| ECDSA P-256 sign                 |      8.4 |      1.7 |        - |        - |    454.5 |    384.6 |
| ECDSA P-256 verify               |      2.2 |      0.7 |        - |        - |    714.3 |    625.0 |
| SM2 keygen                       |        - |        - |        - |        - |    333.3 |    333.3 |
| SM2 sign                         |        - |        - |        - |        - |    333.3 |    312.5 |
| SM2 verify                       |        - |        - |        - |        - |    178.6 |    172.4 |

## RSA-2048 performance (ops/s)

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| RSA-2048 encrypt                 |    138.9 |     44.6 |    140.8 |     44.6 |   1666.7 |   1111.1 |
| RSA-2048 decrypt                 |      2.3 |      0.7 |      2.3 |      0.7 |     14.7 |     11.9 |
| RSA-2048 sign                    |      2.3 |      0.7 |      2.3 |      0.7 |     14.9 |     12.8 |
| RSA-2048 verify                  |    142.9 |     45.9 |    144.9 |     45.9 |   1666.7 |   1000.0 |

## 使用建议

### 1. 哈希算法

#### SHA-256 / SHA-224 / SM3

- **最佳性能**：buffer 地址 **32 B 对齐**，单次 update 长度 **≥ 64 B**，越大越好。
  - RAM + hardware 下 256 KB 单次可达 **~73 MB/s**。
  - 对比非 32 B 对齐场景（~12 MB/s），对齐可带来 **6 倍以上** 提升。
- **必须分片时**：尽量使用大 chunk（≥ 8 KB），减少 update 次数；多次 update 之间 DMA 与 CPU copy 可自动切换，不影响正确性。
- **XIP 场景**：256 KB 对齐数据在 XIP + hardware 下仍可达 **~52 MB/s**，但小 chunk 会明显下降。
- **OTBN 路径**：SHA-256 走 OTBN 收益不大（~4.7 MB/s），不如 hardware DMA 路径；SM3 在 OTBN 下仅 ~2 MB/s，明显低于 hardware 的 ~73 MB/s。

#### SHA-384 / SHA-512

- **最佳性能**：buffer 地址至少 **4 B 对齐**，单次大 buffer update 效果最好。
  - RAM + hardware 下 256 KB 单次可达 **~318 MB/s**。
  - 非 4 B 对齐时骤降到 **~30 MB/s**，对齐至关重要。
- 源地址必须在 **SRAM** 范围内，否则硬件无法直接访问。

### 2. 对称加密（AES / SM4）

- **AES**：CTR/ECB/CBC 在 hardware 模式下普遍优于 software；CFB8 模式非常慢（~0.3 MB/s），不建议用于大数据量。
- **SM4**：ECB 在 hardware 下可达 **~42 MB/s**，但 CTR 仅 ~11 MB/s（RAM）/ ~4.6 MB/s（XIP）。
- XIP 会显著拉低 AES 性能。

### 3. 非对称算法

#### RSA-2048

- **OTBN 加速效果显著**：
  - public（encrypt/verify）：~1667 ops/s（RAM），是 software 的 **~12 倍**。
  - private（decrypt/sign）：~14.9 ops/s（RAM），是 software 的 **~6.5 倍**。
- XIP 会让 OTBN 的 private 操作再下降约 **15%~20%**。
- 仅当 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y` 且密钥为 2048/3072/4096-bit、公钥指数 F4 时才走 OTBN，否则自动软件 fallback。

#### ECDSA / SM2

- OTBN 下 P-256 verify 可达 **~714 ops/s**，P-384 verify 约 **~263 ops/s**，SM2 verify 约 **~178 ops/s**。

### 4. 加速能力汇总

具体硬件加速开关请参考 `zephyr/soc/linkedsemi/lsqsh/otbn/Kconfig.mbedtls`。

| 算法 | Hardware 加速 | OTBN 加速 | 备注 |
|---|---|---|---|
| SHA-224 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s，是 software 的 12 倍；32 B 对齐 + 单次 update ≥64 B 时性能最高，非 32 B 对齐会降到约 12 MB/s |
| SHA-256 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s，是 software 的 12 倍；非 32 B 对齐会降到约 12 MB/s；OTBN 仅约 4.7 MB/s，不建议用 |
| SM3 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s；非 32 B 对齐会降到约 12 MB/s；OTBN 仅约 2 MB/s，不建议用 |
| SHA-384 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 318 MB/s，是 software 的 74 倍；4 B 对齐时性能最高，非 4 B 对齐会降到约 30 MB/s |
| SHA-512 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 318 MB/s，是 software 的 74 倍；4 B 对齐时性能最高，非 4 B 对齐会降到约 30 MB/s |
| AES | ✔ |  | 走 hardware 更快，CTR 在 RAM 下约 16 MB/s，约为 software 的 2.3 倍；XIP 下性能会明显下降 |
| SM4 | ✔ |  | 走 hardware 更快，ECB 约 42 MB/s，CTR 约 11 MB/s；CTR 明显慢于 ECB |
| RSA-2048/3072/4096 |  | ✔ | 走 OTBN 提升最大，public 约 1667 ops/s，是 software 的 12 倍；private 约 14.9 ops/s，是 software 的 6.5 倍；公钥指数 F4 有额外优化 |
| ECDSA P-256/P-384 |  | ✔ | 走 OTBN 远快于 software；P-256 verify 约 714 ops/s，P-384 verify 约 263 ops/s |
| SM2 |  | ✔ | 走 OTBN 远快于 software；verify 约 178 ops/s |

### 5. 通用建议

1. **对齐影响性能，不影响正确性**：SHA-256 32 B 对齐、SHA-512 4 B 对齐时硬件 DMA 路径才能跑满，非对齐时结果仍然正确，但吞吐会明显下降（SHA-256 从 ~73 MB/s 降到 ~12 MB/s，SHA-512 从 ~318 MB/s 降到 ~30 MB/s）。
2. **尽量减少 update 次数**：大 buffer 单次 update 比多次小 update 吞吐高一个数量级。
3. **优先在 RAM 中运行**：XIP 对小 chunk、哈希、RSA private 都有明显惩罚。
4. **OTBN 不是万能**：对 SHA-256/SM3 等已有专用 hardware 的算法，OTBN 反而更慢；OTBN 应留给 RSA/ECDSA/SM2 等非对称算法。
5. **实际业务吞吐通常远低于峰值**：如果业务天然是小 chunk、随机对齐、频繁 update，请参考 `mbedtls_hash` 综合测试的平均值，而非本表中的 BULK 峰值。

> 注：使用 mbed TLS 的 alt 实现时，需关闭 Zephyr crypto driver，否则会与 mbed TLS 的硬件替换冲突。当前 Zephyr crypto driver 已停止维护，除硬件 hash 外不建议使用；若业务直接使用 hardware hash driver，则 mbed TLS 侧不要再开启对应的硬件 hash alt，否则同一硬件资源会产生冲突。

> 注：测试结果在 QSH 的 CPU1 平台上获得，不同 CPU 之间相同硬件加速单元的性能差异可以忽略。
