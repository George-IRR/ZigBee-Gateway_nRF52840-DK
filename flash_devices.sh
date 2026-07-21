#!/bin/bash
# flash_devices.sh — Build and flash End Device / Coordinator
# Usage:
#   ./flash_devices.sh <target> [--dev|--prod]
#
#   target:  switch | coord | all
#   mode:    --dev   → CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=y  (default, auto-join with static key)
#            --prod  → CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n  (manual UART pairing required)
#
# Examples:
#   ./flash_devices.sh all           # build+flash both in dev mode
#   ./flash_devices.sh all --prod    # build+flash both in production mode
#   ./flash_devices.sh coord --prod  # build+flash only coordinator in prod mode
#   ./flash_devices.sh switch --dev  # build+flash only end device in dev mode

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Load config.env ────────────────────────────────────────────────────────────
if [ -f "$SCRIPT_DIR/config.env" ]; then
    source "$SCRIPT_DIR/config.env"
else
    echo "Warning: config.env not found! Using hardcoded defaults."
    SWITCH_DEV_ID="1050247285"
    COORD_DEV_ID="1050246989"
    NCS_TOOLCHAIN_DIR="/home/george/ncs/toolchains/b77d8c1312"
    NCS_VERSION_DIR="/home/george/ncs/v2.9.2"
fi

# ── Toolchain env ──────────────────────────────────────────────────────────────
TC_DIR="$NCS_TOOLCHAIN_DIR"
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

NCS_DIR="${NCS_VERSION_DIR:-/home/george/ncs/v2.9.2}"

SWITCH_SRC="$SCRIPT_DIR/zigbee_end_device/bmp180_device"
COORD_SRC="$SCRIPT_DIR/zigbee_network/network_coordinator"
SWITCH_BUILD="$SWITCH_SRC/build"
COORD_BUILD="$COORD_SRC/build"

# ── Parse arguments ────────────────────────────────────────────────────────────
TARGET="$1"
MODE_FLAG="--dev"
PERSIST_FLAG=""
ERASE_FLAG=""

shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prod|--production)
            MODE_FLAG="--prod"
            ;;
        --dev|--development)
            MODE_FLAG="--dev"
            ;;
        --persist)
            PERSIST_FLAG="--persist"
            ;;
        --factory-reset|--factory_reset)
            ERASE_FLAG="--erase"
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 {switch|coord|all} [--dev|--prod] [--persist] [--factory-reset]"
            exit 1
            ;;
    esac
    shift
done

case "$MODE_FLAG" in
    --prod)
        SECURITY_KCONFIG="-DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n"
        MODE_LABEL="PRODUCTION (manual UART pairing)"
        ;;
    --dev)
        SECURITY_KCONFIG="-DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=y"
        MODE_LABEL="DEVELOPMENT (auto-join with static key)"
        ;;
esac

if [ "$PERSIST_FLAG" = "--persist" ]; then
    PERSIST_KCONFIG="-DCONFIG_ZIGBEE_RESET_PERSISTENT_CONFIG=n"
    PERSIST_LABEL="PERSISTENT"
else
    PERSIST_KCONFIG="-DCONFIG_ZIGBEE_RESET_PERSISTENT_CONFIG=y"
    PERSIST_LABEL="VOLATILE (resets NVRAM on boot)"
fi

ERASE_LABEL="NORMAL"
if [ "$ERASE_FLAG" = "--erase" ]; then
    ERASE_LABEL="MASS ERASE (factory reset flash before load)"
fi

echo ""
echo "=========================================================="
echo "  Mode: $MODE_LABEL"
echo "  Storage: $PERSIST_LABEL"
echo "  Flash action: $ERASE_LABEL"
echo "=========================================================="
echo ""

if [ "$MODE_FLAG" = "--prod" ] && [ "$PERSIST_FLAG" != "--persist" ]; then
    echo "WARNING: --persist flag was NOT provided."
    echo "   The End Device NVRAM will be cleared on every reset/power cycle,"
    echo "   requiring you to perform the manual pairing sequence again!"
    echo ""
fi

