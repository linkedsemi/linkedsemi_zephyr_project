# SPDX-License-Identifier: Apache-2.0
#
# SPDM 请求端例程。USB 仍是 gadget，SPDM 角色是 requester。
# 主机侧要先跑本目录的 Python 响应端：
#   python usb_host_tester_windows.py
#   python3 usb_host_tester_linux.py

cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../spdm_modules)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mctp_spdm_usb_requester)

include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_include_directories(app PRIVATE certs)
target_sources(app PRIVATE
  src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../spdm_response/src/spdm_secret.c
)
