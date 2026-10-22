#!/usr/bin/env python3
"""
MPU6050 Wrist Data Collection & Quality Audit Program
Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)

Author: Antigravity AI Engineering Assistant
Purpose:
  1. Record timestamped 6-DOF IMU data (Acc X,Y,Z in [g], Gyro X,Y,Z in [deg/s]) from ESP32 at 200 Hz.
  2. Perform real-time quality checks: sampling rate stability, dropped packets, sensor clipping/saturation.
  3. Support safe, structured normal-activity recording sessions without dangerous fall simulations.
  4. Save structured CSV datasets into wrist_data/ with session metadata.
"""

import os
import sys
import time
import json
import math
import argparse
import datetime
from typing import Dict, List, Tuple, Any

# Safe Activity Directory
SAFE_ACTIVITIES = {
    "SIT_REST": {
        "name": "Sitting at rest / Reading",
        "description": "Seated comfortably in a chair, resting hands on lap or table.",
        "target_duration_s": 120,
        "instructions": "Sit quietly for 2 minutes. Minimize intentional movements."
    },
    "TYPING_DESK": {
        "name": "Desk typing and mouse work",
        "description": "Working on laptop or computer, typing and using mouse.",
        "target_duration_s": 120,
        "instructions": "Type on keyboard and move mouse naturally for 2 minutes."
    },
    "STANDING_STILL": {
        "name": "Standing still",
        "description": "Standing upright with arms hanging naturally.",
        "target_duration_s": 120,
        "instructions": "Stand quietly for 2 minutes."
    },
    "NORMAL_WALK": {
        "name": "Normal walking",
        "description": "Walking at a natural, comfortable pace indoors.",
        "target_duration_s": 180,
        "instructions": "Walk at normal speed along a hallway or room for 3 minutes."
    },
    "BRISK_WALK": {
        "name": "Brisk walking",
        "description": "Walking at an energetic, faster pace without running.",
        "target_duration_s": 180,
        "instructions": "Walk briskly for 3 minutes. Natural arm swing."
    },
    "CHAIR_TRANSFERS": {
        "name": "Chair sit-to-stand transitions",
        "description": "Sitting down and standing up from an ordinary chair 10 times.",
        "target_duration_s": 120,
        "instructions": "Sit down slowly, wait 3 seconds, stand up. Repeat 10 times."
    },
    "HAND_GESTURES": {
        "name": "Ordinary hand gestures & drinking",
        "description": "Conversational gesturing, reaching for a water bottle, drinking.",
        "target_duration_s": 120,
        "instructions": "Talk and gesture with hands, pick up a cup, take a sip, place it down."
    },
    "OBJECT_PICKUP": {
        "name": "Picking up objects from floor",
        "description": "Bending at knees to pick up a light object from the floor.",
        "target_duration_s": 120,
        "instructions": "Bending down to floor, picking up a pen or book, standing back up 5 times."
    },
    "STAIRS_SAFE": {
        "name": "Ascending and descending stairs (with handrail)",
        "description": "Walking up and down stairs safely, holding handrail.",
        "target_duration_s": 120,
        "instructions": "Walk up and down stairs at a calm pace holding the handrail where available."
    }
}