# ── Build + Flash functions ────────────────────────────────────────────────────
build_switch() {
    echo "[INFO] Building End Device ($MODE_LABEL, $PERSIST_LABEL)..."
    cd "$NCS_DIR"
    west build -b nrf52840dk/nrf52840 \
        -s "$SWITCH_SRC" \
        -d "$SWITCH_BUILD" \
        -- "$SECURITY_KCONFIG" "$PERSIST_KCONFIG"
    echo "[SUCCESS] End Device built."
}

flash_switch() {
    echo "[INFO] Flashing End Device (ID: $SWITCH_DEV_ID)..."
    west flash -d "$SWITCH_BUILD" --domain bmp180_device \
        --dev-id "$SWITCH_DEV_ID" --runner nrfutil $ERASE_FLAG
    echo "[SUCCESS] End Device flashed."
}

build_coord() {
    echo "[INFO] Building Coordinator ($MODE_LABEL)..."
    cd "$NCS_DIR"
    west build -b nrf52840dk/nrf52840 \
        -s "$COORD_SRC" \
        -d "$COORD_BUILD" \
        -- "$SECURITY_KCONFIG"
    echo "[SUCCESS] Coordinator built."
}

flash_coord() {
    echo "[INFO] Flashing Coordinator (ID: $COORD_DEV_ID)..."
    west flash -d "$COORD_BUILD" --domain network_coordinator \
        --dev-id "$COORD_DEV_ID" --runner nrfutil $ERASE_FLAG
    echo "[SUCCESS] Coordinator flashed."
}

# ── Production mode reminder ───────────────────────────────────────────────────
prod_reminder() {
    if [ "$MODE_FLAG" = "--prod" ] || [ "$MODE_FLAG" = "--production" ]; then
        echo ""
        echo "=========================================================="
        echo "  PRODUCTION MODE -- Manual Pairing Required              "
        echo "                                                          "
        echo "  1. Read End Device IEEE address from its log            "
        echo "  2. Coordinator (/dev/ttyACM0):                          "
        echo "       ic_add <IEEE_HEX> <IC_36HEX>                       "
        echo "     -> responds: ic_add_success + steering_started       "
        echo "  3. End Device (/dev/ttyACM2):                           "
        echo "       ic_set <IEEE_HEX> <IC_36HEX>                       "
        echo "     -> responds: ic_set_success                          "
        echo "  4. End Device: join                                     "
        echo "     -> responds: Joined network successfully             "
        echo "                                                          "
        echo "  Full guide: Docs/production_pairing_guide.md            "
        echo "=========================================================="
        echo ""
    fi
}

# ── Main dispatch ──────────────────────────────────────────────────────────────
case "$TARGET" in
    switch|end_device)
        build_switch
        flash_switch
        prod_reminder
        ;;
    coord|coordinator)
        build_coord
        flash_coord
        prod_reminder
        ;;
    all)
        # Build sequentially (west is not parallel-safe), flash in parallel
        build_coord
        build_switch
        flash_coord &
        flash_switch &
        wait
        prod_reminder
        ;;
    *)
        echo "Usage: $0 {switch|coord|all} [--dev|--prod] [--persist] [--factory-reset]"
        echo ""
        echo "  Targets:"
        echo "    switch      Flash the End Device (BMP180 sensor)"
        echo "    coord       Flash the Network Coordinator"
        echo "    all         Build and flash both devices"
        echo ""
        echo "  Modes:"
        echo "    --dev       Development mode: auto-join with static key (default)"
        echo "    --prod      Production mode: manual UART pairing required"
        echo ""
        echo "  Options:"
        echo "    --persist   Enable NVRAM persistence (do not erase network config on boot)"
        echo "    --factory-reset  Force flash erase (clean NVRAM settings before flashing)"
        echo ""
        echo "Examples:"
        echo "  ./flash_devices.sh all           # dev mode (default)"
        echo "  ./flash_devices.sh all --prod    # production mode (volatile)"
        echo "  ./flash_devices.sh all --prod --persist # production mode (persistent)"
        echo "  ./flash_devices.sh all --prod --persist --factory-reset # clean settings, then persistent"
        exit 1
        ;;
esac