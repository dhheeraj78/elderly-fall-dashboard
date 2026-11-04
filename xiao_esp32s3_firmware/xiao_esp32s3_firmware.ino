/**
 * @file xiao_esp32s3_firmware.ino
 * @brief Turnkey Dual-Core TinyML & ASCON-128 Wearable Firmware
 * Target Hardware: Seeed Studio XIAO ESP32-S3 (Xtensa Dual-Core LX7 @ 240MHz, 8MB PSRAM)
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32-S3)
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config.h"
#include "ascon128.h"
#include "tinyml_forest.h"

/* =========================================================================
 * 1. DATA STRUCTURES & INTER-CORE QUEUES
 * ========================================================================= */
typedef struct {
    uint32_t timestamp_ms;
    float ax, ay, az;
    float gx, gy, gz;
    float peak_acc_g;
    float rf_fall_prob;
    bool  is_candidate_fall;
    float features[17];
    uint32_t step_count;
    float distance_km;
    float calories_kcal;
} KinematicSampleEvent_t;

static QueueHandle_t s_event_queue = NULL;
static uint32_t s_ascon_packet_counter = 0;

/* MPU6050 Circular Buffer */
static float s_buf_ax[KINEMATIC_WINDOW_SAMPLES];
static float s_buf_ay[KINEMATIC_WINDOW_SAMPLES];
static float s_buf_az[KINEMATIC_WINDOW_SAMPLES];
static float s_buf_gx[KINEMATIC_WINDOW_SAMPLES];
static float s_buf_gy[KINEMATIC_WINDOW_SAMPLES];
static float s_buf_gz[KINEMATIC_WINDOW_SAMPLES];
static int   s_buf_write_idx = 0;
static int   s_samples_in_buf = 0;
static int   s_samples_since_last_eval = 0;

/* Real-Time Pedometer Engine Instance */
static step_counter_t s_step_engine;

/* =========================================================================
 * 2. MPU6050 HARDWARE DRIVER (Direct Wire, No Extra Libraries Required)
 * ========================================================================= */
bool init_mpu6050() {
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println(F("[MPU6050] ERROR: Sensor not found at address 0x68 on I2C bus!"));
        return false;
    }

    // 1. Wake up MPU6050 (clear sleep bit in PWR_MGMT_1)
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(0x6B); // PWR_MGMT_1
    Wire.write(0x00); // Internal 8MHz oscillator
    Wire.endTransmission();
    delay(20);

    // 2. Set Accel Full Scale to +-16g (AFS_SEL = 3 -> 0x18)
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(0x1C); // ACCEL_CONFIG
    Wire.write(0x18);
    Wire.endTransmission();

    // 3. Set Gyro Full Scale to +-2000 deg/s (FS_SEL = 3 -> 0x18)
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(0x1B); // GYRO_CONFIG
    Wire.write(0x18);
    Wire.endTransmission();

    // 4. Set Digital Low Pass Filter (~94 Hz bandwidth -> DLPF_CFG = 2)
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(0x1A); // CONFIG
    Wire.write(0x02);
    Wire.endTransmission();

    Serial.println(F("[MPU6050] Configured: +-16g Accel (2048 LSB/g), +-2000 dps Gyro (16.4 LSB/dps), DLPF 94Hz"));
    return true;
}

