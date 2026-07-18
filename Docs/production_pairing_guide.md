# Production Mode — Manual Pairing Guide
## (`CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n`)

This guide is for **production builds** where `CONFIG_ZIGBEE_DEVELOPMENT_SECURITY` is disabled.
In this mode no keys are pre-loaded into the firmware — each device must be paired manually via UART.

> [!IMPORTANT]
> Both Coordinator and End Device must be built and flashed with `CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n`
> (set `CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n` in `prj.conf` for both modules).

---

## Step-by-step Pairing Flow

```
[PC / Operator]         [Coordinator /dev/ttyACM0]    [End Device /dev/ttyACM2]
      |                           |                           |
      |--- flash coord ---------->|                           |
      |--- flash end device --------------------------------->|
      |                           |                           |
      | [Coordinator boots]       |                           |
      | Read coordinator log      |                           |
      | "Production mode: waiting for Install Code"           |
      |                           |                           |
      | [End Device boots]        |                           |
      | Read IEEE address from    |                           |
      | end device log:           |                           |
      | "Device IEEE Address: f4ce369e3312a7ec"               |
      |                           |                           |
      | [Generate Install Code]                               |
      | 16 random bytes + CRC-16/X-25 (little-endian)        |
      | Example: 9d2247614fc83712f89147fb5f1298bd520d        |
      |                           |                           |
      | Send to Coordinator:      |                           |
      |  ic_add f4ce369e3312a7ec 9d2247614fc83712f89147fb5f1298bd520d
      |                           | -> "ic_add_success: f4ce369e3312a7ec"
      |                           | -> "steering_started"     |
      |                           |                           |
      | Send to End Device:                                   |
      |  ic_set f4ce369e3312a7ec 9d2247614fc83712f89147fb5f1298bd520d
      |                           |           -> "ic_set_success"
      |                           |                           |
      | Send to End Device: join                              |
      |                           |           -> "join_started"
      |                           |           -> "Joined network successfully"
```

---

## 1. Build Production Firmware

### Coordinator
```bash
# Edit prj.conf to disable development security
# zigbee_network/network_coordinator/prj.conf:
# CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n

cd ~/ncs/v2.9.2
west build -b nrf52840dk/nrf52840 \
    -s ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_network/network_coordinator \
    -d ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_network/network_coordinator/build \
    -- -DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n

west flash -d ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_network/network_coordinator/build \
    --domain network_coordinator --dev-id 1050246989 --runner nrfutil
```

### End Device
```bash
cd ~/ncs/v2.9.2
west build -b nrf52840dk/nrf52840 \
    -s ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_end_device/bmp180_device \
    -d ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_end_device/bmp180_device/build \
    -- -DCONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n

west flash -d ~/Git-Projects/ZigBee-Gateway_nRF52840-DK/zigbee_end_device/bmp180_device/build \
    --dev-id 1050247285 --runner nrfutil
```

### Alternative: Using the Flash Script
Instead of manual commands, you can use the `flash_devices.sh` utility:
* **Volatile Production Mode** (Erases credentials on reboot):
  ```bash
  ./flash_devices.sh all --prod
  ```
* **Persistent Production Mode** (Saves credentials across reboots):
  ```bash
  ./flash_devices.sh all --prod --persist
  ```
* **Persistent Production Mode with Clean NVRAM** (Force Factory Reset settings erase at flash time):
  ```bash
  ./flash_devices.sh all --prod --persist --factory-reset
  ```

---

## 2. Read Boot Logs

Open two serial terminals (115200 baud):
```bash
# Coordinator
minicom -D /dev/ttyACM0 -b 115200

# End Device
minicom -D /dev/ttyACM2 -b 115200
```

Wait for the End Device to print its IEEE address:
```
I: Device IEEE Address: f4ce369e3312a7ec
```

> [!NOTE]
> The Coordinator will also print:
> ```
> I: Production mode: waiting for Install Code registration via UART.
> I:   Send on Coordinator: ic_add <IEEE_ADDR_HEX> <INSTALL_CODE_36HEX>
> I:   Send on End Device:  ic_set <IEEE_ADDR_HEX> <INSTALL_CODE_36HEX>
> I:   Then trigger join:   join  (on End Device)
> ```

---

## 3. Generate an Install Code

An Install Code = 16 random bytes + 2-byte CRC-16/X-25 (reflected, little-endian).

Use the helper Python script:
```bash
python3 - <<'EOF'
import os, struct

def crc16_x25(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF

key = os.urandom(16)                         # 16 random bytes
crc = crc16_x25(key)                         # CRC over the key
ic = key + struct.pack('<H', crc)            # append CRC little-endian
print("Install Code (36 hex chars):", ic.hex())
EOF
```

Copy the output hex string — this is your `<INSTALL_CODE_36HEX>`.

---

## 4. Register Keys via UART

### On the Coordinator terminal (`/dev/ttyACM0`):
```
ic_add f4ce369e3312a7ec <INSTALL_CODE_36HEX>
```
Expected response:
```
ic_add_success: f4ce369e3312a7ec
steering_started
```

### On the End Device terminal (`/dev/ttyACM2`):
```
ic_set f4ce369e3312a7ec <INSTALL_CODE_36HEX>
```
Expected response:
```
ic_set_success
```

---

## 5. Trigger Join

On the End Device terminal:
```
join
```
Expected sequence:
```
join_started
I: Joined network successfully (Extended PAN ID: ...)
I: Network joined successfully.
```

The End Device will then start sending temperature measurements to the Coordinator.

---

## UART Command Reference

### Coordinator (`/dev/ttyACM0`)
| Command | Format | Description |
|---------|--------|-------------|
| `ic_add` | `ic_add <16HEX_IEEE> <36HEX_IC>` | Register End Device key + start steering |
| `factory_reset` | `factory_reset` | Erase NVRAM and reboot |

### End Device (`/dev/ttyACM2`)
| Command | Format | Description |
|---------|--------|-------------|
| `ic_set` | `ic_set <16HEX_IEEE> <36HEX_IC>` | Set own Install Code |
| `join` | `join` | Trigger network steering |
| `factory_reset` | `factory_reset` | Erase NVRAM and reboot |

---

## Format Details

| Field | Example | Notes |
|-------|---------|-------|
| IEEE address | `f4ce369e3312a7ec` | 8 bytes, big-endian hex, no spaces |
| Install Code | `9d2247614fc83712f89147fb5f1298bd520d` | 16 bytes key + 2 bytes CRC = 18 bytes = 36 hex chars |

> [!CAUTION]
> The CRC must be **CRC-16/X-25** (reflected/Kermit variant) appended in **little-endian** order.
> Using the wrong CRC algorithm will cause `ic_set_failed: -50` on the End Device.
