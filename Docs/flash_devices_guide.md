# Flash Devices Script Guide (`flash_devices.sh`)

## Overview

The `flash_devices.sh` shell script automates building and flashing for the Zigbee End Device (BMP180 sensor node) and Network Coordinator on nRF52840-DK hardware targets using the Nordic Connect SDK (NCS) v2.9.2 toolchain and `nrfutil` runner.

It provides centralized execution for configuring build-time security flags, NVRAM persistent storage settings, mass erase options, and target hardware selection.

---

## Configuration (`config.env`)

Before running `flash_devices.sh`, ensure the root `config.env` file contains your environment settings:

```env
# Hardware Device IDs (nRF52840-DK J-Link Serial Numbers)
SWITCH_DEV_ID=1050247285
COORD_DEV_ID=1050246989

# Path to the nRF Connect SDK Toolchain and Workspace
NCS_TOOLCHAIN_DIR=/home/george/ncs/toolchains/b77d8c1312
NCS_VERSION_DIR=/home/george/ncs/v2.9.2
```

The script automatically detects and imports `config.env` to set up environment variables and `PATH` exports required by Zephyr toolchain components.

---

## Command Syntax

```bash
./flash_devices.sh <target> [security_mode] [--persist] [--factory-reset]
```

### 1. Targets (`<target>`)

| Target Name | Aliases | Description |
|---|---|---|
| `switch` | `end_device` | Builds and flashes only the End Device (`zigbee_end_device/bmp180_device`) |
| `coord` | `coordinator` | Builds and flashes only the Network Coordinator (`zigbee_network/network_coordinator`) |
| `all` | - | Builds both target binaries sequentially, then flashes both boards in parallel |

### 2. Security Mode Flags

| Flag | Kconfig Parameter | Default | Description |
|---|---|---|---|
| `--dev`, `--development` | `-DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=y` | Yes | **Development Mode**: Auto-joins network using static pre-shared link key. |
| `--prod`, `--production` | `-DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n` | No | **Production Mode**: Mandatory manual UART pairing using Install Codes. |

### 3. NVRAM Persistence Options

| Flag | Kconfig Parameter | Default | Description |
|---|---|---|---|
| *(omitted)* | `-DCONFIG_ZIGBEE_RESET_PERSISTENT_CONFIG=y` | Yes | **Volatile Storage**: Erases network configuration from NVRAM on boot. Re-pairing is required on reboot. |
| `--persist` | `-DCONFIG_ZIGBEE_RESET_PERSISTENT_CONFIG=n` | No | **Persistent Storage**: Retains network keys and PAN credentials in NVRAM across power cycles and reboots. |

### 4. Flash Erase Options

| Flag | Flasher Runner Flag | Description |
|---|---|---|
| *(omitted)* | *(none)* | Standard flash update without mass erase. |
| `--factory-reset`, `--factory_reset` | `--erase` | Performs mass flash erase before writing firmware. Useful for clearing corrupt NVRAM states. |

---

## Common Usage Examples

### 1. Standard Development Flashing
Builds and flashes both devices in development security mode:
```bash
./flash_devices.sh all
```

### 2. Volatile Production Flashing
Builds and flashes both devices in production security mode without key persistence:
```bash
./flash_devices.sh all --prod
```

### 3. Persistent Production Flashing (Recommended Production Setup)
Builds and flashes both devices in production security mode, storing paired network credentials across reboots:
```bash
./flash_devices.sh all --prod --persist
```

### 4. Clean Factory Reset + Persistent Production Flashing
Performs a full chip erase, then flashes persistent production firmware:
```bash
./flash_devices.sh all --prod --persist --factory-reset
```

### 5. Flashing Individual Target
Flashes only the End Device in persistent production mode:
```bash
./flash_devices.sh switch --prod --persist
```

---

## Internal Workflow & Parallel Flashing

1. **Environment Setup**: Exports Toolchain binaries (`arm-zephyr-eabi-gcc`, `west`, `python3`) and SDK directories.
2. **Sequential Compilation**: Executes `west build` for Coordinator first, then End Device. Sequential build is required because `west` build artifacts share workspace paths.
3. **Parallel Board Flashing**: When target `all` is selected, `west flash` invocations run concurrently in background background threads using `nrfutil`, targeting hardware serial IDs `$COORD_DEV_ID` and `$SWITCH_DEV_ID` simultaneously.
4. **Post-Flash Instructions**: Outputs UART pairing steps if `--prod` mode is selected.

---

## Related Documentation

- [Production Mode Pairing Guide](production_pairing_guide.md)
- [Testing Strategy and Execution Guide](testing_guide.md)
- [Main Repository README](../README.md)
