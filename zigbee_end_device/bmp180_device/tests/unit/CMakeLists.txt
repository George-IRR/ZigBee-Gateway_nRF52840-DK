cmake_minimum_required(VERSION 3.20.0)

set(BOARD_FLASH_RUNNER nrfutil)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bmp180_unit_tests)

target_sources(testbinary PRIVATE
    src/test_bmp180_math.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/bmp180_math.c
)

target_include_directories(testbinary PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
