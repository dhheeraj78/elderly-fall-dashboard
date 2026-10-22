/**
 * @file fall_state_machine.c
 * @brief Implementation of two-tier shock check and soft post-impact immobility state machine.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 */

#include "fall_state_machine.h"

void fall_sm_init(fall_state_machine_t *sm, const fall_sm_config_t *config) {
    if (sm == (void*)0) return;

    if (config != (void*)0) {
        sm->config = *config;
    } else {
        /* Default baseline configuration from fall_model_weights.h */
        sm->config.stage1_acc_threshold = STAGE1_ACC_MAG_THRESHOLD_G;     /* 2.30g */
        sm->config.rf_prob_threshold = FALL_RF_PROBABILITY_THRESHOLD;      /* 0.50f */
        sm->config.immobility_energy_thresh = IMMOBILITY_ENERGY_THRESH;    /* 0.35 g^2 */
        sm->config.immobility_duration_samples = IMMOBILITY_DURATION_SAMPLES; /* 50 samples (1.0s at 50 Hz) */
    }

    fall_sm_reset(sm);
}

void fall_sm_reset(fall_state_machine_t *sm) {
    if (sm == (void*)0) return;
    sm->current_state = FALL_SM_IDLE_MONITORING;
    sm->immobility_sample_counter = 0;
    sm->sum_acc_mag_sq_dev = 0.0f;
    sm->sum_acc_mag = 0.0f;
    sm->sum_acc_mag_sq = 0.0f;
    sm->last_rf_prob = 0.0f;
    sm->last_svm_score = 0.0f;
    sm->last_acc_mag_max = 0.0f;
    sm->alarm_active = 0;
}

fall_state_t fall_sm_process_window(fall_state_machine_t *sm, const float *features) {
    if (sm == (void*)0 || features == (void*)0) return FALL_SM_IDLE_MONITORING;

    /* If an alarm is already confirmed, wait for external acknowledge / reset */
    if (sm->current_state == FALL_SM_FALL_CONFIRMED) {
        return sm->current_state;
    }

    /* Feature 3 is acc_mag_max (peak acceleration in the 2.0s window) */
    float acc_mag_max = features[3];
    sm->last_acc_mag_max = acc_mag_max;

    /* Compute upgraded Random Forest probability */
    float rf_prob = predict_rf_probability(features);
    sm->last_rf_prob = rf_prob;

    /* Also compute legacy SVM score for backwards compatibility */
    sm->last_svm_score = fall_standardize_and_score(features);

    /* Stage 1: Peak acceleration shock check (|a| >= stage1_acc_threshold = 2.30g) */
    if (acc_mag_max < sm->config.stage1_acc_threshold) {
        /* Sub-threshold movement: immediately reject without false alarm */
        sm->current_state = FALL_SM_IDLE_MONITORING;
        return sm->current_state;
    }

    /* Stage 2: Classifier confidence probability */
    if (rf_prob >= sm->config.rf_prob_threshold) {
        /* Both stages met: transition to Stage 2 Soft Immobility Observation */
        sm->current_state = FALL_SM_EVALUATING_IMMOBILITY;
        sm->immobility_sample_counter = 0;
        sm->sum_acc_mag_sq_dev = 0.0f;
        sm->sum_acc_mag = 0.0f;
        sm->sum_acc_mag_sq = 0.0f;
        return sm->current_state;
    }

    /* Normal monitoring state */
    sm->current_state = FALL_SM_IDLE_MONITORING;
    return sm->current_state;
}

fall_state_t fall_sm_feed_sample(fall_state_machine_t *sm, float acc_mag) {
    if (sm == (void*)0) return FALL_SM_IDLE_MONITORING;

    if (sm->current_state != FALL_SM_EVALUATING_IMMOBILITY) {
        return sm->current_state;
    }

    /* Measure post-impact deviation energy relative to 1.0g gravity baseline */
    float dev = acc_mag - 1.0f;
    sm->sum_acc_mag_sq_dev += (dev * dev);
    sm->sum_acc_mag += acc_mag;
    sm->sum_acc_mag_sq += (acc_mag * acc_mag);
    sm->immobility_sample_counter++;

    /* Once post-impact observation duration completes (1.0s = 50 samples at 50 Hz) */
    if (sm->immobility_sample_counter >= sm->config.immobility_duration_samples) {
        int n = sm->immobility_sample_counter;
        float mean_energy = sm->sum_acc_mag_sq_dev / (float)n;

        /* Confirm fall if post-impact mean energy <= 0.35 g^2 */
        if (mean_energy <= sm->config.immobility_energy_thresh) {
            sm->current_state = FALL_SM_FALL_CONFIRMED;
            sm->alarm_active = 1;
        } else {
            /* Active recovery / arm movement detected: false alarm suppressed */
            sm->current_state = FALL_SM_SUPPRESSED_ACTIVE_MOTION;
            sm->alarm_active = 0;
        }
    }

    return sm->current_state;
}
