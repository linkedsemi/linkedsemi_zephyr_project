cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(exception_test)

# 关键：使用 LINKER 选项强制放在最后
# 或者修改 zephyr 目标的编译选项

# 方法1：移除 zephyr 添加的 -fno
#get_target_property(z_flags zephyr INTERFACE_COMPILE_OPTIONS)
#string(REPLACE "-fno-asynchronous-unwind-tables" "" z_flags "${z_flags}")
#set_target_properties(zephyr PROPERTIES INTERFACE_COMPILE_OPTIONS "${z_flags}")

# 方法2：对 app 目标，使用 SHELL 强制顺序
#target_compile_options(app PRIVATE 
#    "SHELL:-fexceptions -fno-omit-frame-pointer"
#    "SHELL:-fasynchronous-unwind-tables"
#)
target_compile_options(app PRIVATE -fexceptions -fno-omit-frame-pointer)
target_link_options(app PRIVATE -fexceptions)
## 方法3：链接阶段强制
#target_link_options(app PRIVATE 
#    "SHELL:-Wl,--no-warn-execstack"
#    -fexceptions
#)
zephyr_link_libraries(
  -march=rv32imafdc_zifencei_xtheade
  -mabi=ilp32d
)

zephyr_link_libraries(stdc++)

target_sources(app PRIVATE src/main.cpp)