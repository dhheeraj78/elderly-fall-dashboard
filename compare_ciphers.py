#!/usr/bin/env python3
"""
FYP Cryptographic Comparison Benchmark: DNA Cryptography vs. Ascon-128
----------------------------------------------------------------------
Compares performance, ciphertext expansion, entropy, and security guarantees
on identical elderly healthcare & fall telemetry payloads.
"""

import time
import math
import json
from ascon_ref import ascon_encrypt, ascon_decrypt, get_random_bytes
from test_dna_telemetry import xor_dna_encrypt, xor_dna_decrypt

def shannon_entropy(data):
    if isinstance(data, str):
        data = data.encode('utf-8')
    counts = {}
    for b in data:
        counts[b] = counts.get(b, 0) + 1
    ent = 0.0
    for c in counts.values():
        p = c / len(data)
        ent -= p * math.log2(p)
    return ent

def run_benchmark(iterations: int = 100):
    sample_vitals = {
        "status": "NORMAL",
        "impact": 1.02,
        "lat": 12.971600,
        "lon": 77.594597,
        "hr": 74,
        "spO2": 98,
        "battery": 90
    }
    raw_json = json.dumps(sample_vitals)
    raw_bytes = raw_json.encode('utf-8')
    p_len = len(raw_bytes)

    # 1. Benchmark DNA Cryptography
    dna_enc_times = []
    dna_dec_times = []
    for _ in range(iterations):
        t0 = time.perf_counter()
        dna_cipher, key = xor_dna_encrypt(raw_json)
        dna_enc_times.append(time.perf_counter() - t0)
        
        t1 = time.perf_counter()
        dna_plain = xor_dna_decrypt(dna_cipher)
        dna_dec_times.append(time.perf_counter() - t1)

    dna_avg_enc = (sum(dna_enc_times) / len(dna_enc_times)) * 1000
    dna_avg_dec = (sum(dna_dec_times) / len(dna_dec_times)) * 1000
    dna_entropy = shannon_entropy(dna_cipher)
    dna_cipher_len = len(dna_cipher)

    # 2. Benchmark Ascon-128
    ascon_key = get_random_bytes(16)
    ascon_nonce = get_random_bytes(16)
    ad = b'ElderCare-Device-9982'

    ascon_enc_times = []
    ascon_dec_times = []
    for _ in range(iterations):
        t0 = time.perf_counter()
        ascon_cipher = ascon_encrypt(ascon_key, ascon_nonce, ad, raw_bytes, 'Ascon-128')
        ascon_enc_times.append(time.perf_counter() - t0)
        
        t1 = time.perf_counter()
        ascon_plain = ascon_decrypt(ascon_key, ascon_nonce, ad, ascon_cipher, 'Ascon-128')
        ascon_dec_times.append(time.perf_counter() - t1)

    ascon_avg_enc = (sum(ascon_enc_times) / len(ascon_enc_times)) * 1000
    ascon_avg_dec = (sum(ascon_dec_times) / len(ascon_dec_times)) * 1000
    ascon_entropy = shannon_entropy(ascon_cipher)
    ascon_cipher_len = len(ascon_cipher)

    print("=" * 72)
    print("   FYP CRYPTOGRAPHIC BENCHMARK: DNA CRYPTOGRAPHY vs. ASCON-128")
    print("=" * 72)
    print(f"Sample Payload (Plaintext JSON): {raw_json}")
    print(f"Plaintext Payload Size:          {p_len} bytes")
    print(f"Benchmark Iterations:            {iterations}")
    print("-" * 72)
    print(f"{'Metric':<28} | {'DNA Cryptography':<18} | {'Ascon-128 (NIST LWC)':<20}")
    print("-" * 72)
    print(f"{'Encryption Latency':<28} | {dna_avg_enc:.4f} ms{'':<9} | {ascon_avg_enc:.4f} ms")
    print(f"{'Decryption Latency':<28} | {dna_avg_dec:.4f} ms{'':<9} | {ascon_avg_dec:.4f} ms")
    print(f"{'Output Ciphertext Size':<28} | {dna_cipher_len} bases (chars){'':<2} | {ascon_cipher_len} bytes (with Tag)")
    print(f"{'Payload Expansion Ratio':<28} | {dna_cipher_len / p_len:.2f}x{'':<13} | {ascon_cipher_len / p_len:.2f}x")
    print(f"{'Information Entropy':<28} | {dna_entropy:.4f} bits/sym{'':<6} | {ascon_entropy:.4f} bits/byte")
    print(f"{'Tamper Detection (MAC)':<28} | None (Raw XOR){'':<4} | 128-bit Auth Tag")
    print(f"{'Format in JSON':<28} | Native ASCII chars | Requires Base64/Hex")
    print("=" * 72)

if __name__ == '__main__':
    run_benchmark()
