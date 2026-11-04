/**
 * @file config.h
 * @brief Hardware Pinout, Network, and Algorithmic Configuration
 * Target Board: Seeed Studio XIAO ESP32-S3 (Dual-Core Xtensa LX7, 8MB Octal PSRAM)
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + TinyML)
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* =========================================================================
 * 1. SEEED STUDIO XIAO ESP32-S3 HARDWARE PINOUT SPECIFICATION
 * ========================================================================= */
// Native I2C Bus for MPU6050 IMU, MAX30102 PPG, and SSD1306 OLED
// NOTE: On XIAO ESP32-S3, standard DevKit GPIO 21/22 are NOT exposed!
// Physical Pin D4 = GPIO 5 (SDA), Physical Pin D5 = GPIO 6 (SCL)
#define XIAO_SDA_PIN            5   // Pin D4
#define XIAO_SCL_PIN            6   // Pin D5
#define I2C_BUS_FREQ_HZ         400000  // 400 kHz Fast-Mode I2C

// I2C 7-bit Device Addresses
#define MPU6050_I2C_ADDR        0x68
#define MAX30102_I2C_ADDR       0x57
#define SSD1306_OLED_ADDR       0x3C

// Built-in User LED on XIAO ESP32-S3 (Active LOW)
#define XIAO_USER_LED_PIN       21  // Yellow user LED on Seeed XIAO S3

// Optional GPS Module (NEO-6M / NEO-M8N) UART Pins
// Physical Pin D6 = GPIO 43 (TX), Physical Pin D7 = GPIO 44 (RX)
#define GPS_UART_NUM            1
#define GPS_TX_PIN              43  // Pin D6
#define GPS_RX_PIN              44  // Pin D7
#define GPS_BAUD_RATE           9600

/* =========================================================================
 * 2. WI-FI & CLOUD BACKEND CONFIGURATION
 * ========================================================================= */
#define WIFI_SSID               "Dhheeraj"
#define WIFI_PASSWORD           "onetwoeight"
#define WIFI_TIMEOUT_MS         15000

// Cloud Firebase Realtime Database Endpoint
#define FIREBASE_HOST_URL       "https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com"
#define FIREBASE_LIVE_PATH      "/telemetry/live.json"
#define FIREBASE_INCIDENT_PATH  "/incidents.json"

// Render Web Dashboard Telemetry Ingestion Webhook
#define RENDER_WEBHOOK_URL      "https://elderly-fall-detection.onrender.com/api/telemetry"

// Associated Authenticated Data (AAD) for ASCON-128 AEAD
#define ASCON_AAD_STRING        "ElderCare-XIAO-S3-V2.4"

/* =========================================================================
 * 3. KINEMATIC SAMPLING & TINYML DECISION FOREST PARAMETERS
 * ========================================================================= */
#define IMU_SAMPLING_RATE_HZ    50          // 50 Hz clinical kinematics sampling
#define IMU_SAMPLE_PERIOD_MS    20          // 20 ms per sample
#define KINEMATIC_WINDOW_SAMPLES 100        // 2.0 second window (50 Hz * 2.0s)
#define KINEMATIC_STEP_SAMPLES   50         // 1.0 second stride (50% overlap)

// Multi-Stage Clinical Gating Thresholds
#define STAGE1_PEAK_SHOCK_THRESH_G  2.30f   // Stage 1: Peak shock (|a| >= 2.30g)
#define STAGE2_RF_FALL_PROB_THRESH  0.50f   // Stage 2: 20-Tree Random Forest (P >= 0.50)
#define STAGE3_IMMOBILITY_SMA_MAX_G 0.15f   // Stage 3: Motionless resting (SMA < 0.15g)
#define STAGE3_IMMOBILITY_SECS_DEF  10      // Default post-fall immobility confirmation window

// MPU6050 Sensitivity Scaling Factors (+-16g, +-2000 deg/s)
#define MPU6050_ACCEL_SCALE_16G 2048.0f     // 2048 LSB/g
#define MPU6050_GYRO_SCALE_2000 16.4f       // 16.4 LSB/(deg/s)

/* =========================================================================
 * 4. FREERTOS TASK SCHEDULING SPECIFICATION
 * ========================================================================= */
// Task execution parameters
#define TASK_SENSOR_CORE        1           // Core 1: 50 Hz hard real-time kinematics
#define TASK_SENSOR_PRIO        5           // High priority
#define TASK_SENSOR_STACK_SIZE  8192

#define TASK_TELEMETRY_CORE     0           // Core 0: Crypto, Wi-Fi, and HTTP
#define TASK_TELEMETRY_PRIO     3           // Normal priority
#define TASK_TELEMETRY_STACK_SIZE 12288

#endif /* CONFIG_H */
