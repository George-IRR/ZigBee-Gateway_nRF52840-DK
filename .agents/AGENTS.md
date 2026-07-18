# Project Rules — ZigBee Gateway nRF52840-DK

## Zephyr / NCS Kconfig

- Never add `CONFIG_ZTEST_NEW_API=y` to any `prj.conf`. The symbol does not exist in
  NCS v2.9.2 / Zephyr 3.7.x and causes a fatal Kconfig abort:
  `error: Aborting due to Kconfig warnings`. The new ZTest API (`ZTEST_SUITE`, `ZTEST`)
  is the default and requires no extra flag.

## Hardware flashing

- `nrfjprog` is NOT installed on this machine. Always add `--west-flash --west-runner nrfutil`
  when running `west twister` with `--device-testing` for any nRF board. Without it Twister
  reports `FATAL ERROR: required program nrfjprog not found`.

## Unit testing — C standard headers

- Source files compiled for the `unit_testing` board are built with the host GCC toolchain
  without any implicit Zephyr headers. Always include `<stddef.h>` explicitly in any file
  that uses `NULL` and does not pull it in via a Zephyr header transitively.
  `<errno.h>` alone does not guarantee `NULL` on all host toolchains.

## BMP180 temperature formula

- The BMP180 compensation formula (datasheet section 3.5) is NOT monotone for arbitrary
  raw ADC values. With the datasheet example calibration (ac6=23153, md=2868), UT values
  below ~23000 drive the intermediate divisor (X1 + md) negative, causing X2 to become
  a large positive number and producing a non-physical result.
  When writing tests that verify monotonicity, use UT values that keep the divisor positive
  (e.g. 25000–35000 for the datasheet calibration). Never use UT=20000 for this purpose.

## Production Mode Pairing (ZIGBEE_DEVELOPMENT_SECURITY=n)

- When `CONFIG_ZIGBEE_DEVELOPMENT_SECURITY=n`, **no keys are pre-loaded** into the firmware.
  The Coordinator does NOT call `zb_set_installcode_policy(ZB_TRUE)` at boot; it is called
  lazily inside `ic_add_callback` only after a key is registered via UART.
- The End Device does **not** auto-join (no `bdb_start_top_level_commissioning` at boot).
  The operator must send UART commands in this order:
  1. **Coordinator** (`/dev/ttyACM0`): `ic_add <IEEE_HEX> <IC_36HEX>` → responds `ic_add_success` + `steering_started`
  2. **End Device** (`/dev/ttyACM2`): `ic_set <IEEE_HEX> <IC_36HEX>` → responds `ic_set_success`
  3. **End Device**: `join` → responds `join_started` then `Joined network successfully`
- The Install Code format is: 16 random bytes + 2-byte CRC-16/X-25 (reflected, appended little-endian) = 18 bytes = 36 hex chars.
- Full step-by-step pairing instructions are in [`Docs/production_pairing_guide.md`](../Docs/production_pairing_guide.md).
- If a user reports "nothing happens after setting ZIGBEE_DEVELOPMENT_SECURITY=n", refer them to this guide.

## README Checklist Maintenance

- After any significant feature implementation, always update the checklist in [`README.md`](../README.md)
  under `## 3. Development Steps`.
- Mark completed items with `[x]` and leave pending items as `[ ]`.
- The checklist must reflect reality — do not leave security, testing, or documented features as `[ ]`
  once they are working and verified.
- Key items to keep accurate:
  - Phase 2: AES-128 encryption, WDT, Deep Sleep, Zigbee join security → update as implemented
  - Phase 3: Message Queue, payload parser → update as implemented
  - Phase 5: Modbus client polling → update as implemented

## Git Commit & Code Comment Guidelines

- All git commit messages and code comments must always be written in English.
- Keep git commit messages brief, concise, and direct.
- **CRITICAL:** Do NOT mention modifications to agent configuration files or rules directories (such as `.agent`, `.agents`, `.agents/AGENTS.md`) in git commit messages.

## E2E Log-Based Testing Scenarios

When developing or executing E2E tests, the agent must verify the following log patterns:

### Scenario 1: Secure Connection of Whitelisted Device (Happy Path)
- Verify detection of new device announcement log (e.g., `ZB_ZDO_SIGNAL_DEVICE_ANNCE`).
- Verify log indicating the IEEE address passed whitelist check successfully.
- Verify security key association success status (e.g., `ZB_STATUS_SUCCESS` or corresponding link key association logs).

### Scenario 2: Rogue Node Rejection (Unauthorized Device)
- Verify rejection log emitted by `zigbee_endpoint_logger` (e.g., `Association rejected: IEEE address not in whitelist`).
- Verify error handler (`zigbee_error_handler`) is called with security-specific error code.
- Verify absence of Transport Network Key log (confirm no "Key Sent" log exists for the rogue IEEE address).

### Scenario 3: Secure Rejoin
- Verify secure rejoin request log (e.g., `Secure rejoin request received` for the target IEEE address).
- Verify successful rejoin completion directly, bypassing the initial key exchange step (no "New Install Code processed" logs).

### Scenario 4: BMP180 Data Integrity
- Verify that payload packet is successfully handed over to the secure APS layer (e.g., `ZCL report sent securely`).
- Verify that Coordinator logs do not contain decryption errors (e.g., no `Cryptkey mismatch` or `MIC failure` logs).

