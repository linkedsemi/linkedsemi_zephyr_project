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
|         1 |     6.74 |     1.96 |    35.68 |     9.80 |     4.40 |     3.22 |
|         2 |     6.74 |     1.96 |    50.31 |    19.52 |     4.65 |     3.92 |
|         4 |     6.75 |     1.96 |    63.48 |    31.71 |     4.69 |     4.01 |
|         8 |     6.75 |     1.95 |    60.37 |    37.95 |     4.71 |     4.17 |
|        16 |     6.75 |     1.93 |    66.82 |    37.93 |     4.72 |     4.25 |
|        32 |     6.75 |     1.90 |    70.62 |    42.33 |     4.73 |     4.25 |
|        64 |     6.75 |     1.93 |    72.69 |    44.73 |     4.73 |     4.30 |
|       128 |     6.75 |     1.90 |    73.70 |    46.40 |     4.73 |     4.13 |
|       256 |     6.75 |     1.83 |    74.00 |    48.25 |     4.73 |     4.27 |

## SHA256 throughput (non-32 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     6.76 |     1.96 |    12.30 |     3.80 |     4.38 |     3.26 |
|         2 |     6.77 |     1.96 |    12.32 |     3.80 |     4.63 |     3.91 |
|         4 |     6.77 |     1.96 |    12.33 |     3.81 |     4.67 |     4.07 |
|         8 |     6.77 |     1.95 |    12.34 |     3.81 |     4.69 |     4.18 |
|        16 |     6.77 |     1.94 |    12.35 |     3.81 |     4.70 |     4.24 |
|        32 |     6.77 |     1.90 |    12.35 |     3.81 |     4.71 |     4.26 |
|        64 |     6.77 |     1.93 |    12.35 |     3.81 |     4.71 |     4.28 |
|       128 |     6.77 |     1.90 |    12.35 |     3.81 |     4.71 |     4.28 |
|       256 |     6.77 |     1.83 |    12.35 |     3.81 |     4.71 |     4.31 |

## SHA512 throughput (4 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     4.56 |     1.32 |    65.78 |    43.38 |     5.86 |     3.23 |
|         2 |     4.58 |     1.32 |   127.42 |    78.34 |     6.38 |     3.87 |
|         4 |     4.60 |     1.32 |   192.90 |   131.99 |     6.50 |     4.00 |
|         8 |     4.60 |     1.32 |   241.08 |   174.58 |     6.56 |     4.12 |
|        16 |     4.61 |     1.31 |   275.33 |   205.76 |     6.59 |     4.20 |
|        32 |     4.61 |     1.31 |   296.55 |   224.41 |     6.61 |     4.23 |
|        64 |     4.61 |     1.26 |   308.26 |   232.77 |     6.62 |     4.25 |
|       128 |     4.60 |     1.23 |   314.46 |   242.24 |     6.62 |     4.26 |
|       256 |     4.60 |     1.21 |   317.25 |   243.90 |     6.63 |     4.28 |

## SHA512 throughput (non-4 B aligned) (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |     4.57 |     1.32 |    29.86 |    15.51 |     5.86 |     3.26 |
|         2 |     4.60 |     1.32 |    30.29 |    16.44 |     6.38 |     3.84 |
|         4 |     4.61 |     1.32 |    30.56 |    16.99 |     6.50 |     4.00 |
|         8 |     4.61 |     1.32 |    30.67 |    17.14 |     6.56 |     4.11 |
|        16 |     4.62 |     1.31 |    30.73 |    17.27 |     6.59 |     4.18 |
|        32 |     4.62 |     1.30 |    30.75 |    17.38 |     6.61 |     4.21 |
|        64 |     4.62 |     1.30 |    30.77 |    17.42 |     6.62 |     4.25 |
|       128 |     4.62 |     1.23 |    30.78 |    17.43 |     6.62 |     4.23 |
|       256 |     4.62 |     1.19 |    30.75 |    17.39 |     6.62 |     4.23 |

## SM3 throughput (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |        - |        - |    35.78 |     8.88 |     1.91 |     1.63 |
|         2 |        - |        - |    50.44 |    18.04 |     1.95 |     1.78 |
|         4 |        - |        - |    63.45 |    30.90 |     1.96 |     1.81 |
|         8 |        - |        - |    60.37 |    30.88 |     1.97 |     1.85 |
|        16 |        - |        - |    66.88 |    37.78 |     1.97 |     1.87 |
|        32 |        - |        - |    70.66 |    42.30 |     1.97 |     1.88 |
|        64 |        - |        - |    72.71 |    44.96 |     1.97 |     1.88 |
|       128 |        - |        - |    73.68 |    46.33 |     1.97 |     1.88 |
|       256 |        - |        - |    74.03 |    45.61 |     1.97 |     1.88 |

