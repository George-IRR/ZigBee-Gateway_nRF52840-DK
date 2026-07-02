cmake_minimum_required(VERSION 3.20.0)

set(BOARD_FLASH_RUNNER nrfutil)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(bmp180_device)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/my_nrf52_timer.c
  src/bmp_180.c
  src/my_rtc.c
)

target_include_directories(app PRIVATE include)
# NORDIC SDK APP END