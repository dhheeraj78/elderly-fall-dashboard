#!/usr/bin/env python3
"""
DNA Cryptography Testbench & Software Verifier for FYP ElderCare Telemetry
--------------------------------------------------------------------------
Implements the exact DNA encryption & decryption logic from 'Server Code.docx'
Allows testing the complete cryptographic pipeline locally and against Firebase
without requiring physical hardware connected.
"""

import json
import time
import datetime
import argparse
import requests

FIREBASE_DB_URL = 'https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com'

# ==============================================================================
# DNA CRYPTOGRAPHY ENGINE (From Server Code.docx)
# ==============================================================================

# Mapping dictionaries
DNA_TO_BIN = {'A': '00', 'T': '01', 'C': '10', 'G': '11'}
BIN_TO_DNA = {'00': 'A', '01': 'T', '10': 'C', '11': 'G'}
DNA_BASES = ['A', 'T', 'C', 'G']

def text_to_binary(text: str) -> str:
    """Step 1: Convert plaintext string into an 8-bit binary string."""
    return ''.join(format(ord(c), '08b') for c in text)

def binary_to_dna(binary_data: str) -> str:
    """Step 2: Map binary 2-bit chunks to DNA bases (A, T, C, G)."""
    return ''.join(BIN_TO_DNA[binary_data[i:i+2]] for i in range(0, len(binary_data), 2))

def dna_to_binary(dna_sequence: str) -> str:
    """Step 3: Convert DNA nucleotide sequence back to binary bits."""
    return ''.join(DNA_TO_BIN[base] for base in dna_sequence)

def binary_to_text(binary_data: str) -> str:
    """Step 4: Convert 8-bit binary stream back to ASCII characters."""
    chars = [chr(int(binary_data[i:i+8], 2)) for i in range(0, len(binary_data), 8)]
    return ''.join(chars)

def generate_dna_key(length: int) -> str:
    """Step 5: Generate cyclic DNA key matching the sequence length."""
    return ''.join(DNA_BASES[i % 4] for i in range(length))

def generate_dna_key_using_multiplier(length: int, multiplier: int = 7) -> str:
    """Step 5b: Generate pseudo-random DNA key using a multiplier formula."""
    return ''.join(DNA_BASES[(i * multiplier) % 4] for i in range(length))

def xor_dna_encrypt(plaintext: str, use_multiplier: bool = False) -> tuple[str, str]:
    """
    Encrypts plaintext using DNA XOR cipher.
    Returns: (encrypted_dna, dna_key)
    """
    bin_data = text_to_binary(plaintext)
    dna_seq = binary_to_dna(bin_data)
    
    if use_multiplier:
        dna_key = generate_dna_key_using_multiplier(len(dna_seq))
    else:
        dna_key = generate_dna_key(len(dna_seq))
        
    bin_seq = dna_to_binary(dna_seq)
    bin_key = dna_to_binary(dna_key)
    
    encrypted_bin = ''.join(str(int(bin_seq[i]) ^ int(bin_key[i])) for i in range(len(bin_seq)))
    encrypted_dna = binary_to_dna(encrypted_bin)
    
    return encrypted_dna, dna_key

def xor_dna_decrypt(encrypted_dna: str, use_multiplier: bool = False) -> str:
    """
    Decrypts encrypted DNA sequence back to original plaintext.
    """
    if use_multiplier:
        dna_key = generate_dna_key_using_multiplier(len(encrypted_dna))
    else:
        dna_key = generate_dna_key(len(encrypted_dna))
        
    enc_bin = dna_to_binary(encrypted_dna)
    key_bin = dna_to_binary(dna_key)
    
    decrypted_bin = ''.join(str(int(enc_bin[i]) ^ int(key_bin[i])) for i in range(len(enc_bin)))
    return binary_to_text(decrypted_bin)

# ==============================================================================
# VERIFICATION SUITE & LOCAL BENCHMARKS
# ==============================================================================

