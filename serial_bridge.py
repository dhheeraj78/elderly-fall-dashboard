import serial
import re
import requests
import json
import time
import datetime

PORT = '/dev/tty.usbserial-0001'
BAUD = 115200
FIREBASE_DB_URL = 'https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com'

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
                        timestamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
                        
                        payload = {
                            "status": status,
                            "impact": float(data['impact']),
                            "lat": float(data['lat']),
                            "lon": float(data['lon']),
                            "hr": int(data['hr']),
                            "spO2": int(data['spo2']),
                            "battery": 89,
                            "timestamp": timestamp
                        }
                        
                        # 1. Update live telemetry state via HTTP PATCH
                        live_url = f"{FIREBASE_DB_URL}/telemetry/live.json"
                        print(f"PATCHing live telemetry: {payload}")
                        try:
                            r_live = requests.patch(live_url, json=payload, timeout=2)
                            print(f"Firebase Match Response: {r_live.status_code}")
                        except Exception as req_err:
                            print(f"Firebase PATCH Error: {req_err}")
                        
                        # 2. When a fall occurs, POST the incident record
                        if status == "FALL_DETECTED":
                            incidents_url = f"{FIREBASE_DB_URL}/incidents.json"
                            print("Fall incident detected! POSTing event record to Firebase...")
                            try:
                                r_inc = requests.post(incidents_url, json=payload, timeout=2)
                                print(f"Firebase Incident Response: {r_inc.status_code}")
                            except Exception as req_err:
                                print(f"Firebase POST Error: {req_err}")
    except Exception as e:
        print(f"Serial connection lost or error: {e}. Retrying in 3 seconds...")
        time.sleep(3)
