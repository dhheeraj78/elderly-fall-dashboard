/**
 * @file step_counter.h
 * @brief Real-time Pedometer & Daily Auto-Reset Engine for Wrist-Worn Wearables.
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32 + MPU6050)
 *
 * Algorithm Specifications:
 * -----------------------------------------------------------------------------------------
 * 1. Rotation-Invariant Magnitude:
 *      Computes ||a|| = sqrt(ax^2 + ay^2 + az^2) to allow arbitrary wrist orientation.
 * 2. Exponential Moving Average (EMA) Gravity Baseline:
 *      ema_accel = 0.05 * ||a|| + 0.95 * ema_accel (alpha = 0.05).
 * 3. Adaptive Dynamic Threshold:
 *      dyn_thresh = ema_accel + 0.22g.
 * 4. Human Gait Cadence Window:
 *      Rejects intervals < 280 ms (> 214 steps/min, mechanical vibration / jitter)
 *      Rejects intervals > 1200 ms (< 50 steps/min, pause in walking).
 * 5. 4-Step Debounce Streak:
 *      Requires 4 consecutive rhythmic steps before confirming a walking bout to
 *      completely eliminate false steps from typing, fidgeting, scratching, or hand gestures.
 * 6. Metric Calculations:
 *      Distance: ~0.75 m/step (0.00075 km/step).
 *      Calories: ~0.040 kcal/step.
 * 7. Midnight Auto-Reset:
 *      Monitors GPS UTC date (dd) or RTC. When day rolls over, daily counters reset to 0.
 * -----------------------------------------------------------------------------------------
 */

#ifndef STEP_COUNTER_H
#define STEP_COUNTER_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEDOMETER_MIN_STEP_INTERVAL_MS   280   /* Max ~214 steps/min (running cadence limit) */
#define PEDOMETER_MAX_STEP_INTERVAL_MS   1200  /* Min ~50 steps/min (walking cadence limit) */
#define PEDOMETER_DEBOUNCE_STREAK_LIMIT  4     /* 4 consecutive steps required to credit bout */
#define PEDOMETER_STRIDE_KM_PER_STEP     0.00075f /* 0.75 m average adult stride */
#define PEDOMETER_KCAL_PER_STEP          0.040f   /* 0.040 kcal average per step */
#define PEDOMETER_EMA_ALPHA              0.05f    /* Smoothing factor for gravity tracking */
#define PEDOMETER_DYNAMIC_THRESHOLD_G    0.22f    /* Acceleration delta above baseline */

typedef struct step_tracker_t {
    uint32_t total_steps_today;    /* Accumulated steps for current calendar day */
    float    distance_km;          /* Total distance traversed today (km) */
    float    calories_kcal;        /* Active calories burned today (kcal) */
    uint8_t  current_day;          /* Calendar day (1-31) parsed from GPS/RTC */
    float    ema_accel;            /* Dynamic gravity baseline (EMA) */
    bool     peak_detected;        /* State tracker for peak/crossing phase */
    uint32_t last_step_time_ms;    /* Timestamp (ms) of previous confirmed step peak */
    uint8_t  debounce_streak;      /* Consecutive candidate steps detected */
} step_tracker_t;

/**
 * @brief Initialize step tracker context, setting gravity EMA baseline to 1.0g.
 * 
 * @param st Pointer to step_tracker_t context
 * @param initial_day Current calendar day (1-31), or 0 if uninitialized
 */
static inline void step_tracker_init(step_tracker_t *st, uint8_t initial_day) {
    if (st == (void*)0) return;
    st->total_steps_today = 0;
    st->distance_km = 0.0f;
    st->calories_kcal = 0.0f;
    st->current_day = initial_day;
    st->ema_accel = 1.0f; /* 1.0g initial gravity baseline */
    st->peak_detected = false;
    st->last_step_time_ms = 0;
    st->debounce_streak = 0;
}

/**
 * @brief Auto-resets daily pedometer metrics when calendar day rolls over at midnight.
 * 
 * @param st Pointer to step_tracker_t context
 * @param now_day Current calendar day (1-31) from GPS NMEA or RTC
 */