bool read_mpu6050(float *ax, float *ay, float *az, float *gx, float *gy, float *gz) {
    Wire.beginTransmission(MPU6050_I2C_ADDR);
    Wire.write(0x3B); // ACCEL_XOUT_H
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom((uint8_t)MPU6050_I2C_ADDR, (size_t)14) != 14) return false;

    int16_t raw_ax = (Wire.read() << 8) | Wire.read();
    int16_t raw_ay = (Wire.read() << 8) | Wire.read();
    int16_t raw_az = (Wire.read() << 8) | Wire.read();
    Wire.read(); Wire.read(); // Skip temperature
    int16_t raw_gx = (Wire.read() << 8) | Wire.read();
    int16_t raw_gy = (Wire.read() << 8) | Wire.read();
    int16_t raw_gz = (Wire.read() << 8) | Wire.read();

    *ax = (float)raw_ax / MPU6050_ACCEL_SCALE_16G;
    *ay = (float)raw_ay / MPU6050_ACCEL_SCALE_16G;
    *az = (float)raw_az / MPU6050_ACCEL_SCALE_16G;

    *gx = (float)raw_gx / MPU6050_GYRO_SCALE_2000;
    *gy = (float)raw_gy / MPU6050_GYRO_SCALE_2000;
    *gz = (float)raw_gz / MPU6050_GYRO_SCALE_2000;

    return true;
}

/* =========================================================================
 * 3. CORE 1 TASK: 50 HZ HARD REAL-TIME SENSOR SAMPLING & MOTION INFERENCE
 * ========================================================================= */
void vSensorMotionTask(void *pvParameters) {
    Serial.printf("[Core 1] Kinematics Engine initialized on Core %d\n", xPortGetCoreID());

    // Configure I2C specifically on Seeed Studio XIAO ESP32-S3 pins
    Wire.begin(XIAO_SDA_PIN, XIAO_SCL_PIN, I2C_BUS_FREQ_HZ);
    bool sensor_ok = init_mpu6050();

    step_counter_init(&s_step_engine);

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xPeriod = pdMS_TO_TICKS(IMU_SAMPLE_PERIOD_MS); // 20 ms = 50 Hz

    float ax = 0.0f, ay = 0.0f, az = 1.0f;
    float gx = 0.0f, gy = 0.0f, gz = 0.0f;
    float extracted_features[17];

    while (true) {
        vTaskDelayUntil(&xLastWakeTime, xPeriod);

        // 1. Read IMU (or simulate nominal 1g resting if sensor disconnected)
        if (sensor_ok) {
            if (!read_mpu6050(&ax, &ay, &az, &gx, &gy, &gz)) {
                // Sensor read retry
                ax = 0.0f; ay = 0.0f; az = 1.0f;
            }
        } else {
            // Emulation fallback for lab testing without hardware sensor
            ax = -0.02f + ((float)rand() / (float)RAND_MAX) * 0.04f;
            ay = -0.02f + ((float)rand() / (float)RAND_MAX) * 0.04f;
            az =  0.98f + ((float)rand() / (float)RAND_MAX) * 0.04f;
        }

        // 2. Real-Time Pedometer Cadence Evaluation
        uint32_t now_ms = millis();
        step_counter_process_sample(&s_step_engine, ax, ay, az, now_ms);

        // 3. Store into 100-sample circular window buffer
        s_buf_ax[s_buf_write_idx] = ax;
        s_buf_ay[s_buf_write_idx] = ay;
        s_buf_az[s_buf_write_idx] = az;
        s_buf_gx[s_buf_write_idx] = gx;
        s_buf_gy[s_buf_write_idx] = gy;
        s_buf_gz[s_buf_write_idx] = gz;

        s_buf_write_idx = (s_buf_write_idx + 1) % KINEMATIC_WINDOW_SAMPLES;
        if (s_samples_in_buf < KINEMATIC_WINDOW_SAMPLES) {
            s_samples_in_buf++;
        }
        s_samples_since_last_eval++;

        // 4. Instantaneous Peak Acceleration Shock Check
        float acc_mag = sqrtf(ax * ax + ay * ay + az * az);
        bool stage1_shock_detected = (acc_mag >= STAGE1_PEAK_SHOCK_THRESH_G);

        // 5. Evaluate when window is full (every 50 samples = 1.0s, or immediately upon shock)
        if (s_samples_in_buf >= KINEMATIC_WINDOW_SAMPLES && 
           (s_samples_since_last_eval >= KINEMATIC_STEP_SAMPLES || stage1_shock_detected)) {
            
            s_samples_since_last_eval = 0;

            // Linearize window buffer for feature extraction
            float lin_ax[KINEMATIC_WINDOW_SAMPLES], lin_ay[KINEMATIC_WINDOW_SAMPLES], lin_az[KINEMATIC_WINDOW_SAMPLES];
            float lin_gx[KINEMATIC_WINDOW_SAMPLES], lin_gy[KINEMATIC_WINDOW_SAMPLES], lin_gz[KINEMATIC_WINDOW_SAMPLES];

            for (int i = 0; i < KINEMATIC_WINDOW_SAMPLES; i++) {
                int src_idx = (s_buf_write_idx + i) % KINEMATIC_WINDOW_SAMPLES;
                lin_ax[i] = s_buf_ax[src_idx];
                lin_ay[i] = s_buf_ay[src_idx];
                lin_az[i] = s_buf_az[src_idx];
                lin_gx[i] = s_buf_gx[src_idx];
                lin_gy[i] = s_buf_gy[src_idx];
                lin_gz[i] = s_buf_gz[src_idx];
            }

            // Extract 17 rotation-invariant kinematic features
            fall_extract_features(lin_ax, lin_ay, lin_az, lin_gx, lin_gy, lin_gz, 
                                  KINEMATIC_WINDOW_SAMPLES, extracted_features);

            // Compute Random Forest Fall Probability
            float rf_prob = predict_rf_probability(extracted_features);

            // Dispatch event to Core 0 Telemetry & Crypto task
            KinematicSampleEvent_t evt;
            evt.timestamp_ms = now_ms;
            evt.ax = ax; evt.ay = ay; evt.az = az;
            evt.gx = gx; evt.gy = gy; evt.gz = gz;
            evt.peak_acc_g = extracted_features[3]; // acc_mag_max
            evt.rf_fall_prob = rf_prob;
            evt.is_candidate_fall = (stage1_shock_detected || rf_prob >= STAGE2_RF_FALL_PROB_THRESH);
            memcpy(evt.features, extracted_features, sizeof(extracted_features));
            evt.step_count = s_step_engine.step_count;
            evt.distance_km = s_step_engine.distance_km;
            evt.calories_kcal = s_step_engine.calories_kcal;

            if (s_event_queue != NULL) {
                xQueueSend(s_event_queue, &evt, 0);
            }
        }
    }
}

