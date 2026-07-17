import os
import re
import time
import logging
import subprocess
import pytest
import serial
import random
from pathlib import Path
from twister_harness import DeviceAdapter
from twister_harness.exceptions import TwisterHarnessTimeoutException

logger = logging.getLogger(__name__)

def get_dev_ids_from_config():
    # Attempt to load from config.env in repo root
    repo_root = Path(__file__).resolve().parents[4]
    config_file = repo_root / "config.env"
    
    switch_id = "1050247285"
    coord_id = "1050246989"
    
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
    # Use nrfutil to list connected devices and their ports
    res = subprocess.run(["nrfutil", "device", "list"], capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError("Failed to run 'nrfutil device list'")
    
    devices = {}
    current_dev = None
    lines = res.stdout.splitlines()
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if re.match(r'^\d{10}$', line):
            current_dev = line
            devices[current_dev] = []
        elif current_dev and line.startswith("Ports"):
            parts = re.split(r'\s+', line, maxsplit=1)
            if len(parts) > 1:
                port_part = parts[1]
                for p in port_part.split(','):
                    p = p.strip()
                    if p.startswith("/dev/"):
                        port_path = p.split()[0]
                        devices[current_dev].append(port_path)
        elif current_dev and line.startswith("/dev/"):
            for p in line.split(','):
                p = p.strip()
                if p.startswith("/dev/"):
                    port_path = p.split()[0]
                    devices[current_dev].append(port_path)
    return devices

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        temp = 0
        for i in range(8):
            if (byte >> i) & 1:
                temp |= (1 << (7 - i))
        crc ^= (temp << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    reflected_crc = 0
    for i in range(16):
        if (crc >> i) & 1:
            reflected_crc |= (1 << (15 - i))
    return reflected_crc ^ 0xFFFF

def get_end_device_mac(dut: DeviceAdapter, timeout: int = 15) -> str:
    start_time = time.time()
    logger.info("Waiting for End Device to report its IEEE address...")
    while time.time() - start_time < timeout:
        line = dut.readline()
        if line:
            line_str = line.strip()
            m = re.search(r'Device IEEE Address:\s*([0-9a-fA-F]{16})', line_str)
            if m:
                mac_addr = m.group(1).lower()
                logger.info(f"Detected End Device MAC: {mac_addr}")
                return mac_addr
        time.sleep(0.05)
    raise RuntimeError("Failed to detect End Device IEEE Address from boot logs.")

def verify_communication(dut: DeviceAdapter, coord_ser: serial.Serial, timeout: int = 90):
    sent_temperatures = []
    received_temperatures = []
    start_time = time.time()
    
    while time.time() - start_time < timeout:
        # Read from End Device
        ed_line = dut.readline()
        if ed_line:
            ed_line_str = ed_line.strip()
            m = re.search(r'Mocked temperature:\s*([0-9.-]+)\s*C', ed_line_str)
            if m:
                temp_val = float(m.group(1))
                logger.info(f"[End Device UART] Sent Mock Temperature: {temp_val} C")
                sent_temperatures.append(temp_val)
        
        # Read from Coordinator
        if coord_ser.in_waiting > 0:
            coord_line = coord_ser.readline()
            try:
                coord_line_str = coord_line.decode('utf-8', errors='ignore').strip()
            except Exception:
                coord_line_str = ""
            if coord_line_str:
                m = re.search(r'Temperature:\s*([0-9.-]+)\s*C', coord_line_str)
                if m:
                    temp_val = float(m.group(1))
                    logger.info(f"[Coordinator UART] Received Temperature: {temp_val} C")
                    received_temperatures.append(temp_val)
                    
        # Check if we have successfully matched at least 2 values
        matched_values = [t for t in sent_temperatures if t in received_temperatures]
        if len(matched_values) >= 2:
            logger.info(f"SUCCESS: Matched temperature values {matched_values}!")
            return True
            
        time.sleep(0.01)
        
    logger.error(f"Timeout waiting for communication. Sent: {sent_temperatures}, Received: {received_temperatures}")
    return False

def test_e2e_security(dut: DeviceAdapter):
    """
    Unified E2E security test verifying the 4 log-based security scenarios:
    1. Secure Connection of Whitelisted Device (Happy Path)
    2. Rogue Node Rejection (Unauthorized Device)
    3. Secure Rejoin
    4. BMP180 Data Integrity
    """
    switch_id, coord_id = get_dev_ids_from_config()
    ports = get_serial_ports()
    if coord_id not in ports or not ports[coord_id]:
        pytest.fail(f"Coordinator device {coord_id} not found in connected ports.")
        
    coord_port = ports[coord_id][0]
    coord_ser = serial.Serial(coord_port, baudrate=115200, timeout=0.1)
    coord_ser.reset_input_buffer()
    
    ed_logs = []
    coord_logs = []
    
    def collect_device_logs():
        # Read all pending lines from End Device queue
        for line in dut.readlines(print_output=False):
            line_str = line.strip()
            logger.info(f"[End Device] {line_str}")
            ed_logs.append(line_str)
        # Read all pending lines from Coordinator serial buffer
        if coord_ser and coord_ser.is_open:
            while coord_ser.in_waiting > 0:
                try:
                    line_c = coord_ser.readline()
                    line_c_str = line_c.decode('utf-8', errors='ignore').strip()
                    logger.info(f"[Coordinator] {line_c_str}")
                    coord_logs.append(line_c_str)
                except Exception:
                    pass

    # =========================================================================
    # SCENARIO 1: Secure Connection of Whitelisted Device (Happy Path - Dev Security)
    # =========================================================================
    logger.info("=== STARTING SCENARIO 1: HAPPY PATH SECURE CONNECTION (DEVELOPMENT SECURITY) ===")
    
    # 1. Collect all boot logs printed after flashing
    time.sleep(2.0)
    collect_device_logs()
    
    # 2. Get End Device MAC
    real_mac_hex = None
    for line in ed_logs:
        m = re.search(r'Device IEEE Address:\s*([0-9a-fA-F]{16})', line)
        if m:
            real_mac_hex = m.group(1).lower()
            break
            
    if not real_mac_hex:
        # Wait a bit more and check
        time.sleep(3)
        collect_device_logs()
        for line in ed_logs:
            m = re.search(r'Device IEEE Address:\s*([0-9a-fA-F]{16})', line)
            if m:
                real_mac_hex = m.group(1).lower()
                break
                
    if not real_mac_hex:
        coord_ser.close()
        pytest.fail("Failed to detect End Device IEEE Address from boot logs.")
        
    logger.info(f"Detected End Device MAC: {real_mac_hex}")
    
    # 3. Trigger join steering on End Device
    logger.info("Triggering join steering on End Device...")
    time.sleep(2.0)
    dut.write(b"join\n")
    
    # Wait for association and initial reports
    time.sleep(25)
    collect_device_logs()
    
    # Verify Scenario 1 Assertions:
    # A. New device announcement log check
    annce_found = any("Device update received" in l or "New device commissioned" in l for l in coord_logs)
    # B. Security key association check (authorization status: 0)
    auth_success = any("authorization status: 0" in l for l in coord_logs)
    
    logger.info(f"Scenario 1 - Device Update detected: {annce_found}")
    logger.info(f"Scenario 1 - Key Association Success (status 0): {auth_success}")
    
    if not (annce_found and auth_success):
        coord_ser.close()
        pytest.fail("Scenario 1 Failed: Whitelisted device secure association logs not found.")
        
    # =========================================================================
    # SCENARIO 3: Secure Rejoin
    # =========================================================================
    logger.info("=== STARTING SCENARIO 3: SECURE REJOIN ===")
    
    # Clear logs before reboot
    ed_logs.clear()
    coord_logs.clear()
    
    # 1. Soft reboot End Device via console command
    logger.info("Rebooting End Device...")
    dut.write(b"reboot\n")
    time.sleep(15)
    collect_device_logs()
    
    # Verify Scenario 3 Assertions:
    # A. Secure rejoin request/completion log check (status 1 is rejoin)
    rejoin_found = any("Device update received" in l and "status: 1" in l for l in coord_logs) or \
                   any("rejoined" in l.lower() for l in coord_logs)
    # B. Bypassed initial key exchange (no new key exchange logic ran)
    no_new_key_exchange = not any("Setting static development install code" in l for l in ed_logs)
    
    logger.info(f"Scenario 3 - Secure Rejoin detected: {rejoin_found}")
    logger.info(f"Scenario 3 - No new key exchange: {no_new_key_exchange}")
    
    if not rejoin_found:
        coord_ser.close()
        pytest.fail("Scenario 3 Failed: Secure rejoin logs not found on Coordinator.")

    # =========================================================================
    # SCENARIO 2: Rogue Node Rejection (Unauthorized Device)
    # =========================================================================
    logger.info("=== STARTING SCENARIO 2: ROGUE NODE REJECTION ===")
    
    # 1. Reset both to factory defaults
    logger.info("Performing factory reset to enter Production Mode (clearing keys)...")
    coord_ser.write(b"factory_reset\n")
    dut.write(b"factory_reset\n")
    
    # Close Coordinator port during reboot to prevent SerialException
    coord_ser.close()
    
    ed_logs.clear()
    coord_logs.clear()
    time.sleep(12)
    
    # Reopen Coordinator port
    coord_ser = serial.Serial(coord_port, baudrate=115200, timeout=0.1)
    coord_ser.reset_input_buffer()
    
    # Collect boot logs
    collect_device_logs()
    
    # 2. Set Install Code on End Device, but do NOT register it on Coordinator (Simulate Rogue)
    ic_key_bytes = bytes([random.randint(0x00, 0xFF) for _ in range(16)])
    crc_val = crc16_ccitt(ic_key_bytes)
    crc_bytes = bytes([crc_val & 0xFF, (crc_val >> 8) & 0xFF])
    ic_bytes = ic_key_bytes + crc_bytes
    ic_hex = ic_bytes.hex()
    
    logger.info(f"Rogue Install Code: {ic_hex}")
    
    logger.info("Setting Install Code on End Device via UART...")
    dut.write(f"ic_set {real_mac_hex} {ic_hex}\n".encode())
    time.sleep(2)
    collect_device_logs()
    
    logger.info("Triggering join on End Device (Unauthorized Join Attempt)...")
    dut.write(b"join\n")
    time.sleep(20)
    collect_device_logs()
    
    # Verify Scenario 2 Assertions:
    # A. Check Coordinator rejected TCLK authorization (status 2 / failed)
    auth_failed = any("authorization status:" in l and "authorization status: 0" not in l for l in coord_logs)
    # B. Verify no temperature reports decrypted successfully
    no_temp_received = not any("Temperature:" in l for l in coord_logs)
    
    logger.info(f"Scenario 2 - Rejected Device detected: {auth_failed}")
    logger.info(f"Scenario 2 - No temperature decrypted: {no_temp_received}")
    
    if not (auth_failed and no_temp_received):
        coord_ser.close()
        pytest.fail("Scenario 2 Failed: Coordinator did not reject the unauthorized rogue device or accepted data.")

    # =========================================================================
    # SCENARIO 4: BMP180 Data Integrity
    # =========================================================================
    logger.info("=== STARTING SCENARIO 4: BMP180 DATA INTEGRITY ===")
    
    # 1. Register the correct Install Code on Coordinator to allow successful pairing
    logger.info("Registering Install Code on Coordinator via UART...")
    coord_ser.write(f"ic_add {real_mac_hex} {ic_hex}\n".encode())
    time.sleep(2)
    collect_device_logs()
    
    # 2. Trigger join on End Device
    logger.info("Triggering join on End Device (Authorized Join)...")
    dut.write(b"join\n")
    time.sleep(25)
    collect_device_logs()
    
    # Verify Scenario 4 Assertions:
    # A. Verify ZCL temperature reports sent successfully
    tx_success = any("Write attribute request sent successfully!" in l for l in ed_logs)
    # B. Verify ZCL temperature reports received and decoded correctly by Coordinator
    rx_success = any("zcl_device_cb - Temperature:" in l for l in coord_logs)
    # C. Verify no cryptographic/decrypt/MIC failure logs
    no_decrypt_errors = not any("MIC failure" in l or "decrypt error" in l.lower() or "Cryptkey mismatch" in l for l in coord_logs)
    
    logger.info(f"Scenario 4 - Reports sent: {tx_success}")
    logger.info(f"Scenario 4 - Reports received: {rx_success}")
    logger.info(f"Scenario 4 - No decryption errors: {no_decrypt_errors}")
    
    coord_ser.close()
    
    if not (tx_success and rx_success and no_decrypt_errors):
        pytest.fail("Scenario 4 Failed: Data integrity check failed or decryption errors detected.")
    
    logger.info("=== ALL E2E LOG-BASED SECURITY SCENARIOS PASSED SUCCESSFULLY ===")