static inline void step_tracker_check_midnight(step_tracker_t *st, uint8_t now_day) {
    if (st == (void*)0 || now_day == 0) return;

    /* Initialize day tracking on first valid fix */
    if (st->current_day == 0) {
        st->current_day = now_day;
        return;
    }

    /* Check if day rolled over */
    if (now_day != st->current_day) {
        st->total_steps_today = 0;
        st->distance_km = 0.0f;
        st->calories_kcal = 0.0f;
        st->current_day = now_day;
        st->debounce_streak = 0;
    }
}

/**
 * @brief Process instantaneous 3-axis accelerometer sample at 50 Hz.
 * 
 * @param st Pointer to step_tracker_t context
 * @param ax Instantaneous acceleration X (g)
 * @param ay Instantaneous acceleration Y (g)
 * @param az Instantaneous acceleration Z (g)
 * @param now_ms System uptime in milliseconds (e.g. pdTICKS_TO_MS(xTaskGetTickCount()))
 */
static inline void step_tracker_process_sample(step_tracker_t *st, float ax, float ay, float az, uint32_t now_ms) {
    if (st == (void*)0) return;

    /* 1. Rotation-Invariant Vector Magnitude */
    float a_mag = sqrtf(ax * ax + ay * ay + az * az);

    /* 2. Exponential Moving Average Gravity Baseline (alpha = 0.05) */
    st->ema_accel = (PEDOMETER_EMA_ALPHA * a_mag) + ((1.0f - PEDOMETER_EMA_ALPHA) * st->ema_accel);

    /* 3. Adaptive Dynamic Threshold */
    float dyn_thresh = st->ema_accel + PEDOMETER_DYNAMIC_THRESHOLD_G;

    /* 4. Peak Detection with Hysteresis & Falling-Edge Completion */
    if (!st->peak_detected) {
        if (a_mag >= dyn_thresh) {
            st->peak_detected = true;
        }
    } else {
        /* Falling edge: acceleration relaxes back toward baseline */
        if (a_mag < (st->ema_accel + 0.04f)) {
            st->peak_detected = false;

            if (st->last_step_time_ms == 0) {
                /* First candidate step initialized */
                st->debounce_streak = 1;
                st->last_step_time_ms = now_ms;
            } else {
                uint32_t dt = now_ms - st->last_step_time_ms;

                /* 5. Human Gait Cadence Window Verification (280 ms to 1200 ms) */
                if (dt >= PEDOMETER_MIN_STEP_INTERVAL_MS && dt <= PEDOMETER_MAX_STEP_INTERVAL_MS) {
                    st->last_step_time_ms = now_ms;

                    /* 6. 4-Step Debounce Streak Filter (Rejects fidgets, twitches, typing) */
                    if (st->debounce_streak < PEDOMETER_DEBOUNCE_STREAK_LIMIT) {
                        st->debounce_streak++;
                        if (st->debounce_streak == PEDOMETER_DEBOUNCE_STREAK_LIMIT) {
                            /* Streak validated: retroactively credit all 4 initial steps */
                            st->total_steps_today += PEDOMETER_DEBOUNCE_STREAK_LIMIT;
                            st->distance_km = (float)st->total_steps_today * PEDOMETER_STRIDE_KM_PER_STEP;
                            st->calories_kcal = (float)st->total_steps_today * PEDOMETER_KCAL_PER_STEP;
                        }
                    } else {
                        /* Continuous verified walking gait */
                        st->total_steps_today += 1;
                        st->distance_km = (float)st->total_steps_today * PEDOMETER_STRIDE_KM_PER_STEP;
                        st->calories_kcal = (float)st->total_steps_today * PEDOMETER_KCAL_PER_STEP;
                    }
                } else if (dt > PEDOMETER_MAX_STEP_INTERVAL_MS) {
                    /* Cadence broken (> 1.2s between steps): reset debounce streak */
                    st->debounce_streak = 1;
                    st->last_step_time_ms = now_ms;
                }
                /* If dt < 280 ms: high-frequency jitter/shake, ignored without resetting cadence */
            }
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STEP_COUNTER_H */