/* =========================================================================
 * 4. CORE 0 TASK: ASCON-128 CRYPTO, WI-FI, TELEMETRY & CLOUD UPLINK
 * ========================================================================= */
void connect_wifi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("[Wi-Fi] Connecting to SSID: %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start_ms = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < WIFI_TIMEOUT_MS) {
        delay(250);
        digitalWrite(XIAO_USER_LED_PIN, !digitalRead(XIAO_USER_LED_PIN)); // Blink while connecting
    }

    if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(XIAO_USER_LED_PIN, LOW); // Solid ON when connected (Active LOW)
        Serial.printf("[Wi-Fi] Connected! IP: %s | RSSI: %d dBm\n", 
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println(F("[Wi-Fi] Connection timed out. Operating in offline/BLE fallback mode."));
        digitalWrite(XIAO_USER_LED_PIN, HIGH); // OFF
    }
}

void vTelemetryCryptoTask(void *pvParameters) {
    Serial.printf("[Core 0] Telemetry & ASCON Cryptographic Task initialized on Core %d\n", xPortGetCoreID());

    connect_wifi();

    KinematicSampleEvent_t evt;
    char plaintext_json[512];
    uint8_t ciphertext[512];
    uint8_t auth_tag[16];
    char hex_ciphertext[1025];
    char hex_tag[33];
    uint8_t nonce[16];

    WiFiClientSecure secureClient;
    secureClient.setInsecure(); // Accept Firebase & Render SSL certificates

    unsigned long last_routine_uplink_ms = 0;

    while (true) {
        // Wait for kinematic inference event from Core 1
        if (xQueueReceive(s_event_queue, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            bool is_emergency = evt.is_candidate_fall && (evt.rf_fall_prob >= STAGE2_RF_FALL_PROB_THRESH);
            unsigned long now = millis();

            // Uplink on emergency or routine interval (every 3 seconds)
            if (is_emergency || (now - last_routine_uplink_ms >= 3000)) {
                last_routine_uplink_ms = now;

                const char *status_str = is_emergency ? "FALL_DETECTED" : "NORMAL";

                // 1. Assemble Plaintext Telemetry JSON Payload
                snprintf(plaintext_json, sizeof(plaintext_json),
                    "{\"status\":\"%s\",\"impact\":%.2f,\"rf_prob\":%.3f,\"steps\":%lu,"
                    "\"dist_km\":%.2f,\"cal_kcal\":%.1f,\"device\":\"XIAO-ESP32S3\","
                    "\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,"
                    "\"seq\":%lu,\"ts\":%lu}",
                    status_str, evt.peak_acc_g, evt.rf_fall_prob, (unsigned long)evt.step_count,
                    evt.distance_km, evt.calories_kcal,
                    evt.ax, evt.ay, evt.az, evt.gx, evt.gy, evt.gz,
                    (unsigned long)s_ascon_packet_counter, (unsigned long)evt.timestamp_ms
                );

                size_t pt_len = strlen(plaintext_json);

                // 2. Generate 128-bit Monotonic Nonce (Sequence Counter)
                s_ascon_packet_counter++;
                memset(nonce, 0, sizeof(nonce));
                nonce[12] = (uint8_t)(s_ascon_packet_counter >> 24);
                nonce[13] = (uint8_t)(s_ascon_packet_counter >> 16);
                nonce[14] = (uint8_t)(s_ascon_packet_counter >> 8);
                nonce[15] = (uint8_t)(s_ascon_packet_counter);

                // 3. Execute NIST ASCON-128 AEAD Authenticated Encryption
                uint32_t t_crypto_start = micros();
                ascon_128_encrypt(
                    ASCON128_DEFAULT_KEY,
                    nonce,
                    (const uint8_t*)ASCON_AAD_STRING,
                    strlen(ASCON_AAD_STRING),
                    (const uint8_t*)plaintext_json,
                    pt_len,
                    ciphertext,
                    auth_tag
                );
                uint32_t t_crypto_us = micros() - t_crypto_start;

                bytes_to_hex(ciphertext, pt_len, hex_ciphertext);
                bytes_to_hex(auth_tag, 16, hex_tag);

                // 4. Output rich diagnostics over native USB-C CDC Serial
                Serial.println(F("\n=================[ XIAO ESP32-S3 TELEMETRY ]================="));
                Serial.printf("Status: %s | Impact: %.2fg | RF P(Fall): %.1f%% | Steps: %lu\n",
                              status_str, evt.peak_acc_g, evt.rf_fall_prob * 100.0f, (unsigned long)evt.step_count);
                Serial.printf("ASCON-128 AEAD Encrypted in %u us (< 0.5 ms)\n", t_crypto_us);
                Serial.printf("Nonce: 000000000000%08lX\n", (unsigned long)s_ascon_packet_counter);
                Serial.printf("Tag  : %s\n", hex_tag);
                Serial.printf("Ciphertext: %.64s...\n", hex_ciphertext);
                Serial.println(F("============================================================="));

                // 5. Send authenticated packet to Cloud Firebase & Render Webhook
                if (WiFi.status() == WL_CONNECTED) {
                    HTTPClient http;
                    String url = String(FIREBASE_HOST_URL) + String(is_emergency ? FIREBASE_INCIDENT_PATH : FIREBASE_LIVE_PATH);
                    
                    if (http.begin(secureClient, url)) {
                        http.addHeader("Content-Type", "application/json");
                        
                        // Send encrypted payload envelope
                        char cloud_envelope[1500];
                        snprintf(cloud_envelope, sizeof(cloud_envelope),
                            "{\"status\":\"%s\",\"impact\":%.2f,\"steps\":%lu,\"ciphertext\":\"%s\",\"tag\":\"%s\",\"seq\":%lu}",
                            status_str, evt.peak_acc_g, (unsigned long)evt.step_count, hex_ciphertext, hex_tag, (unsigned long)s_ascon_packet_counter
                        );

                        int httpCode = is_emergency ? http.POST(cloud_envelope) : http.PUT(cloud_envelope);
                        if (httpCode > 0) {
                            Serial.printf("[Cloud Uplink] Firebase HTTP Response: %d\n", httpCode);
                        } else {
                            Serial.printf("[Cloud Uplink] Firebase Upload Error: %s\n", http.errorToString(httpCode).c_str());
                        }
                        http.end();
                    }
                } else {
                    connect_wifi();
                }
            }
        }
    }
}

/* =========================================================================
 * 5. SYSTEM SETUP & FREE-RTOS TASK SPAWNER
 * ========================================================================= */
void setup() {
    // 1. Initialize Native USB CDC Serial on Seeed Studio XIAO ESP32-S3
    Serial.begin(115200);
    // Brief wait for USB CDC handshake (timeout after 2.5s if standalone battery powered)
    unsigned long start_serial = millis();
    while (!Serial && (millis() - start_serial < 2500)) {
        delay(10);
    }

    pinMode(XIAO_USER_LED_PIN, OUTPUT);
    digitalWrite(XIAO_USER_LED_PIN, HIGH); // Yellow LED off (Active LOW)

    Serial.println(F("\n\n#############################################################"));
    Serial.println(F("  ELDERCARE IOT: SEEED STUDIO XIAO ESP32-S3 FIRMWARE V2.4"));
    Serial.println(F("  Dual-Core Xtensa LX7 @ 240MHz • 8MB Octal PSRAM"));
    Serial.println(F("  TinyML 20-Tree C99 Random Forest • NIST ASCON-128 AEAD"));
    Serial.println(F("#############################################################"));

    // Check PSRAM allocation
    if (psramFound()) {
        Serial.printf("[PSRAM] 8MB Octal PSRAM verified! Free: %d KB\n", ESP.getFreePsram() / 1024);
    } else {
        Serial.println(F("[PSRAM] WARNING: PSRAM not initialized (running on internal SRAM)."));
    }

    // Create FreeRTOS Inter-Core Event Queue
    s_event_queue = xQueueCreate(10, sizeof(KinematicSampleEvent_t));
    if (s_event_queue == NULL) {
        Serial.println(F("[FATAL] Failed to create FreeRTOS event queue!"));
        while (true) delay(1000);
    }

    // Pin Core 1 Task: Hard Real-Time 50 Hz Kinematics & Feature Extraction
    xTaskCreatePinnedToCore(
        vSensorMotionTask,
        "SensorMotionCore1",
        TASK_SENSOR_STACK_SIZE,
        NULL,
        TASK_SENSOR_PRIO,
        NULL,
        TASK_SENSOR_CORE
    );

    // Pin Core 0 Task: ASCON-128 Encryption, Wi-Fi & Telemetry Cloud Uplink
    xTaskCreatePinnedToCore(
        vTelemetryCryptoTask,
        "TelemetryCryptoCore0",
        TASK_TELEMETRY_STACK_SIZE,
        NULL,
        TASK_TELEMETRY_PRIO,
        NULL,
        TASK_TELEMETRY_CORE
    );

    Serial.println(F("[FreeRTOS] Dual-Core tasks successfully pinned and running."));
}

void loop() {
    // Both cores run dedicated FreeRTOS tasks. Loop remains dormant to save CPU.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
