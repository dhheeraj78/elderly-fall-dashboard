# Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
## Wearable Fall Detection ML Pipeline using SisFall & Wrist Domain Adaptation

This directory contains the machine learning fall-detection pipeline designed for deployment on an **ESP32 microcontroller** paired with an **MPU6050 6-axis IMU** (3-axis accelerometer + 3-axis gyroscope), secured with **ASCON authenticated encryption**.

---

## 1. Project Overview & Hardware Context

- **Microcontroller:** ESP32 (Tensilica Xtensa 32-bit LX6 dual-core, 240 MHz, 520 KB SRAM)
- **Motion Sensor:** MPU6050 (3-axis Accelerometer up to ±16g + 3-axis Gyroscope up to ±2000 °/s, I2C interface)
- **Target Device Form Factor:** **Wrist-worn smartwatch / fitness band**
- **Primary Source Dataset:** SisFall (*A Fall and Movement Dataset for Elderly People and Adults*, SISTEMIC)
- **Secondary Benchmark Dataset for Wrist Validation:** UMAFall (*University of Malaga Multimodal Fall Dataset*)

> [!WARNING]
> **Domain Shift Advisory (Waist vs. Wrist):**  
> SisFall data was recorded with a sensor securely fastened to the **waist/belt** near the body's center of mass. Your physical wearable is designed for the **wrist**. Models trained strictly on waist data cannot be assumed to generalize directly to the wrist without accounting for arm swing dynamics, gesticulation spikes, and posture variations. A dedicated wrist-domain validation plan is documented in Section 7.

---

## 2. SisFall Dataset Inspection (Phase 1 Verified)

The SisFall dataset was located in `~/Downloads/SisFall_dataset` and linked to `./data/SisFall_dataset`.

| Metric | Verified Value | Documentation Reference |
| :--- | :--- | :--- |
| **Total Participants** | 38 | 23 Young Adults (`SA01`-`SA23`), 15 Elderly (`SE01`-`SE15`) |
| **Total Text Files** | 4,505 | 2,707 ADLs (60.09%) + 1,798 Falls (39.91%) |
| **Total Raw Sensor Rows** | 15,858,929 | ~22.03 hours of continuous motion recordings |
| **Sampling Frequency** | 200.0 Hz | 5 ms interval between consecutive samples |
| **Columns per File** | 9 columns | ASCII comma-delimited, trailing semicolon (;) |
| **Corrupted Rows / Files** | 0 | Every file parses without corrupt lines or NaN |

---

## 3. Sensor Mapping & Physical Conversion

| Column Index | Sensor in SisFall | Full-Scale Range | ADC Resolution | Conversion to Physical Units | Selected for MPU6050? | Rationale |
| :---: | :--- | :---: | :---: | :--- | :---: | :--- |
| **0, 1, 2** | **ADXL345** (Acc X, Y, Z) | **±16g** | 13-bit | `AD / 256.0` [g] | **YES** | Fall impacts exceed 8g (often 10–15g). The ±16g range prevents clipping and matches MPU6050 configured at ±16g. |
| **3, 4, 5** | **ITG3200** (Gyro X, Y, Z) | **±2000 °/s** | 16-bit | `RD / 16.384` [°/s] | **YES** | InvenSense predecessor to MPU6050 gyroscope core. Identical ±2000 °/s full scale and 16-bit architecture. |
| **6, 7, 8** | **MMA8451Q** (Acc X, Y, Z) | ±8g | 14-bit | `AD / 1024.0` [g] | **NO** | Saturates at ±8g during fall impact peaks; redundant accelerometer channel. |

---

## 4. Preprocessing Pipeline & Temporal Labeling (Phase 2 Verified)

### The Fall-Windowing Trap (Solved):
In a 15-second fall recording (3,000 samples at 200 Hz):
- **Seconds 0 to ~4:** Participant walks normally towards the mat (Pre-fall walking).
- **Seconds ~4 to ~6:** Loss of balance, free-fall transition, and high-impact spike ($>3g$ to $16g$).
- **Seconds ~6 to 15:** Participant lies motionless on the mat (Post-fall rest).

