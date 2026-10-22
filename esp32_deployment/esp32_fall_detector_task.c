/**
 * @file esp32_fall_detector_task.c
 * @brief Reference FreeRTOS Task and MPU6050 Driver for ESP-IDF.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 *
 * NOTE: For review only. Do not flash to hardware until wrist-domain calibration
 *       is completed.
 */

#include "fall_model_weights.h"
#include "fall_feature_extraction.h"
#include "fall_state_machine.h"
#include <string.h>
#include <math.h>

/* ESP-IDF Headers (Included when compiling in ESP-IDF project) */
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"
static const char *TAG_FALL = "FALL_DETECTOR";
#endif

#define MPU6050_I2C_ADDR         0x68
#define MPU6050_PWR_MGMT_1       0x6B
#define MPU6050_SMPLRT_DIV       0x19
#define MPU6050_CONFIG           0x1A
#define MPU6050_GYRO_CONFIG      0x1B
#define MPU6050_ACCEL_CONFIG     0x1C
#define MPU6050_ACCEL_XOUT_H     0x3B

/* Circular buffer for sliding window */
typedef struct {
    float acc_x[FALL_MODEL_WINDOW_SAMPLES];
    float acc_y[FALL_MODEL_WINDOW_SAMPLES];
    float acc_z[FALL_MODEL_WINDOW_SAMPLES];
    float gyro_x[FALL_MODEL_WINDOW_SAMPLES];
    float gyro_y[FALL_MODEL_WINDOW_SAMPLES];
    float gyro_z[FALL_MODEL_WINDOW_SAMPLES];
    int write_idx;
    int samples_since_last_inference;
} fall_circular_buffer_t;

static fall_circular_buffer_t s_circ_buf;
static fall_state_machine_t   s_state_machine;

/**
 * @brief Configure MPU6050 for +-16g accelerometer and +-2000 deg/s gyroscope.
 */
void mpu6050_configure_for_fall_detection(int i2c_port) {
#ifdef ESP_PLATFORM
    // 1. Wake up MPU6050 (clear sleep bit in PWR_MGMT_1)
    uint8_t pwr_wake[2] = {MPU6050_PWR_MGMT_1, 0x00};
    i2c_master_write_to_device(i2c_port, MPU6050_I2C_ADDR, pwr_wake, 2, pdMS_TO_TICKS(100));

    // 2. Set Accel Full Scale to +-16g (AFS_SEL = 3 in ACCEL_CONFIG 0x1C)
    //    Bit [4:3] = 11 -> 0x18
    uint8_t accel_cfg[2] = {MPU6050_ACCEL_CONFIG, 0x18};
    i2c_master_write_to_device(i2c_port, MPU6050_I2C_ADDR, accel_cfg, 2, pdMS_TO_TICKS(100));

    // 3. Set Gyro Full Scale to +-2000 deg/s (FS_SEL = 3 in GYRO_CONFIG 0x1B)
    //    Bit [4:3] = 11 -> 0x18
    uint8_t gyro_cfg[2] = {MPU6050_GYRO_CONFIG, 0x18};
    i2c_master_write_to_device(i2c_port, MPU6050_I2C_ADDR, gyro_cfg, 2, pdMS_TO_TICKS(100));

    // 4. Set Digital Low Pass Filter (DLPF_CFG = 2 -> ~94 Hz bandwidth)
    uint8_t dlpf_cfg[2] = {MPU6050_CONFIG, 0x02};
    i2c_master_write_to_device(i2c_port, MPU6050_I2C_ADDR, dlpf_cfg, 2, pdMS_TO_TICKS(100));

    ESP_LOGI(TAG_FALL, "MPU6050 initialized: Accel +-16g (2048 LSB/g), Gyro +-2000 dps (16.4 LSB/dps)");
#else
    (void)i2c_port;
#endif
}

/**
 * @brief Burst read 14 bytes (Acc X,Y,Z, Temp, Gyro X,Y,Z) in a single I2C transaction.
 */