def generate_mock_stream(activity_code: str, duration_s: float):
    """
    Generate realistic synthetic wrist motion at 200 Hz for development / pipeline validation
    when hardware is not physically plugged in.
    """
    dt = 0.005  # 200 Hz
    total_samples = int(duration_s / dt)
    t = 0.0
    start_ms = int(time.time() * 1000)

    for i in range(total_samples):
        # Base gravity vector (~1.0g pointing downward on arm)
        ax = 0.05 * math.sin(t * 0.5)
        ay = 0.98 + 0.02 * math.sin(t * 0.3)
        az = 0.10 * math.cos(t * 0.4)
        gx = 2.0 * math.sin(t)
        gy = 1.5 * math.cos(t)
        gz = 1.0 * math.sin(t * 1.5)

        if activity_code == "TYPING_DESK":
            # Small, rapid key-strike impulses on wrist
            key_tap = 0.25 * math.sin(t * 40.0) if (i % 30 < 5) else 0.0
            ay = 0.95 + key_tap
            az = 0.25 + 0.1 * math.sin(t * 5.0)
            gx = 8.0 * math.sin(t * 4.0)

        elif activity_code in ["NORMAL_WALK", "BRISK_WALK"]:
            # Rhythmic arm swing
            f_step = 1.8 if activity_code == "NORMAL_WALK" else 2.4
            swing_amp = 0.6 if activity_code == "NORMAL_WALK" else 1.2
            gyro_amp = 120.0 if activity_code == "NORMAL_WALK" else 220.0

            ay = 1.0 + swing_amp * math.sin(2.0 * math.pi * f_step * t)
            ax = 0.3 * math.cos(2.0 * math.pi * f_step * t)
            az = 0.2 * math.sin(2.0 * math.pi * f_step * t + 0.5)
            gx = gyro_amp * math.sin(2.0 * math.pi * f_step * t)
            gy = (gyro_amp * 0.4) * math.cos(2.0 * math.pi * f_step * t)

        elif activity_code == "HAND_GESTURES":
            # Sudden gesturing rotational velocity bursts
            if (i % 600) < 150:
                gx = 320.0 * math.sin(t * 6.0)
                gy = 180.0 * math.cos(t * 4.0)
                ay = 1.0 + 0.7 * math.sin(t * 5.0)

        elif activity_code == "CHAIR_TRANSFERS":
            # Periodic torso tilt and sit acceleration transition
            if (i % 800) < 200:
                ay = 0.4 + 0.5 * math.cos(t * 1.5)
                az = 0.8 * math.sin(t * 1.5)
                gx = 75.0 * math.sin(t * 2.0)

        # Add slight sensor noise
        noise_a = ((i % 7) - 3) * 0.003
        noise_g = ((i % 11) - 5) * 0.15

        current_ms = start_ms + int(t * 1000)
        yield current_ms, ax + noise_a, ay + noise_a, az + noise_a, gx + noise_g, gy + noise_g, gz + noise_g
        t += dt


