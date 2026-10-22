/**
 * @file esp32_data_logger_firmware.c
 * @brief Lightweight 200 Hz MPU6050 Data Logger for Wrist Calibration.
 * Part of: Secure Wearable Health Monitoring System (ASCON + ESP32 + MPU6050)
 *
 * Description:
 *   Configures MPU6050 via I2C at 400 kHz (+-16g, +-2000 deg/s).
 *   Reads 14 bytes in a single burst every 5 ms (200 Hz).
 *   Outputs compact CSV over UART: timestamp_ms,ax,ay,az,gx,gy,gz
 *   No WiFi/Bluetooth or emergency alarms active during logging.
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   400000

#define MPU6050_ADDR         0x68
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_CONFIG       0x1A
#define MPU6050_GYRO_CONFIG  0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B

/* Sensitivities for +-16g and +-2000 deg/s */
#define ACCEL_SCALE_FACTOR   2048.0f /* 32768 / 16g */
#define GYRO_SCALE_FACTOR    16.4f   /* 32768 / 2000 deg/s */

static const char *TAG = "WRIST_LOGGER";

static esp_err_t mpu6050_init(void) {
    // Wake up device
    uint8_t cmd_wake[2] = {MPU6050_PWR_MGMT_1, 0x00};
    esp_err_t err = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, cmd_wake, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    // Set Accel range to +-16g (0x18)
    uint8_t cmd_accel[2] = {MPU6050_ACCEL_CONFIG, 0x18};
    err = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, cmd_accel, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    // Set Gyro range to +-2000 deg/s (0x18)
    uint8_t cmd_gyro[2] = {MPU6050_GYRO_CONFIG, 0x18};
    err = i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, cmd_gyro, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    // Digital Low Pass Filter (DLPF_CFG = 2 -> ~94 Hz bandwidth)
    uint8_t cmd_dlpf[2] = {MPU6050_CONFIG, 0x02};
    return i2c_master_write_to_device(I2C_MASTER_NUM, MPU6050_ADDR, cmd_dlpf, 2, pdMS_TO_TICKS(100));
}

void wrist_logging_task(void *pvParameters) {
    (void)pvParameters;
    uint8_t raw[14];
    uint8_t reg = MPU6050_ACCEL_XOUT_H;

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(5); // 5 ms = 200 Hz

    ESP_LOGI(TAG, "Starting 200 Hz wrist data streaming. Format: ms,ax,ay,az,gx,gy,gz");

    while (1) {
        int64_t now_us = esp_timer_get_time();
        uint32_t now_ms = (uint32_t)(now_us / 1000);

        // 14-byte I2C burst read
        esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, MPU6050_ADDR, &reg, 1, raw, 14, pdMS_TO_TICKS(5));
        if (ret == ESP_OK) {
            int16_t rx = (raw[0] << 8) | raw[1];
            int16_t ry = (raw[2] << 8) | raw[3];
            int16_t rz = (raw[4] << 8) | raw[5];

            int16_t rgx = (raw[8]  << 8) | raw[9];
            int16_t rgy = (raw[10] << 8) | raw[11];
            int16_t rgz = (raw[12] << 8) | raw[13];

            float ax = (float)rx / ACCEL_SCALE_FACTOR;
            float ay = (float)ry / ACCEL_SCALE_FACTOR;
            float az = (float)rz / ACCEL_SCALE_FACTOR;

            float gx = (float)rgx / GYRO_SCALE_FACTOR;
            float gy = (float)rgy / GYRO_SCALE_FACTOR;
            float gz = (float)rgz / GYRO_SCALE_FACTOR;

            // Stream raw line over UART
            printf("%lu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n", (unsigned long)now_ms, ax, ay, az, gx, gy, gz);
        }

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
