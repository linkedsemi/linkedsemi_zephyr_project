# SPDX-License-Identifier: Apache-2.0
#
# SPDM 响应端例程。本板是 USB gadget + SPDM responder。
# 主机侧（PC 是 SPDM 请求方）在本目录执行：
#   python usb_host_tester_windows.py
#   python3 usb_host_tester_linux.py

cmake_minimum_required(VERSION 3.20.0)

# libspdm 胶水在同级 spdm_modules（不再放进 zephyr/modules）。
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../spdm_modules)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mctp_spdm_usb)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_include_directories(app PRIVATE certs)
target_sources(app PRIVATE src/main.c src/spdm_secret.c)
