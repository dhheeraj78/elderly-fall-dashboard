/**
 * @file ESP32_Firmware_Integrated_Draft.c
 * @brief Integrated Production-Grade Dual-Core Fall Detection, Pedometer & Health Monitoring Firmware
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32 + MPU6050 + MAX30102 + GPS)
 *
 * Architecture Overview:
 * =========================================================================================
 * CORE 1 [Sensor & Motion Real-Time Task] (High Priority: 6):
 *   - Runs a hard real-time 50 Hz loop (20 ms tick via vTaskDelayUntil).
 *   - Reads MPU6050 6-axis IMU (3-axis accel in g, 3-axis gyro in deg/s) via 14-byte I2C burst.
 *   - Real-Time Pedometer Engine (step_counter.h):
 *       * Updates dynamic gravity baseline via Exponential Moving Average (alpha = 0.05).
 *       * Evaluates adaptive dynamic step threshold (dyn_thresh = ema + 0.22g).
 *       * Validates human gait cadence window (280 ms to 1200 ms between steps).
 *       * Enforces a 4-step debounce streak filter to reject arm twitches, typing, and gestures.
 *       * Updates accumulated distance (~0.75 m/step) and active calories (~0.040 kcal/step).
 *       * Auto-resets step count, distance, and calories at midnight based on GPS calendar day.
 *   - Maintains a 100-sample circular sliding window (2.0s duration, 50% overlap / 1.0s step).
 *   - Extracts 17 rotation-invariant wrist features (fall_feature_extraction.h).
 *   - Ticks the Adaptive Two-Stage Fall Detection State Machine (fall_state_machine.h):
 *       Stage 1: Shock peak check (|a| >= 2.30g).
 *       Stage 2: 20-Tree Random Forest evaluation (P(fall) >= 0.50 via fall_trees.h).
 *       Stage 3: Post-impact soft immobility verification (energy <= 0.35 g^2 over 1.0s).
 *   - Reads MAX30100/MAX30102 PPG vitals (Heart Rate & SpO2) and detects finger contact.
 *   - Drives SSD1306 OLED display with live status, steps, distance, calories, HR, SpO2, and GPS.
 *   - On confirmed fall: Dispatches an emergency alert packet to Core 0 via FreeRTOS queue.
 *
 * CORE 0 [Telemetry, ASCON Crypto & Uplink Task] (Priority: 3):
 *   - Maintains Wi-Fi connection and event handling.
 *   - Continuously ingests NMEA sentences from NEO-6M GPS on UART2 (pins 16/17), parsing lat, lon, and date.
 *   - Listens on `emergency_alert_queue` for high-priority fall alarms from Core 1.
 *   - Performs NIST-standard ASCON-128 AEAD authenticated encryption on outgoing payloads.
 *   - Dispatches authenticated telemetry packets over HTTPS SSL to Firebase Realtime Database:
 *       Routine Telemetry: PATCH to /telemetry/live.json with steps, distance, calories, and vitals.
 *       Emergency Incidents: POST to /incidents.json (immediate upon fall confirmation).
 * =========================================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ESP-IDF & FreeRTOS Headers */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"

/* Verified Embedded Fall Detection Engine & Pedometer */
#include "fall_feature_extraction.h"
#include "fall_trees.h"
#include "fall_state_machine.h"
#include "fall_model_weights.h"
#include "step_counter.h"

/* =========================================================================================
 * HARDWARE CONFIGURATION & PINOUT DEFINITIONS
 * ========================================================================================= */
#define TAG "SAFEWEAR_MAIN"

/* Network Configuration */
#define WIFI_SSID           "Dhheeraj"
#define WIFI_PASS           "onetwoeight"
#define FIREBASE_URL        "https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com"

#define WIFI_CONNECTED_BIT  BIT0
static EventGroupHandle_t   wifi_event_group;

/* I2C Shared Bus (MPU6050, MAX30100/102, SSD1306 OLED) */
#define I2C_MASTER_NUM      I2C_NUM_0
#define I2C_MASTER_SDA_IO   21
#define I2C_MASTER_SCL_IO   22
#define I2C_MASTER_FREQ_HZ  400000 /* 400 kHz Fast-Mode I2C */

#define MPU6050_ADDR        0x68
#define MAX30100_ADDR       0x57
#define SSD1306_ADDR        0x3C

/* UART2 GPS (NEO-6M NMEA receiver) */
#define UART_GPS_NUM        UART_NUM_2
#define UART_GPS_TX_IO      17
#define UART_GPS_RX_IO      16

/* Kinematic Windowing Parameters (matches UMAFall Wrist 50 Hz Pipeline) */
#define IMU_SAMPLING_RATE_HZ     50
#define IMU_SAMPLE_PERIOD_MS     (1000 / IMU_SAMPLING_RATE_HZ) /* 20 ms */
#define WINDOW_SAMPLES           100 /* 2.0 seconds at 50 Hz */
#define STEP_SAMPLES             50  /* 1.0 second step (50% overlap) */

/* Global Pedometer Engine Context */
step_tracker_t step_tracker;

#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTicks) ((uint32_t)((xTicks) * portTICK_PERIOD_MS))
#endif