## SM3 unaligned throughput (MB/s)

| Step (KB) | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|-----------|-------|-------|-------|-------|-------|-------|
|         1 |        - |        - |    12.30 |     3.80 |     1.90 |     1.63 |
|         2 |        - |        - |    12.32 |     3.80 |     1.95 |     1.78 |
|         4 |        - |        - |    12.32 |     3.80 |     1.96 |     1.82 |
|         8 |        - |        - |    12.34 |     3.81 |     1.96 |     1.85 |
|        16 |        - |        - |    12.35 |     3.81 |     1.96 |     1.87 |
|        32 |        - |        - |    12.35 |     3.81 |     1.97 |     1.88 |
|        64 |        - |        - |    12.35 |     3.81 |     1.97 |     1.88 |
|       128 |        - |        - |    12.35 |     3.81 |     1.97 |     1.88 |
|       256 |        - |        - |    12.35 |     3.81 |     1.97 |     1.88 |

## Cipher throughput (MB/s)

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| AES-128 ECB encrypt              |     7.71 |     0.93 |    13.18 |    12.00 |        - |        - |
| AES-128 ECB decrypt              |     8.34 |     1.84 |    10.42 |     9.65 |        - |        - |
| AES-256 ECB encrypt              |     6.23 |     1.56 |    10.25 |     9.41 |        - |        - |
| AES-256 ECB decrypt              |     6.66 |     1.46 |     6.85 |     6.43 |        - |        - |
| AES-128 CBC encrypt              |     7.60 |     1.69 |    13.09 |     9.49 |        - |        - |
| AES-128 CBC decrypt              |     6.38 |     1.50 |    10.41 |     8.00 |        - |        - |
| AES-256 CBC encrypt              |     6.20 |     1.47 |    10.85 |     8.26 |        - |        - |
| AES-256 CBC decrypt              |     5.34 |     1.23 |     7.11 |     5.90 |        - |        - |
| AES-128 CTR encrypt              |     7.45 |     1.80 |    16.03 |    15.77 |        - |        - |
| AES-256 CTR encrypt              |     6.06 |     1.44 |    12.80 |    12.64 |        - |        - |
| AES-128 CFB128 encrypt           |     7.27 |     1.72 |    10.18 |     6.53 |        - |        - |
| AES-128 CFB128 decrypt           |     7.35 |     1.08 |    10.67 |     7.22 |        - |        - |
| AES-256 CFB128 encrypt           |     5.94 |     1.38 |     8.31 |     5.81 |        - |        - |
| AES-256 CFB128 decrypt           |     6.00 |     1.04 |     8.49 |     6.15 |        - |        - |
| AES-128 CFB8 encrypt             |     0.35 |     0.06 |     0.61 |     0.22 |        - |        - |
| AES-128 CFB8 decrypt             |     0.44 |     0.05 |     0.49 |     0.26 |        - |        - |
| AES-256 CFB8 encrypt             |     0.36 |     0.02 |     0.48 |     0.20 |        - |        - |
| AES-256 CFB8 decrypt             |     0.29 |     0.03 |     0.49 |     0.18 |        - |        - |
| AES-128 OFB encrypt              |     7.19 |     1.85 |    10.98 |     6.96 |        - |        - |
| AES-256 OFB encrypt              |     5.89 |     1.17 |     8.82 |     5.63 |        - |        - |
| AES-128 XTS encrypt              |     6.13 |     1.68 |     7.79 |     3.80 |        - |        - |
| AES-128 XTS decrypt              |     6.61 |     1.59 |     6.77 |     3.50 |        - |        - |
| AES-256 XTS encrypt              |     6.13 |     0.99 |     7.79 |     3.80 |        - |        - |
| AES-256 XTS decrypt              |     6.61 |     1.60 |     6.77 |     3.54 |        - |        - |
| SM4 ECB encrypt                  |        - |        - |    41.91 |    41.66 |        - |        - |
| SM4 ECB decrypt                  |        - |        - |    41.92 |    41.41 |        - |        - |
| SM4 CTR encrypt                  |        - |        - |    10.54 |     2.89 |        - |        - |
| SM4 CTR decrypt                  |        - |        - |    10.63 |     4.64 |        - |        - |

