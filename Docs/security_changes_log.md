# Security and Encryption Implementation Log

This document tracks the design decisions, architecture, and modifications made on the `feature/zigbee-security` branch to implement Zigbee 3.0 joining security and AES encryption.

---

## 1. Planned Security Architecture

To secure the network and prevent sniffers from capturing the Network Key, we are implementing two layers of defense:

1. **Permit Join Window:** The commissioning window will not be open permanently. It will close automatically after 180 seconds and must be explicitly opened.
2. **Install Code Policy (TCLK Exchange):** Mandate a pre-shared 18-byte Install Code (16-byte random key + 2-byte CRC16) for new devices. The Coordinator will reject any device attempting to join using the default global Link Key.

```
[ Joining End Device ]                               [ Coordinator / Trust Center ]
         |                                                       |
   (Has unique IC)                                        (Registers EUI64 + IC)
         |                                                       |
         |------------- 1. MAC Association Request ------------->|
         |                                                       |
         |<------------ 2. TCLK Key Exchange --------------------|
         |   (Both derive TCLK locally from the Install Code)    |
         |                                                       |
         |<------------ 3. Transport Network Key ----------------|
         |      (Network Key encrypted with unique TCLK)         |
```

---

## 2. Coordinator UART Integration (Casing QR Code Simulation)

To allow dynamic registration of devices without re-flashing the Coordinator:
* The Coordinator application listens on the serial UART interface.
* The host PC sends a command:
  ```
  ic_add <16-HEX-IEEE-ADDR> <36-HEX-INSTALL-CODE-WITH-CRC>
  ```
* When parsed, the Coordinator schedules ZBOSS API execution:
  ```c
  zb_secur_ic_add(device_addr, ZB_IC_TYPE_128, install_code, callback);
  ```

---

## 3. Development and E2E Test Automation Strategy

To save development time and automate testing, we are implementing two key strategies:

### 3.1 Development Mode Bypass (Method B)
We introduced a Kconfig symbol `CONFIG_ZIGBEE_DEVELOPMENT_SECURITY`:
* **End Device:** Sets a hardcoded, static Install Code at startup.
* **Coordinator:** Automatically registers this specific hardcoded EUI64 and Install Code on boot.
* This allows plug-and-play testing during direct code iterations without requiring any UART commands or external scripts.

### 3.2 Pytest E2E Automation (Method C)
The automated Pytest E2E test suite (`test_e2e.py`) tests both scenarios:
1. **Development Mode Test:** Verifies successful commission and communication when both boards rely on the static development Install Code.
2. **Production Mode Test:** Erases persistent configuration/NVRAM, dynamically generates a random EUI64 and Install Code (with correct CRC), automatically registers them via UART on both devices, starts commissioning, and verifies the key exchange.

---

## 4. Key Discovery: CRC-16/X-25 and Byte-Ordering

During implementation, we uncovered critical ZBOSS stack rules:
* **CRC Algorithm:** The checksum calculation algorithm is **CRC-16/X-25** (reflected).
* **Key Byte Reversal:** The 16-byte key must be stored in the binary buffer in **little-endian (reversed)** byte order when passed to `zb_secur_ic_set()`, and the CRC must be calculated over the reversed key.
* **Dynamic Candidate Test:** We implemented an 8-candidate dynamically running firmware loop that identified candidate 3 (reversed key + LE CRC) as the only one returning `RET_OK` (0).
* **Final Matching Key Configuration:**
  * Key: `ff ee dd cc bb aa 99 88 77 66 55 44 33 22 11 00`
  * Checksum: `52 0d` (little-endian of `0x0D52`).

---

## 5. Thread Safety (ZBOSS Scheduler Integration)

ZBOSS is a single-threaded stack. Directly invoking ZBOSS API functions (`zb_secur_ic_add`, `zb_secur_ic_set`, bdb_start_top_level_commissioning) from the UART thread led to memory corruption and stack assertions (fatal ZBOSS crash).
* **Fix:** We encapsulated all UART commands into asynchronous callback routines scheduled onto the ZBOSS thread via `ZB_SCHEDULE_APP_CALLBACK()`.
* This completely eliminated the stack crashes and stabilized all E2E operations.

---

## 6. Verification & E2E Pytest Results

The unified E2E test was executed through Twister on the physical hardware:
* **Command:**
  ```bash
  west twister -T zigbee_end_device/bmp180_device/tests/e2e -p nrf52840dk/nrf52840 --device-testing --device-serial /dev/ttyACM2 --west-runner nrfutil --west-flash="--dev-id=1050247285"
  ```
* **Output:**
  ```
  INFO    - Total complete:    1/   1  100%  skipped:    0, failed:    0, error:    0
  INFO    - 1 of 1 test configurations passed (100.00%) in 66.67 seconds
  ```
Both Phase 1 (Development) and Phase 2 (Production) completed without errors, proving the robust security implementation.