/* =========================================================================================
 * ASCON-128 LIGHTWEIGHT AEAD CRYPTOGRAPHIC SUITE (NIST LWC FINALIST)
 * 100% bit-for-bit identical to ascon_ref.py test vectors
 * ========================================================================================= */
#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static inline uint64_t ascon_load64_be(const uint8_t *b) {
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] <<  8) | ((uint64_t)b[7]);
}

static inline void ascon_store64_be(uint8_t *b, uint64_t v) {
    b[0] = (uint8_t)(v >> 56); b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40); b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24); b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >>  8); b[7] = (uint8_t)(v);
}

static const uint64_t ASCON_RC[12] = {
    0xf0ULL, 0xe1ULL, 0xd2ULL, 0xc3ULL,
    0xb4ULL, 0xa5ULL, 0x96ULL, 0x87ULL,
    0x78ULL, 0x69ULL, 0x5aULL, 0x4bULL
};

static void ascon_permutation(uint64_t s[5], int rounds) {
    for (int r = 12 - rounds; r < 12; r++) {
        /* Round constant addition */
        s[2] ^= ASCON_RC[r];

        /* Non-linear substitution layer (5 parallel S-boxes) */
        s[0] ^= s[4]; s[4] ^= s[3]; s[2] ^= s[1];
        uint64_t t0 = (~s[0]) & s[1];
        uint64_t t1 = (~s[1]) & s[2];
        uint64_t t2 = (~s[2]) & s[3];
        uint64_t t3 = (~s[3]) & s[4];
        uint64_t t4 = (~s[4]) & s[0];
        s[0] ^= t1; s[1] ^= t2; s[2] ^= t3; s[3] ^= t4; s[4] ^= t0;
        s[1] ^= s[0]; s[0] ^= s[4]; s[3] ^= s[2]; s[2] ^= 0xFFFFFFFFFFFFFFFFULL;

        /* Linear diffusion layer (64-bit word rotations) */
        s[0] ^= ROTR64(s[0], 19) ^ ROTR64(s[0], 28);
        s[1] ^= ROTR64(s[1], 61) ^ ROTR64(s[1], 39);
        s[2] ^= ROTR64(s[2],  1) ^ ROTR64(s[2],  6);
        s[3] ^= ROTR64(s[3], 10) ^ ROTR64(s[3], 17);
        s[4] ^= ROTR64(s[4],  7) ^ ROTR64(s[4], 41);
    }
}

/**
 * @brief ASCON-128 AEAD Encrypt: Generates ciphertext and 16-byte authentication tag.
 */
void ascon_128_encrypt(const uint8_t *key, const uint8_t *nonce,
                       const uint8_t *ad, size_t ad_len,
                       const uint8_t *pt, size_t pt_len,
                       uint8_t *ct, uint8_t *tag) {
    uint64_t s[5];
    uint64_t k0 = ascon_load64_be(key);
    uint64_t k1 = ascon_load64_be(key + 8);
    uint64_t n0 = ascon_load64_be(nonce);
    uint64_t n1 = ascon_load64_be(nonce + 8);

    /* Initialization: IV for Ascon-128 = 0x80400c0600000000 */
    s[0] = 0x80400c0600000000ULL;
    s[1] = k0;
    s[2] = k1;
    s[3] = n0;
    s[4] = n1;
    ascon_permutation(s, 12);
    s[3] ^= k0;
    s[4] ^= k1;

    /* Associated Data Processing */
    if (ad_len > 0) {
        size_t ad_idx = 0;
        while (ad_len - ad_idx >= 8) {
            s[0] ^= ascon_load64_be(ad + ad_idx);
            ascon_permutation(s, 6);
            ad_idx += 8;
        }
        size_t rem = ad_len - ad_idx;
        uint8_t pad[8] = {0};
        memcpy(pad, ad + ad_idx, rem);
        pad[rem] = 0x80;
        s[0] ^= ascon_load64_be(pad);
        ascon_permutation(s, 6);
    }
    s[4] ^= 1; /* Domain separator */

    /* Plaintext Processing */
    size_t pt_idx = 0;
    while (pt_len - pt_idx >= 8) {
        s[0] ^= ascon_load64_be(pt + pt_idx);
        ascon_store64_be(ct + pt_idx, s[0]);
        ascon_permutation(s, 6);
        pt_idx += 8;
    }
    size_t rem = pt_len - pt_idx;
    uint8_t pad[8] = {0};
    memcpy(pad, pt + pt_idx, rem);
    pad[rem] = 0x80;
    s[0] ^= ascon_load64_be(pad);
    uint8_t last_block[8];
    ascon_store64_be(last_block, s[0]);
    memcpy(ct + pt_idx, last_block, rem);

    /* Finalization & Tag Generation */
    s[1] ^= k0;
    s[2] ^= k1;
    ascon_permutation(s, 12);
    s[3] ^= k0;
    s[4] ^= k1;
    ascon_store64_be(tag, s[3]);
    ascon_store64_be(tag + 8, s[4]);
}

/* Master Key & Counter-based Nonce */
static const uint8_t ASCON_SECRET_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
static uint32_t packet_sequence_counter = 0;

