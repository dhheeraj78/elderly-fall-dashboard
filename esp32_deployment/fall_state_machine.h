/**
 * @file fall_state_machine.h
 * @brief Configurable Two-Stage Fall Detection State Machine for Wrist Wearables.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 *
 * Architecture:
 *   Stage 1: Peak Acceleration Shock Check (acc_mag_max >= STAGE1_ACC_MAG_THRESHOLD_G = 2.30g)
 *   Stage 2: Random Forest Classifier Probability >= FALL_RF_PROBABILITY_THRESHOLD (0.50)
 *   Confirmation: Soft Post-Impact Immobility Check (Mean Energy <= 0.35 g^2 for 1.0s)
 */

#ifndef FALL_STATE_MACHINE_H
#define FALL_STATE_MACHINE_H

#include "fall_feature_extraction.h"
#include "fall_model_weights.h"
#include "fall_trees.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FALL_SM_IDLE_MONITORING = 0,
    FALL_SM_IMPACT_DETECTED,
    FALL_SM_EVALUATING_IMMOBILITY,
    FALL_SM_FALL_CONFIRMED,
    FALL_SM_SUPPRESSED_ACTIVE_MOTION
} fall_state_t;

typedef struct {
    float stage1_acc_threshold;       /* Stage 1 peak shock limit (STAGE1_ACC_MAG_THRESHOLD_G = 2.30g) */
    float rf_prob_threshold;          /* Stage 2 RF probability threshold (FALL_RF_PROBABILITY_THRESHOLD = 0.50) */
    float immobility_energy_thresh;   /* Soft post-impact mean energy limit (IMMOBILITY_ENERGY_THRESH = 0.35 g^2) */
    int   immobility_duration_samples;/* Samples to monitor post-impact (IMMOBILITY_DURATION_SAMPLES = 50 samples = 1.0s) */
} fall_sm_config_t;

typedef struct {
    fall_state_t current_state;
    fall_sm_config_t config;
    int   immobility_sample_counter;
    float sum_acc_mag_sq_dev;
    float sum_acc_mag;
    float sum_acc_mag_sq;
    float last_rf_prob;
    float last_svm_score;
    float last_acc_mag_max;
    int   alarm_active;
} fall_state_machine_t;

/**
 * @brief Initialize the fall state machine with default or custom parameters.
 */
void fall_sm_init(fall_state_machine_t *sm, const fall_sm_config_t *config);

/**
 * @brief Process one completed window through Adaptive Two-Tier Decision Logic.
 * 
 * @param sm Pointer to state machine context
 * @param features 17-element feature vector extracted from current window
 * @return fall_state_t Current state after window evaluation
 */
fall_state_t fall_sm_process_window(fall_state_machine_t *sm, const float *features);

/**
 * @brief Feed a raw instantaneous acceleration magnitude sample during the post-impact window.
 * 
 * @param sm Pointer to state machine context
 * @param acc_mag Instantaneous acceleration magnitude (g)
 * @return fall_state_t Updated state
 */
fall_state_t fall_sm_feed_sample(fall_state_machine_t *sm, float acc_mag);

/**
 * @brief Reset state machine back to idle monitoring (e.g. after alarm acknowledged).
 */
void fall_sm_reset(fall_state_machine_t *sm);

#ifdef __cplusplus
}
#endif

#endif /* FALL_STATE_MACHINE_H */
