cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(exception_test)


target_compile_options(app PRIVATE -fexceptions -fno-omit-frame-pointer)
target_link_options(app PRIVATE -fexceptions)


zephyr_link_libraries(stdc++)

target_sources(app PRIVATE src/main.cpp)