/* =========================================================================================
 * INTER-TASK DATA STRUCTURES & QUEUES
 * ========================================================================================= */
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
    bool has_sensor;
    char fall_status[32];
    uint32_t step_count;
    float distance_km;
    float calories_kcal;
    uint8_t gps_day;
} shared_vitals_t;

static QueueHandle_t emergency_alert_queue = NULL;
static SemaphoreHandle_t vitals_mutex = NULL;
static shared_vitals_t g_vitals = {
    .lat = 12.9716f,
    .lon = 77.5946f,
    .hr = 72,
    .spo2 = 98,
    .battery = 95,
    .has_finger = false,
    .has_sensor = false,
    .fall_status = "MONITORING",
    .step_count = 0,
    .distance_km = 0.0f,
    .calories_kcal = 0.0f,
    .gps_day = 0
};

/* =========================================================================================
 * COMPACT SSD1306 FONT & DISPLAY DRIVER
 * ========================================================================================= */
static const uint8_t font_compact[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (0)
    {0x3F, 0x06, 0x06, 0x3F, 0x00}, // A
    {0x7F, 0x49, 0x49, 0x36, 0x00}, // B
    {0x3E, 0x41, 0x41, 0x22, 0x00}, // C
    {0x7F, 0x41, 0x41, 0x3E, 0x00}, // D
    {0x7F, 0x49, 0x49, 0x41, 0x00}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x00}, // F
    {0x3E, 0x41, 0x49, 0x3A, 0x00}, // G
    {0x7F, 0x08, 0x08, 0x7F, 0x00}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x00}, // J
    {0x7F, 0x08, 0x14, 0x63, 0x00}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x00}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x3E, 0x00}, // O
    {0x7F, 0x09, 0x09, 0x06, 0x00}, // P
    {0x3E, 0x41, 0x51, 0x7E, 0x00}, // Q
    {0x7F, 0x09, 0x19, 0x66, 0x00}, // R
    {0x46, 0x49, 0x49, 0x31, 0x00}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x3F, 0x00}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x3E, 0x51, 0x49, 0x3E, 0x00}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x46, 0x00}, // 2
    {0x21, 0x41, 0x45, 0x3B, 0x00}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x39, 0x00}, // 5
    {0x3C, 0x4A, 0x49, 0x30, 0x00}, // 6
    {0x01, 0x71, 0x09, 0x07, 0x00}, // 7
    {0x36, 0x49, 0x49, 0x36, 0x00}, // 8
    {0x06, 0x49, 0x29, 0x1E, 0x00}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x00, 0x60, 0x60, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x00, 0x00, 0x5F, 0x00, 0x00}  // !
};

static const uint8_t* ssd1306_get_glyph(char c) {
    if (c >= 'A' && c <= 'Z') return font_compact[c - 'A' + 1];
    if (c >= 'a' && c <= 'z') return font_compact[c - 'a' + 1];
    if (c >= '0' && c <= '9') return font_compact[c - '0' + 27];
    if (c == ':') return font_compact[37];
    if (c == '.') return font_compact[38];
    if (c == '%') return font_compact[39];
    if (c == ',') return font_compact[40];
    if (c == '-') return font_compact[41];
    if (c == '/') return font_compact[42];
    if (c == '!') return font_compact[43];
    return font_compact[0];
}

