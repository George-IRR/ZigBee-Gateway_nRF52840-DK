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
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

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
    Unified E2E security test verifying both Development and Production modes
    within a single test run to prevent serial conflicts and multi-flash failures.
    """
    switch_id, coord_id = get_dev_ids_from_config()
    ports = get_serial_ports()
    if coord_id not in ports or not ports[coord_id]:
        pytest.fail(f"Coordinator device {coord_id} not found in connected ports.")
        
    coord_port = ports[coord_id][0]
    coord_ser = serial.Serial(coord_port, baudrate=115200, timeout=0.1)
    coord_ser.reset_input_buffer()
    
    # =========================================================================
    # PHASE 1: Development Mode
    # =========================================================================
    logger.info("=== STARTING PHASE 1: DEVELOPMENT MODE ===")
    
    # 1. Reset both to start fresh
    logger.info("Performing factory reset on Coordinator and End Device...")
    coord_ser.write(b"factory_reset\n")
    dut.write(b"factory_reset\n")
    time.sleep(8)
    
    coord_ser.reset_input_buffer()
    
    # 2. Get End Device real MAC
    real_mac_hex = get_end_device_mac(dut)
    
    # 3. Dynamic register development key on Coordinator (to support any hardware/CI)
    logger.info(f"Pre-registering development Install Code for EUI64 {real_mac_hex} on Coordinator...")
    coord_ser.write(f"ic_add {real_mac_hex} 00112233445566778899aabbccddeeff4278\n".encode())
    
    success_found = False
    start_wait = time.time()
    while time.time() - start_wait < 5:
        if coord_ser.in_waiting > 0:
            line = coord_ser.readline().decode('utf-8', errors='ignore').strip()
            if "ic_add_success" in line:
                logger.info("Coordinator registered development key successfully!")
                success_found = True
                break
        time.sleep(0.05)
        
    # 4. Trigger steering on End Device
    logger.info("Triggering join steering on End Device...")
    dut.write(b"join\n")
    
    # 5. Verify communication
    success = verify_communication(dut, coord_ser, timeout=65)
    if not success:
        coord_ser.close()
        pytest.fail("E2E Development Mode Phase Failed: Communication check timed out.")
        
    # =========================================================================
    # PHASE 2: Production Mode (Dynamic UART registration)
    # =========================================================================
    logger.info("=== STARTING PHASE 2: PRODUCTION MODE ===")
    
    # 1. Reset both to clean state
    logger.info("Performing factory reset on Coordinator and End Device...")
    coord_ser.write(b"factory_reset\n")
    dut.write(b"factory_reset\n")
    time.sleep(8)
    
    coord_ser.reset_input_buffer()
    
    # 2. Get End Device real MAC
    real_mac_hex = get_end_device_mac(dut)
    
    # 3. Generate a random Install Code (16 bytes key + 2 bytes CRC)
    ic_key_bytes = bytes([random.randint(0x00, 0xFF) for _ in range(16)])
    crc_val = crc16_ccitt(ic_key_bytes)
    crc_bytes = bytes([crc_val & 0xFF, (crc_val >> 8) & 0xFF])
    ic_bytes = ic_key_bytes + crc_bytes
    ic_hex = ic_bytes.hex()
    
    logger.info(f"Dynamic MAC: {real_mac_hex}, Install Code: {ic_hex}")
    
    # 4. Register Install Code on Coordinator via UART
    logger.info("Registering Install Code on Coordinator via UART...")
    coord_ser.write(f"ic_add {real_mac_hex} {ic_hex}\n".encode())
    
    success_found = False
    start_wait = time.time()
    while time.time() - start_wait < 5:
        if coord_ser.in_waiting > 0:
            line = coord_ser.readline().decode('utf-8', errors='ignore').strip()
            if "ic_add_success" in line:
                logger.info("Coordinator registered key successfully!")
                success_found = True
                break
        time.sleep(0.05)
        
    # 5. Set Install Code on End Device via UART
    logger.info("Setting Install Code on End Device via UART...")
    dut.write(f"ic_set {real_mac_hex} {ic_hex}\n".encode())
    
    ed_success = False
    start_wait = time.time()
    while time.time() - start_wait < 5:
        line = dut.readline()
        if line and "ic_set_success" in line:
            logger.info("End Device set key successfully!")
            ed_success = True
            break
        time.sleep(0.05)
        
    # 6. Trigger steering on End Device
    logger.info("Triggering join steering on End Device...")
    dut.write(b"join\n")
    
    # 7. Verify communication
    success = verify_communication(dut, coord_ser, timeout=65)
    coord_ser.close()
    
    if not success:
        pytest.fail("E2E Production Mode Phase Failed: Communication check timed out.")