Blindly labeling every 2-second window from a fall file as "Fall" corrupts the training set by treating normal walking and quiet resting as falls.  
**Our Solution (`preprocess_sisfall.py`):**
1. We compute acceleration magnitude $A_{mag}(t) = \sqrt{a_x^2 + a_y^2 + a_z^2}$ and identify $t_{impact} = \arg\max(A_{mag})$.
2. Windows covering $[t_{impact} - 1.0s, t_{impact} + 1.0s]$ are labeled as **Fall Impact** (`label = 1`).
3. Windows from ADL recordings are labeled as **ADL** (`label = 0`).
4. Windows from fall files outside the impact zone (`PRE_FALL` and `POST_FALL`) are tagged explicitly and excluded from the primary binary classifier to preserve label fidelity.

### Participant-Independent Data Splits:
To ensure zero data leakage, subjects are strictly partitioned:
- **TRAIN (26 subjects):** `SA01`–`SA16`, `SE01`–`SE05`, `SE07`–`SE11`
- **VAL (6 subjects):** `SA17`–`SA19`, `SE06`, `SE12`, `SE13` *(includes `SE06`, the judo expert who simulated falls)*
- **TEST (6 subjects):** `SA20`–`SA23`, `SE14`, `SE15`

### Quantitative Split Summary (`split_summary.csv`):
| Split | Subjects | Recordings | Total Windows | ADL Windows | Fall Impact Windows | Fall Class % |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **TRAIN** | 26 | 3,038 | **38,450** | 33,646 | 4,804 | 12.49% |
| **VAL** | 6 | 733 | **9,062** | 7,861 | 1,201 | 13.25% |
| **TEST** | 6 | 734 | **9,047** | 7,852 | 1,195 | 13.21% |
| **TOTAL** | **38** | **4,505** | **56,559** | **49,359** | **7,200** | **12.73%** |

---

## 5. Downsampling Evaluation: 200 Hz vs. 100 Hz vs. 50 Hz

We tested downsampling across multiple fall recordings using an 8th-order Chebyshev anti-aliasing filter:
* **Impulse Durations:** Fall impact transients last only **10 to 25 milliseconds**.
* **Peak Attenuation:** Downsampling from 200 Hz to 50 Hz attenuates sharp slip impact peaks by **30% to 70%** (e.g., a 16.2g peak drops to 4.8g) because the filter smooths the instantaneous spike over a 20 ms interval.
* **Firmware Consistency:** If the model is trained at 200 Hz, the ESP32 firmware **must** sample at 200 Hz. If the ESP32 samples at 50 Hz to conserve battery and I2C bandwidth, the model **must** be trained with `--sample-rate 50` so feature distributions match.

---

## 6. Generated Visualizations & Plots

The following plots were generated and saved in `./plots/`:
1. `plots/fall_signals_analysis.png`: Acceleration and angular velocity traces for 4 representative fall scenarios (slip, trip, fainting, sit-down fall) highlighting the impact window vs. pre/post resting phases.
2. `plots/adl_signals_analysis.png`: Profiles for normal walking (`D01`), chair transfers (`D07`), stumbles (`D18`), and gentle jumps (`D19`).
3. `plots/waist_vs_wrist_dynamics.png`: Kinematic comparison contrasting waist acceleration/rotation with wrist arm-swing dynamics.
4. `plots/downsampling_effect_analysis.png`: Comparison of full recordings and zoomed 1.0-second impact spikes across 200 Hz, 100 Hz, and 50 Hz.

---

## 7. UMAFall Dataset Investigation & Wrist Validation Plan