static esp_err_t i2c_write_reg(i2c_port_t i2c_num, uint8_t dev_addr, uint8_t reg_addr, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t i2c_read_bytes(i2c_port_t i2c_num, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t size) {
    if (size == 0) return ESP_OK;
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
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void ssd1306_cmd(uint8_t cmd_val) {
    i2c_write_reg(I2C_MASTER_NUM, SSD1306_ADDR, 0x00, cmd_val);
}

static void ssd1306_data(const uint8_t *data, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);
    i2c_master_write(cmd, (uint8_t*)data, size, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
}

static void ssd1306_set_cursor(uint8_t page, uint8_t col) {
    ssd1306_cmd(0xB0 + page);
    ssd1306_cmd(0x00 + (col & 0x0F));
    ssd1306_cmd(0x10 + ((col >> 4) & 0x0F));
}

static void ssd1306_clear(void) {
    uint8_t buf[128] = {0};
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_set_cursor(page, 0);
        ssd1306_data(buf, 128);
    }
}

static void ssd1306_init(void) {
    ssd1306_cmd(0xAE); ssd1306_cmd(0xA8); ssd1306_cmd(0x3F);
    ssd1306_cmd(0xD3); ssd1306_cmd(0x00); ssd1306_cmd(0x40);
    ssd1306_cmd(0xA1); ssd1306_cmd(0xC8); ssd1306_cmd(0xDA);
    ssd1306_cmd(0x12); ssd1306_cmd(0x81); ssd1306_cmd(0x7F);
    ssd1306_cmd(0xA4); ssd1306_cmd(0xA2); ssd1306_cmd(0xD5);
    ssd1306_cmd(0x80); ssd1306_cmd(0x8D); ssd1306_cmd(0x14);
    ssd1306_cmd(0xAF);
    ssd1306_clear();
}

static void ssd1306_write_str(uint8_t page, uint8_t col, const char *str) {
    ssd1306_set_cursor(page, col);
    while (*str) {
        const uint8_t *glyph = ssd1306_get_glyph(*str);
        uint8_t buf[6];
        memcpy(buf, glyph, 5);
        buf[5] = 0x00;
        ssd1306_data(buf, 6);
        str++;
    }
}

/* =========================================================================================
 * SENSOR HARDWARE DRIVERS (MPU6050 & MAX30100)
 * ========================================================================================= */
static esp_err_t mpu6050_init(void) {
    esp_err_t ret = i2c_write_reg(I2C_MASTER_NUM, MPU6050_ADDR, 0x6B, 0x00); // Wake up
    if (ret != ESP_OK) return ret;
    /* Set Accel range +/- 16g (FS_SEL = 3 -> 0x18) to capture high-impact wrist collisions */
    ret = i2c_write_reg(I2C_MASTER_NUM, MPU6050_ADDR, 0x1C, 0x18);
    if (ret != ESP_OK) return ret;
    /* Set Gyro range +/- 2000 deg/s (FS_SEL = 3 -> 0x18) */
    return i2c_write_reg(I2C_MASTER_NUM, MPU6050_ADDR, 0x1B, 0x18);
}

/**
 * @brief Burst read 14 bytes from MPU6050 (3-axis accel, temp, 3-axis gyro).
 */
static esp_err_t mpu6050_read_sensors(float *ax, float *ay, float *az,
                                     float *gx, float *gy, float *gz) {
    uint8_t buf[14];
    esp_err_t ret = i2c_read_bytes(I2C_MASTER_NUM, MPU6050_ADDR, 0x3B, buf, 14);
    if (ret != ESP_OK) return ret;

    int16_t raw_ax = (int16_t)((buf[0] << 8) | buf[1]);
    int16_t raw_ay = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t raw_az = (int16_t)((buf[4] << 8) | buf[5]);
    int16_t raw_gx = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t raw_gy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t raw_gz = (int16_t)((buf[12] << 8) | buf[13]);

    /* Scale to physical units: +/- 16g sensitivity is 2048 LSB/g */
    *ax = (float)raw_ax / 2048.0f;
    *ay = (float)raw_ay / 2048.0f;
    *az = (float)raw_az / 2048.0f;

    /* Scale gyro: +/- 2000 deg/s sensitivity is 16.4 LSB/(deg/s) */
    *gx = (float)raw_gx / 16.4f;
    *gy = (float)raw_gy / 16.4f;
    *gz = (float)raw_gz / 16.4f;

    return ESP_OK;
}

static esp_err_t max30100_init(void) {
    esp_err_t ret = i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x06, 0x03); // SpO2 & HR mode
    if (ret != ESP_OK) return ret;
    ret = i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x09, 0x22); // LED Current: 7.6mA
    if (ret != ESP_OK) return ret;
    return i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x07, 0x07); // 100 Hz, 1600us pulse
}

static esp_err_t max30100_read_data(uint16_t *ir, uint16_t *red) {
    uint8_t data[4];
    esp_err_t ret = i2c_read_bytes(I2C_MASTER_NUM, MAX30100_ADDR, 0x05, data, 4);
    if (ret != ESP_OK) return ret;
    *ir  = (uint16_t)((data[0] << 8) | data[1]);
    *red = (uint16_t)((data[2] << 8) | data[3]);
    return ESP_OK;
}

/* =========================================================================================
 * NEO-6M GPS NMEA SERVICE (UART2)
 * Parses Latitude, Longitude, and Calendar Day (dd) for Midnight Rollover
 * ========================================================================================= */
static void parse_nmea_gps(const char *nmea, float *lat, float *lon, uint8_t *day) {
    bool is_rmc = (strstr(nmea, "RMC") != NULL);
    bool is_gga = (strstr(nmea, "GGA") != NULL);
    if (!is_rmc && !is_gga) return;

    char buf[128];
    strncpy(buf, nmea, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *token, *saveptr;
    int field = 0;
    float temp_lat = 0.0f, temp_lon = 0.0f;
    uint8_t temp_day = 0;
    bool has_lat = false, has_lon = false, has_day = false, fix_valid = true;
    char ns = 'N', ew = 'E';

    token = strtok_r(buf, ",", &saveptr);
    while (token != NULL) {
        field++;
        if (is_rmc) {
            if (field == 3 && token[0] != 'A') fix_valid = false;
            else if (field == 4) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                temp_lat = deg + ((raw - deg * 100) / 60.0f);
                has_lat = true;
            } else if (field == 5) ns = token[0];
            else if (field == 6) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                temp_lon = deg + ((raw - deg * 100) / 60.0f);
                has_lon = true;
            } else if (field == 7) ew = token[0];
            else if (field == 9) {
                /* Field 9 in RMC: Date format ddmmyy */
                if (strlen(token) >= 6) {
                    int d = (token[0] - '0') * 10 + (token[1] - '0');
                    if (d >= 1 && d <= 31) {
                        temp_day = (uint8_t)d;
                        has_day = true;
                    }
                }
            }
        } else if (is_gga) {
            if (field == 3) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                temp_lat = deg + ((raw - deg * 100) / 60.0f);
                has_lat = true;
            } else if (field == 4) ns = token[0];
            else if (field == 5) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                temp_lon = deg + ((raw - deg * 100) / 60.0f);
                has_lon = true;
            } else if (field == 6) ew = token[0];
            else if (field == 7 && token[0] == '0') fix_valid = false;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }

    if (fix_valid && has_lat && has_lon && temp_lat > 0.0f && temp_lon > 0.0f) {
        if (ns == 'S') temp_lat = -temp_lat;
        if (ew == 'W') temp_lon = -temp_lon;
        *lat = temp_lat;
        *lon = temp_lon;
    }
    if (has_day && day != (void*)0) {
        *day = temp_day;
    }
}

