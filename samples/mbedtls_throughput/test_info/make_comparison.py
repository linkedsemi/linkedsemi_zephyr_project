#!/usr/bin/env python3
"""Parse hash_test log files and generate comparison.md."""
import re
from pathlib import Path

ROOT = Path(__file__).parent

# Mapping of file name -> display label
FILES = {
    "software_ram": "RAM + software",
    "software_xip": "XIP + software",
    "hardware_ram": "RAM + hardware",
    "hardware_xip": "XIP + hardware",
    "otbn_ram": "RAM + OTBN",
    "otbn_xip": "XIP + OTBN",
}

# Sections that should be kept for each mode. Entries not listed for a mode are
# considered "software fallback / not native to this mode" and filtered out.
MODE_SECTIONS = {
    "software_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
    "software_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
    "hardware_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
    "hardware_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
    "otbn_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
    "otbn_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa", "rsa_keygen"},
}

# Historical RSA-2048 operation baseline measured with pseudo-random numbers,
# kept for comparison with the current hardware true-RNG dataset.
RSA_PSEUDO = {
    "otbn_ram": {"encrypt": 1666.7, "decrypt": 14.7, "sign": 14.9, "verify": 1666.7},
    "otbn_xip": {"encrypt": 1111.1, "decrypt": 11.9, "sign": 12.8, "verify": 1000.0},
}


