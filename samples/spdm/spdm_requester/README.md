# SPDM over MCTP USB 请求方

板端是 SPDM 1.3 请求方（USB 仍是 gadget）。PC 跑 Python 响应方来接它。

## 这是干什么的

USB 角色和 `spdm_response` **一样**，都是 gadget（VID `2FE3` PID `0001`）。
SPDM 角色反过来：本例程里 **板子是请求方**。USB 配置完成后，板子主动发
`GET_VERSION`，一路跑到拆会话。PC 上的 Python 脚本当响应方。

不要和 `spdm_response` 搞混：

- `spdm_response`：板子等 PC 来问（设备 = 响应方，PC = 请求方）。
- 本例程：板子主动去问 PC（设备 = 请求方，PC = 响应方）。

对端例程见 [spdm_response](../spdm_response/README.md)。

协商版本是 **SPDM 1.3**。板子会依次做：

- `GET_VERSION` / `GET_CAPABILITIES` / `NEGOTIATE_ALGORITHMS`
- `GET_DIGESTS` / `GET_CERTIFICATE` / `CHALLENGE`（用 `certs/` 里编进固件的 CA 验 PC 的证书链）
- `GET_KEY_PAIR_INFO` / `SET_KEY_PAIR_INFO` CHANGE（保持 slot 0 绑定）
- `KEY_EXCHANGE` / `FINISH`（`EVENT_ALL_POLICY`）
- `GET_SUPPORTED_EVENT_TYPES` / `SUBSCRIBE_EVENT_TYPES`
- 加密通道里 `VENDOR_DEFINED`：板子发 `1111`，PC 回 `2222`（3 轮）
- 退订，然后 `END_SESSION`

## 依赖

- 板子要有 USB device。
- west 工作区里要有 DMTF libspdm：`modules/lib/libspdm`。
- 主机在本目录安装 Python 依赖：`pip install -r requirements.txt`。

## 怎么跑主机测试

PC 上的脚本是 SPDM **响应方**。请 **先插板或复位，然后5s内，跑pc上面的脚本**。
固件在 USB 配置完成后会再等约 5 秒才开始 `GET_VERSION`，给主机留连接时间。

Windows 先用 Zadig 把设备绑成 **WinUSB**（VID `2FE3` PID `0001`）：

```bash
python usb_host_tester_windows.py
```

Linux / WSL：

```bash
python3 usb_host_tester_linux.py
```

每个系统只有一个脚本，协议和 USB 后端都写在里面，没有 `--role`。
角色由目录决定：跑本目录的脚本，PC 就是响应方。

## 编译

在 west 工作区根目录执行：

```bash
west build -p always -b lsqsh_evb@1os/lsqsh/cpu1 linkedsemi_zephyr_project/samples/spdm/spdm_requester
```

## 补充

- 本例程会链接 `../spdm_response/src/spdm_secret.c`。共享的 libspdm 同时编了 requester 和 responder 库，链接时必须提供事件 / 密钥对 / 签名这些 HAL 符号，否则会缺符号。请求方运行时几乎走不到签名回调。
- 对端信任锚是 `certs/certs_generated.h`，和响应方例程同一套 CA。在任一端跑 `certs/gen_certs.py` 都会把 `certs_generated.h` 同步到另一端，然后两边固件都要重编。
- MCTP：板子 EID 10，PC EID 20。USB VID/PID 仍是 `2FE3:0001`。

## 参考

- [SPDM DSP0274](https://www.dmtf.org/dsp/DSP0274)
- [SPDM over MCTP DSP0275](https://www.dmtf.org/dsp/DSP0275)
- [libspdm](https://github.com/DMTF/libspdm)
