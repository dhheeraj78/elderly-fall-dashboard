/**
 * @file fall_feature_extraction.c
 * @brief Implementation of 17-feature extraction and SVM inference in C99.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 */

#include "fall_feature_extraction.h"
#include <math.h>
#include <float.h>

void fall_extract_features(
    const float *acc_x,
    const float *acc_y,
    const float *acc_z,
    const float *gyro_x,
    const float *gyro_y,
    const float *gyro_z,
    int n_samples,
    float *features_out
) {
    if (n_samples <= 1 || features_out == (void*)0) {
        return;
    }

    float sum_acc_mag = 0.0f;
    float sum_acc_sq  = 0.0f;
    float min_acc_mag = FLT_MAX;
    float max_acc_mag = -FLT_MAX;
    int pk_idx = 0;

    float sum_gyro_mag = 0.0f;
    float sum_gyro_sq  = 0.0f;
    float min_gyro_mag = FLT_MAX;
    float max_gyro_mag = -FLT_MAX;

    float sum_jerk = 0.0f;
    float max_jerk = 0.0f;
    float max_jerk_dt = 0.0f;

    float prev_acc_mag = 0.0f;
    const float dt = 1.0f / FALL_MODEL_SAMPLING_RATE_HZ; /* 0.020s at 50 Hz */

    /* First pass: compute magnitudes, sums, mins, maxs, and jerk */
    for (int i = 0; i < n_samples; i++) {
        float ax = acc_x[i];
        float ay = acc_y[i];
        float az = acc_z[i];
        float a_mag = sqrtf(ax * ax + ay * ay + az * az);

        sum_acc_mag += a_mag;
        sum_acc_sq  += (a_mag * a_mag);
        if (a_mag < min_acc_mag) min_acc_mag = a_mag;
        if (a_mag > max_acc_mag) {
            max_acc_mag = a_mag;
            pk_idx = i;
        }

        float gx = gyro_x[i];
        float gy = gyro_y[i];
        float gz = gyro_z[i];
        float g_mag = sqrtf(gx * gx + gy * gy + gz * gz);

        sum_gyro_mag += g_mag;
        sum_gyro_sq  += (g_mag * g_mag);
        if (g_mag < min_gyro_mag) min_gyro_mag = g_mag;
        if (g_mag > max_gyro_mag) max_gyro_mag = g_mag;

        if (i > 0) {
            float jerk = fabsf(a_mag - prev_acc_mag);
            sum_jerk += jerk;
            if (jerk > max_jerk) max_jerk = jerk;

            float j_dt = jerk / dt;
            if (j_dt > max_jerk_dt) max_jerk_dt = j_dt;
        }
        prev_acc_mag = a_mag;
    }

    float mean_acc_mag  = sum_acc_mag / (float)n_samples;
    float mean_gyro_mag = sum_gyro_mag / (float)n_samples;

    /* Second pass: compute population variance and std (ddof=0 matching numpy) */
    float var_acc_sum = 0.0f;
    float var_gyro_sum = 0.0f;

    for (int i = 0; i < n_samples; i++) {
        float ax = acc_x[i];
        float ay = acc_y[i];
        float az = acc_z[i];
        float a_mag = sqrtf(ax * ax + ay * ay + az * az);
        float diff_a = a_mag - mean_acc_mag;
        var_acc_sum += (diff_a * diff_a);

        float gx = gyro_x[i];
        float gy = gyro_y[i];
        float gz = gyro_z[i];
        float g_mag = sqrtf(gx * gx + gy * gy + gz * gz);
        float diff_g = g_mag - mean_gyro_mag;
        var_gyro_sum += (diff_g * diff_g);
    }

    float std_acc_mag  = sqrtf(var_acc_sum / (float)n_samples);
    float rms_acc_mag  = sqrtf(sum_acc_sq / (float)n_samples);

    float std_gyro_mag = sqrtf(var_gyro_sum / (float)n_samples);
    float rms_gyro_mag = sqrtf(sum_gyro_sq / (float)n_samples);

    float mean_jerk = sum_jerk / (float)(n_samples - 1);

    /* -------------------------------------------------------------
     * Upgraded Kinetic Discriminators (Features 14, 15, 16)
     * ------------------------------------------------------------- */
    /* 14. Post-Impact Immobility Variance (PII) */
    int post_start = pk_idx + 10;
    int post_end = pk_idx + 60;
    if (post_end > n_samples) post_end = n_samples;

    float pii_var = 0.0f;
    if (post_start < n_samples - 5) {
        int sub_len = post_end - post_start;
        float sub_sum = 0.0f;
        float sub_sq = 0.0f;
        for (int k = post_start; k < post_end; k++) {
            float ax = acc_x[k];
            float ay = acc_y[k];
            float az = acc_z[k];
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            sub_sum += mag;
            sub_sq += (mag * mag);
        }
        float sub_mean = sub_sum / (float)sub_len;
        pii_var = (sub_sq / (float)sub_len) - (sub_mean * sub_mean);
        if (pii_var < 0.0f) pii_var = 0.0f;
    } else {
        int tail_start = n_samples - 25;
        if (tail_start < 0) tail_start = 0;
        int tail_len = n_samples - tail_start;
        float sub_sum = 0.0f;
        float sub_sq = 0.0f;
        for (int k = tail_start; k < n_samples; k++) {
            float ax = acc_x[k];
            float ay = acc_y[k];
            float az = acc_z[k];
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            sub_sum += mag;
            sub_sq += (mag * mag);
        }
        float sub_mean = sub_sum / (float)tail_len;
        pii_var = (sub_sq / (float)tail_len) - (sub_mean * sub_mean);
        if (pii_var < 0.0f) pii_var = 0.0f;
    }

    /* 15. Tilt / Orientation Differential (Delta theta in degrees) */
    int pre_end = pk_idx - 5;
    if (pre_end < 5) pre_end = 5;
    if (pre_end > 30) pre_end = 30;

    float pre_ax = 0.0f, pre_ay = 0.0f, pre_az = 0.0f;
    for (int k = 0; k < pre_end; k++) {
        pre_ax += acc_x[k];
        pre_ay += acc_y[k];
        pre_az += acc_z[k];
    }
    pre_ax /= (float)pre_end;
    pre_ay /= (float)pre_end;
    pre_az /= (float)pre_end;
    float norm_pre = sqrtf(pre_ax * pre_ax + pre_ay * pre_ay + pre_az * pre_az);

    int post_start_t = pk_idx + 10;
    if (post_start_t < 50) post_start_t = 50;
    if (post_start_t > n_samples - 10) post_start_t = n_samples - 10;
    int post_len = n_samples - post_start_t;

    float post_ax = 0.0f, post_ay = 0.0f, post_az = 0.0f;
    for (int k = post_start_t; k < n_samples; k++) {
        post_ax += acc_x[k];
        post_ay += acc_y[k];
        post_az += acc_z[k];
    }
    post_ax /= (float)post_len;
    post_ay /= (float)post_len;
    post_az /= (float)post_len;
    float norm_post = sqrtf(post_ax * post_ax + post_ay * post_ay + post_az * post_az);

    float delta_theta = 0.0f;
    if (norm_pre > 1e-4f && norm_post > 1e-4f) {
        float dot = (pre_ax * post_ax + pre_ay * post_ay + pre_az * post_az) / (norm_pre * norm_post);
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        delta_theta = acosf(dot) * (180.0f / 3.14159265358979323846f);
    }

    /* Write all 17 features in exact canonical order */
    features_out[0]  = mean_acc_mag;                     /* acc_mag_mean */
    features_out[1]  = std_acc_mag;                      /* acc_mag_std */
    features_out[2]  = min_acc_mag;                      /* acc_mag_min */
    features_out[3]  = max_acc_mag;                      /* acc_mag_max */
    features_out[4]  = max_acc_mag - min_acc_mag;        /* acc_mag_range */
    features_out[5]  = rms_acc_mag;                      /* acc_mag_rms */

    features_out[6]  = mean_gyro_mag;                    /* gyro_mag_mean */
    features_out[7]  = std_gyro_mag;                     /* gyro_mag_std */
    features_out[8]  = min_gyro_mag;                     /* gyro_mag_min */
    features_out[9]  = max_gyro_mag;                     /* gyro_mag_max */
    features_out[10] = max_gyro_mag - min_gyro_mag;      /* gyro_mag_range */
    features_out[11] = rms_gyro_mag;                     /* gyro_mag_rms */

    features_out[12] = mean_jerk;                        /* acc_jerk_mean */
    features_out[13] = max_jerk;                         /* acc_jerk_max */

    features_out[14] = pii_var;                          /* post_impact_immobility_var */
    features_out[15] = delta_theta;                      /* tilt_differential */
    features_out[16] = max_jerk_dt;                      /* peak_acc_jerk_dt */
}

float fall_standardize_and_score(const float *features) {
    float score = FALL_SVM_INTERCEPT;
    for (int i = 0; i < FALL_MODEL_NUM_FEATURES; i++) {
        float x_scaled = (features[i] - FALL_SCALER_MEANS[i]) / FALL_SCALER_SCALES[i];
        score += (FALL_SVM_WEIGHTS[i] * x_scaled);
    }
    return score;
}

int fall_predict(const float *features, float threshold, float *out_score) {
    float score = fall_standardize_and_score(features);
    if (out_score != (void*)0) {
        *out_score = score;
    }
    return (score >= threshold) ? 1 : 0;
}