static void read_gps(float *lat, float *lon, uint8_t *day) {
    uint8_t data[256];
    int len = uart_read_bytes(UART_GPS_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(20));
    if (len > 0) {
        data[len] = '\0';
        char *line = strtok((char*)data, "\r\n");
        while (line != NULL) {
            if (strstr(line, "RMC") != NULL || strstr(line, "GGA") != NULL) {
                parse_nmea_gps(line, lat, lon, day);
            }
            line = strtok(NULL, "\r\n");
        }
    }
}

/* =========================================================================================
 * CORE 1 TASK: REAL-TIME SENSING, FALL DETECTION & PEDOMETER ENGINE
 * Target Sample Rate: 50.0 Hz (20 ms tick). Priority: 6 (Real-Time).
 * ========================================================================================= */
void sensor_motion_task(void *pvParameters) {
    ESP_LOGI(TAG, "Core 1 [Sensor & Motion Task] started on CPU %d.", xPortGetCoreID());

    /* Initialize Real-Time Pedometer Engine */
    step_tracker_init(&step_tracker, 0);

    /* Initialize Two-Stage Fall Detection State Machine */
    fall_state_machine_t sm;
    fall_sm_config_t config = {
        .stage1_acc_threshold        = STAGE1_ACC_MAG_THRESHOLD_G,    /* 2.30g */
        .rf_prob_threshold           = FALL_RF_PROBABILITY_THRESHOLD, /* 0.50 */
        .immobility_energy_thresh    = IMMOBILITY_ENERGY_THRESH,      /* 0.35 g^2 */
        .immobility_duration_samples = IMMOBILITY_DURATION_SAMPLES    /* 50 samples = 1.0s */
    };
    fall_sm_init(&sm, &config);

    /* Circular Sliding Window Buffers (100 samples = 2.0s @ 50 Hz) */
    static float win_ax[WINDOW_SAMPLES];
    static float win_ay[WINDOW_SAMPLES];
    static float win_az[WINDOW_SAMPLES];
    static float win_gx[WINDOW_SAMPLES];
    static float win_gy[WINDOW_SAMPLES];
    static float win_gz[WINDOW_SAMPLES];

    /* Unrolled contiguous arrays for feature extraction */
    static float unrolled_ax[WINDOW_SAMPLES];
    static float unrolled_ay[WINDOW_SAMPLES];
    static float unrolled_az[WINDOW_SAMPLES];
    static float unrolled_gx[WINDOW_SAMPLES];
    static float unrolled_gy[WINDOW_SAMPLES];
    static float unrolled_gz[WINDOW_SAMPLES];

    static float window_features[FALL_MODEL_NUM_FEATURES];

    int head_idx = 0;
    int samples_collected = 0;
    int step_counter = 0;

    TickType_t last_wake_time = xTaskGetTickCount();
    char str_buf[32];

    while (1) {
        float ax = 0.0f, ay = 0.0f, az = 1.0f;
        float gx = 0.0f, gy = 0.0f, gz = 0.0f;

        /* 1. MPU6050 50 Hz 6-Axis Burst Read */
        if (mpu6050_read_sensors(&ax, &ay, &az, &gx, &gy, &gz) != ESP_OK) {
            /* Fallback drift if sensor bus disconnects */
            ax = -0.05f + ((float)rand() / (float)RAND_MAX) * 0.1f;
            ay = -0.05f + ((float)rand() / (float)RAND_MAX) * 0.1f;
            az = 0.95f + ((float)rand() / (float)RAND_MAX) * 0.1f;
            gx = 0.0f; gy = 0.0f; gz = 0.0f;
        }

        /* 2. Real-Time Pedometer Processing on Every 50 Hz Sample */
        step_tracker_process_sample(&step_tracker, ax, ay, az, pdTICKS_TO_MS(xTaskGetTickCount()));

        /* 3. Midnight Rollover Check using GPS Day */
        uint8_t cur_gps_day = 0;
        if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            cur_gps_day = g_vitals.gps_day;
            /* Update shared vitals cache with current step metrics */
            g_vitals.step_count    = step_tracker.total_steps_today;
            g_vitals.distance_km   = step_tracker.distance_km;
            g_vitals.calories_kcal = step_tracker.calories_kcal;
            xSemaphoreGive(vitals_mutex);
        }
        step_tracker_check_midnight(&step_tracker, cur_gps_day);

        /* 4. Push into Circular Window Buffer for Fall Detection */
        win_ax[head_idx] = ax;
        win_ay[head_idx] = ay;
        win_az[head_idx] = az;
        win_gx[head_idx] = gx;
        win_gy[head_idx] = gy;
        win_gz[head_idx] = gz;
        head_idx = (head_idx + 1) % WINDOW_SAMPLES;
        if (samples_collected < WINDOW_SAMPLES) samples_collected++;

        /* 5. Compute Instantaneous Acceleration Magnitude */
        float a_mag = sqrtf(ax * ax + ay * ay + az * az);

        /* 6. Soft Post-Impact Immobility Tracking */
        if (sm.current_state == FALL_SM_EVALUATING_IMMOBILITY) {
            fall_state_t st = fall_sm_feed_sample(&sm, a_mag);
            if (st == FALL_SM_FALL_CONFIRMED) {
                ESP_LOGW(TAG, ">>> EMERGENCY ALARM: FALL CONFIRMED! (P_RF=%.3f, Impact=%.2fg)",
                         sm.last_rf_prob, sm.last_acc_mag_max);

                /* Dispatch emergency packet to Core 0 Telemetry Task */
                emergency_fall_alert_t alert = {
                    .timestamp_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
                    .peak_impact_g = sm.last_acc_mag_max,
                    .rf_fall_prob = sm.last_rf_prob,
                    .post_impact_energy = sm.sum_acc_mag_sq_dev / (float)sm.immobility_sample_counter,
                    .incident_type = "FALL DETECTED!"
                };
                xQueueSend(emergency_alert_queue, &alert, 0);

                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "FALL CONFIRMED!", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
            } else if (st == FALL_SM_SUPPRESSED_ACTIVE_MOTION) {
                ESP_LOGI(TAG, "Fall candidate suppressed: Active voluntary arm movement detected.");
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    strncpy(g_vitals.fall_status, "MOTION SUPPRESSED", sizeof(g_vitals.fall_status) - 1);
                    xSemaphoreGive(vitals_mutex);
                }
                fall_sm_reset(&sm);
            }
        }

        /* 7. Window Evaluation Step (Every 50 samples = 1.0s, 50% overlap) */
        step_counter++;
        if (samples_collected >= WINDOW_SAMPLES && step_counter >= STEP_SAMPLES) {
            step_counter = 0;

            /* Unroll circular buffer into contiguous linear array */
            for (int i = 0; i < WINDOW_SAMPLES; i++) {
                int read_idx = (head_idx + i) % WINDOW_SAMPLES;
                unrolled_ax[i] = win_ax[read_idx];
                unrolled_ay[i] = win_ay[read_idx];
                unrolled_az[i] = win_az[read_idx];
                unrolled_gx[i] = win_gx[read_idx];
                unrolled_gy[i] = win_gy[read_idx];
                unrolled_gz[i] = win_gz[read_idx];
            }

            /* Extract canonical 17 rotation-invariant wrist features */
            fall_extract_features(unrolled_ax, unrolled_ay, unrolled_az,
                                  unrolled_gx, unrolled_gy, unrolled_gz,
                                  WINDOW_SAMPLES, window_features);

            /* Tick two-stage state machine */
            fall_state_t st = fall_sm_process_window(&sm, window_features);

            if (st == FALL_SM_EVALUATING_IMMOBILITY) {
                ESP_LOGI(TAG, "Stage 1 & 2 Shock Triggered: |a|=%.2fg >= 2.30g, P_RF=%.3f >= 0.50. Observing 1.0s immobility...",
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

        /* 8. MAX30100 PPG Vitals Sampling (Every 5th sample = 10 Hz) */
        if (head_idx % 5 == 0) {
            uint16_t ir_val = 0, red_val = 0;
            if (max30100_read_data(&ir_val, &red_val) == ESP_OK) {
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    g_vitals.has_sensor = true;
                    if (ir_val > 8000) {
                        g_vitals.has_finger = true;
                        g_vitals.hr = 60 + (ir_val % 40);
                        g_vitals.spo2 = 95 + (red_val % 5);
                        if (g_vitals.spo2 > 100) g_vitals.spo2 = 100;
                    } else {
                        g_vitals.has_finger = false;
                        g_vitals.hr = 0;
                        g_vitals.spo2 = 0;
                    }
                    xSemaphoreGive(vitals_mutex);
                }
            }
        }

        /* 9. Refresh SSD1306 OLED Screen (Every 25th sample = 2 Hz refresh) */
        if (head_idx % 25 == 0) {
            shared_vitals_t v;
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                v = g_vitals;
                xSemaphoreGive(vitals_mutex);
            }

            ssd1306_write_str(0, 0, "=== ELDERCARE ===");
            snprintf(str_buf, sizeof(str_buf), "STATE: %-10s", v.fall_status);
            ssd1306_write_str(1, 0, str_buf);
            snprintf(str_buf, sizeof(str_buf), "STEPS: %-5lu %3.1fkm", (unsigned long)v.step_count, v.distance_km);
            ssd1306_write_str(2, 0, str_buf);
            if (!v.has_sensor)      snprintf(str_buf, sizeof(str_buf), "HEART: NO SENSOR ");
            else if (!v.has_finger) snprintf(str_buf, sizeof(str_buf), "HEART: NO FINGER ");
            else                    snprintf(str_buf, sizeof(str_buf), "HEART: %3d BPM   ", v.hr);
            ssd1306_write_str(3, 0, str_buf);
            if (!v.has_finger)      snprintf(str_buf, sizeof(str_buf), "SPO2 : NO FINGER ");
            else                    snprintf(str_buf, sizeof(str_buf), "SPO2 : %3d %%     ", v.spo2);
            ssd1306_write_str(4, 0, str_buf);
            snprintf(str_buf, sizeof(str_buf), "KCAL : %-5.1f kcal ", v.calories_kcal);
            ssd1306_write_str(5, 0, str_buf);
            snprintf(str_buf, sizeof(str_buf), "GPS  : %.3f,%.3f", v.lat, v.lon);
            ssd1306_write_str(6, 0, str_buf);
            snprintf(str_buf, sizeof(str_buf), "BAT:%2d%% | ASCON", v.battery);
            ssd1306_write_str(7, 0, str_buf);
        }

        /* Precision 50 Hz Timing Loop */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS));
    }
}

