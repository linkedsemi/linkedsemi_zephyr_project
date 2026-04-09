#!/usr/bin/env python3
import hid
import time
import sys

# 设备 VID/PID
VENDOR_ID = 0x2fe3
PRODUCT_ID = 0x0007

# MCP2221 命令常量
MCP2221_I2C_WR_DATA = 0x90
MCP2221_I2C_RD_DATA = 0x91
MCP2221_I2C_GET_DATA = 0x40
MCP2221_I2C_PARAM_OR_STATUS = 0x10

def send_hid_report(dev, data):
    """
    发送 HID 报告：解决跨平台长度不一致及 Linux 短包问题
    """
    target_len = 64
    # 强制构造一个 64 字节的 bytearray
    full_data = bytearray(target_len)
    
    # 将输入数据拷贝到这 64 字节的前部
    # 这样就算你只传 [0x40]，后面也会自动补 63 个 0x00
    for i in range(min(len(data), target_len)):
        full_data[i] = data[i]

    if sys.platform == "win32":
        # Windows 需要额外的 0x00 Report ID 占位符
        # 实际发往总线的依然是这 64 字节
        report = b'\x00' + full_data
    else:
        # Linux / macOS 直接发送这 64 字节
        report = full_data

    # 打印实际发送长度，确保在 Linux 上也是 64
    # print(f"DEBUG: Platform {sys.platform}, Sending len: {len(report)}")
    dev.write(report)

def read_hid_report(dev, size=64):
    """读取 HID 报告"""
    return bytearray(dev.read(size))

def mcp_send_report_with_status_check(dev, txbuf):
    """发送命令并立即检查状态（模拟原始代码的行为）"""
    # 发送主命令
    send_hid_report(dev, txbuf)
    rx = read_hid_report(dev)
    print(f"  Main cmd response: {[hex(b) for b in rx[:10]]}")
    
    # 立即发送0x10检查状态
    status_tx = bytearray(64)
    status_tx[0] = MCP2221_I2C_PARAM_OR_STATUS
    send_hid_report(dev, status_tx)
    status_rx = read_hid_report(dev)
    print(f"  Status check (0x10): {[hex(b) for b in status_rx[:10]]}")
    
    return rx, status_rx

def main():
    print(f"Trying to open device: VID={VENDOR_ID:04x}, PID={PRODUCT_ID:04x}")
    try:
        dev = hid.device()
        dev.open(VENDOR_ID, PRODUCT_ID)
        print("Device opened successfully.")
    except IOError as e:
        print("Failed to open device:", e)
        return

    try:
        # Step 1: 发送 SMBus Block Write
        print("\n[1] Sending SMBus Block Write (cmd=0x02)...")
        write_data = bytes([0xb8, 0x22, 0x00, 0x4c, 0xa5, 0x00, 0x01, 0x00])
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_WR_DATA
        tx[1] = len(write_data) + 1
        tx[2] = 0
        tx[3] = (0x68 << 1) | 0
        tx[4] = 0x02
        tx[5:5+len(write_data)] = write_data

        rx, status_rx = mcp_send_report_with_status_check(dev, tx[:5+len(write_data)])

        time.sleep(0.01)

        # Step 2: 重复写操作（模拟原始代码的行为）
        print("\n[2] Repeating write command...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_WR_DATA
        tx[1] = len(write_data) + 1
        tx[2] = 0
        tx[3] = (0x68 << 1) | 0
        tx[4] = 0x02
        tx[5:5+len(write_data)] = write_data
        rx, status_rx = mcp_send_report_with_status_check(dev, tx[:5+len(write_data)])

        time.sleep(0.01)

        # Step 3: 发送读命令
        print("\n[3] Sending SMBus Read Request (0x91)...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_RD_DATA
        tx[1] = 1
        tx[2] = 0
        tx[3] = (0x68 << 1) | 1
        tx[4] = 0x01
        rx, status_rx = mcp_send_report_with_status_check(dev, tx[:5])

        time.sleep(0.01)

        # Step 4: 发送状态检查命令（0x10），这是关键！
        print("\n[4] Explicit status check (0x10)...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_PARAM_OR_STATUS
        send_hid_report(dev, tx)
        rx = read_hid_report(dev)
        print(f"  Status response: {[hex(b) for b in rx[:10]]}")

        time.sleep(0.01)

        # Step 5: 获取数据 (0x40)
        print("\n[5] Fetching data via 0x40...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_GET_DATA
        send_hid_report(dev, tx[:1])
        rx = read_hid_report(dev)
        print(f"  ← Full HID report: {[hex(b) for b in rx[:15]]}")

        # Step 6: 再次发送读命令（模拟原始代码的重复操作）
        print("\n[6] Repeating read command...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_RD_DATA
        tx[1] = 1
        tx[2] = 0
        tx[3] = (0x68 << 1) | 1
        tx[4] = 0x01
        rx, status_rx = mcp_send_report_with_status_check(dev, tx[:5])

        time.sleep(0.01)

        # Step 7: 再次发送状态检查
        print("\n[7] Final status check (0x10)...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_PARAM_OR_STATUS
        send_hid_report(dev, tx)
        rx = read_hid_report(dev)
        print(f"  Final status: {[hex(b) for b in rx[:10]]}")

        time.sleep(0.01)

        # Step 8: 最终获取数据 (0x40)
        print("\n[8] Final data fetch (0x40)...")
        tx = bytearray(64)
        tx[0] = MCP2221_I2C_GET_DATA
        send_hid_report(dev, tx[:1])
        rx = read_hid_report(dev)
        print(f"  ← Final data: {[hex(b) for b in rx[:15]]}")

        # 关键：在成功获取数据后，尝试在异常状态下重复发送命令
        print("\n[9] Now trying to trigger error by sending rapid commands...")
        for i in range(10):
            # 发送0x40命令
            tx = bytearray(64)
            tx[0] = MCP2221_I2C_GET_DATA
            send_hid_report(dev, tx[:1])
            rx = read_hid_report(dev)
            
            # 立即发送0x10状态命令
            tx = bytearray(64)
            tx[0] = MCP2221_I2C_PARAM_OR_STATUS
            send_hid_report(dev, tx[:1])
            status_rx = read_hid_report(dev)
            
            print(f"    Iteration {i+1}: 0x40 -> {[hex(b) for b in rx[:5]]}, 0x10 -> {[hex(b) for b in status_rx[:5]]}")
            
            time.sleep(0.001)  # Very short delay to stress test

    finally:
        dev.close()

if __name__ == "__main__":
    main()