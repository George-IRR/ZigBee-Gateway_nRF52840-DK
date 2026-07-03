cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bmp180_e2e_tests)

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/my_nrf52_timer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bmp_180.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bmp180_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/my_rtc.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