int mpu6050_burst_read_6dof(int i2c_port, float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
#ifdef ESP_PLATFORM
    uint8_t raw_bytes[14];
    uint8_t reg = MPU6050_ACCEL_XOUT_H;
    esp_err_t ret = i2c_master_write_read_device(i2c_port, MPU6050_I2C_ADDR, &reg, 1, raw_bytes, 14, pdMS_TO_TICKS(20));
    if (ret != ESP_OK) return -1;

    int16_t raw_ax = (raw_bytes[0]  << 8) | raw_bytes[1];
    int16_t raw_ay = (raw_bytes[2]  << 8) | raw_bytes[3];
    int16_t raw_az = (raw_bytes[4]  << 8) | raw_bytes[5];

    int16_t raw_gx = (raw_bytes[8]  << 8) | raw_bytes[9];
    int16_t raw_gy = (raw_bytes[10] << 8) | raw_bytes[11];
    int16_t raw_gz = (raw_bytes[12] << 8) | raw_bytes[13];

    // Convert raw counts to physical units matching Python training pipeline
    *ax = (float)raw_ax / MPU6050_ACCEL_LSB_PER_G;  // Acceleration in [g]
    *ay = (float)raw_ay / MPU6050_ACCEL_LSB_PER_G;
    *az = (float)raw_az / MPU6050_ACCEL_LSB_PER_G;

    *gx = (float)raw_gx / MPU6050_GYRO_LSB_PER_DPS; // Angular velocity in [deg/s]
    *gy = (float)raw_gy / MPU6050_GYRO_LSB_PER_DPS;
    *gz = (float)raw_gz / MPU6050_GYRO_LSB_PER_DPS;

    return 0;
#else
    (void)i2c_port; (void)ax; (void)ay; (void)az; (void)gx; (void)gy; (void)gz;
    return 0;
#endif
}

/**
 * @brief Primary Fall Detection Task executed every 5 ms (200 Hz).
 */
void fall_detection_task_step(int i2c_port) {
    float ax, ay, az, gx, gy, gz;
    if (mpu6050_burst_read_6dof(i2c_port, &ax, &ay, &az, &gx, &gy, &gz) != 0) {
        return;
    }

    int idx = s_circ_buf.write_idx;
    s_circ_buf.acc_x[idx] = ax;
    s_circ_buf.acc_y[idx] = ay;
    s_circ_buf.acc_z[idx] = az;
    s_circ_buf.gyro_x[idx] = gx;
    s_circ_buf.gyro_y[idx] = gy;
    s_circ_buf.gyro_z[idx] = gz;

    s_circ_buf.write_idx = (idx + 1) % FALL_MODEL_WINDOW_SAMPLES;
    s_circ_buf.samples_since_last_inference++;

    // Feed sample to Stage 2 if actively confirming immobility
    if (s_state_machine.current_state == FALL_SM_EVALUATING_IMMOBILITY) {
        float a_mag = sqrtf(ax * ax + ay * ay + az * az);
        fall_state_t st = fall_sm_feed_sample(&s_state_machine, a_mag);
        if (st == FALL_SM_FALL_CONFIRMED) {
            // TRIGGER EMERGENCY ALERT via ASCON encrypted payload
#ifdef ESP_PLATFORM
            ESP_LOGW(TAG_FALL, "EMERGENCY: Fall Confirmed! Dispatching ASCON telemetry.");
#endif
        } else if (st == FALL_SM_SUPPRESSED_ACTIVE_MOTION) {
#ifdef ESP_PLATFORM
            ESP_LOGI(TAG_FALL, "Stage 2: False alarm suppressed (active post-impact motion).");
#endif
        }
    }

    // Trigger Stage 1 Inference every 1.0 second (200 samples)
    if (s_circ_buf.samples_since_last_inference >= FALL_MODEL_STEP_SAMPLES) {
        s_circ_buf.samples_since_last_inference = 0;

        // Unroll circular buffer into linear window
        float linear_ax[FALL_MODEL_WINDOW_SAMPLES];
        float linear_ay[FALL_MODEL_WINDOW_SAMPLES];
        float linear_az[FALL_MODEL_WINDOW_SAMPLES];
        float linear_gx[FALL_MODEL_WINDOW_SAMPLES];
        float linear_gy[FALL_MODEL_WINDOW_SAMPLES];
        float linear_gz[FALL_MODEL_WINDOW_SAMPLES];

        int read_ptr = s_circ_buf.write_idx; // Oldest sample
        for (int i = 0; i < FALL_MODEL_WINDOW_SAMPLES; i++) {
            linear_ax[i] = s_circ_buf.acc_x[read_ptr];
            linear_ay[i] = s_circ_buf.acc_y[read_ptr];
            linear_az[i] = s_circ_buf.acc_z[read_ptr];
            linear_gx[i] = s_circ_buf.gyro_x[read_ptr];
            linear_gy[i] = s_circ_buf.gyro_y[read_ptr];
            linear_gz[i] = s_circ_buf.gyro_z[read_ptr];
            read_ptr = (read_ptr + 1) % FALL_MODEL_WINDOW_SAMPLES;
        }

        float feats[FALL_MODEL_NUM_FEATURES];
        fall_extract_features(linear_ax, linear_ay, linear_az, linear_gx, linear_gy, linear_gz, FALL_MODEL_WINDOW_SAMPLES, feats);
        fall_sm_process_window(&s_state_machine, feats);
    }
}
