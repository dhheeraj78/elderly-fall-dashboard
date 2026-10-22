#!/usr/bin/env python3
"""
Offline Wrist-Data Calibration and False-Alarm Tuning Script
Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)

Author: Antigravity AI Engineering Assistant
Purpose:
  1. Ingest real or mock wrist recordings collected via record_wrist_data.py.
  2. Evaluate the waist-trained Linear SVM and Two-Stage State Machine on wrist data.
  3. Report Stage 1 triggers, Stage 2 confirmed false alarms, and False Alarms/Hour (FA/hr).
  4. Perform grid search on calibration sessions only to tune (SVM threshold, immobility variance, duration).
  5. Test tuned parameters on held-out evaluation sessions.
  6. Highlight that ADL calibration measures specificity only, not fall sensitivity.
"""

import os
import sys
import json
import glob
import math
import joblib
import argparse
import numpy as np
import pandas as pd
from typing import Dict, List, Tuple, Any

# Canonical feature names in exact model order
FEATURE_NAMES = [
    "acc_mag_mean", "acc_mag_std", "acc_mag_min", "acc_mag_max", "acc_mag_range", "acc_mag_rms",
    "gyro_mag_mean", "gyro_mag_std", "gyro_mag_min", "gyro_mag_max", "gyro_mag_range", "gyro_mag_rms",
    "acc_jerk_mean", "acc_jerk_max"
]


def extract_features_from_window(acc: np.ndarray, gyro: np.ndarray) -> np.ndarray:
    """Extract the 14 rotation-invariant features matching C implementation bit-for-bit."""
    acc_mag = np.linalg.norm(acc, axis=1)
    gyro_mag = np.linalg.norm(gyro, axis=1)

    # Acceleration stats
    mean_a = float(np.mean(acc_mag))
    std_a  = float(np.std(acc_mag)) # ddof=0 population std
    min_a  = float(np.min(acc_mag))
    max_a  = float(np.max(acc_mag))
    rng_a  = max_a - min_a
    rms_a  = float(np.sqrt(np.mean(acc_mag ** 2)))

    # Gyroscope stats
    mean_g = float(np.mean(gyro_mag))
    std_g  = float(np.std(gyro_mag))
    min_g  = float(np.min(gyro_mag))
    max_g  = float(np.max(gyro_mag))
    rng_g  = max_g - min_g
    rms_g  = float(np.sqrt(np.mean(gyro_mag ** 2)))

    # Jerk stats
    jerk = np.abs(np.diff(acc_mag))
    mean_jerk = float(np.mean(jerk)) if len(jerk) > 0 else 0.0
    max_jerk  = float(np.max(jerk)) if len(jerk) > 0 else 0.0

    return np.array([
        mean_a, std_a, min_a, max_a, rng_a, rms_a,
        mean_g, std_g, min_g, max_g, rng_g, rms_g,
        mean_jerk, max_jerk
    ], dtype=np.float32)


def evaluate_recording_file(
    csv_path: str,
    scaler,
    model,
    svm_threshold: float,
    immobility_variance_thresh: float,
    immobility_duration_sec: float,
    window_sec: float = 2.0,
    step_sec: float = 1.0,
    sample_rate: float = 200.0
) -> Dict[str, Any]:
    """Run two-stage fall detection across one continuous wrist recording."""
    df = pd.read_csv(csv_path)
    if len(df) < int(window_sec * sample_rate):
        return {"total_windows": 0, "stage1_triggers": 0, "stage2_alarms": 0, "duration_s": 0.0}

    acc = df[["acc_x_g", "acc_y_g", "acc_z_g"]].values
    gyro = df[["gyro_x_dps", "gyro_y_dps", "gyro_z_dps"]].values
    acc_mag = np.linalg.norm(acc, axis=1)

    win_samples = int(window_sec * sample_rate)
    step_samples = int(step_sec * sample_rate)
    immobile_samples = int(immobility_duration_sec * sample_rate)

    n_samples = len(df)
    total_windows = 0
    stage1_triggers = 0
    stage2_alarms = 0
    window_scores = []

    for start_idx in range(0, n_samples - win_samples + 1, step_samples):
        end_idx = start_idx + win_samples
        total_windows += 1

        feats = extract_features_from_window(acc[start_idx:end_idx], gyro[start_idx:end_idx])
        feats_scaled = scaler.transform([feats])
        score = float(model.decision_function(feats_scaled)[0])
        window_scores.append(score)

        # Stage 1: Impact trigger check
        if score >= svm_threshold:
            stage1_triggers += 1

            # Stage 2: Post-impact immobility check
            # Look at the window immediately following the trigger
            post_start = end_idx
            post_end = min(n_samples, post_start + immobile_samples)

            if post_end - post_start >= int(immobile_samples * 0.5): # at least 50% window available
                post_acc_mag = acc_mag[post_start:post_end]
                var_motion = float(np.var(post_acc_mag))

                # If motion variance is below threshold, the arm is stationary -> False Alarm confirmed!
                if var_motion < immobility_variance_thresh:
                    stage2_alarms += 1

    duration_s = n_samples / sample_rate

    return {
        "file_name": os.path.basename(csv_path),
        "total_windows": total_windows,
        "duration_s": duration_s,
        "stage1_triggers": stage1_triggers,
        "stage2_alarms": stage2_alarms,
        "max_score": float(np.max(window_scores)) if window_scores else 0.0,
        "mean_score": float(np.mean(window_scores)) if window_scores else 0.0
    }


