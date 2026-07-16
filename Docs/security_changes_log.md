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

To save development time and avoid entering credentials manually every time a device is flashed:

### 3.1 Development Mode Bypass
We will introduce a `#define DEVELOPMENT_MODE` block:
* **End Device:** Hardcodes its own MAC and Install Code.
* **Coordinator:** Automatically pre-registers this hardcoded End Device credentials at boot time.
* This allows simple plug-and-play testing during direct code iterations.

### 3.2 Pytest E2E Automation
For automated CI/CD and regression testing:
* The Pytest script (`test_e2e.py`) will automatically fetch/generate the test Install Code and EUI64.
* It will issue the `ic_add` command over the Coordinator's serial port before initiating joining.
* This completely eliminates manual typing and makes E2E tests runnable on every push.