## ECDSA performance (ops/s)

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| ECDSA P-384 keygen               |      4.7 |      0.7 |        - |        - |    192.3 |    178.6 |
| ECDSA P-384 sign                 |      4.7 |      0.8 |        - |        - |    178.6 |    178.6 |
| ECDSA P-384 verify               |      1.1 |      0.2 |        - |        - |    263.2 |    250.0 |
| ECDSA P-256 keygen               |      9.7 |      1.3 |        - |        - |    416.7 |    416.7 |
| ECDSA P-256 sign                 |      9.6 |      1.2 |        - |        - |    454.5 |    416.7 |
| ECDSA P-256 verify               |      2.5 |      0.4 |        - |        - |    714.3 |    625.0 |
| SM2 keygen                       |        - |        - |        - |        - |    333.3 |    357.1 |
| SM2 sign                         |        - |        - |        - |        - |    333.3 |    333.3 |
| SM2 verify                       |        - |        - |        - |        - |    178.6 |    178.6 |

## RSA-2048 performance (ops/s)

| Mode | sw RAM | sw XIP | OTBN RAM (TRNG) | OTBN XIP (TRNG) | OTBN RAM (pseudo) | OTBN XIP (pseudo) |
|------|-------|-------|-------|-------|-------|-------|
| RSA-2048 encrypt                 |              133.3 |               40.2 |              909.1 |              256.4 |             1666.7 |             1111.1 |
| RSA-2048 decrypt                 |                2.3 |                0.7 |               14.7 |               12.2 |               14.7 |               11.9 |
| RSA-2048 sign                    |                2.3 |                0.7 |               14.9 |               12.4 |               14.9 |               12.8 |
| RSA-2048 verify                  |              144.9 |               45.0 |             1666.7 |              666.7 |             1666.7 |             1000.0 |

## RSA-2048 keygen performance

| Mode | software_ram | software_xip | hardware_ram | hardware_xip | otbn_ram | otbn_xip |
|------|-------|-------|-------|-------|-------|-------|
| keygen loop 1                    |     5443 |    14726 |        - |        - |     1262 |     4279 |
| keygen loop 2                    |     3965 |    18532 |        - |        - |      713 |     2018 |
| keygen loop 3                    |    14232 |    71743 |        - |        - |     1786 |     3733 |
| keygen loop 4                    |     8118 |    17179 |        - |        - |     3681 |     6190 |
| keygen loop 5                    |    13911 |    34447 |        - |        - |     2226 |     1110 |
| keygen loop 6                    |     8429 |    40463 |        - |        - |     2395 |     2415 |
| keygen loop 7                    |    15621 |    33107 |        - |        - |     3265 |      676 |
| keygen loop 8                    |    14167 |    42767 |        - |        - |     1914 |     6022 |
| keygen loop 9                    |     2313 |    22550 |        - |        - |      940 |     6621 |
| keygen loop 10                   |     6109 |     9904 |        - |        - |      126 |     1821 |
| average keygen time (ms)         |   9230.8 |  30541.8 |        - |        - |   1830.8 |   3488.5 |
| total keygen time (ms)           |    92308 |   305418 |        - |        - |    18308 |    34885 |
| min ~ max keygen time (ms)       | 2313 ~ 15621 | 9904 ~ 71743 |        - |        - | 126 ~ 3681 | 676 ~ 6621 |

> 注：RSA-2048 密钥生成必须使用硬件真随机数（TRNG），软件随机数库无法满足 RSA 密钥对随机数质量的要求。上表中每次 keygen 耗时差异很大，是因为素数搜索过程中需要反复获取随机数并进行素性检测，而 TRNG 的取值以及大素数分布本身具有随机性，导致单次耗时从数百毫秒到数万毫秒不等。

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

- **OTBN 加速效果显著**（当前测试使用硬件真随机数）：
  - public（encrypt/verify）：RAM 下约 **909 ops/s**，XIP 下约 **256~666 ops/s**。
  - private（decrypt/sign）：RAM 下约 **14 ops/s**，XIP 下约 **12 ops/s**。
- **RSA 需要两处真随机数注入**：
  1. **OTBN 侧**：使用 RSA 前必须调用 `ls_otbn_random_callback_register()` 注册 TRNG 回调，为 OTBN 的 `RND`/`URND` 提供真随机数；OTBN 在 Miller-Rabin 素性检测（`CHECK_PRIME`）时需要用这些随机数生成 base。
  2. **mbedtls 侧**：`mbedtls_rsa_genkey()` 传入的 RNG 回调（如 `rsa_keygen_get_rng`）也必须返回真随机数，用于在 CPU 侧生成素数候选值；伪随机数会导致 candidate 分布有偏，可能找不到素数。
