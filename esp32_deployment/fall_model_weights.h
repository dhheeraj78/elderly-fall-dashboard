/**
 * @file fall_model_weights.h
 * @brief Model weights, scaling parameters, and two-stage gating limits for ESP32 fall detection.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 *
 * Training Base: UMAFall Wrist Kinematics Dataset (50 Hz, 2.0s Windows)
 * Features: 17 rotation-invariant wrist features (14 magnitude + 3 kinetic discriminators)
 */

#ifndef FALL_MODEL_WEIGHTS_H
#define FALL_MODEL_WEIGHTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Preprocessing & Windowing Specifications */
#define FALL_MODEL_NUM_FEATURES        17
#define FALL_MODEL_SAMPLING_RATE_HZ    50.0f
#define FALL_MODEL_WINDOW_SEC          2.0f
#define FALL_MODEL_STEP_SEC            1.0f
#define FALL_MODEL_WINDOW_SAMPLES      100   /* 2.0s * 50 Hz */
#define FALL_MODEL_STEP_SAMPLES        50    /* 1.0s * 50 Hz (50% overlap) */

/* Adaptive Two-Stage Soft-Window Calibration Parameters */
#define STAGE1_ACC_MAG_THRESHOLD_G     2.30f /* Stage 1: Peak shock check (|a| >= 2.30g captures soft falls) */
#define FALL_RF_PROBABILITY_THRESHOLD  0.50f /* Stage 2: Random Forest confidence probability */
#define IMMOBILITY_ENERGY_THRESH       0.35f /* Soft post-impact energy limit (g^2) */
#define IMMOBILITY_DURATION_SAMPLES    50    /* 1.0s post-impact at 50 Hz */

/* Linear SVM Decision Thresholds (Legacy Reference / Comparative Baseline) */
#define FALL_THRESHOLD_BALANCED_F1     -0.0500f /* Balanced F1 SVM boundary */
#define FALL_THRESHOLD_HIGH_RECALL    -0.4000f /* High Sensitivity SVM boundary */

/* Feature Names in Canonical Extraction Order:
 *   0: acc_mag_mean
 *   1: acc_mag_std
 *   2: acc_mag_min
 *   3: acc_mag_max
 *   4: acc_mag_range
 *   5: acc_mag_rms
 *   6: gyro_mag_mean
 *   7: gyro_mag_std
 *   8: gyro_mag_min
 *   9: gyro_mag_max
 *  10: gyro_mag_range
 *  11: gyro_mag_rms
 *  12: acc_jerk_mean
 *  13: acc_jerk_max
 *  14: post_impact_immobility_var
 *  15: tilt_differential
 *  16: peak_acc_jerk_dt
 */

/* StandardScaler Training Means (mu_i) */
static const float FALL_SCALER_MEANS[FALL_MODEL_NUM_FEATURES] = {
      1.08605037f,  /*  0: acc_mag_mean */
      0.28272968f,  /*  1: acc_mag_std */
      0.69030529f,  /*  2: acc_mag_min */
      2.09402904f,  /*  3: acc_mag_max */
      1.40372375f,  /*  4: acc_mag_range */
      1.15819916f,  /*  5: acc_mag_rms */
     67.57954455f,  /*  6: gyro_mag_mean */
     43.58844572f,  /*  7: gyro_mag_std */
      9.39433065f,  /*  8: gyro_mag_min */
    182.49229685f,  /*  9: gyro_mag_max */
    173.09796620f,  /* 10: gyro_mag_range */
     81.64147352f,  /* 11: gyro_mag_rms */
      0.07788168f,  /* 12: acc_jerk_mean */
      0.55352404f,  /* 13: acc_jerk_max */
      0.17789046f,  /* 14: post_impact_immobility_var */
     26.50710654f,  /* 15: tilt_differential */
     27.67620187f  /* 16: peak_acc_jerk_dt */
};

/* StandardScaler Training Scales (sigma_i) */
static const float FALL_SCALER_SCALES[FALL_MODEL_NUM_FEATURES] = {
      0.16617362f,  /*  0: acc_mag_mean */
      0.36862064f,  /*  1: acc_mag_std */
      0.27288374f,  /*  2: acc_mag_min */
      1.75008968f,  /*  3: acc_mag_max */
      1.92429531f,  /*  4: acc_mag_range */
      0.28550139f,  /*  5: acc_mag_rms */
     54.34276138f,  /*  6: gyro_mag_mean */
     32.85223114f,  /*  7: gyro_mag_std */
     11.64813981f,  /*  8: gyro_mag_min */
    127.78944240f,  /*  9: gyro_mag_max */
    122.28315957f,  /* 10: gyro_mag_range */
     61.91948037f,  /* 11: gyro_mag_rms */
      0.11306470f,  /* 12: acc_jerk_mean */
      0.94474361f,  /* 13: acc_jerk_max */
      0.55359282f,  /* 14: post_impact_immobility_var */
     34.77157550f,  /* 15: tilt_differential */
     47.23718041f  /* 16: peak_acc_jerk_dt */
};

/* Linear SVM Model Weights (w_i) */
static const float FALL_SVM_WEIGHTS[FALL_MODEL_NUM_FEATURES] = {
     +0.10976834f,  /*  0: acc_mag_mean */
     -1.08428345f,  /*  1: acc_mag_std */
     -0.34226972f,  /*  2: acc_mag_min */
     +0.38822830f,  /*  3: acc_mag_max */
     +0.40161932f,  /*  4: acc_mag_range */
     +0.68024905f,  /*  5: acc_mag_rms */
     -0.13325599f,  /*  6: gyro_mag_mean */
     +0.01059537f,  /*  7: gyro_mag_std */
     +0.00894560f,  /*  8: gyro_mag_min */
     +0.23462128f,  /*  9: gyro_mag_max */
     +0.24433391f,  /* 10: gyro_mag_range */
     -0.22281181f,  /* 11: gyro_mag_rms */
     -0.87722056f,  /* 12: acc_jerk_mean */
     +0.17305891f,  /* 13: acc_jerk_max */
     -0.09333614f,  /* 14: post_impact_immobility_var */
     +0.07903423f,  /* 15: tilt_differential */
     +0.17305891f  /* 16: peak_acc_jerk_dt */
};

/* Linear SVM Bias / Intercept */
static const float FALL_SVM_INTERCEPT = -0.40102712f;

#ifdef __cplusplus
}
#endif

#endif /* FALL_MODEL_WEIGHTS_H */
