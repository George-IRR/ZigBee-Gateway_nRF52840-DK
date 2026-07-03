#!/bin/bash

# Absolute paths to build directories
SWITCH_BUILD="/home/george/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_end_device/bmp180_device/build"
COORD_BUILD="/home/george/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_network/network_coordinator/build"

# Device IDs
COORD_DEV_ID="1050246989"
SWITCH_DEV_ID="1050247285"

# Set up toolchain environment variables if nRF Connect toolchain is installed
TC_DIR="/home/george/ncs/toolchains/b77d8c1312"
if [ -d "$TC_DIR" ]; then
    export PATH="$TC_DIR/bin:$TC_DIR/usr/bin:$TC_DIR/usr/local/bin:$TC_DIR/opt/bin:$TC_DIR/opt/nanopb/generator-bin:$TC_DIR/opt/zephyr-sdk/arm-zephyr-eabi/bin:$TC_DIR/opt/zephyr-sdk/riscv64-zephyr-elf/bin:$PATH"
    export LD_LIBRARY_PATH="$TC_DIR/lib:$TC_DIR/lib/x86_64-linux-gnu:$TC_DIR/usr/local/lib:$LD_LIBRARY_PATH"
    export GIT_EXEC_PATH="$TC_DIR/usr/local/libexec/git-core"
    export GIT_TEMPLATE_DIR="$TC_DIR/usr/local/share/git-core/templates"
    export PYTHONHOME="$TC_DIR/usr/local"
    export PYTHONPATH="$TC_DIR/usr/local/lib/python3.12:$TC_DIR/usr/local/lib/python3.12/site-packages"
    export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
    export ZEPHYR_SDK_INSTALL_DIR="$TC_DIR/opt/zephyr-sdk"
fi

flash_switch() {
    echo "Flashing Light Switch (End Device)..."
    west flash -d "$SWITCH_BUILD" --domain bmp180_device --dev-id "$SWITCH_DEV_ID"
}

flash_coordinator() {
    echo "Flashing Network Coordinator..."
    west flash -d "$COORD_BUILD" --domain network_coordinator --dev-id "$COORD_DEV_ID"
}

case "$1" in
    switch|end_device)
        flash_switch
        ;;
    coord|coordinator)
        flash_coordinator
        ;;
    all)
        flash_coordinator &
        flash_switch &
        wait
        ;;
    *)
        echo "Usage: $0 {switch|coord|all}"
        echo "  switch      - Flash the Light Switch end device"
        echo "  coord       - Flash the Network Coordinator"
        echo "  all         - Flash both devices in parallel"
        exit 1
        ;;
esac