def run_local_verification():
    """Runs a complete test suite on sample medical & telemetry payloads."""
    print("=" * 70)
    print("  DNA CRYPTOGRAPHY SOFTWARE TESTBENCH (Verification Mode)")
    print("=" * 70)
    
    test_cases = [
        {
            "name": "Standard Normal Telemetry",
            "payload": {
                "status": "NORMAL",
                "impact": 1.02,
                "lat": 12.971600,
                "lon": 77.594597,
                "hr": 72,
                "spO2": 98,
                "battery": 92
            }
        },
        {
            "name": "Emergency Fall Event Telemetry",
            "payload": {
                "status": "FALL_DETECTED",
                "impact": 3.85,
                "lat": 12.972100,
                "lon": 77.595200,
                "hr": 108,
                "spO2": 94,
                "battery": 88
            }
        },
        {
            "name": "Extreme/Critical Cardiac Vitals",
            "payload": {
                "status": "CRITICAL",
                "impact": 0.98,
                "lat": 12.971650,
                "lon": 77.594620,
                "hr": 135,
                "spO2": 89,
                "battery": 65
            }
        }
    ]
    
    all_passed = True
    for i, tc in enumerate(test_cases, 1):
        print(f"\n--- Test Case #{i}: {tc['name']} ---")
        raw_json = json.dumps(tc['payload'])
        print(f"Plaintext (Original JSON): {raw_json}")
        print(f"Plaintext size:            {len(raw_json)} bytes ({len(raw_json)*8} bits)")
        
        # Benchmark Encryption
        t0 = time.perf_counter()
        encrypted_dna, key = xor_dna_encrypt(raw_json)
        enc_time_ms = (time.perf_counter() - t0) * 1000
        
        print(f"Encrypted DNA Sequence:    {encrypted_dna[:64]}... [{len(encrypted_dna)} bases]")
        print(f"Encryption Latency:        {enc_time_ms:.4f} ms")
        
        # Benchmark Decryption
        t1 = time.perf_counter()
        decrypted_json = xor_dna_decrypt(encrypted_dna)
        dec_time_ms = (time.perf_counter() - t1) * 1000
        
        print(f"Decryption Latency:        {dec_time_ms:.4f} ms")
        print(f"Decrypted Result:          {decrypted_json}")
        
        # Verify lossless roundtrip
        if raw_json == decrypted_json:
            print("Status:                    [PASS] Lossless Roundtrip Confirmed (100% Match)")
        else:
            print("Status:                    [FAIL] Decrypted output mismatch!")
            all_passed = False
            
    print("\n" + "=" * 70)
    if all_passed:
        print("  ALL TEST CASES PASSED SUCCESSFULLY!")
        print("  Mathematical correctness of DNA Encryption/Decryption verified.")
    else:
        print("  TESTS FAILED!")
    print("=" * 70)
    return all_passed

# ==============================================================================
# FIREBASE CLOUD PUSH TEST
# ==============================================================================

def push_sample_to_firebase(status_type: str = "NORMAL"):
    """
    Encrypts a sample telemetry record with DNA Cryptography and pushes it to
    Firebase Realtime Database (telemetry/live) to test cloud interoperability.
    """
    print("=" * 70)
    print(f"  FIREBASE END-TO-END TEST: Pushing Encrypted {status_type} Telemetry")
    print("=" * 70)
    
    now = datetime.datetime.now(datetime.timezone.utc).isoformat()
    if status_type.upper() == "FALL":
        status_val = "FALL_DETECTED"
        impact_val = 3.65
        hr_val = 110
        spo2_val = 95
    else:
        status_val = "NORMAL"
        impact_val = 1.04
        hr_val = 74
        spo2_val = 98

    sample_vitals = {
        "status": status_val,
        "impact": impact_val,
        "lat": 12.971600,
        "lon": 77.594597,
        "hr": hr_val,
        "spO2": spo2_val,
        "battery": 90,
        "timestamp": now
    }
    
    raw_json = json.dumps(sample_vitals)
    print(f"\n1. Original Health Telemetry:")
    print(f"   {raw_json}")
    
    encrypted_dna, key = xor_dna_encrypt(raw_json)
    print(f"\n2. Encrypted into DNA Nucleotide Stream:")
    print(f"   {encrypted_dna[:80]}... (Total {len(encrypted_dna)} bases)")
    
    # Payload saved to Firebase cloud:
    cloud_payload = {
        "encrypted_dna": encrypted_dna,
        "cipher": "DNA-XOR-V1",
        "timestamp": now,
        "is_encrypted": True
    }
    
    live_url = f"{FIREBASE_DB_URL}/telemetry/live.json"
    print(f"\n3. Uploading Encrypted Payload to Firebase Cloud ({live_url})...")
    
    try:
        res = requests.put(live_url, json=cloud_payload, timeout=5)
        if res.status_code == 200:
            print(f"   [SUCCESS] Firebase responded HTTP 200 OK!")
            print(f"   Data in Firebase is now securely encrypted with DNA bases.")
            print(f"\n4. Verifying Client Decryption from Firebase:")
            # Fetch back from Firebase to simulate web client reading it:
            fetched = requests.get(live_url, timeout=5).json()
            decrypted_text = xor_dna_decrypt(fetched["encrypted_dna"])
            recovered = json.loads(decrypted_text)
            print(f"   Recovered HR: {recovered['hr']} bpm, SpO2: {recovered['spO2']}%, Status: {recovered['status']}")
            print(f"   [SUCCESS] End-to-end cloud roundtrip complete!")
        else:
            print(f"   [ERROR] Firebase responded with status {res.status_code}: {res.text}")
    except Exception as e:
        print(f"   [CONNECTION ERROR] {e}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="DNA Cryptography Testbench")
    parser.add_argument("--push", choices=["normal", "fall"], help="Push encrypted sample directly to Firebase")
    args = parser.parse_args()
    
    if args.push:
        push_sample_to_firebase(args.push)
    else:
        run_local_verification()
