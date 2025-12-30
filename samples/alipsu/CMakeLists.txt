cmake_minimum_required(VERSION 3.22)

# 添加这行来指定board overlay文件
set(DTC_OVERLAY_FILE boards/alipsu.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(alipsu_app)

# 添加PMBus/Alipsu库目录到包含路径
include_directories($ENV{ZEPHYR_BASE}/drivers/sensor/ls_pmbus)
include_directories($ENV{ZEPHYR_BASE}/drivers/sensor/alipsu)

# 添加应用程序源文件
FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# 确保设备树生成正确
zephyr_library_include_directories(${ZEPHYR_BASE}/include)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/sensor/alipsu)

# 添加依赖关系，确保PMBus库在MP5023之前编译
add_dependencies(app zephyr_interface)