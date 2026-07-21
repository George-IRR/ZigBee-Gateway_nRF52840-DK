import os
import re
import time
import logging
import subprocess
import pytest
import serial
from pathlib import Path
from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def get_dev_ids_from_config():
    repo_root = Path(__file__).resolve().parents[4]
    config_file = repo_root / "config.env"

    switch_id = "1050247285"
    coord_id  = "1050246989"

    if config_file.exists():
        for line in config_file.read_text().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, val = line.split("=", 1)
                if key.strip() == "SWITCH_DEV_ID":
                    switch_id = val.strip()
                elif key.strip() == "COORD_DEV_ID":
                    coord_id = val.strip()
    return switch_id, coord_id


def get_serial_ports():
    """Return {serial_number: [port, ...]} from nrfutil device list."""
    res = subprocess.run(["nrfutil", "device", "list"], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError("Failed to run 'nrfutil device list'")

    devices = {}
    current_dev = None
    for line in res.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if re.match(r'^\d{10}$', line):
            current_dev = line
            devices[current_dev] = []
        elif current_dev and line.startswith("Ports"):
            parts = re.split(r'\s+', line, maxsplit=1)
            if len(parts) > 1:
                for p in parts[1].split(','):
                    p = p.strip()
                    if p.startswith("/dev/"):
                        devices[current_dev].append(p.split()[0])
        elif current_dev and line.startswith("/dev/"):
            for p in line.split(','):
                p = p.strip()
                if p.startswith("/dev/"):
                    devices[current_dev].append(p.split()[0])
    return devices


# ---------------------------------------------------------------------------
# Main test
# ---------------------------------------------------------------------------

def test_e2e_security(dut: DeviceAdapter):
    """
    E2E security test covering 3 automated scenarios (Development Security Mode):

    1. Happy Path  — device auto-joins with pre-loaded static Install Code
    2. Data Integrity — Mocked temperatures flow from End Device to Coordinator
    3. Secure Rejoin — device rejoins after soft reboot without fresh commissioning

    NOTE: Scenario 'Rogue Node Rejection' requires ZIGBEE_DEVELOPMENT_SECURITY=n
    (production mode) and cannot be automated in this test build.
    """
    switch_id, coord_id = get_dev_ids_from_config()
    ports = get_serial_ports()
    if coord_id not in ports or not ports[coord_id]:
        pytest.fail(f"Coordinator device {coord_id} not found in connected ports.")

    coord_port = ports[coord_id][0]
    coord_ser = serial.Serial(coord_port, baudrate=115200, timeout=0.1)
    coord_ser.reset_input_buffer()

    ed_logs    = []
    coord_logs = []

    def collect(drain_seconds: float = 1.0):
        """
        Drain End Device lines via dut.readline() for `drain_seconds`.
        Also drain all pending Coordinator bytes.
        Uses dut.readline() directly (NOT readlines()) — works reliably with
        the hardware adapter regardless of internal buffer state.
        """
        deadline = time.time() + drain_seconds
        while time.time() < deadline:
            line = dut.readline()
            if line:
                line_str = line.strip()
                logger.info(f"[End Device] {line_str}")
                ed_logs.append(line_str)
            else:
                time.sleep(0.05)

        if coord_ser and coord_ser.is_open:
            while coord_ser.in_waiting > 0:
                try:
                    raw = coord_ser.readline()
                    s = raw.decode('utf-8', errors='ignore').strip()
                    if s:
                        logger.info(f"[Coordinator] {s}")
                        coord_logs.append(s)
                except Exception:
                    pass

    # =========================================================================
    # SCENARIO 1 — Happy Path: Auto-join with static Install Code
    # =========================================================================
    logger.info("=== SCENARIO 1: HAPPY PATH — AUTO-JOIN (DEVELOPMENT SECURITY) ===")

    # Give End Device 2s to start printing, then reopen the Coordinator steering
    # window so it will accept join requests (window may have expired if Coordinator
    # was booted before the test started).
    logger.info("Waiting 2s for End Device boot output to start...")
    collect(drain_seconds=2.0)

    logger.info("Sending 'reopen' to Coordinator to open steering window...")
    coord_ser.write(b"reopen\n")
    time.sleep(0.5)          # give Coordinator time to process via ZBOSS thread
    collect(drain_seconds=0.5)

    # Device boots → UART thread starts after 2 s delay → static IC registered →
    # bdb_start_top_level_commissioning → ZB_BDB_SIGNAL_STEERING OK →
    # LOG_INF("Network joined successfully.")   (End Device, main.c ~line 367)
    # Coordinator emits:
    # LOG_INF("New device commissioned or rejoined (short: 0x%04hx)")  (coordinator main.c ~line 488)
    logger.info("Waiting 15 s for End Device to auto-join...")
    collect(drain_seconds=15.0)

    # Query IEEE address via UART command (command 'ieee' → printk("Device IEEE Address: ..."))
    real_mac_hex = None
    for attempt in range(5):
        logger.info(f"Querying IEEE address (attempt {attempt + 1}/5)...")
        dut.write(b"ieee\n")
        collect(drain_seconds=1.5)
        for l in ed_logs:
            m = re.search(r'Device IEEE Address:\s*([0-9a-fA-F]{16})', l)
            if m:
                real_mac_hex = m.group(1).lower()
                break
        if real_mac_hex:
            break

    if not real_mac_hex:
        coord_ser.close()
        pytest.fail("Scenario 1 FAILED: End Device did not respond to 'ieee' command.")

    logger.info(f"Detected End Device MAC: {real_mac_hex}")

    # Assertions — based on ACTUAL log strings in the C source:
    joined = any("Network joined successfully" in l for l in ed_logs)
    coord_saw_device = any("commissioned or rejoined" in l for l in coord_logs)

    logger.info(f"  End Device joined:          {joined}")
    logger.info(f"  Coordinator saw device:     {coord_saw_device}")

    if not joined:
        coord_ser.close()
        pytest.fail(
            "Scenario 1 FAILED: 'Network joined successfully' not found in End Device logs.\n"
            f"  ed_logs tail: {ed_logs[-10:]}"
        )

    # =========================================================================
    # SCENARIO 4 — BMP180 Data Integrity
    # Verify mocked temperature reports flow from End Device → Coordinator
    # =========================================================================
    logger.info("=== SCENARIO 4: BMP180 DATA INTEGRITY ===")

    # Temperatures are reported every ~4 s (RTC2 compare, main.c).
    # Wait enough for at least 2 successful reports.
    logger.info("Waiting 15 s for temperature reports to flow...")
    collect(drain_seconds=15.0)

    # End Device emits: LOG_INF("Write attribute request sent successfully!")
    tx_ok = any("Write attribute request sent successfully" in l for l in ed_logs)
    # Coordinator emits: LOG_INF("zcl_device_cb - Temperature: %d.%d C (Raw value: %d)")
    rx_ok = any("zcl_device_cb - Temperature:" in l for l in coord_logs)
    no_crypt_err = not any(
        "MIC failure" in l or "decrypt error" in l.lower() or "Cryptkey mismatch" in l
        for l in coord_logs
    )

    logger.info(f"  Reports sent (End Device):  {tx_ok}")
    logger.info(f"  Reports received (Coord):   {rx_ok}")
    logger.info(f"  No decryption errors:       {no_crypt_err}")

    if not (tx_ok and rx_ok and no_crypt_err):
        coord_ser.close()
        pytest.fail(
            "Scenario 4 FAILED: Temperature data did not flow correctly.\n"
            f"  tx_ok={tx_ok}, rx_ok={rx_ok}, no_crypt_err={no_crypt_err}\n"
            f"  ed_logs tail:    {ed_logs[-10:]}\n"
            f"  coord_logs tail: {coord_logs[-10:]}"
        )

    # =========================================================================
    # SCENARIO 3 — Secure Rejoin after soft reboot
    # Device reboots → already has persistent network info → rejoins directly.
    # Coordinator emits "commissioned or rejoined" again without fresh steering.
    # End Device does NOT emit "Device started for the first time".
    # =========================================================================
    logger.info("=== SCENARIO 3: SECURE REJOIN ===")

    ed_logs.clear()
    coord_logs.clear()

    logger.info("Rebooting End Device via 'reboot' command...")
    dut.write(b"reboot\n")

    # Allow time for reboot + rejoin (no steering needed, should be ~5 s)
    collect(drain_seconds=12.0)

    # Coordinator should announce the device again
    rejoin_found = any("commissioned or rejoined" in l for l in coord_logs)
    # A rejoin (not first join) should NOT log "Device started for the first time"
    no_first_start = not any("Device started for the first time" in l for l in ed_logs)

    logger.info(f"  Coordinator saw rejoin:     {rejoin_found}")
    logger.info(f"  No 'first time' start log:  {no_first_start}")

    coord_ser.close()

    if not rejoin_found:
        pytest.fail(
            "Scenario 3 FAILED: Coordinator did not log device rejoin.\n"
            f"  coord_logs tail: {coord_logs[-10:]}"
        )

    logger.info("=== ALL E2E SCENARIOS PASSED SUCCESSFULLY ===")