- **随机数源影响加速效率**：当前 keygen 流程由 CPU 生成候选值并做试除，OTBN 跑 `CHECK_PRIME` 和 `KEY_FROM_PQ`。TRNG 的读取开销（尤其是 OTBN RND/URND 同步）是 keygen 耗时的主要波动来源之一，因此 public 操作（encrypt/verify）在 TRNG 下的性能也会低于伪随机数场景。
- **密钥生成**：RSA-2048 keygen 耗时不稳定，单次从 **126 ms 到 71743 ms** 不等，主要取决于素数搜索过程中 TRNG 读取和素性检测的开销；OTBN keygen 明显快于纯 software。
- XIP 会让 OTBN 的 private 操作再下降约 **15%~20%**。
- 仅当 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y` 且密钥为 2048/3072/4096-bit、公钥指数 F4 时才走 OTBN，否则自动软件 fallback。

#### ECDSA / SM2

- OTBN 下 P-256 verify 可达 **~714 ops/s**，P-384 verify 约 **~263 ops/s**，SM2 verify 约 **~178 ops/s**。

### 4. 加速能力汇总

具体硬件/OTBN 加速开关集中在 `zephyr/soc/linkedsemi/lsqsh/otbn/Kconfig.mbedtls`：

- Hardware：
  - `CONFIG_MBEDTLS_CIPHER_AES_LINKEDSEMI`
  - `CONFIG_MBEDTLS_SM4_LINKEDSEMI_HARDWARE_ALT`
  - `CONFIG_MBEDTLS_SHA224_SHA256_SM3_LINKEDSEMI_HARDWARE_ALT`
  - `CONFIG_MBEDTLS_SHA384_SHA512_LINKEDSEMI_HARDWARE_ALT`
- OTBN：
  - `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT`
  - `CONFIG_MBEDTLS_ECDSA_SECP256R1_SECP384R1_SM2_LINKEDSEMI_OTBN_ALT`
  - `CONFIG_MBEDTLS_ECP_DP_SM2_ENABLED`
  - `CONFIG_MBEDTLS_SHA256_SM3_LINKEDSEMI_OTBN_ALT`
  - `CONFIG_MBEDTLS_SHA384_SHA512_LINKEDSEMI_OTBN_ALT`

| 算法 | Hardware 加速 | OTBN 加速 | 备注 |
|---|---|---|---|
| SHA-224 | ✔ | ✔ | 推荐使用 hardware |
| SHA-256 | ✔ | ✔ | 推荐使用 hardware；不建议使用 OTBN（慢于 software） |
| SM3 | ✔ | ✔ | 推荐使用 hardware；不建议使用 OTBN（慢于 software） |
| SHA-384 | ✔ | ✔ | 推荐使用 hardware |
| SHA-512 | ✔ | ✔ | 推荐使用 hardware |
| AES | ✔ |  | 推荐使用 hardware |
| SM4 | ✔ |  | 推荐使用 hardware |
| RSA-2048/3072/4096 |  | ✔ | 推荐使用 OTBN |
| RSA-2048/3072/4096 keygen |  | ✔ | 推荐使用 OTBN；RSA 密钥生成必须使用硬件真随机数，软件随机数库无法满足 RSA 密钥对随机数的要求 |
| ECDSA P-256/P-384 |  | ✔ | 推荐使用 OTBN |
| SM2 |  | ✔ | 推荐使用 OTBN |

### 5. 通用建议

1. **对齐影响性能，不影响正确性**：SHA-256 32 B 对齐、SHA-512 4 B 对齐时硬件 DMA 路径才能跑满，非对齐时结果仍然正确，但吞吐会明显下降（SHA-256 从 ~73 MB/s 降到 ~12 MB/s，SHA-512 从 ~318 MB/s 降到 ~30 MB/s）。
2. **尽量减少 update 次数**：大 buffer 单次 update 比多次小 update 吞吐高一个数量级。
3. **优先在 RAM 中运行**：XIP 对小 chunk、哈希、RSA private 都有明显惩罚。
4. **OTBN 不是万能**：对 SHA-256/SM3 等已有专用 hardware 的算法，OTBN 反而更慢；OTBN 应留给 RSA/ECDSA/SM2 等非对称算法。
5. **实际业务吞吐通常远低于峰值**：如果业务天然是小 chunk、随机对齐、频繁 update，请参考 `mbedtls_hash` 综合测试的平均值，而非本表中的 BULK 峰值。

> 注：使用 mbed TLS 的 alt 实现时，需关闭 Zephyr crypto driver，否则会与 mbed TLS 的硬件替换冲突。当前 Zephyr crypto driver 已停止维护，除硬件 hash 外不建议使用；若业务直接使用 hardware hash driver，则 mbed TLS 侧不要再开启对应的硬件 hash alt，否则同一硬件资源会产生冲突。

> 注：测试结果在 QSH 的 CPU1 平台上获得，不同 CPU 之间相同硬件加速单元的性能差异可以忽略。