def parse_file(path: Path):
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    data = {
        "sha256": {},
        "sha512": {},
        "sha256_unaligned": {},
        "sha512_unaligned": {},
        "sm3": {},
        "sm3_unaligned": {},
        "cipher": [],
        "ecdsa": [],
        "rsa": [],
        "rsa_keygen": None,
    }
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("mbedtls SHA256 throughput"):
            i += 2
            data["sha256"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls SHA512 throughput"):
            i += 2
            data["sha512"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls SHA256 unaligned throughput"):
            i += 2
            data["sha256_unaligned"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls SHA512 unaligned throughput"):
            i += 2
            data["sha512_unaligned"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls SM3 throughput"):
            i += 2
            data["sm3"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls SM3 unaligned throughput"):
            i += 2
            data["sm3_unaligned"] = read_step_table(lines, i)
            i = skip_table(lines, i)
        elif line.startswith("mbedtls cipher throughput"):
            i += 1
            data["cipher"], i = read_cipher(lines, i)
        elif "ECDSA throughput" in line:
            i += 1
            data["ecdsa"], i = read_ecdsa(lines, i)
        elif line.startswith("mbedtls RSA-2048 throughput"):
            i += 1
            data["rsa"], i = read_rsa(lines, i)
            # The keygen section follows immediately after the RSA ops table.
            if i < len(lines) and "RSA-2048 keygen" in lines[i]:
                data["rsa_keygen"], i = read_rsa_keygen(lines, i)
        else:
            i += 1
    return data


def read_step_table(lines, start):
    table = {}
    i = start
    while i < len(lines) and lines[i].strip():
        m = re.match(r"^\s*(\d+)\s+(\d+)\s+(\S+)", lines[i])
        if m:
            table[int(m.group(1))] = {"time": m.group(2), "mbps": m.group(3)}
        i += 1
    return table


def skip_table(lines, start):
    i = start
    while i < len(lines) and lines[i].strip():
        i += 1
    return i


def read_cipher(lines, start):
    items = []
    seen = set()
    i = start
    while i < len(lines) and lines[i].strip():
        m = re.match(r"^  (.+?):\s+([\d.]+)\s+MB/s\s+\((\d+)\s+runs\)", lines[i])
        if m:
            name = m.group(1).strip()
            # The sample code accidentally prints "encrypt" for some decrypt modes;
            # keep the first occurrence so the table is stable.
            if name not in seen:
                items.append({"name": name, "mbps": m.group(2), "runs": m.group(3)})
                seen.add(name)
        i += 1
    return items, i


def read_ecdsa(lines, start):
    items = []
    i = start
    while i < len(lines) and lines[i].strip():
        m = re.match(r"^  (.+?):\s+(\d+)\s+ops/s\s+\((\d+)\s+loops?,\s+(\d+)\s+ms\)", lines[i])
        if m:
            loops = int(m.group(3))
            ms = int(m.group(4))
            ops_float = loops * 1000.0 / ms if ms else 0.0
            items.append({
                "name": m.group(1).strip(),
                "ops": ops_float,
                "loops": loops,
                "ms": ms,
            })
        i += 1
    return items, i


def read_rsa(lines, start):
    items = []
    i = start
    while i < len(lines) and lines[i].strip():
        # The keygen section follows the throughput ops; stop before it.
        if "RSA-2048 keygen" in lines[i]:
            break
        m = re.match(r"^  (.+?):\s+(\d+)\s+ops/s\s+\((\d+)\s+loops?,\s+(\d+)\s+ms\)", lines[i])
        if m:
            loops = int(m.group(3))
            ms = int(m.group(4))
            ops_float = loops * 1000.0 / ms if ms else 0.0
            items.append({
                "name": m.group(1).strip(),
                "ops": ops_float,
                "loops": loops,
                "ms": ms,
            })
        i += 1
    return items, i


def read_rsa_keygen(lines, start):
    """Read the RSA-2048 keygen loop timings and return total/average ms."""
    # Header line: "  RSA-2048 keygen (N loops)"
    header = lines[start]
    m = re.search(r"(\d+)\s+loops?", header)
    loops = int(m.group(1)) if m else 0
    i = start + 1
    times = []
    while i < len(lines) and lines[i].strip():
        m = re.match(r"^  RSA-2048 keygen loop \d+:\s+(\d+)\s+ms", lines[i])
        if m:
            times.append(int(m.group(1)))
        i += 1
    if not times:
        return None, i
    total_ms = sum(times)
    avg_ms = total_ms / len(times)
    return {
        "loops": loops,
        "times": times,
        "total_ms": total_ms,
        "avg_ms": avg_ms,
    }, i


def filter_data(data, mode):
    """Remove sections that are not native to the selected mode."""
    keep = MODE_SECTIONS[mode]
    filtered = {}
    for k, v in data.items():
        if k in keep:
            filtered[k] = v
        else:
            filtered[k] = {} if isinstance(v, dict) else [] if isinstance(v, list) else None
    return filtered


def step_table(name, key, datasets):
    # Collect steps from all datasets that actually have data.
    steps = set()
    for k in FILES:
        if datasets[k].get(key):
            steps.update(datasets[k][key].keys())
    if not steps:
        return ""
    steps = sorted(steps)
    md = f"\n## {name}\n\n"
    md += "| Step (KB) | " + " | ".join(FILES.keys()) + " |\n"
    md += "|-----------|" + "|".join(["-------"] * len(FILES)) + "|\n"
    for step in steps:
        vals = []
        for k in FILES:
            d = datasets[k].get(key, {})
            if step in d:
                vals.append(d[step]["mbps"])
            else:
                vals.append("-")
        md += f"| {step:9d} | " + " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    return md


def list_table(title, item_key, value_key, datasets, fmt="{:.2f}"):
    # Gather unique names preserving order across modes.
    names = []
    seen = set()
    for k in FILES:
        for item in datasets[k].get(item_key, []):
            if item["name"] not in seen:
                names.append(item["name"])
                seen.add(item["name"])
    if not names:
        return ""
    md = f"\n## {title}\n\n"
    md += "| Mode | " + " | ".join(FILES.keys()) + " |\n"
    md += "|------|" + "|".join(["-------"] * len(FILES)) + "|\n"
    for name in names:
        vals = []
        for k in FILES:
            found = next((x for x in datasets[k].get(item_key, []) if x["name"] == name), None)
            if found and not (item_key == "cipher" and "AES" in name and (k == "otbn_ram" or k == "otbn_xip")) and not (item_key == "ecdsa" and (k == "hardware_ram" or k == "hardware_xip")):
                vals.append(fmt.format(found[value_key]))
            else:
                vals.append("-")
        md += f"| {name:32s} | " + " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    return md


def rsa_table(datasets):
    """Render RSA-2048 ops table: software + OTBN(true) + OTBN(pseudo).

    RSA has no dedicated hardware accelerator; the hardware modes fall back to
    software, so those columns are hidden here.
    """
    ops_names = ["RSA-2048 encrypt", "RSA-2048 decrypt", "RSA-2048 sign", "RSA-2048 verify"]
    op_keys = {"RSA-2048 encrypt": "encrypt", "RSA-2048 decrypt": "decrypt",
               "RSA-2048 sign": "sign", "RSA-2048 verify": "verify"}
    labels = ["software_ram", "software_xip", "otbn_ram", "otbn_xip",
              "otbn_ram_pseudo", "otbn_xip_pseudo"]
    label_names = {
        "software_ram": "sw RAM",
        "software_xip": "sw XIP",
        "otbn_ram": "OTBN RAM (TRNG)",
        "otbn_xip": "OTBN XIP (TRNG)",
        "otbn_ram_pseudo": "OTBN RAM (pseudo)",
        "otbn_xip_pseudo": "OTBN XIP (pseudo)",
    }
    if not any(datasets[k].get("rsa") for k in FILES):
        return ""
    md = "\n## RSA-2048 performance (ops/s)\n\n"
    md += "| Mode | " + " | ".join(label_names[k] for k in labels) + " |\n"
    md += "|------|" + "|".join(["-------"] * len(labels)) + "|\n"
    for name in ops_names:
        key = op_keys[name]
        vals = [f"{name:32s}"]
        for label in labels:
            if label.endswith("_pseudo"):
                real_key = label.replace("_pseudo", "")
                pseudo = RSA_PSEUDO.get(real_key, {}).get(key)
                vals.append(f"{pseudo:.1f}" if pseudo is not None else "-")
            else:
                item = next((x for x in datasets[label].get("rsa", []) if x["name"] == name), None)
                vals.append(f"{item['ops']:.1f}" if item else "-")
        md += "| " + " | ".join(f"{v:>8s}" if i == 0 else f"{v:>18s}" for i, v in enumerate(vals)) + " |\n"
    return md


def keygen_table(title, key, datasets):
    """Render a table for RSA keygen loop timings across modes."""
    modes = [k for k in FILES if datasets[k].get(key)]
    if not modes:
        return ""
    md = f"\n## {title}\n\n"
    md += "| Mode | " + " | ".join(FILES.keys()) + " |\n"
    md += "|------|" + "|".join(["-------"] * len(FILES)) + "|\n"
    max_loops = max(len(datasets[k][key]["times"]) for k in modes)
    for loop_idx in range(1, max_loops + 1):
        md += f"| {'keygen loop ' + str(loop_idx):32s} | "
        vals = []
        for k in FILES:
            d = datasets[k].get(key)
            # RSA keygen has no dedicated hardware acceleration; hardware modes
            # fall back to software, so hide them for clarity.
            if k in ("hardware_ram", "hardware_xip"):
                vals.append("-")
            elif d and loop_idx <= len(d["times"]):
                vals.append(f"{d['times'][loop_idx - 1]}")
            else:
                vals.append("-")
        md += " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    md += f"| {'average keygen time (ms)':32s} | "
    vals = []
    for k in FILES:
        d = datasets[k].get(key)
        if k in ("hardware_ram", "hardware_xip"):
            vals.append("-")
        elif d:
            vals.append(f"{d['avg_ms']:.1f}")
        else:
            vals.append("-")
    md += " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    md += f"| {'total keygen time (ms)':32s} | "
    vals = []
    for k in FILES:
        d = datasets[k].get(key)
        if k in ("hardware_ram", "hardware_xip"):
            vals.append("-")
        elif d:
            vals.append(f"{d['total_ms']}")
        else:
            vals.append("-")
    md += " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    md += f"| {'min ~ max keygen time (ms)':32s} | "
    vals = []
    for k in FILES:
        d = datasets[k].get(key)
        if k in ("hardware_ram", "hardware_xip"):
            vals.append("-")
        elif d:
            vals.append(f"{min(d['times'])} ~ {max(d['times'])}")
        else:
            vals.append("-")
    md += " | ".join(f"{v:>8s}" for v in vals) + " |\n"
    md += "\n> 注：RSA-2048 密钥生成必须使用硬件真随机数（TRNG），软件随机数库无法满足 RSA 密钥对随机数质量的要求。上表中每次 keygen 耗时差异很大，是因为素数搜索过程中需要反复获取随机数并进行素性检测，而 TRNG 的取值以及大素数分布本身具有随机性，导致单次耗时从数百毫秒到数万毫秒不等。\n"
    return md


def main():
    raw_datasets = {k: parse_file(ROOT / k) for k in FILES}
    datasets = {k: filter_data(raw_datasets[k], k) for k in FILES}

    md = "# mbedtls throughput 性能对比\n\n"
    md += "对比六种运行方式。\n\n"
    for k, desc in FILES.items():
        md += f"- **{k}**：{desc}\n"

    md += step_table("SHA256 throughput (32 B aligned) (MB/s)", "sha256", datasets)
    md += step_table("SHA256 throughput (non-32 B aligned) (MB/s)", "sha256_unaligned", datasets)
    md += step_table("SHA512 throughput (4 B aligned) (MB/s)", "sha512", datasets)
    md += step_table("SHA512 throughput (non-4 B aligned) (MB/s)", "sha512_unaligned", datasets)
    md += step_table("SM3 throughput (MB/s)", "sm3", datasets)
    md += step_table("SM3 unaligned throughput (MB/s)", "sm3_unaligned", datasets)
    md += list_table("Cipher throughput (MB/s)", "cipher", "mbps", datasets, fmt="{}")
    md += list_table("ECDSA performance (ops/s)", "ecdsa", "ops", datasets, fmt="{:.1f}")
    md += rsa_table(datasets)
    md += keygen_table("RSA-2048 keygen performance", "rsa_keygen", datasets)

    md += """\n## 使用建议

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
"""

    out = ROOT / "qsh_crypto_throughput.md"
    out.write_text(md, encoding="utf-8")
    print(f"Generated {out}")


if __name__ == "__main__":
    main()