def record_session(
    activity_code: str,
    session_id: str,
    output_dir: str = "./wrist_data",
    duration_s: float = 60.0,
    port: str = "",
    baud: int = 115200,
    mock: bool = False
) -> str:
    """Record a single safe activity session and save to CSV with quality audit."""
    os.makedirs(output_dir, exist_ok=True)

    if activity_code not in SAFE_ACTIVITIES:
        print(f"[ERROR] Unknown activity code '{activity_code}'.")
        print(f"Available safe activities: {', '.join(SAFE_ACTIVITIES.keys())}")
        sys.exit(1)

    act_info = SAFE_ACTIVITIES[activity_code]
    ts_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_fname = f"{session_id}_{activity_code}_{ts_str}.csv"
    csv_path = os.path.join(output_dir, csv_fname)

    print("=" * 80)
    print(f" WRIST DATA COLLECTION SESSION: {session_id}")
    print(f" Activity: {act_info['name']} ({activity_code})")
    print(f" Target Duration: {duration_s:.1f} seconds | Expected Rate: 200 Hz")
    print(f" Instructions: {act_info['instructions']}")
    print("=" * 80)

    # Prepare data quality trackers
    samples_recorded = 0
    dropped_packets = 0
    saturated_acc_count = 0
    saturated_gyro_count = 0
    prev_timestamp_ms = None
    delta_t_list = []

    rows = []

    t_start = time.time()

    if mock or not port:
        print("[INFO] Operating in SIMULATION / MOCK SENSOR mode.")
        print("Streaming synthetic 200 Hz MPU6050 readings matching wrist dynamics...\n")
        generator = generate_mock_stream(activity_code, duration_s)

        for ms, ax, ay, az, gx, gy, gz in generator:
            samples_recorded += 1
            if prev_timestamp_ms is not None:
                dt = ms - prev_timestamp_ms
                delta_t_list.append(dt)
                if dt > 8:  # expected 5 ms at 200 Hz
                    dropped_packets += 1

            prev_timestamp_ms = ms

            if abs(ax) >= 15.8 or abs(ay) >= 15.8 or abs(az) >= 15.8:
                saturated_acc_count += 1
            if abs(gx) >= 1980.0 or abs(gy) >= 1980.0 or abs(gz) >= 1980.0:
                saturated_gyro_count += 1

            rows.append({
                "timestamp_ms": ms,
                "acc_x_g": round(ax, 5),
                "acc_y_g": round(ay, 5),
                "acc_z_g": round(az, 5),
                "gyro_x_dps": round(gx, 3),
                "gyro_y_dps": round(gy, 3),
                "gyro_z_dps": round(gz, 3)
            })

    else:
        # Hardware serial streaming mode
        import serial
        print(f"[INFO] Connecting to hardware serial on {port} at {baud} baud...")
        try:
            ser = serial.Serial(port, baud, timeout=1.0)
            ser.reset_input_buffer()
            print("[OK] Connected. Recording begins now. Follow activity instructions.")

            while (time.time() - t_start) < duration_s:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                parts = line.split(",")
                if len(parts) != 7:
                    continue

                try:
                    ms = int(parts[0])
                    ax = float(parts[1])
                    ay = float(parts[2])
                    az = float(parts[3])
                    gx = float(parts[4])
                    gy = float(parts[5])
                    gz = float(parts[6])
                except ValueError:
                    continue

                samples_recorded += 1
                if prev_timestamp_ms is not None:
                    dt = ms - prev_timestamp_ms
                    delta_t_list.append(dt)
                    if dt > 8:
                        dropped_packets += 1
                prev_timestamp_ms = ms

                if abs(ax) >= 15.8 or abs(ay) >= 15.8 or abs(az) >= 15.8:
                    saturated_acc_count += 1
                if abs(gx) >= 1980.0 or abs(gy) >= 1980.0 or abs(gz) >= 1980.0:
                    saturated_gyro_count += 1

                rows.append({
                    "timestamp_ms": ms,
                    "acc_x_g": round(ax, 5),
                    "acc_y_g": round(ay, 5),
                    "acc_z_g": round(az, 5),
                    "gyro_x_dps": round(gx, 3),
                    "gyro_y_dps": round(gy, 3),
                    "gyro_z_dps": round(gz, 3)
                })

            ser.close()
        except Exception as e:
            print(f"[ERROR] Failed to read from serial port: {e}")
            sys.exit(1)

    actual_duration_s = time.time() - t_start
    effective_rate_hz = samples_recorded / actual_duration_s if actual_duration_s > 0 else 0.0

    # Write output CSV
    import csv
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "timestamp_ms", "acc_x_g", "acc_y_g", "acc_z_g", "gyro_x_dps", "gyro_y_dps", "gyro_z_dps"
        ])
        writer.writeheader()
        writer.writerows(rows)

    # Append to Session Manifest JSON
    manifest_path = os.path.join(output_dir, "session_manifest.json")
    manifest = []
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
        except Exception:
            manifest = []

    session_entry = {
        "session_id": session_id,
        "activity_code": activity_code,
        "activity_name": act_info["name"],
        "file_name": csv_fname,
        "file_path": os.path.abspath(csv_path),
        "recorded_at": ts_str,
        "duration_seconds": round(actual_duration_s, 2),
        "total_samples": samples_recorded,
        "effective_sampling_rate_hz": round(effective_rate_hz, 1),
        "dropped_packets": dropped_packets,
        "saturated_accel_samples": saturated_acc_count,
        "saturated_gyro_samples": saturated_gyro_count,
        "data_quality_pass": (dropped_packets == 0 and saturated_acc_count == 0)
    }
    manifest.append(session_entry)

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print("\n" + "=" * 80)
    print(" SESSION RECORDING COMPLETE & QUALITY AUDIT SUMMARY")
    print("=" * 80)
    print(f"  - Output File:               {csv_path}")
    print(f"  - Total Samples Recorded:    {samples_recorded:,}")
    print(f"  - Elapsed Duration:          {actual_duration_s:.2f} seconds")
    print(f"  - Effective Sampling Rate:   {effective_rate_hz:.1f} Hz (Target: 200.0 Hz)")
    print(f"  - Dropped Packets (> 8 ms):  {dropped_packets}")
    print(f"  - Saturated Accel (±16g):    {saturated_acc_count}")
    print(f"  - Saturated Gyro (±2000°/s): {saturated_gyro_count}")
    print(f"  - Data Quality Status:       {'PASS' if session_entry['data_quality_pass'] else 'WARNING'}")
    print("=" * 80)

    return csv_path


def main():
    parser = argparse.ArgumentParser(description="Record 200 Hz MPU6050 Wrist Data")
    parser.add_argument("--activity", type=str, default="NORMAL_WALK", choices=list(SAFE_ACTIVITIES.keys()), help="Safe activity code")
    parser.add_argument("--session-id", type=str, default="SES01", help="Session identifier (e.g. SES01)")
    parser.add_argument("--duration", type=float, default=60.0, help="Recording duration in seconds")
    parser.add_argument("--output-dir", type=str, default="./wrist_data", help="Output directory")
    parser.add_argument("--port", type=str, default="", help="Serial port (e.g. /dev/tty.usbserial-0001)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--mock", action="store_true", help="Generate realistic synthetic wrist motion without hardware")
    args = parser.parse_args()

    record_session(
        activity_code=args.activity,
        session_id=args.session_id,
        output_dir=args.output_dir,
        duration_s=args.duration,
        port=args.port,
        baud=args.baud,
        mock=args.mock
    )


if __name__ == "__main__":
    main()
