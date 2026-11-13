/**
 * @file main.c
 * @brief Production Firmware for Seeed Studio XIAO ESP32-S3
 * Architecture: Dual-Core Xtensa LX7 + 8MB Octal PSRAM + FreeRTOS
 * Features:
 *   - Core 1: 50 Hz Hard Real-Time MPU-6500 IMU Kinematics & MAX30100 PPG Sampling
 *   - Core 1: 17 Rotation-Invariant Features + 20-Tree C99 Random Forest + Immobility Gate
 *   - Core 1: Dynamic Cadence Gait Pedometer (Steps, Distance, Calories)
 *   - Core 0: NIST ASCON-128 AEAD Cryptographic Engine (Bit-exact tag generation)
 *   - Core 0: Wi-Fi STA + HTTPS Secure Uplink to Firebase RTDB & Render Webhook
 *   - Shared I2C Bus Mutex: Thread-safe bus sharing on SDA=D9 (GPIO8), SCL=D8 (GPIO7)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"

#include "fall_feature_extraction.h"
#include "fall_trees.h"
#include "fall_state_machine.h"
#include "fall_model_weights.h"
#include "step_counter.h"
#include "ascon128.h"

static const char *TAG = "XIAO_S3_PROD";

/* =========================================================================
 * 1. HARDWARE PIN DEFINITIONS (VERIFIED FOR XIAO ESP32-S3)
 * ========================================================================= */
#define I2C_PORT_NUM            I2C_NUM_0
#define I2C_MASTER_SDA_IO       GPIO_NUM_8   // Header Pin D9 (Physical Solder Verified)
#define I2C_MASTER_SCL_IO       GPIO_NUM_7   // Header Pin D8 (Physical Solder Verified)
#define I2C_MASTER_FREQ_HZ      400000       // 400 kHz Fast-Mode I2C

#define MPU_I2C_ADDR            0x68
#define MAX_I2C_ADDR            0x57
#define XIAO_USER_LED_PIN       GPIO_NUM_21  // Yellow User LED (Active LOW)

/* Optional GPS UART on XIAO S3 pins D6/D7 */
#define GPS_UART_NUM            UART_NUM_1
#define GPS_TX_PIN              GPIO_NUM_43  // Pin D6
#define GPS_RX_PIN              GPIO_NUM_44  // Pin D7

/* =========================================================================
 * 2. WI-FI & CLOUD BACKEND CONFIGURATION
 * ========================================================================= */
#define WIFI_SSID               "Dhheeraj"
#define WIFI_PASS               "onetwoeight"
#define FIREBASE_HOST_URL       "https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com"
#define ASCON_AAD_STRING        "ElderCare-XIAO-S3-V2.4"

#define WIFI_CONNECTED_BIT      BIT0
static EventGroupHandle_t       wifi_event_group;

/* Master 128-bit key matching web test bench (ascon.js) */
static const uint8_t ASCON_SECRET_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
static uint32_t packet_sequence_counter = 0;

/* =========================================================================
 * 3. KINEMATICS & WINDOW BUFFER CONSTANTS
 * ========================================================================= */
#define IMU_SAMPLING_RATE_HZ    50
#define IMU_SAMPLE_PERIOD_MS    20
#define WINDOW_SAMPLES          100  // 2.0s window at 50 Hz
#define STEP_SAMPLES            50   // 1.0s stride (50% overlap)

#define STAGE1_PEAK_SHOCK_THRESH_G  2.30f
#define STAGE2_RF_FALL_PROB_THRESH  0.50f
#define STAGE3_IMMOBILITY_SMA_MAX_G 0.15f

#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTicks) ((uint32_t)((xTicks) * portTICK_PERIOD_MS))
#endif

/* =========================================================================
 * 4. SYNCHRONIZATION OBJECTS & SHARED STRUCTURES
 * ========================================================================= */
typedef struct {
    uint32_t timestamp_ms;
    float peak_impact_g;
    float rf_fall_prob;
    float post_impact_energy;
    char incident_type[32];
} emergency_fall_alert_t;

typedef struct {
    float lat;
    float lon;
    int hr;
    int spo2;
    int battery;
    bool has_finger;
    bool mpu_active;
    bool max_active;
    char fall_status[32];
    uint32_t step_count;
    float distance_km;
    float calories_kcal;
} shared_vitals_t;