def run_calibration(
    data_dir: str = "./wrist_data",
    models_dir: str = "./saved_models",
    svm_threshold: float = 0.45,
    immobility_variance: float = 0.05,
    immobility_duration_sec: float = 2.0,
    tune_grid: bool = False
):
    print("=" * 80)
    print(" WRIST-SPECIFIC CALIBRATION & FALSE ALARM TUNING PIPELINE")
    print("=" * 80)

    model_path = os.path.join(models_dir, "linear_svm_mag.joblib")
    scaler_path = os.path.join(models_dir, "scaler_mag.joblib")

    if not os.path.exists(model_path) or not os.path.exists(scaler_path):
        print(f"[ERROR] Model files not found in {models_dir}. Please run train_fall_detection.py first.")
        sys.exit(1)

    model = joblib.load(model_path)
    scaler = joblib.load(scaler_path)

    csv_files = sorted(glob.glob(os.path.join(data_dir, "*.csv")))
    if not csv_files:
        print(f"[ERROR] No recording CSV files found in {data_dir}.")
        print("Please record wrist sessions using record_wrist_data.py first.")
        sys.exit(1)

    print(f"[OK] Found {len(csv_files)} wrist recording sessions in {data_dir}.")

    # Load session manifest if present
    manifest_path = os.path.join(data_dir, "session_manifest.json")
    manifest_dict = {}
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, "r") as f:
                for entry in json.load(f):
                    manifest_dict[entry["file_name"]] = entry
        except Exception:
            pass

    # Split sessions into calibration (60%) and held-out evaluation (40%)
    calib_files = []
    eval_files = []
    for idx, f in enumerate(csv_files):
        if idx % 2 == 0:
            calib_files.append(f)
        else:
            eval_files.append(f)

    print(f"  - Calibration Sessions: {len(calib_files)} files")
    print(f"  - Held-out Eval Sessions: {len(eval_files)} files")

    def evaluate_file_set(files, th_svm, th_var, t_dur):
        results = []
        tot_dur_s = 0.0
        tot_win = 0
        tot_s1 = 0
        tot_s2 = 0

        for fpath in files:
            fname = os.path.basename(fpath)
            res = evaluate_recording_file(
                fpath, scaler, model, th_svm, th_var, t_dur
            )
            act = manifest_dict.get(fname, {}).get("activity_code", fname.split("_")[1] if "_" in fname else "UNK")
            res["activity_code"] = act
            results.append(res)
            tot_dur_s += res["duration_s"]
            tot_win += res["total_windows"]
            tot_s1 += res["stage1_triggers"]
            tot_s2 += res["stage2_alarms"]

        tot_hours = tot_dur_s / 3600.0 if tot_dur_s > 0 else 0.0
        s1_rate_hr = (tot_s1 / tot_hours) if tot_hours > 0 else 0.0
        s2_rate_hr = (tot_s2 / tot_hours) if tot_hours > 0 else 0.0

        return {
            "results": results,
            "total_duration_hours": tot_hours,
            "total_windows": tot_win,
            "stage1_triggers": tot_s1,
            "stage2_alarms": tot_s2,
            "stage1_triggers_per_hour": s1_rate_hr,
            "stage2_false_alarms_per_hour": s2_rate_hr
        }

    # Evaluate Baseline Configuration
    print(f"\n[Evaluating Baseline Parameters: SVM Threshold = {svm_threshold:+.2f}, Immobility Variance = {immobility_variance:.3f} g^2]")
    base_eval = evaluate_file_set(csv_files, svm_threshold, immobility_variance, immobility_duration_sec)

    print("\n" + "=" * 90)
    print(f"{'Activity':<18} | {'File Name':<32} | {'Duration':<10} | {'Stage 1 Triggers':<18} | {'Stage 2 False Alarms':<18}")
    print("=" * 90)
    for r in base_eval["results"]:
        print(f"{r['activity_code']:<18} | {r['file_name']:<32} | {r['duration_s']:6.1f}s   | {r['stage1_triggers']:4d} windows        | {r['stage2_alarms']:4d} alarms")
    print("=" * 90)

    print(f"\nSummary Across All {len(csv_files)} Sessions ({base_eval['total_duration_hours']*60:.1f} monitored minutes):")
    print(f"  - Total Windows Evaluated:            {base_eval['total_windows']:,}")
    print(f"  - Stage 1 Raw Triggers:              {base_eval['stage1_triggers']} ({base_eval['stage1_triggers_per_hour']:.1f} per hour)")
    print(f"  - Stage 2 Confirmed False Alarms:    {base_eval['stage2_alarms']} ({base_eval['stage2_false_alarms_per_hour']:.1f} per hour)")

    # Grid search optimization if requested
    if tune_grid and len(calib_files) > 0:
        print("\n" + "=" * 80)
        print(" GRID SEARCH OPTIMIZATION (Calibration Sessions Only)")
        print("=" * 80)

        best_params = None
        best_fa_rate = float("inf")

        svm_grid = [0.45, 0.75, 1.00, 1.25, 1.50]
        var_grid = [0.02, 0.04, 0.06, 0.08]
        dur_grid = [1.5, 2.0, 2.5]

        for s_th in svm_grid:
            for v_th in var_grid:
                for d_val in dur_grid:
                    m = evaluate_file_set(calib_files, s_th, v_th, d_val)
                    rate = m["stage2_false_alarms_per_hour"]
                    if rate < best_fa_rate:
                        best_fa_rate = rate
                        best_params = (s_th, v_th, d_val)

        print(f"Optimal Parameters Found on Calibration Set:")
        print(f"  - SVM Threshold:        {best_params[0]:+.2f}")
        print(f"  - Immobility Variance:  {best_params[1]:.3f} g^2")
        print(f"  - Immobility Window:    {best_params[2]:.1f} s")
        print(f"  - Calib False Alarm Rate: {best_fa_rate:.2f} FA / hour")

        # Test on held-out evaluation sessions
        print("\n[Validating on Held-Out Evaluation Sessions...]")
        val_eval = evaluate_file_set(eval_files, best_params[0], best_params[1], best_params[2])
        print(f"  - Held-out Eval FA Rate: {val_eval['stage2_false_alarms_per_hour']:.2f} FA / hour ({val_eval['stage2_alarms']} alarms in {val_eval['total_duration_hours']*60:.1f} minutes)")

    print("\n" + "=" * 80)
    print(" CRITICAL SCIENTIFIC DISCLAIMER (REQUIREMENT 7)")
    print("=" * 80)
    print("  Minimizing false alarms on normal wrist activities measures Specificity only.")
    print("  It DOES NOT establish that the model can successfully detect falls on the wrist.")
    print("  To validate Fall Recall / Sensitivity on a wristwatch, testing against an independent")
    print("  dataset with real wrist fall sensor data (such as UMAFall wrist data or safe laboratory")
    print("  crash-mat recordings) is strictly required before clinical deployment.")
    print("=" * 80)


def main():
    parser = argparse.ArgumentParser(description="Calibrate Fall Model on Wrist Recordings")
    parser.add_argument("--data-dir", type=str, default="./wrist_data", help="Directory with wrist CSV recordings")
    parser.add_argument("--models-dir", type=str, default="./saved_models", help="Directory with saved model & scaler")
    parser.add_argument("--svm-threshold", type=float, default=0.45, help="SVM score threshold for Stage 1")
    parser.add_argument("--immobility-variance", type=float, default=0.05, help="Immobility variance threshold (g^2)")
    parser.add_argument("--immobility-duration-sec", type=float, default=2.0, help="Immobility duration (seconds)")
    parser.add_argument("--tune", action="store_true", help="Perform grid search over calibration sessions")
    args = parser.parse_args()

    run_calibration(
        data_dir=args.data_dir,
        models_dir=args.models_dir,
        svm_threshold=args.svm_threshold,
        immobility_variance=args.immobility_variance,
        immobility_duration_sec=args.immobility_duration_sec,
        tune_grid=args.tune
    )


if __name__ == "__main__":
    main()
