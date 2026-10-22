/**
 * @file fall_feature_extraction.h
 * @brief Embedded C Feature Extraction and SVM Inference for ESP32 + MPU6050
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 */

#ifndef FALL_FEATURE_EXTRACTION_H
#define FALL_FEATURE_EXTRACTION_H

#include "fall_model_weights.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract the 17 rotation-invariant features from 6-DOF IMU arrays.
 * 
 * @param acc_x    Array of acceleration X values in [g] (length >= n_samples)
 * @param acc_y    Array of acceleration Y values in [g] (length >= n_samples)
 * @param acc_z    Array of acceleration Z values in [g] (length >= n_samples)
 * @param gyro_x   Array of angular velocity X values in [deg/s] (length >= n_samples)
 * @param gyro_y   Array of angular velocity Y values in [deg/s] (length >= n_samples)
 * @param gyro_z   Array of angular velocity Z values in [deg/s] (length >= n_samples)
 * @param n_samples Number of samples in the window (typically 100 for 2.0s at 50 Hz)
 * @param features_out Destination buffer of at least 17 floats
 */
void fall_extract_features(
    const float *acc_x,
    const float *acc_y,
    const float *acc_z,
    const float *gyro_x,
    const float *gyro_y,
    const float *gyro_z,
    int n_samples,
    float *features_out
);

/**
 * @brief Standardize the 17-element feature vector and compute the SVM decision score.
 * 
 * @param features Pointer to 17 unscaled feature values
 * @return float Decision function score: f(x) = sum(w_i * (x_i - mu_i) / sigma_i) + b
 */
float fall_standardize_and_score(const float *features);

/**
 * @brief Predict binary fall class using a decision threshold.
 * 
 * @param features  Pointer to 17 unscaled feature values
 * @param threshold Decision threshold (e.g. FALL_THRESHOLD_BALANCED_F1 or FALL_THRESHOLD_HIGH_RECALL)
 * @param out_score Optional pointer to store the raw decision score (can be NULL)
 * @return int      1 if Fall detected, 0 if Normal ADL
 */
int fall_predict(const float *features, float threshold, float *out_score);

#ifdef __cplusplus
}
#endif

#endif /* FALL_FEATURE_EXTRACTION_H */