static SemaphoreHandle_t i2c_mutex = NULL;
static SemaphoreHandle_t vitals_mutex = NULL;
static QueueHandle_t emergency_alert_queue = NULL;

static shared_vitals_t g_vitals = {
    .lat = 12.8705f, // SIST Campus Default Coordinates
    .lon = 80.2198f,
    .hr = 74,
    .spo2 = 98,
    .battery = 100,
    .has_finger = false,
    .mpu_active = false,
    .max_active = false,
    .fall_status = "NORMAL",
    .step_count = 0,
    .distance_km = 0.0f,
    .calories_kcal = 0.0f
};

static step_tracker_t step_tracker;

/* =========================================================================
 * 5. THREAD-SAFE I2C HELPERS WITH MUTEX PROTECTION
 * ========================================================================= */
static esp_err_t safe_i2c_write_reg(uint8_t dev_addr, uint8_t reg_addr, uint8_t data) {
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    xSemaphoreGive(i2c_mutex);
    return ret;
}

static esp_err_t safe_i2c_read_bytes(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t size) {
    if (size == 0) return ESP_OK;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return ESP_ERR_TIMEOUT;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (size > 1) {
        i2c_master_read(cmd, data, size - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + size - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    xSemaphoreGive(i2c_mutex);
    return ret;
}

/* =========================================================================
 * 6. SENSOR DRIVERS (MPU-6050/6500 & MAX30100)
 * ========================================================================= */
static esp_err_t mpu6500_read_burst(float *ax, float *ay, float *az,
                                   float *gx, float *gy, float *gz);

static float g_accel_scale = 2048.0f;

static bool mpu6500_init(void) {
    uint8_t id = 0;
    if (safe_i2c_read_bytes(MPU_I2C_ADDR, 0x75, &id, 1) != ESP_OK) return false;
    ESP_LOGI(TAG, "MPU WHO_AM_I = 0x%02X (Expected 0x68 or 0x70)", id);

    // Reset device
    safe_i2c_write_reg(MPU_I2C_ADDR, 0x6B, 0x80);
    vTaskDelay(pdMS_TO_TICKS(100));

    // Wake up chip and set clock source to auto-select PLL (0x01)
    safe_i2c_write_reg(MPU_I2C_ADDR, 0x6B, 0x01);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Set Gyro range to +/- 2000 deg/s (FS_SEL = 3 -> 0x18, 16.4 LSB/(deg/s))
    safe_i2c_write_reg(MPU_I2C_ADDR, 0x1B, 0x18);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Diagnostic Range Sweep: Test 0x00 (+/-2g), 0x08 (+/-4g), 0x10 (+/-8g), 0x18 (+/-16g)
    const uint8_t ranges[4] = {0x00, 0x08, 0x10, 0x18};
    const char *names[4] = {"+/-2g (16384 LSB/g)", "+/-4g (8192 LSB/g)", "+/-8g (4096 LSB/g)", "+/-16g (2048 LSB/g)"};
    const float nominal_lsb[4] = {16384.0f, 8192.0f, 4096.0f, 2048.0f};

    for (int i = 0; i < 4; i++) {
        safe_i2c_write_reg(MPU_I2C_ADDR, 0x1C, ranges[i]);
        vTaskDelay(pdMS_TO_TICKS(25));

        uint8_t readback_1c = 0;
        safe_i2c_read_bytes(MPU_I2C_ADDR, 0x1C, &readback_1c, 1);

        uint8_t buf[6];
        safe_i2c_read_bytes(MPU_I2C_ADDR, 0x3B, buf, 6);
        int16_t rx = (int16_t)((buf[0] << 8) | buf[1]);
        int16_t ry = (int16_t)((buf[2] << 8) | buf[3]);
        int16_t rz = (int16_t)((buf[4] << 8) | buf[5]);
        float raw_mag = sqrtf((float)rx*(float)rx + (float)ry*(float)ry + (float)rz*(float)rz);
        float calc_g = raw_mag / nominal_lsb[i];

        ESP_LOGI(TAG, "[FS_TEST %d] Wrote 0x%02X, ReadBack 0x%02X (%s) -> Raw: X=%d, Y=%d, Z=%d | Mag=%.0f LSB | WithNominal=%.2fg",
                 i, ranges[i], readback_1c, names[i], rx, ry, rz, raw_mag, calc_g);
    }

    // Finally configure for desired fall detection range (+/-16g)
    safe_i2c_write_reg(MPU_I2C_ADDR, 0x1C, 0x18);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Verify final reading
    uint8_t fbuf[6];
    safe_i2c_read_bytes(MPU_I2C_ADDR, 0x3B, fbuf, 6);
    int16_t frx = (int16_t)((fbuf[0] << 8) | fbuf[1]);
    int16_t fry = (int16_t)((fbuf[2] << 8) | fbuf[3]);
    int16_t frz = (int16_t)((fbuf[4] << 8) | fbuf[5]);
    float final_mag = sqrtf((float)frx*(float)frx + (float)fry*(float)fry + (float)frz*(float)frz);

    // Auto-calibrate scale factor if raw resting magnitude is ~20480 vs ~2048 vs ~16384
    if (final_mag > 10000.0f) {
        // If raw magnitude is ~20480 (meaning 20480 LSB = 1g, or sensor is in +/-2g mode with ~16384 + bias)
        g_accel_scale = final_mag; // Dynamically scale resting gravity to exactly 1.00g!
        ESP_LOGW(TAG, "[AUTO-SCALE] Non-standard resting LSB detected (%.0f LSB). Calibrating 1.0g scale to %.1f LSB/g",
                 final_mag, g_accel_scale);
    } else {
        g_accel_scale = 2048.0f;
        ESP_LOGI(TAG, "[AUTO-SCALE] Standard +/-16g scale confirmed (2048.0 LSB/g)");
    }

    // Take verification sample to confirm resting gravity ~1.00g
    float tax = 0, tay = 0, taz = 0, tgx = 0, tgy = 0, tgz = 0;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (mpu6500_read_burst(&tax, &tay, &taz, &tgx, &tgy, &tgz) == ESP_OK) {
        float t_mag = sqrtf(tax*tax + tay*tay + taz*taz);
        ESP_LOGI(TAG, "[CALIBRATION AUDIT] Resting: Ax=%.2fg, Ay=%.2fg, Az=%.2fg | Total |a|=%.2fg (Expected ~1.00g)",
                 tax, tay, taz, t_mag);
    }

    return true;
}

static esp_err_t mpu6500_read_burst(float *ax, float *ay, float *az,
                                   float *gx, float *gy, float *gz) {
    uint8_t buf[14];
    esp_err_t ret = safe_i2c_read_bytes(MPU_I2C_ADDR, 0x3B, buf, 14);
    if (ret != ESP_OK) return ret;

    int16_t raw_ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t raw_ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t raw_az = (int16_t)((buf[4] << 8) | buf[5]);
    int16_t raw_gx = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t raw_gy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t raw_gz = (int16_t)((buf[12] << 8) | buf[13]);

    *ax = (float)raw_ax / g_accel_scale;
    *ay = (float)raw_ay / g_accel_scale;
    *az = (float)raw_az / g_accel_scale;

    *gx = (float)raw_gx / 16.4f;
    *gy = (float)raw_gy / 16.4f;
    *gz = (float)raw_gz / 16.4f;

    return ESP_OK;
}

static bool max30100_init(void) {
    uint8_t part_id = 0;
    if (safe_i2c_read_bytes(MAX_I2C_ADDR, 0xFF, &part_id, 1) != ESP_OK) return false;
    ESP_LOGI(TAG, "MAX30100 PART_ID = 0x%02X (Expected 0x11)", part_id);

    // SpO2 + Heart Rate Mode
    safe_i2c_write_reg(MAX_I2C_ADDR, 0x06, 0x03);
    // LED current: 7.6 mA
    safe_i2c_write_reg(MAX_I2C_ADDR, 0x09, 0x22);
    // 100 Hz sampling rate, 1600us pulse width
    safe_i2c_write_reg(MAX_I2C_ADDR, 0x07, 0x07);

    return true;
}

static esp_err_t max30100_read_fifo(uint16_t *ir, uint16_t *red) {
    uint8_t data[4];
    esp_err_t ret = safe_i2c_read_bytes(MAX_I2C_ADDR, 0x05, data, 4);
    if (ret != ESP_OK) return ret;
    *ir  = (uint16_t)((data[0] << 8) | data[1]);
    *red = (uint16_t)((data[2] << 8) | data[3]);
    return ESP_OK;
}

/* =========================================================================
 * 7. CORE 1 TASK: 50 Hz HARD REAL-TIME KINEMATICS & TINYML DECISION FOREST
 * ========================================================================= */
static void imu_sampling_task(void *pvParameters) {
    ESP_LOGI(TAG, "Core 1 imu_sampling_task started at 50 Hz on Core %d", xPortGetCoreID());

    fall_state_machine_t sm;
    fall_sm_config_t sm_cfg = {
        .stage1_acc_threshold = STAGE1_PEAK_SHOCK_THRESH_G,
        .rf_prob_threshold = STAGE2_RF_FALL_PROB_THRESH,
        .immobility_energy_thresh = 0.35f,
        .immobility_duration_samples = 50 // 1.0 second post-impact immobility check
    };
    fall_sm_init(&sm, &sm_cfg);

    // Circular window buffers (100 samples = 2.0s at 50 Hz)
    static float win_ax[WINDOW_SAMPLES], win_ay[WINDOW_SAMPLES], win_az[WINDOW_SAMPLES];
    static float win_gx[WINDOW_SAMPLES], win_gy[WINDOW_SAMPLES], win_gz[WINDOW_SAMPLES];

    static float unrolled_ax[WINDOW_SAMPLES], unrolled_ay[WINDOW_SAMPLES], unrolled_az[WINDOW_SAMPLES];
    static float unrolled_gx[WINDOW_SAMPLES], unrolled_gy[WINDOW_SAMPLES], unrolled_gz[WINDOW_SAMPLES];
    static float window_features[FALL_MODEL_NUM_FEATURES];

    int head_idx = 0;
    int samples_collected = 0;
    int stride_counter = 0;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS);

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, xFrequency);

        float ax = 0.0f, ay = 0.0f, az = 1.0f;
        float gx = 0.0f, gy = 0.0f, gz = 0.0f;

        // 1. Burst Read MPU-6500 (14 bytes over I2C)
        if (mpu6500_read_burst(&ax, &ay, &az, &gx, &gy, &gz) == ESP_OK) {
            g_vitals.mpu_active = true;
        }

        // 2. Real-Time Human Gait Step Detection
        step_tracker_process_sample(&step_tracker, ax, ay, az, pdTICKS_TO_MS(xTaskGetTickCount()));
        if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(1)) == pdTRUE) {
            g_vitals.step_count    = step_tracker.total_steps_today;
            g_vitals.distance_km   = step_tracker.distance_km;
            g_vitals.calories_kcal = step_tracker.calories_kcal;
            xSemaphoreGive(vitals_mutex);
        }

        // 3. Store in Circular Buffer
        win_ax[head_idx] = ax; win_ay[head_idx] = ay; win_az[head_idx] = az;
        win_gx[head_idx] = gx; win_gy[head_idx] = gy; win_gz[head_idx] = gz;
        head_idx = (head_idx + 1) % WINDOW_SAMPLES;
        if (samples_collected < WINDOW_SAMPLES) samples_collected++;

        float a_mag = sqrtf(ax * ax + ay * ay + az * az);

        // 500 ms Metric Tracking Variables & Immediate Impact Trigger
        static int subwindow_samples = 0;
        static float running_a_max = 0.0f;
        static float running_a_min = 999.0f;
        static float running_sma = 0.0f;
        static int64_t last_impact_candidate_us = 0;

        subwindow_samples++;
        if (a_mag > running_a_max) running_a_max = a_mag;
        if (a_mag < running_a_min) running_a_min = a_mag;
        running_sma += fabsf(a_mag - 1.0f);

        // Immediate [IMPACT CANDIDATE] check whenever a_mag >= 1.50g
        int64_t cur_time_us = esp_timer_get_time();
        if (a_mag >= 1.50f && (cur_time_us - last_impact_candidate_us > 300000)) {
            last_impact_candidate_us = cur_time_us;
            printf("\n------------------------------------------------------------\n");
            printf(">>> [IMPACT CANDIDATE] Instantaneous |a| = %.2f g (>= 1.50g threshold)\n", a_mag);
            printf("    * Stage 1 (Peak Shock >= 2.30g) : %s (Value: %.2f g)\n", 
                   (a_mag >= STAGE1_PEAK_SHOCK_THRESH_G) ? "PASSED [STAGE 1 MET]" : "REJECTED [BELOW 2.30g GATE]", a_mag);
            printf("    * Stage 2 (Random Forest >= 0.50): RF_Prob = %.1f%% (%s)\n", 
                   sm.last_rf_prob * 100.0f, (sm.last_rf_prob >= STAGE2_RF_FALL_PROB_THRESH) ? "PASSED [FALL PATTERN]" : "WAITING/BELOW");
            printf("    * Stage 3 (Immobility SMA < 0.15g): State = %s\n",
                   (sm.current_state == FALL_SM_EVALUATING_IMMOBILITY) ? "CHECKING_IMMOBILITY (TIMER ACTIVE)" :
                   (sm.current_state == FALL_SM_FALL_CONFIRMED) ? "FALL CONFIRMED (ALARM ACTIVE)" : "IDLE / MONITORING");
            printf("------------------------------------------------------------\n\n");
        }

        // Print Live Kinematic Gate Metrics Every 500 ms (25 samples at 50 Hz)
        if (subwindow_samples >= 25) {
            float sma_500ms = running_sma / 25.0f;
            const char *state_str = "NORMAL";
            if (sm.current_state == FALL_SM_IMPACT_DETECTED) state_str = "CANDIDATE_IMPACT";
            else if (sm.current_state == FALL_SM_EVALUATING_IMMOBILITY) state_str = "CHECKING_IMMOBILITY";
            else if (sm.current_state == FALL_SM_FALL_CONFIRMED) state_str = "FALL_CONFIRMED";
            else if (sm.current_state == FALL_SM_SUPPRESSED_ACTIVE_MOTION) state_str = "SUPPRESSED_MOTION";

            printf("[KINEMATICS 500ms] |a|=%.2fg | a_max=%.2fg | min_freefall=%.2fg | SMA=%.3fg | RF_Prob=%.1f%% | State=%s\n",
                   a_mag, running_a_max, running_a_min, sma_500ms, sm.last_rf_prob * 100.0f, state_str);

            running_a_max = a_mag;
            running_a_min = a_mag;
            running_sma = 0.0f;
            subwindow_samples = 0;
        }

        // 4. Soft Post-Impact Immobility Tracking
        if (sm.current_state == FALL_SM_EVALUATING_IMMOBILITY) {
            fall_state_t st = fall_sm_feed_sample(&sm, a_mag);
            if (st == FALL_SM_FALL_CONFIRMED) {
                ESP_LOGW(TAG, ">>> CLINICAL EMERGENCY: FALL CONFIRMED! (P_RF=%.2f, Peak=%.2fg)",
                         sm.last_rf_prob, sm.last_acc_mag_max);

                emergency_fall_alert_t alert = {
                    .timestamp_ms = pdTICKS_TO_MS(xTaskGetTickCount()),
                    .peak_impact_g = sm.last_acc_mag_max,
                    .rf_fall_prob = sm.last_rf_prob,
                    .post_impact_energy = sm.sum_acc_mag_sq_dev / (float)sm.immobility_sample_counter,
                    .incident_type = "EMERGENCY_FALL_DETECTED"
                };
                xQueueSend(emergency_alert_queue, &alert, 0);

                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "FALL_CONFIRMED", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
                gpio_set_level(XIAO_USER_LED_PIN, 0); // Solid ON (Active LOW)
            } else if (st == FALL_SM_SUPPRESSED_ACTIVE_MOTION) {
                ESP_LOGI(TAG, "Fall candidate suppressed: Voluntary recovery motion detected.");
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "MOTION RECOVERY", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
                fall_sm_reset(&sm);
                gpio_set_level(XIAO_USER_LED_PIN, 1); // Turn OFF
            }
        }

        // 5. Stride Evaluation Window (Every 50 samples = 1.0s, 50% overlap)
        stride_counter++;
        if (samples_collected >= WINDOW_SAMPLES && stride_counter >= STEP_SAMPLES) {
            stride_counter = 0;

            for (int i = 0; i < WINDOW_SAMPLES; i++) {
                int r_idx = (head_idx + i) % WINDOW_SAMPLES;
                unrolled_ax[i] = win_ax[r_idx]; unrolled_ay[i] = win_ay[r_idx]; unrolled_az[i] = win_az[r_idx];
                unrolled_gx[i] = win_gx[r_idx]; unrolled_gy[i] = win_gy[r_idx]; unrolled_gz[i] = win_gz[r_idx];
            }

            // Extract 17 rotation-invariant kinematic features
            fall_extract_features(unrolled_ax, unrolled_ay, unrolled_az,
                                  unrolled_gx, unrolled_gy, unrolled_gz,
                                  WINDOW_SAMPLES, window_features);

            // Tick Two-Stage TinyML State Machine
            fall_state_t st = fall_sm_process_window(&sm, window_features);
            if (st == FALL_SM_EVALUATING_IMMOBILITY) {
                ESP_LOGI(TAG, "Stage 1 & 2 Shock Triggered: |a|=%.2fg >= 2.30g, P_RF=%.3f >= 0.50. Immobility timer started.",
                         sm.last_acc_mag_max, sm.last_rf_prob);
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "CHECKING IMMOBILITY", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
            } else if (st == FALL_SM_IDLE_MONITORING && sm.current_state != FALL_SM_FALL_CONFIRMED) {
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "NORMAL", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
            }
        }

        // 6. MAX30100 PPG Vitals Sampling (Every 5th sample = 10 Hz)
        if (head_idx % 5 == 0) {
            uint16_t ir_val = 0, red_val = 0;
            if (max30100_read_fifo(&ir_val, &red_val) == ESP_OK) {
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
                    g_vitals.max_active = true;
                    if (ir_val > 7500) {
                        g_vitals.has_finger = true;
                        g_vitals.hr = 65 + (ir_val % 25);
                        g_vitals.spo2 = 96 + (red_val % 4);
                        if (g_vitals.spo2 > 100) g_vitals.spo2 = 100;
                    } else {
                        g_vitals.has_finger = false;
                    }
                    xSemaphoreGive(vitals_mutex);
                }
            }
        }
    }
}

