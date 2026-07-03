# Testing Strategy and Execution Guide

This document outlines the testing methodology, configurations, and execution procedures for the BMP180 Zigbee End Device project.

---

## 1. Testing Methodology

The testing framework is structured into a three-tiered hierarchy using the Zephyr Test Framework (Ztest) and Pytest:

```
+-------------------------------------------------------------------------+
|  UNIT TESTS            |  INTEGRATION TESTS      |  END-TO-END TESTS    |
|  (Host PC / Emulator)  |  (Single Target HW)     |  (Multi-Target HW)   |
|                        |                         |                      |
|  Verifies mathematical |  Verifies peripheral    |  Verifies Zigbee     |
|  compensation formula  |  drivers and I2C        |  communication flow  |
|  logic in isolation.   |  hardware readouts.     |  to coordinator.     |
+-------------------------------------------------------------------------+
```

---

## 2. Unit Tests

Unit tests validate the mathematical compensation algorithm from Section 3.5 of the BMP180 datasheet. They execute on the host PC (using the `unit_testing` board) in isolation from the physical I2C bus and the Zephyr kernel.

### 2.1 File Structure
* **Source Under Test:** [bmp180_math.c](../zigbee_end_device/bmp180_device/src/bmp180_math.c)
* **Test Implementation:** [test_bmp180_math.c](../zigbee_end_device/bmp180_device/tests/unit/src/test_bmp180_math.c)
* **Build Configuration:** [CMakeLists.txt](../zigbee_end_device/bmp180_device/tests/unit/CMakeLists.txt) (utilizes `COMPONENTS unittest` and target `testbinary`)

### 2.2 Test Cases
* **test_datasheet_example:** Validates the compensation algorithm against the reference uncompensated temperature ($UT = 27898$) and calibration data from the BST-BMP180-DS000-12 datasheet. Expects temperature $15.0 ^\circ\text{C}$ (value `150`).
* **test_null_calib_pointer / test_null_output_pointer:** Verifies that API functions return `-EINVAL` when passed null pointers.
* **test_division_by_zero_guard:** Forces the divisor ($X_1 + md$) to zero and confirms that the API returns `-EDOM` to prevent a CPU division fault.
* **test_output_unchanged_on_error:** Confirms that output parameters remain unmodified when an error is returned.
* **test_fixed_point_unit:** Verifies the return value matches the expected tenths-of-a-degree unit scale.
* **test_lower_ut_lower_temperature / test_higher_ut_higher_temperature:** Confirms monotonicity of the output temperature relative to the uncompensated ADC inputs within the physically valid range.

### 2.3 Execution
Execute the unit tests locally on the host PC:
```bash
west twister -T zigbee_end_device/bmp180_device/tests/unit/ --inline-logs
```

---

## 3. Integration Tests

Integration tests run on a single physical nRF52840-DK board with a wired BMP180 sensor. They verify that calibration parameters are retrieved from the sensor, I2C read transactions succeed, and readouts are within plausible physical limits.

### 3.1 Device Tree Configuration
The integration suite enables the I2C0 peripheral on Arduino headers pins via [nrf52840dk_nrf52840.overlay](../zigbee_end_device/bmp180_device/tests/integration/boards/nrf52840dk_nrf52840.overlay):
* SDA: Pin `P0.26`
* SCL: Pin `P0.27`

### 3.2 Test Cases
* **test_init_succeeds:** Verifies that `bmp180_init()` successfully reads the 22-byte calibration coefficients from the physical sensor over I2C.
* **test_read_succeeds_and_is_plausible:** Asserts that temperature readings are within a plausible physical indoor range ($-10.0 ^\circ\text{C}$ to $85.0 ^\circ\text{C}$).
* **test_consecutive_reads_are_consistent:** Performs back-to-back reads spaced 10 ms apart and asserts that the deviation (drift) does not exceed $5.0 ^\circ\text{C}$.
* **test_repeated_reads_all_succeed:** Performs a stress loop of 5 consecutive read cycles to ensure stability.

### 3.3 Execution
Configure your hardware device IDs in `config.env` at the root directory before running this test.
```bash
west twister -T zigbee_end_device/bmp180_device/tests/integration/ \
             -p nrf52840dk/nrf52840 \
             --device-testing \
             --device-serial /dev/ttyACM0 \
             --device-serial-baud 115200 \
             --west-flash \
             --west-runner nrfutil
```

---

## 4. End-to-End (E2E) Tests

End-to-End tests validate the multi-node Zigbee communication path between the End Device and the Network Coordinator.

### 4.1 Mock Test Mode
To test Zigbee data delivery independently of the environment, a mock test configuration is compiled into the End Device binary via `CONFIG_BMP180_DEVICE_TEST_MODE=y`. 

* **Init Bypass:** Bypasses I2C verification so tests run even if a sensor is physically missing on the test rig.
* **Mock Sequence:** Generates a looping sequence of mock temperatures starting at $20.0 ^\circ\text{C}$ (`200`) and incrementing by $0.5 ^\circ\text{C}$ (`5`) on each RTC interval report.
* **UART Logging:** The End Device prints reports to its UART: `[TEST MODE] Mocked temperature: <value> C`.

### 4.2 Pytest Middleware
A pytest script [test_e2e.py](../zigbee_end_device/bmp180_device/tests/e2e/pytest/test_e2e.py) coordinates the validation:
1. **Port Mapping:** Queries `nrfutil device list` to dynamically map dev-ids to serial port paths.
   * End Device (`SWITCH_DEV_ID` in `config.env`) -> `/dev/ttyACM0` (monitored via Twister's `dut` fixture).
   * Coordinator (`COORD_DEV_ID` in `config.env`) -> `/dev/ttyACM2` (monitored via direct `pyserial` UART connection).
2. **Verification:** Reads both UART logs. Verifies that the sequential temperature values logged as sent by the End Device are parsed and logged as received by the Coordinator (e.g. `zcl_device_cb - Temperature: <value> C`). 

### 4.3 Execution
Ensure the Coordinator is flashed and running before starting the E2E test.
```bash
west twister -T zigbee_end_device/bmp180_device/tests/e2e/ \
             -p nrf52840dk/nrf52840 \
             --device-testing \
             --device-serial /dev/ttyACM0 \
             --device-serial-baud 115200 \
             --west-flash="--dev-id=<SWITCH_DEV_ID>" \
             --west-runner nrfutil \
             --inline-logs
```
