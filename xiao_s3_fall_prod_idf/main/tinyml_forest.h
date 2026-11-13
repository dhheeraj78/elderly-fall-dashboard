/**
 * @file tinyml_forest.h
 * @brief Unified Header for TinyML Random Forest (20 Trees) and Kinematic Feature Extraction.
 * Target: Seeed Studio XIAO ESP32-S3 (Dual-Core Xtensa LX7)
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32-S3)
 */

#ifndef TINYML_FOREST_H
#define TINYML_FOREST_H

#include "fall_model_weights.h"
#include "fall_feature_extraction.h"
#include "fall_trees.h"
#include "fall_state_machine.h"
#include "step_counter.h"

#ifdef __cplusplus
extern "C" {
#endif

// Feature names in canonical index order matching python training pipeline
static const char* const FALL_FEATURE_NAMES[17] = {
    "acc_mag_mean",
    "acc_mag_std",
    "acc_mag_min",
    "acc_mag_max",
    "acc_mag_range",
    "acc_mag_rms",
    "gyro_mag_mean",
    "gyro_mag_std",
    "gyro_mag_min",
    "gyro_mag_max",
    "gyro_mag_range",
    "gyro_mag_rms",
    "acc_jerk_mean",
    "acc_jerk_max",
    "post_impact_immobility_var",
    "tilt_differential",
    "peak_acc_jerk_dt"
};

#ifdef __cplusplus
}
#endif

#endif /* TINYML_FOREST_H */

