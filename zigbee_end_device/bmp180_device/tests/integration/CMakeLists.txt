cmake_minimum_required(VERSION 3.20.0)

set(BOARD_FLASH_RUNNER nrfutil)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bmp180_integration_tests)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE
    ${app_sources}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bmp_180.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bmp180_math.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
