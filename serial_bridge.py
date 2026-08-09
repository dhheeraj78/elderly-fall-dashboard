import serial
import re
import requests
import json
import time

PORT = '/dev/tty.usbserial-0001'
BAUD = 115200
URL = 'http://localhost:3000/api/alert'

print(f"Starting Serial Bridge on {PORT}...")

# Pattern to match:
# HEALTH_MONITOR: Data - Status: NORMAL, Impact: 1.03, Lat: 12.971600, Lon: 77.594597, HR: 60, SpO2: 97
pattern = re.compile(
    r"HEALTH_MONITOR:\s+Data\s+-\s+Status:\s*(?P<status>[^,]+),\s*Impact:\s*(?P<impact>[-+]?\d*\.\d+|\d+),\s*Lat:\s*(?P<lat>[-+]?\d*\.\d+|\d+),\s*Lon:\s*(?P<lon>[-+]?\d*\.\d+|\d+),\s*HR:\s*(?P<hr>\d+),\s*SpO2:\s*(?P<spo2>\d+)"
)

while True:
    try:
        with serial.Serial(PORT, BAUD, timeout=1) as ser:
            print(f"Connected to {PORT} successfully.")
            # Clear initial buffer
            ser.reset_input_buffer()
            while True:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if not line:
                    continue
                if "HEALTH_MONITOR" in line:
                    print(f"[Serial LOG]: {line}")
                    match = pattern.search(line)
                    if match:
                        data = match.groupdict()
                        status_raw = data['status'].strip()
                        status = "FALL_DETECTED" if "FALL" in status_raw else "NORMAL"
                        payload = {
                            "status": status,
                            "impact": float(data['impact']),
                            "lat": float(data['lat']),
                            "lon": float(data['lon']),
                            "hr": int(data['hr']),
                            "spo2": int(data['spo2']),
                            "ax": 0.05,
                            "ay": -0.05,
                            "az": float(data['impact'])
                        }
                        print(f"Sending payload: {payload}")
                        try:
                            r = requests.post(URL, json=payload, timeout=2)
                            print(f"Response: {r.status_code} - {r.text}")
                        except Exception as req_err:
                            print(f"HTTP Post Error: {req_err}")
    except Exception as e:
        print(f"Serial connection lost or error: {e}. Retrying in 3 seconds...")
        time.sleep(3)
