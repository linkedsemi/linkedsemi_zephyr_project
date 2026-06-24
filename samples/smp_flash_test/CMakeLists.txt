# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# 显式指定 tracing UART overlay，避免 west build 工作目录不同导致自动加载失败
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/lsqsh_evb_smp_smp_xip.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smp_flash_test)

zephyr_include_directories(include)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
