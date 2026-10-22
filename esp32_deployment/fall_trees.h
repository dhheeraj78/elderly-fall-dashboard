/**
 * @file fall_trees.h
 * @brief Auto-generated C99 Random Forest Decision Trees for ESP32 Wrist Wearable.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 *
 * Ensemble Size: 20 shallow decision trees
 * Feature Count: 17 rotation-invariant wrist features
 * Memory Overhead: 0 bytes dynamic heap (pure flash code constants)
 */

#ifndef FALL_TREES_H
#define FALL_TREES_H

#ifdef __cplusplus
extern "C" {
#endif

#define FALL_RF_NUM_TREES     20
#define FALL_RF_NUM_FEATURES  17

/**
 * @brief Predict fall probability using the 20-tree Random Forest ensemble.
 * 
 * @param features Pointer to 17-element extracted feature array
 * @return float Probability of fall in range [0.0f, 1.0f]
 */
float predict_rf_probability(const float *features);

#ifdef __cplusplus
}
#endif

#endif /* FALL_TREES_H */
