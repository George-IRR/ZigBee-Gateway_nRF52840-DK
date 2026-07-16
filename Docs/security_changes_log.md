# Security and Criptare Implementation Log

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
* The Coordinator application will listen on the serial UART interface.
* The host PC can send a command:
  ```
  ic_add <16-HEX-IEEE-ADDR> <36-HEX-INSTALL-CODE-WITH-CRC>
  ```
* When parsed, the Coordinator calls the ZBOSS API:
  ```c
  zb_secur_ic_add(device_addr, ZB_IC_TYPE_128, install_code, callback);
  ```

---

## 3. Development and E2E Test Automation Strategy

To save development time and automate testing, we are implementing two key strategies:

### 3.1 Development Mode Bypass (Method B)
We will introduce a Kconfig symbol or preprocessor definition (`CONFIG_ZIGBEE_DEVELOPMENT_SECURITY`):
* **End Device:** Sets a hardcoded, static Install Code at startup.
* **Coordinator:** Automatically registers this specific hardcoded EUI64 and Install Code on boot.
* This allows plug-and-play testing during direct code iterations without requiring any UART commands or external scripts.

### 3.2 Pytest E2E Automation (Method C)
The automated Pytest E2E test suite (`test_e2e.py`) will test both scenarios:

1. **Development Mode Test (`test_e2e_development_mode`):**
   * Verifies successful commission and communication when both boards rely on the static development Install Code.
   * Ensures out-of-the-box security works correctly.

2. **Production Mode Test (`test_e2e_production_mode`):**
   * Erases persistent configuration/NVRAM on both boards.
   * Dynamically generates a random EUI64 and random Install Code (calculating the correct CCITT CRC16).
   * Automatically transmits the `ic_add <EUI64> <INSTALL_CODE>` registration command over the Coordinator's UART port.
   * Sets the corresponding Install Code on the End Device.
   * Starts the commission process and verifies the key exchange, network association, and subsequent temperature reports.