### SisFall vs. UMAFall Comparison:
| Dimension | SisFall Dataset | UMAFall Dataset | Cross-Domain Consideration |
| :--- | :--- | :--- | :--- |
| **Sensor Placement** | **Waist only** (belt) | **Wrist**, Waist, Chest, Ankle, Pocket | UMAFall includes a dedicated wrist sensor. |
| **Sensors** | ADXL345 (±16g), ITG3200 (±2000°/s), MMA8451Q (±8g) | 3-axis Accel + 3-axis Gyro | Both provide 6-DOF IMU data. |
| **Native Sampling Rate** | **200 Hz** | **100 Hz** | Downsample SisFall to 100 Hz (or 50 Hz for both) before cross-evaluating. |
| **Units** | Raw bits converted to $g$ and $^\circ/s$ | Pre-converted to $g$ and $^\circ/s$ | Physical units match. |
| **Subjects** | 38 (23 young adults, 15 elderly) | 17 (young adult volunteers only) | SisFall offers superior age diversity. |
| **Activities** | 19 ADLs + 15 Falls (4,505 files) | 8 ADLs + 3 Falls | SisFall has broader activity coverage. |

### Wrist Validation Strategy:
1. **Do not merge blindly:** Sensor axis orientation on a watch depends on whether it is worn on the left or right arm and facing inward or outward.
2. **Prioritize Rotation-Invariant Features:** Rely heavily on vector magnitude features ($A_{mag}$, $\omega_{mag}$, energy, jerk) that are mathematically independent of sensor orientation.
3. **Two-Stage Validation:**
   - *Stage 1 (Benchmark):* Train on SisFall waist data to learn fundamental whole-body impact physics.
   - *Stage 2 (Wrist Benchmark):* Evaluate the model against UMAFall's wrist recordings to quantify the False Positive Rate (FPR) induced by arm swings and typing, tuning decision thresholds to prevent false alarms on wrist wearables.

---

## 8. Reproducibility & Commands

```bash
# 1. Activate Virtual Environment
source "FINAL YEAR PROJECT/venv/bin/activate"

# 2. Inspect Dataset (Phase 1)
python3 dataset_inspection.py

# 3. Run Preprocessing Pipeline (Phase 2)
python3 preprocess_sisfall.py --window-sec 2.0 --step-sec 1.0 --sample-rate 200.0

# 4. Train Models & Tune Thresholds (Phase 3)
python3 train_fall_detection.py --target-recall 0.85

# 5. Evaluate on Untouched Test Set (Phase 3)
python3 evaluate_model.py
```

---

## 9. Phase 3 Test Set Evaluation Summary

| Model Configuration | Fall Recall | Precision | Specificity | FPR | F1-Score | Accuracy | ROC-AUC |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1. Simple Acc Mag (>= 3.0g)** | 47.62% | 33.49% | 85.61% | 14.39% | 0.3932 | 80.59% | 0.7256 |
| **2. Linear SVM Mag (Default th=0.0)** | 67.70% | 50.69% | 89.98% | 10.02% | 0.5797 | 87.03% | 0.8733 |
| **3. Linear SVM Mag (Tuned Balanced th=+0.45)** | 51.97% | **73.84%** | **97.20%** | **2.80%** | **0.6100** | **91.22%** | 0.8733 |
| **4. Linear SVM Mag (Tuned High-Recall th=-0.35)** | **86.53%** | 27.90% | 65.97% | 34.03% | 0.4220 | 68.69% | 0.8733 |
| **5. Linear SVM All 38 Feats (Waist Benchmark)** | 83.10% | 55.91% | 90.03% | 9.97% | 0.6685 | 89.11% | **0.9356** |

- Artifacts:
  - Saved models: `saved_models/linear_svm_mag.joblib`, `saved_models/scaler_mag.joblib`, `saved_models/model_metadata.json`
  - Plots: `plots/evaluation_roc_curves.png`, `plots/evaluation_confusion_matrices.png`
  - Report: `reports/model_evaluation_report.md`