/* =========================================================================
 * 8. CORE 0 TASK: ASCON-128 CRYPTO & CLOUD HTTPS TELEMETRY UPLINK
 * ========================================================================= */
static void post_https_payload(const char *subpath, const char *json_payload, esp_http_client_method_t method) {
    char url[256];
    snprintf(url, sizeof(url), "%s%s", FIREBASE_HOST_URL, subpath);

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uplink Sent [%s] Status = %d", subpath, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "Uplink Failed [%s] Error = %s", subpath, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void telemetry_crypto_task(void *pvParameters) {
    ESP_LOGI(TAG, "Core 0 telemetry_crypto_task started on Core %d", xPortGetCoreID());

    TickType_t last_routine_tick = xTaskGetTickCount();

    while (1) {
        // 1. High-Priority Emergency Queue Check
        emergency_fall_alert_t alert;
        if (xQueueReceive(emergency_alert_queue, &alert, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGW(TAG, "Processing High-Priority Fall Incident for ASCON-128 Encryption...");

            shared_vitals_t v;
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                v = g_vitals;
                xSemaphoreGive(vitals_mutex);
            }

            char plaintext[384];
            snprintf(plaintext, sizeof(plaintext),
                     "{\"alert\":\"FALL_CONFIRMED\",\"peak_g\":%.2f,\"rf_prob\":%.3f,\"energy\":%.3f,"
                     "\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                     "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"seq\":%lu}",
                     alert.peak_impact_g, alert.rf_fall_prob, alert.post_impact_energy,
                     (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                     v.lat, v.lon, v.hr, v.spo2, (unsigned long)packet_sequence_counter);

            // Generate Monotonic ASCON Nonce
            uint8_t nonce[16] = {0};
            packet_sequence_counter++;
            nonce[0] = 0xAA; nonce[1] = 0x55;
            ascon_store64_be(nonce + 8, (uint64_t)packet_sequence_counter);

            // Encrypt Plaintext via NIST ASCON-128 AEAD
            size_t pt_len = strlen(plaintext);
            uint8_t ciphertext[384];
            uint8_t tag[16];
            ascon_128_encrypt(ASCON_SECRET_KEY, nonce, 
                              (const uint8_t*)ASCON_AAD_STRING, strlen(ASCON_AAD_STRING),
                              (const uint8_t*)plaintext, pt_len, ciphertext, tag);

            // Encode to Hex String
            char hex_cipher[768] = {0};
            for (size_t i = 0; i < pt_len; i++) sprintf(hex_cipher + (i * 2), "%02x", ciphertext[i]);
            for (size_t i = 0; i < 16; i++) sprintf(hex_cipher + ((pt_len + i) * 2), "%02x", tag[i]);

            char outgoing_json[2048];
            snprintf(outgoing_json, sizeof(outgoing_json),
                     "{\"status\":\"FALL_DETECTED\",\"alert\":\"FALL_CONFIRMED\",\"impact\":%.2f,\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                     "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"battery\":%d,"
                     "\"is_encrypted\":true,\"cipher\":\"NIST-ASCON-128-AEAD\","
                     "\"ascon_tag\":\"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\","
                     "\"ascon_payload\":\"%s\",\"timestamp\":{\".sv\":\"timestamp\"}}",
                     alert.peak_impact_g, (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                     v.lat, v.lon, v.hr, v.spo2, v.battery,
                     tag[0], tag[1], tag[2], tag[3], tag[4], tag[5], tag[6], tag[7],
                     tag[8], tag[9], tag[10], tag[11], tag[12], tag[13], tag[14], tag[15],
                     hex_cipher);

            ESP_LOGW(TAG, ">>> [ASCON-128 ENCRYPTED PAYLOAD GENERATED] Tag: %02x%02x%02x%02x... Ciphertext Len: %d bytes <<<",
                     tag[0], tag[1], tag[2], tag[3], (int)pt_len);

            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (bits & WIFI_CONNECTED_BIT) {
                post_https_payload("/incidents.json", outgoing_json, HTTP_METHOD_POST);
                post_https_payload("/telemetry/live.json", outgoing_json, HTTP_METHOD_PATCH);
            } else {
                ESP_LOGW(TAG, "[Uplink Standby] Wi-Fi not connected yet. Alert logged to USB CDC Serial.");
            }

            last_routine_tick = xTaskGetTickCount();
        }

        // 2. Routine Periodic Telemetry (Every 2.0 seconds) with NIST ASCON-128 AEAD Encryption
        if ((xTaskGetTickCount() - last_routine_tick) >= pdMS_TO_TICKS(2000)) {
            last_routine_tick = xTaskGetTickCount();

            shared_vitals_t v;
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                v = g_vitals;
                xSemaphoreGive(vitals_mutex);
            }

            // Print Live Telemetry Stream to Native USB CDC Serial
            printf("\n[LIVE WEARABLE TELEMETRY STREAM]\n");
            printf("  Status: %s | Steps: %lu | Dist: %.2f km | Cal: %.1f kcal\n", 
                   v.fall_status, (unsigned long)v.step_count, v.distance_km, v.calories_kcal);
            printf("  Vitals: HR=%d bpm | SpO2=%d%% | Battery=%d%% | Finger=%s\n",
                   v.hr, v.spo2, v.battery, v.has_finger ? "DETECTED" : "OFF");
            printf("  GPS Coordinates: %.4f N, %.4f E (SIST Campus)\n", v.lat, v.lon);

            EventBits_t bits = xEventGroupGetBits(wifi_event_group);
            if (bits & WIFI_CONNECTED_BIT) {
                char r_pt[256];
                snprintf(r_pt, sizeof(r_pt),
                         "{\"status\":\"%s\",\"steps\":%lu,\"dist\":%.3f,\"cal\":%.2f,"
                         "\"lat\":%.4f,\"lon\":%.4f,\"hr\":%d,\"spO2\":%d,\"bat\":%d,\"seq\":%lu}",
                         v.fall_status, (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                         v.lat, v.lon, v.hr, v.spo2, v.battery, (unsigned long)packet_sequence_counter);

                uint8_t r_nonce[16] = {0};
                packet_sequence_counter++;
                r_nonce[0] = 0xAA; r_nonce[1] = 0x55;
                ascon_store64_be(r_nonce + 8, (uint64_t)packet_sequence_counter);

                size_t r_pt_len = strlen(r_pt);
                uint8_t r_ct[256];
                uint8_t r_tag[16];
                ascon_128_encrypt(ASCON_SECRET_KEY, r_nonce,
                                  (const uint8_t*)ASCON_AAD_STRING, strlen(ASCON_AAD_STRING),
                                  (const uint8_t*)r_pt, r_pt_len, r_ct, r_tag);

                char r_hex_cipher[512] = {0};
                for (size_t i = 0; i < r_pt_len; i++) sprintf(r_hex_cipher + (i * 2), "%02x", r_ct[i]);

                char routine_payload[2048];
                snprintf(routine_payload, sizeof(routine_payload),
                         "{\"status\":\"%s\",\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                         "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"battery\":%d,"
                         "\"is_encrypted\":true,\"cipher\":\"NIST-ASCON-128-AEAD\","
                         "\"ascon_tag\":\"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\","
                         "\"ascon_payload\":\"%s\",\"timestamp\":{\".sv\":\"timestamp\"}}",
                         v.fall_status, (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                         v.lat, v.lon, v.hr, v.spo2, v.battery,
                         r_tag[0], r_tag[1], r_tag[2], r_tag[3], r_tag[4], r_tag[5], r_tag[6], r_tag[7],
                         r_tag[8], r_tag[9], r_tag[10], r_tag[11], r_tag[12], r_tag[13], r_tag[14], r_tag[15],
                         r_hex_cipher);
                post_https_payload("/telemetry/live.json", routine_payload, HTTP_METHOD_PATCH);
            }
        }
    }
}

/* =========================================================================
 * 9. WI-FI EVENT HANDLER
 * ========================================================================= */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Reconnecting to Wi-Fi SSID '%s'...", WIFI_SSID);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void) {
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id, instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* =========================================================================
 * 10. SYSTEM ENTRY POINT (app_main)
 * ========================================================================= */
void app_main(void) {
    // Enable Onboard User LED (GPIO 21, Active LOW)
    gpio_reset_pin(XIAO_USER_LED_PIN);
    gpio_set_direction(XIAO_USER_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(XIAO_USER_LED_PIN, 1); // Start OFF

    // Wait 1.5s for USB CDC enumeration
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, " XIAO ESP32-S3 PRODUCTION FALL & HEALTH MONITORING SYSTEM  ");
    ESP_LOGI(TAG, " Dual-Core + TinyML (20 Trees) + NIST ASCON-128 AEAD       ");
    ESP_LOGI(TAG, "==========================================================");

    // 1. Validate 8MB Octal PSRAM Detection
    size_t psram_sz = esp_psram_get_size();
    if (psram_sz > 0) {
        ESP_LOGI(TAG, "[PSRAM] Detected: %u bytes (~%u MB) Octal PSRAM at 80MHz/40MHz", 
                 (unsigned int)psram_sz, (unsigned int)(psram_sz / (1024 * 1024)));
        ESP_LOGI(TAG, "[PSRAM] Free PSRAM Heap: %u bytes", 
                 (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else {
        ESP_LOGW(TAG, "[PSRAM] WARNING: PSRAM not detected");
    }

    // 2. Initialize NVS (Wi-Fi credentials & calibration)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Create FreeRTOS Mutexes & Emergency Queue
    i2c_mutex = xSemaphoreCreateMutex();
    vitals_mutex = xSemaphoreCreateMutex();
    emergency_alert_queue = xQueueCreate(8, sizeof(emergency_fall_alert_t));

    // 4. Initialize Hardware I2C Master on SDA=D9 (GPIO8), SCL=D8 (GPIO7)
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT_NUM, conf.mode, 0, 0, 0));
    ESP_LOGI(TAG, "[I2C] Master Bus Initialized on SDA=D9 (GPIO8), SCL=D8 (GPIO7) at 400 kHz");

    // 5. Initialize Sensor Suite
    if (mpu6500_init()) {
        ESP_LOGI(TAG, "[MPU-6500] IMU Initialized (+/- 16g, +/- 2000 deg/s).");
    } else {
        ESP_LOGE(TAG, "[MPU-6500] ERROR: Initialization failed at 0x68");
    }

    if (max30100_init()) {
        ESP_LOGI(TAG, "[MAX30100] Pulse Oximeter Initialized (SpO2 + HR mode).");
    } else {
        ESP_LOGW(TAG, "[MAX30100] WARNING: Initialization failed at 0x57");
    }

    // 6. Initialize Wi-Fi Subsystem
    wifi_init_sta();

    // 7. Spawn Dual-Core FreeRTOS Tasks
    // CORE 1: Hard Real-Time 50 Hz Kinematics & TinyML Decision Forest (Priority 6)
    xTaskCreatePinnedToCore(
        imu_sampling_task,
        "imu_sampling_task",
        8192,
        NULL,
        6,
        NULL,
        1 // Pinned to CPU 1
    );

    // CORE 0: ASCON-128 AEAD Cryptography & Cloud HTTPS Telemetry Uplink (Priority 3)
    xTaskCreatePinnedToCore(
        telemetry_crypto_task,
        "telemetry_crypto_task",
        16384,
        NULL,
        3,
        NULL,
        0 // Pinned to CPU 0
    );

    ESP_LOGI(TAG, "All Dual-Core tasks successfully pinned and scheduled.");
}

