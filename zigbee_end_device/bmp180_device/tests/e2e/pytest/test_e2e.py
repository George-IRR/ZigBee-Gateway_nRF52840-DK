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

def test_e2e_communication(dut: DeviceAdapter):
    """
    End-to-End ZigBee Communication Test:
    - End Device (flashed automatically by Twister, accessed via 'dut')
    - Coordinator (pre-flashed or running, accessed via serial port dynamically discovered)
    
    Verifies that the mocked temperature values sent by the End Device over Zigbee
    are correctly received and logged by the Coordinator.
    """
    switch_id, coord_id = get_dev_ids_from_config()
    logger.info(f"Looking for Switch (ED) ID: {switch_id}, Coordinator ID: {coord_id}")
    
    ports = get_serial_ports()
    if coord_id not in ports or not ports[coord_id]:
        pytest.fail(f"Coordinator device {coord_id} not found in connected ports.")
        
    coord_port = ports[coord_id][0] # use vcom 0
    logger.info(f"Coordinator port found: {coord_port}")
    
    # Open Coordinator UART connection
    coord_ser = serial.Serial(coord_port, baudrate=115200, timeout=0.1)
    coord_ser.reset_input_buffer()
    
    # Track sent and received mocked temperatures
    sent_temperatures = []
    received_temperatures = []
    
    start_time = time.time()
    timeout = 90  # 90 seconds timeout for network join & 2-3 reports
    
    logger.info("Starting monitoring of End Device and Coordinator logs...")
    
    while time.time() - start_time < timeout:
        # Read from End Device (via Twister's device adapter)
        ed_line = dut.readline()
        if ed_line:
            ed_line_str = ed_line.strip()
            # Look for: "[TEST MODE] Mocked temperature: 20.0 C"
            m = re.search(r'Mocked temperature:\s*([0-9.-]+)\s*C', ed_line_str)
            if m:
                temp_val = float(m.group(1))
                logger.info(f"[End Device UART] Sent Mock Temperature: {temp_val} C")
                sent_temperatures.append(temp_val)
        
        # Read from Coordinator (via PySerial)
        if coord_ser.in_waiting > 0:
            coord_line = coord_ser.readline()
            try:
                coord_line_str = coord_line.decode('utf-8', errors='ignore').strip()
            except Exception:
                coord_line_str = ""
            if coord_line_str:
                # Look for: "zcl_device_cb - Temperature: 20.0 C" or similar
                m = re.search(r'Temperature:\s*([0-9.-]+)\s*C', coord_line_str)
                if m:
                    temp_val = float(m.group(1))
                    logger.info(f"[Coordinator UART] Received Temperature: {temp_val} C")
                    received_temperatures.append(temp_val)
                    
        # Check if we have successfully matched at least 2 consecutive values
        matched_values = []
        for t in sent_temperatures:
            if t in received_temperatures:
                matched_values.append(t)
                
        if len(matched_values) >= 2:
            logger.info(f"SUCCESS: Matched temperature values {matched_values} between End Device and Coordinator!")
            coord_ser.close()
            return
            
        time.sleep(0.01)
        
    coord_ser.close()
    logger.error(f"E2E Test Timeout. Sent: {sent_temperatures}, Received: {received_temperatures}")
    pytest.fail("E2E Test Failed: Did not match at least 2 mocked temperature transmissions within timeout.")