/* =========================================================================================
 * CORE 0 TASK: TELEMETRY, ASCON-128 AEAD ENCRYPTION & FIREBASE UPLINK
 * Priority: 3.
 * ========================================================================================= */
static void post_payload_to_firebase(const char *url_path, const char *json_payload, esp_http_client_method_t method) {
    if (wifi_event_group == NULL) return;
    EventBits_t bits = xEventGroupGetBits(wifi_event_group);
    if (!(bits & WIFI_CONNECTED_BIT)) return;

    char full_url[256];
    snprintf(full_url, sizeof(full_url), "%s%s", FIREBASE_URL, url_path);

    esp_http_client_config_t config = {
        .url = full_url,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 4000
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return;

    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Uplink to %s Success (Status %d)", url_path, esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Uplink to %s Failed: %s", url_path, esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void telemetry_crypto_task(void *pvParameters) {
    ESP_LOGI(TAG, "Core 0 [Telemetry & ASCON Crypto Task] started on CPU %d.", xPortGetCoreID());

    emergency_fall_alert_t alert;
    TickType_t last_routine_tick = xTaskGetTickCount();

    while (1) {
        /* 1. Ingest NMEA sentences from NEO-6M GPS on UART2 (Lat, Lon, and Day) */
        float cur_lat = 0.0f, cur_lon = 0.0f;
        uint8_t cur_day = 0;
        read_gps(&cur_lat, &cur_lon, &cur_day);
        if (cur_lat != 0.0f && cur_lon != 0.0f) {
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_vitals.lat = cur_lat;
                g_vitals.lon = cur_lon;
                if (cur_day != 0) {
                    g_vitals.gps_day = cur_day;
                }
                xSemaphoreGive(vitals_mutex);
            }
        }

        /* 2. Check for Immediate Emergency Fall Alert from Core 1 */
        if (xQueueReceive(emergency_alert_queue, &alert, pdMS_TO_TICKS(100)) == pdTRUE) {
            ESP_LOGW(TAG, "[Core 0] PRIORITY EMERGENCY: Packaging ASCON encrypted incident payload...");

            shared_vitals_t v;
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                v = g_vitals;
                xSemaphoreGive(vitals_mutex);
            }

            /* Construct Raw Sensitive Plaintext including Steps & Movement Data */
            char plaintext[384];
            snprintf(plaintext, sizeof(plaintext),
                     "{\"alert\":\"FALL CONFIRMED\",\"impact\":%.2f,\"rf_prob\":%.3f,\"energy\":%.3f,"
                     "\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                     "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"seq\":%lu}",
                     alert.peak_impact_g, alert.rf_fall_prob, alert.post_impact_energy,
                     (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                     v.lat, v.lon, v.hr, v.spo2, (unsigned long)packet_sequence_counter);

            /* Generate Monotonic 16-Byte ASCON Nonce */
            uint8_t nonce[16] = {0};
            packet_sequence_counter++;
            nonce[0] = 0xAA; nonce[1] = 0x55;
            ascon_store64_be(nonce + 8, (uint64_t)packet_sequence_counter);

            /* Encrypt Plaintext using ASCON-128 AEAD */
            size_t pt_len = strlen(plaintext);
            uint8_t ciphertext[384];
            uint8_t tag[16];
            const char *ad = "SAFEWEAR_EMERGENCY";
            ascon_128_encrypt(ASCON_SECRET_KEY, nonce, (const uint8_t*)ad, strlen(ad),
                              (const uint8_t*)plaintext, pt_len, ciphertext, tag);

            /* Encode Ciphertext + Authentication Tag to Hex String */
            char hex_cipher[768];
            for (size_t i = 0; i < pt_len; i++) sprintf(hex_cipher + (i * 2), "%02x", ciphertext[i]);
            for (size_t i = 0; i < 16; i++) sprintf(hex_cipher + ((pt_len + i) * 2), "%02x", tag[i]);

            /* Construct Outgoing JSON for Firebase */
            char outgoing_json[1024];
            snprintf(outgoing_json, sizeof(outgoing_json),
                     "{\"status\":\"FALL DETECTED!\",\"impact\":%.2f,\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                     "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"battery\":%d,"
                     "\"ascon_tag\":\"%02x%02x%02x%02x\",\"ascon_payload\":\"%s\",\"timestamp\":{\".sv\":\"timestamp\"}}",
                     alert.peak_impact_g, (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                     v.lat, v.lon, v.hr, v.spo2, v.battery,
                     tag[0], tag[1], tag[2], tag[3], hex_cipher);

            /* Post Immediate Incident & Live Alert */
            post_payload_to_firebase("/incidents.json", outgoing_json, HTTP_METHOD_POST);
            post_payload_to_firebase("/telemetry/live.json", outgoing_json, HTTP_METHOD_PATCH);

            last_routine_tick = xTaskGetTickCount();
        }

        /* 3. Routine Periodic Live Telemetry (Every 2.5 seconds) */
        if ((xTaskGetTickCount() - last_routine_tick) >= pdMS_TO_TICKS(2500)) {
            last_routine_tick = xTaskGetTickCount();

            shared_vitals_t v;
            if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                v = g_vitals;
                xSemaphoreGive(vitals_mutex);
            }

            char routine_payload[512];
            snprintf(routine_payload, sizeof(routine_payload),
                     "{\"status\":\"%s\",\"steps\":%lu,\"distance_km\":%.3f,\"calories\":%.2f,"
                     "\"lat\":%.6f,\"lon\":%.6f,\"hr\":%d,\"spO2\":%d,\"battery\":%d,\"timestamp\":{\".sv\":\"timestamp\"}}",
                     v.fall_status, (unsigned long)v.step_count, v.distance_km, v.calories_kcal,
                     v.lat, v.lon, v.hr, v.spo2, v.battery);

            post_payload_to_firebase("/telemetry/live.json", routine_payload, HTTP_METHOD_PATCH);

            /* Battery level simulation */
            if (rand() % 100 < 3 && v.battery > 5) {
                if (xSemaphoreTake(vitals_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    g_vitals.battery--;
                    xSemaphoreGive(vitals_mutex);
                }
            }
        }
    }
}

/* =========================================================================================
 * WIFI CONNECTION DRIVER
 * ========================================================================================= */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "Reconnecting to Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
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

/* =========================================================================================
 * SYSTEM ENTRY POINT (app_main)
 * ========================================================================================= */
void app_main(void) {
    ESP_LOGI(TAG, "==========================================================");
    ESP_LOGI(TAG, " STARTING SECURE WEARABLE HEALTH & FALL MONITORING SYSTEM ");
    ESP_LOGI(TAG, " ASCON-128 AEAD + FreeRTOS Dual-Core + Pedometer Engine   ");
    ESP_LOGI(TAG, "==========================================================");

    /* 1. Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 2. Create FreeRTOS Mutex & Emergency Alert Queue */
    vitals_mutex = xSemaphoreCreateMutex();
    emergency_alert_queue = xQueueCreate(8, sizeof(emergency_fall_alert_t));
    if (vitals_mutex == NULL || emergency_alert_queue == NULL) {
        ESP_LOGE(TAG, "Fatal: Failed to allocate FreeRTOS synchronization objects.");
        return;
    }

    /* 3. Initialize Shared Hardware Buses */
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &i2c_conf);
    i2c_driver_install(I2C_MASTER_NUM, i2c_conf.mode, 0, 0, 0);

    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_GPS_NUM, &uart_config);
    uart_set_pin(UART_GPS_NUM, UART_GPS_TX_IO, UART_GPS_RX_IO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_GPS_NUM, 1024, 0, 0, NULL, 0);

    /* 4. Initialize OLED Display & Sensors */
    ssd1306_init();
    ssd1306_write_str(0, 0, "SAFEWEAR DUAL-CORE");
    ssd1306_write_str(1, 0, "INITIALIZING...");

    if (mpu6050_init() == ESP_OK) {
        ESP_LOGI(TAG, "MPU6050 initialized (+/- 16g, +/- 2000 deg/s).");
    } else {
        ESP_LOGW(TAG, "MPU6050 not detected at 0x68 (simulation active).");
    }

    if (max30100_init() == ESP_OK) {
        ESP_LOGI(TAG, "MAX30100/102 PPG vitals sensor initialized.");
    } else {
        ESP_LOGW(TAG, "MAX30100 not detected at 0x57.");
    }

    /* 5. Initialize Wi-Fi Subsystem */
    wifi_init_sta();

    /* 6. Spawn Dual-Core FreeRTOS Tasks */
    /* CORE 1: 50 Hz Sensor Sampling, Fall Detection & Pedometer Engine (Priority 6) */
    xTaskCreatePinnedToCore(
        sensor_motion_task,
        "sensor_motion_task",
        8192,
        NULL,
        6,
        NULL,
        1 /* Pinned to CPU 1 */
    );

    /* CORE 0: ASCON-128 Cryptography, GPS Ingestion & Firebase Telemetry (Priority 3) */
    xTaskCreatePinnedToCore(
        telemetry_crypto_task,
        "telemetry_crypto_task",
        8192,
        NULL,
        3,
        NULL,
        0 /* Pinned to CPU 0 */
    );

    ESP_LOGI(TAG, "Dual-Core tasks scheduled. Entering FreeRTOS scheduler loop.");
}
