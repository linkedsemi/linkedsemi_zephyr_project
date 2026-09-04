# SPDM over MCTP USB 响应方

板端是 SPDM 1.3 响应方（USB gadget + MCTP 端点）。PC 跑 Python 请求方来测它。

## 这是干什么的

烧到板上之后，板子以 **USB 设备（gadget）** 枚举，同时作为 **SPDM 响应方**：
PC 发 `GET_VERSION` 等请求，板子用 libspdm 回答。

不要和 `spdm_requester` 搞混：

- 两个例程的 USB 角色一样，都是 gadget（VID `2FE3` PID `0001`）。
- 差别只在 SPDM 角色。本例程是 **设备当响应方**；对端例程是设备当请求方。

协议栈：USB → MCTP（本端 EID 10，主机 EID 20）→ libspdm 响应方。
消息类型 `0x05`（明文）/ `0x06`（会话加密）。版本 **SPDM 1.3**。
密码学用 Zephyr mbedtls（`cryptlib_mbedtls`）。度量（MEASUREMENTS）未打开。

设备当请求方的对应例程见 [spdm_requester](../spdm_requester/README.md)。

## 握手会跑哪些命令

板子会应答：

- `GET_VERSION` / `GET_CAPABILITIES` / `NEGOTIATE_ALGORITHMS`
- `GET_DIGESTS`（slot 0；SPDM 1.3 多密钥会带 KeyPairID / CertInfo / KeyUsage）
- `GET_CERTIFICATE`（slot 0，ECDSA P-256 + SHA-256）
- `CHALLENGE`（设备私钥签 transcript，含 8 字节 requester context）
- `GET_KEY_PAIR_INFO` / `SET_KEY_PAIR_INFO`（密钥对 1 绑到 slot 0）
- `KEY_EXCHANGE` / `FINISH`（ECDHE P-256，AES-128-GCM，握手明文，`EVENT_ALL_POLICY`）
- 会话内：`GET_SUPPORTED_EVENT_TYPES` / `SUBSCRIBE_EVENT_TYPES`
- 加密的 `VENDOR_DEFINED`：主机发 ASCII `1111`，板子回 `2222`，然后 `END_SESSION`
- 再连一次时，若 `GET_DIGESTS` 哈希和缓存一致，会跳过证书/挑战，直接 KEY_EX

## 证书

密钥和主机脚本都在本例程目录里，不要再到 `tool_hu` 去找。

- **固件**编译 `certs/certs_generated.h`（SPDM 证书链 + 设备私钥 PEM）。
- **主机脚本**（PC 是 SPDM 请求方）只用 `certs/ca.cert.pem` 验板子的证书。设备私钥只留在固件镜像里。

重新生成证书后必须重编固件，否则板子和主机 CA 对不上：

```bash
python certs/gen_certs.py
```

## 依赖

- 板子要有 USB device。
- west 工作区里要有 DMTF libspdm：`modules/lib/libspdm`（没有就 `west update libspdm`）。
- 主机在本目录安装 Python 依赖：`pip install -r requirements.txt`。

## 怎么跑主机测试

先烧本例程，插上 USB。Windows 还要用 Zadig 把设备绑成 **WinUSB**
（List All Devices，VID `2FE3` PID `0001`），装完拔掉再插一次。

Windows：

```bash
python usb_host_tester_windows.py
```

Linux / WSL：

```bash
python3 usb_host_tester_linux.py
```

每个系统只有一个脚本，协议和 USB 后端都写在里面，没有 `--role`。
角色由目录决定：跑本目录的脚本，PC 就是请求方。

## 编译

在 west 工作区根目录执行：

```bash
west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 linkedsemi_zephyr_project/samples/spdm/spdm_response
```

## SPDM 1.3 多密钥

能力位 `MULTI_KEY_CAP_NEG`，协商参数里再开 `MULTI_KEY_CONN`。
之后 `GET_DIGESTS` 在 slot 0 的 32 字节哈希后面会带：KeyPairID=1、
DeviceCert 模型、KEY_EX + CHALLENGE 用途。`GET/SET_KEY_PAIR_INFO`
用来查询并保持和 slot 0 的绑定。

## 事件订阅

能力位 `EVENT_CAP`。`KEY_EXCHANGE` 带 `EVENT_ALL_POLICY` 时，
响应方会先订阅全部事件。主机再在 AES-128-GCM 会话里发
`GET_SUPPORTED_EVENT_TYPES` / `SUBSCRIBE_EVENT_TYPES`。
DMTF 组里有 EventLost、CertificateChanged。`END_SESSION` 前先退订。

## 会话（KEY_EX）

已打开。能力：`KEY_EX_CAP | ENCRYPT_CAP | MAC_CAP | HANDSHAKE_IN_THE_CLEAR_CAP`。
算法：DHE secp256r1、AES-128-GCM、SPDM HMAC 密钥派生、OpaqueDataFormat1。
HKDF 标签前缀是 `spdm1.3 `（注意末尾空格）。

流程：

1. 证书 / 挑战 / 密钥对走完后，主机发 `KEY_EXCHANGE`，带临时 P-256 公钥（64 字节 X||Y）。板子回自己的临时公钥，并对 transcript 哈希 TH 做 ECDSA 签名。
2. 两边用 ECDH 共享秘密做 HKDF-Extract，再 Expand 出 `req hs data` / `rsp hs data` / `finished` 密钥。
3. 主机发明文 `FINISH`（用 request finished key 做 HMAC）。板子回 `FINISH_RSP`。之后应用消息走 MCTP type `0x06`（AES-128-GCM）。
4. 主机订阅事件，然后每秒发一次 `VENDOR_DEFINED_REQUEST`，载荷 ASCII `1111`，共 10 秒；板子回 `2222`。
5. 主机退订，再发加密的 `END_SESSION`。
6. 主机再发 `GET_DIGESTS`。slot 0 哈希若和缓存一致，就跳过 `GET_CERTIFICATE` / `CHALLENGE`，直接再跑一轮 KEY_EX。

USB-MCTP 单包最长 255 字节（1 字节长度字段已经把 4 字节 USB 头算进去了）。
本例程把 libmctp `pkt_size` 收到 251，大块 `GET_CERTIFICATE` 会分片，而不是发送失败。

## 接线

主机和板子之间接一根 USB 线即可。

## 参考

- [SPDM DSP0274](https://www.dmtf.org/dsp/DSP0274)
- [SPDM over MCTP DSP0275](https://www.dmtf.org/dsp/DSP0275)
- [libspdm](https://github.com/DMTF/libspdm)
