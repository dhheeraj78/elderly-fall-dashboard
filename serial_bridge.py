import sys
import re
import requests
import json
import time
import datetime
import argparse

# ==============================================================================
# CONFIGURATION
# ==============================================================================
PORT = '/dev/tty.usbserial-0001'
BAUD = 115200
FIREBASE_DB_URL = 'https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com'

# ==============================================================================
# DNA CRYPTOGRAPHY ENGINE (From FYP Cryptographic Module)
# ==============================================================================
DNA_TO_BIN = {'A': '00', 'T': '01', 'C': '10', 'G': '11'}
BIN_TO_DNA = {'00': 'A', '01': 'T', '10': 'C', '11': 'G'}
DNA_BASES = ['A', 'T', 'C', 'G']

def text_to_binary(text: str) -> str:
    return ''.join(format(ord(c), '08b') for c in text)

def binary_to_dna(binary_data: str) -> str:
    return ''.join(BIN_TO_DNA[binary_data[i:i+2]] for i in range(0, len(binary_data), 2))

def dna_to_binary(dna_sequence: str) -> str:
    return ''.join(DNA_TO_BIN[base] for base in dna_sequence)

def generate_dna_key(length: int) -> str:
    return ''.join(DNA_BASES[i % 4] for i in range(length))

def xor_dna_encrypt(plaintext: str) -> str:
    bin_data = text_to_binary(plaintext)
    dna_seq = binary_to_dna(bin_data)
    dna_key = generate_dna_key(len(dna_seq))
    
    bin_seq = dna_to_binary(dna_seq)
    bin_key = dna_to_binary(dna_key)
    
    encrypted_bin = ''.join(str(int(bin_seq[i]) ^ int(bin_key[i])) for i in range(len(bin_seq)))
    return binary_to_dna(encrypted_bin)

# ==============================================================================
# TELEMETRY DISPATCH WITH DNA ENCRYPTION
# ==============================================================================
def dispatch_telemetry(payload: dict):
    """
    Encrypts the telemetry payload using DNA Cryptography and dispatches to Firebase.
    """
    raw_json = json.dumps(payload)
    
    t0 = time.perf_counter()
    encrypted_dna = xor_dna_encrypt(raw_json)
    enc_latency_ms = (time.perf_counter() - t0) * 1000
    
    timestamp = payload.get("timestamp") or datetime.datetime.now(datetime.timezone.utc).isoformat()
    status = payload.get("status", "NORMAL")
    
    cloud_payload = {
        "encrypted_dna": encrypted_dna,
        "cipher": "DNA-XOR-V1",
        "timestamp": timestamp,
        "is_encrypted": True
    }
    
    print(f"\n[DNA ENCRYPTED]: {encrypted_dna[:48]}... ({len(encrypted_dna)} bases in {enc_latency_ms:.2f}ms)")
    print(f"[PLAINTEXT]:     HR: {payload.get('hr')} bpm | SpO2: {payload.get('spO2')}% | Status: {status}")
    
    # 1. Update live telemetry state via HTTP PATCH
    live_url = f"{FIREBASE_DB_URL}/telemetry/live.json"
    try:
        r_live = requests.put(live_url, json=cloud_payload, timeout=3)
        print(f"[FIREBASE]:      PATCH telemetry/live -> HTTP {r_live.status_code}")
    except Exception as req_err:
        print(f"[FIREBASE ERR]:  {req_err}")
    
    # 2. When a fall occurs, POST the incident record
    if status == "FALL_DETECTED":
        incidents_url = f"{FIREBASE_DB_URL}/incidents.json"
        print("[ALERT]:         Critical fall detected! POSTing encrypted record to incidents...")
        try:
            r_inc = requests.post(incidents_url, json=cloud_payload, timeout=3)
            print(f"[FIREBASE]:      POST incidents -> HTTP {r_inc.status_code}")
        except Exception as req_err:
            print(f"[FIREBASE ERR]:  {req_err}")

# ==============================================================================
# SIMULATION MODE (Without Hardware Connected)
# ==============================================================================
def run_simulation_loop():
    print("=" * 70)
    print("  STARTING SERIAL BRIDGE IN SOFTWARE SIMULATION MODE (--simulate)")
    print("  Testing DNA Cryptography End-to-End without hardware")
    print("=" * 70)
    
    sim_count = 0
    while True:
        sim_count += 1
        now = datetime.datetime.now(datetime.timezone.utc).isoformat()
        
        # Every 8th cycle simulate a fall event to test emergency alert triggers
        is_fall = (sim_count % 8 == 0)
        
        payload = {
            "status": "FALL_DETECTED" if is_fall else "NORMAL",
            "impact": 3.82 if is_fall else 1.02,
            "lat": 12.971600 + (sim_count * 0.00001),
            "lon": 77.594597 + (sim_count * 0.00001),
            "hr": 105 if is_fall else (70 + (sim_count % 6)),
            "spO2": 95 if is_fall else (98 + (sim_count % 2)),
            "battery": 89,
            "timestamp": now
        }
        
        dispatch_telemetry(payload)
        time.sleep(3.0)

# ==============================================================================
# HARDWARE SERIAL MODE (With ESP32 Connected)
# ==============================================================================
def run_hardware_serial_loop():
    import serial
    print(f"Starting Serial Bridge on {PORT} at {BAUD} baud...")
    
    pattern = re.compile(
        r"HEALTH_MONITOR:\s+Data\s+-\s+Status:\s*(?P<status>[^,]+),\s*Impact:\s*(?P<impact>[-+]?\d*\.\d+|\d+),\s*Lat:\s*(?P<lat>[-+]?\d*\.\d+|\d+),\s*Lon:\s*(?P<lon>[-+]?\d*\.\d+|\d+),\s*HR:\s*(?P<hr>\d+),\s*SpO2:\s*(?P<spo2>\d+)"
    )
    
    while True:
        try:
            with serial.Serial(PORT, BAUD, timeout=1) as ser:
                print(f"Connected to {PORT} successfully.")
                ser.reset_input_buffer()
                while True:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if not line:
                        continue
                    if "HEALTH_MONITOR" in line:
                        print(f"\n[HARDWARE LOG]: {line}")
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
                            
                            dispatch_telemetry(payload)
        except Exception as e:
            print(f"Serial connection lost or error: {e}. Retrying in 3 seconds...")
            time.sleep(3)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="ElderCare Telemetry Serial & DNA Bridge")
    parser.add_argument("--simulate", action="store_true", help="Run in software simulation mode without hardware")
    args = parser.parse_args()
    
    if args.simulate:
        run_simulation_loop()
    else:
        run_hardware_serial_loop()
