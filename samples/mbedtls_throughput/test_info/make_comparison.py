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
    "software_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
    "software_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
    "hardware_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
    "hardware_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
    "otbn_ram": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
    "otbn_xip": {"sha256", "sha512", "sha256_unaligned", "sha512_unaligned", "sm3", "sm3_unaligned", "cipher", "ecdsa", "rsa"},
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


def filter_data(data, mode):
    """Remove sections that are not native to the selected mode."""
    keep = MODE_SECTIONS[mode]
    filtered = {}
    for k, v in data.items():
        if k in keep:
            filtered[k] = v
        else:
            filtered[k] = {} if isinstance(v, dict) else []
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


def main():
    raw_datasets = {k: parse_file(ROOT / k) for k in FILES}
    datasets = {k: filter_data(raw_datasets[k], k) for k in FILES}

    md = "# mbedtls throughput 性能对比\n\n"
    md += "对比六种运行方式。\n\n"
    for k, desc in FILES.items():
        md += f"- **{k}**：{desc}\n"
    md += "\n"

    md += step_table("SHA256 throughput (32 B aligned) (MB/s)", "sha256", datasets)
    md += step_table("SHA256 throughput (non-32 B aligned) (MB/s)", "sha256_unaligned", datasets)
    md += step_table("SHA512 throughput (4 B aligned) (MB/s)", "sha512", datasets)
    md += step_table("SHA512 throughput (non-4 B aligned) (MB/s)", "sha512_unaligned", datasets)
    md += step_table("SM3 throughput (MB/s)", "sm3", datasets)
    md += step_table("SM3 unaligned throughput (MB/s)", "sm3_unaligned", datasets)
    md += list_table("Cipher throughput (MB/s)", "cipher", "mbps", datasets, fmt="{}")
    md += list_table("ECDSA performance (ops/s)", "ecdsa", "ops", datasets, fmt="{:.1f}")
    md += list_table("RSA-2048 performance (ops/s)", "rsa", "ops", datasets, fmt="{:.1f}")

    md += """\n## 使用建议\n\n### 1. 哈希算法\n\n#### SHA-256 / SHA-224 / SM3\n\n- **最佳性能**：buffer 地址 **32 B 对齐**，单次 update 长度 **≥ 64 B**，越大越好。\n  - RAM + hardware 下 256 KB 单次可达 **~73 MB/s**。\n  - 对比非 32 B 对齐场景（~12 MB/s），对齐可带来 **6 倍以上** 提升。\n- **必须分片时**：尽量使用大 chunk（≥ 8 KB），减少 update 次数；多次 update 之间 DMA 与 CPU copy 可自动切换，不影响正确性。\n- **XIP 场景**：256 KB 对齐数据在 XIP + hardware 下仍可达 **~52 MB/s**，但小 chunk 会明显下降。\n- **OTBN 路径**：SHA-256 走 OTBN 收益不大（~4.7 MB/s），不如 hardware DMA 路径；SM3 在 OTBN 下仅 ~2 MB/s，明显低于 hardware 的 ~73 MB/s。\n\n#### SHA-384 / SHA-512\n\n- **最佳性能**：buffer 地址至少 **4 B 对齐**，单次大 buffer update 效果最好。\n  - RAM + hardware 下 256 KB 单次可达 **~318 MB/s**。\n  - 非 4 B 对齐时骤降到 **~30 MB/s**，对齐至关重要。\n- 源地址必须在 **SRAM** 范围内，否则硬件无法直接访问。\n\n### 2. 对称加密（AES / SM4）\n\n- **AES**：CTR/ECB/CBC 在 hardware 模式下普遍优于 software；CFB8 模式非常慢（~0.3 MB/s），不建议用于大数据量。\n- **SM4**：ECB 在 hardware 下可达 **~42 MB/s**，但 CTR 仅 ~11 MB/s（RAM）/ ~4.6 MB/s（XIP）。\n- XIP 会显著拉低 AES 性能。\n\n### 3. 非对称算法\n\n#### RSA-2048\n\n- **OTBN 加速效果显著**：\n  - public（encrypt/verify）：~1667 ops/s（RAM），是 software 的 **~12 倍**。\n  - private（decrypt/sign）：~14.9 ops/s（RAM），是 software 的 **~6.5 倍**。\n- XIP 会让 OTBN 的 private 操作再下降约 **15%~20%**。\n- 仅当 `CONFIG_MBEDTLS_RSA_LINKEDSEMI_OTBN_ALT=y` 且密钥为 2048/3072/4096-bit、公钥指数 F4 时才走 OTBN，否则自动软件 fallback。\n\n#### ECDSA / SM2\n\n- OTBN 下 P-256 verify 可达 **~714 ops/s**，P-384 verify 约 **~263 ops/s**，SM2 verify 约 **~178 ops/s**。\n\n### 4. 加速能力汇总\n\n具体硬件加速开关请参考 `zephyr/soc/linkedsemi/lsqsh/otbn/Kconfig.mbedtls`。\n\n| 算法 | Hardware 加速 | OTBN 加速 | 备注 |\n|---|---|---|---|\n| SHA-224 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s，是 software 的 12 倍；32 B 对齐 + 单次 update ≥64 B 时性能最高，非 32 B 对齐会降到约 12 MB/s |\n| SHA-256 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s，是 software 的 12 倍；非 32 B 对齐会降到约 12 MB/s；OTBN 仅约 4.7 MB/s，不建议用 |\n| SM3 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 73 MB/s；非 32 B 对齐会降到约 12 MB/s；OTBN 仅约 2 MB/s，不建议用 |\n| SHA-384 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 318 MB/s，是 software 的 74 倍；4 B 对齐时性能最高，非 4 B 对齐会降到约 30 MB/s |\n| SHA-512 | ✔ | ✔ | 走 hardware 最划算，256 KB 单次约 318 MB/s，是 software 的 74 倍；4 B 对齐时性能最高，非 4 B 对齐会降到约 30 MB/s |\n| AES | ✔ |  | 走 hardware 更快，CTR 在 RAM 下约 16 MB/s，约为 software 的 2.3 倍；XIP 下性能会明显下降 |\n| SM4 | ✔ |  | 走 hardware 更快，ECB 约 42 MB/s，CTR 约 11 MB/s；CTR 明显慢于 ECB |\n| RSA-2048/3072/4096 |  | ✔ | 走 OTBN 提升最大，public 约 1667 ops/s，是 software 的 12 倍；private 约 14.9 ops/s，是 software 的 6.5 倍；公钥指数 F4 有额外优化 |\n| ECDSA P-256/P-384 |  | ✔ | 走 OTBN 远快于 software；P-256 verify 约 714 ops/s，P-384 verify 约 263 ops/s |\n| SM2 |  | ✔ | 走 OTBN 远快于 software；verify 约 178 ops/s |\n\n### 5. 通用建议\n\n1. **对齐影响性能，不影响正确性**：SHA-256 32 B 对齐、SHA-512 4 B 对齐时硬件 DMA 路径才能跑满，非对齐时结果仍然正确，但吞吐会明显下降（SHA-256 从 ~73 MB/s 降到 ~12 MB/s，SHA-512 从 ~318 MB/s 降到 ~30 MB/s）。\n2. **尽量减少 update 次数**：大 buffer 单次 update 比多次小 update 吞吐高一个数量级。\n3. **优先在 RAM 中运行**：XIP 对小 chunk、哈希、RSA private 都有明显惩罚。\n4. **OTBN 不是万能**：对 SHA-256/SM3 等已有专用 hardware 的算法，OTBN 反而更慢；OTBN 应留给 RSA/ECDSA/SM2 等非对称算法。\n5. **实际业务吞吐通常远低于峰值**：如果业务天然是小 chunk、随机对齐、频繁 update，请参考 `mbedtls_hash` 综合测试的平均值，而非本表中的 BULK 峰值。\n\n> 注：使用 mbed TLS 的 alt 实现时，需关闭 Zephyr crypto driver，否则会与 mbed TLS 的硬件替换冲突。当前 Zephyr crypto driver 已停止维护，除硬件 hash 外不建议使用；若业务直接使用 hardware hash driver，则 mbed TLS 侧不要再开启对应的硬件 hash alt，否则同一硬件资源会产生冲突。\n\n> 注：测试结果在 QSH 的 CPU1 平台上获得，不同 CPU 之间相同硬件加速单元的性能差异可以忽略。\n"""

    out = ROOT / "qsh_crypto_throughput.md"
    out.write_text(md, encoding="utf-8")
    print(f"Generated {out}")


if __name__ == "__main__":
    main()
