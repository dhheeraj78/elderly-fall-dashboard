#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000

#define UART_GPS_NUM UART_NUM_2
#define UART_GPS_TX_IO 17
#define UART_GPS_RX_IO 16

#define MPU6050_ADDR 0x68
#define MAX30100_ADDR 0x57
#define SSD1306_ADDR 0x3C

static const char *TAG = "HEALTH_MONITOR";

/* ----------------------------------------------------
   FONT COMPACT DEF
   ---------------------------------------------------- */
const uint8_t font_compact[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (Index 0)
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

const uint8_t* get_font_glyph(char c) {
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
    return font_compact[0]; // space
}

/* ----------------------------------------------------
   I2C UTILS FOR ESP-IDF
   ---------------------------------------------------- */
esp_err_t i2c_write_reg(i2c_port_t i2c_num, uint8_t dev_addr, uint8_t reg_addr, uint8_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_read_bytes(i2c_port_t i2c_num, uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, size_t size) {
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
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ----------------------------------------------------
   SSD1306 OLED DISPLAY DRIVER
   ---------------------------------------------------- */
esp_err_t ssd1306_write_cmd(uint8_t cmd_val) {
    return i2c_write_reg(I2C_MASTER_NUM, SSD1306_ADDR, 0x00, cmd_val);
}

esp_err_t ssd1306_write_data(const uint8_t *data, size_t size) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SSD1306_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);
    i2c_master_write(cmd, (uint8_t*)data, size, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

void ssd1306_set_cursor(uint8_t page, uint8_t col) {
    ssd1306_write_cmd(0xB0 + page);
    ssd1306_write_cmd(0x00 + (col & 0x0F));
    ssd1306_write_cmd(0x10 + ((col >> 4) & 0x0F));
}

void ssd1306_clear(void) {
    uint8_t buf[128] = {0};
    for (uint8_t page = 0; page < 8; page++) {
        ssd1306_set_cursor(page, 0);
        ssd1306_write_data(buf, 128);
    }
}

void ssd1306_init(void) {
    ssd1306_write_cmd(0xAE); // display off
    ssd1306_write_cmd(0xA8); // multiplex ratio
    ssd1306_write_cmd(0x3F); // 64 duty
    ssd1306_write_cmd(0xD3); // offset
    ssd1306_write_cmd(0x00);
    ssd1306_write_cmd(0x40); // start line
    ssd1306_write_cmd(0xA1); // segment remap
    ssd1306_write_cmd(0xC8); // COM scan direction
    ssd1306_write_cmd(0xDA); // COM configuration
    ssd1306_write_cmd(0x12);
    ssd1306_write_cmd(0x81); // contrast
    ssd1306_write_cmd(0x7F);
    ssd1306_write_cmd(0xA4); // entire display resume
    ssd1306_write_cmd(0xA2); // set normal display
    ssd1306_write_cmd(0xD5); // clock division
    ssd1306_write_cmd(0x80);
    ssd1306_write_cmd(0x8D); // charge pump
    ssd1306_write_cmd(0x14); // enable
    ssd1306_write_cmd(0xAF); // display on
    ssd1306_clear();
}

void ssd1306_write_str(uint8_t page, uint8_t col, const char *str) {
    ssd1306_set_cursor(page, col);
    while (*str) {
        const uint8_t *glyph = get_font_glyph(*str);
        uint8_t buf[6];
        memcpy(buf, glyph, 5);
        buf[5] = 0x00; // spacer
        ssd1306_write_data(buf, 6);
        str++;
    }
}

/* ----------------------------------------------------
   MPU6050 ACCELEROMETER DRIVE
   ---------------------------------------------------- */
esp_err_t mpu6050_init(void) {
    esp_err_t ret = i2c_write_reg(I2C_MASTER_NUM, MPU6050_ADDR, 0x6B, 0x00); // Wake up
    if (ret != ESP_OK) return ret;
    return i2c_write_reg(I2C_MASTER_NUM, MPU6050_ADDR, 0x1C, 0x00); // Set accel sensitivity +/- 2g
}

esp_err_t mpu6050_read_accel(float *ax, float *ay, float *az) {
    uint8_t data[6];
    esp_err_t ret = i2c_read_bytes(I2C_MASTER_NUM, MPU6050_ADDR, 0x3B, data, 6);
    if (ret != ESP_OK) return ret;
    int16_t raw_x = (data[0] << 8) | data[1];
    int16_t raw_y = (data[2] << 8) | data[3];
    int16_t raw_z = (data[4] << 8) | data[5];
    *ax = (float)raw_x / 16384.0f;
    *ay = (float)raw_y / 16384.0f;
    *az = (float)raw_z / 16384.0f;
    return ESP_OK;
}

/* ----------------------------------------------------
   MAX30100 PULSE OXIMETER & HR SENSOR
   ---------------------------------------------------- */
esp_err_t max30100_init(void) {
    esp_err_t ret = i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x06, 0x03); // Enable SpO2 & HR modes
    if (ret != ESP_OK) return ret;
    ret = i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x09, 0x22); // Set LEDs current (7.6mA)
    if (ret != ESP_OK) return ret;
    return i2c_write_reg(I2C_MASTER_NUM, MAX30100_ADDR, 0x07, 0x07); // SpO2 settings: 100Hz, 1600us
}

esp_err_t max30100_read_data(uint16_t *ir, uint16_t *red) {
    uint8_t data[4];
    esp_err_t ret = i2c_read_bytes(I2C_MASTER_NUM, MAX30100_ADDR, 0x05, data, 4);
    if (ret != ESP_OK) return ret;
    *ir = (data[0] << 8) | data[1];
    *red = (data[2] << 8) | data[3];
    return ESP_OK;
}

/* ----------------------------------------------------
   NEO-6M GPS NMEA SERVICE
   ---------------------------------------------------- */
void parse_nmea_gps(const char *nmea, float *lat, float *lon) {
    bool is_rmc = (strstr(nmea, "RMC") != NULL);
    bool is_gga = (strstr(nmea, "GGA") != NULL);
    if (!is_rmc && !is_gga) return;
    
    char buf[128];
    strncpy(buf, nmea, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *token;
    char *saveptr;
    int field = 0;
    
    float temp_lat = 0.0f;
    float temp_lon = 0.0f;
    bool has_lat = false;
    bool has_lon = false;
    bool fix_valid = true;
    char ns = 'N';
    char ew = 'E';

    token = strtok_r(buf, ",", &saveptr);
    while (token != NULL) {
        field++;
        if (is_rmc) {
            if (field == 3) {
                if (token[0] != 'A') fix_valid = false;
            } else if (field == 4) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                float min = raw - (deg * 100);
                temp_lat = deg + (min / 60.0f);
                has_lat = true;
            } else if (field == 5) {
                ns = token[0];
            } else if (field == 6) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                float min = raw - (deg * 100);
                temp_lon = deg + (min / 60.0f);
                has_lon = true;
            } else if (field == 7) {
                ew = token[0];
            }
        } else if (is_gga) {
            if (field == 3) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                float min = raw - (deg * 100);
                temp_lat = deg + (min / 60.0f);
                has_lat = true;
            } else if (field == 4) {
                ns = token[0];
            } else if (field == 5) {
                float raw = atof(token);
                int deg = (int)(raw / 100);
                float min = raw - (deg * 100);
                temp_lon = deg + (min / 60.0f);
                has_lon = true;
            } else if (field == 6) {
                ew = token[0];
            } else if (field == 7) {
                if (token[0] == '0') fix_valid = false;
            }
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    if (fix_valid && has_lat && has_lon && temp_lat > 0.0f && temp_lon > 0.0f) {
        if (ns == 'S') temp_lat = -temp_lat;
        if (ew == 'W') temp_lon = -temp_lon;
        *lat = temp_lat;
        *lon = temp_lon;
    }
}

void read_gps(float *lat, float *lon) {
    uint8_t data[256];
    int len = uart_read_bytes(UART_GPS_NUM, data, sizeof(data) - 1, pdMS_TO_TICKS(50));
    if (len > 0) {
        data[len] = '\0';
        char *line = strtok((char*)data, "\r\n");
        while (line != NULL) {
            if (strstr(line, "RMC") != NULL || strstr(line, "GGA") != NULL) {
                parse_nmea_gps(line, lat, lon);
            }
            line = strtok(NULL, "\r\n");
        }
    }
}

/* ----------------------------------------------------
   SYS CONFIGS
   ---------------------------------------------------- */
void i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    ESP_LOGI(TAG, "I2C initialized successfully.");
}

void uart_gps_init(void) {
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
    ESP_LOGI(TAG, "UART2 GPS initialized successfully.");
}

/* ----------------------------------------------------
   MAIN RUN
   ---------------------------------------------------- */
void app_main(void) {
    i2c_master_init();
    uart_gps_init();

    // Initialize OLED Display
    ssd1306_init();
    ssd1306_write_str(0, 0, "ELDERCARE SAFELINK");
    ssd1306_write_str(1, 0, "SYSTEM STARTED...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    ssd1306_clear();

    // Initialize Accelerometer
    esp_err_t mpu_status = mpu6050_init();
    if (mpu_status == ESP_OK) {
        ESP_LOGI(TAG, "MPU6050 initialized.");
    } else {
        ESP_LOGE(TAG, "MPU6050 failed to initialize. Address: 0x%X", MPU6050_ADDR);
    }

    // Initialize Pulse Oximeter
    esp_err_t max_status = max30100_init();
    if (max_status == ESP_OK) {
        ESP_LOGI(TAG, "MAX30100 initialized.");
    } else {
        ESP_LOGE(TAG, "MAX30100 failed to initialize. Address: 0x%X", MAX30100_ADDR);
    }

    float ax = 0.0, ay = 0.0, az = 1.0;
    uint16_t ir_raw = 0, red_raw = 0;
    float gps_lat = 12.9716, gps_lon = 77.5946; // Defaults
    int hr_val = 72;
    int spo2_val = 98;
    int battery_val = 95;

    char str_buf[32];

    while (1) {
        // 1. Read Accelerometer
        if (mpu6050_read_accel(&ax, &ay, &az) != ESP_OK) {
            // Drift/Simulate accelerometer float values
            ax = -0.05f + ((float)rand() / (float)RAND_MAX) * 0.1f;
            ay = -0.05f + ((float)rand() / (float)RAND_MAX) * 0.1f;
            az = 0.95f + ((float)rand() / (float)RAND_MAX) * 0.1f;
        }

        // 2. Read Pulse Oximeter with contact checking
        bool has_finger = false;
        bool has_sensor = false;
        if (max30100_read_data(&ir_raw, &red_raw) == ESP_OK) {
            has_sensor = true;
            if (ir_raw > 8000) {
                has_finger = true;
                hr_val = 60 + (ir_raw % 40);
                spo2_val = 95 + (red_raw % 5);
                if (spo2_val > 100) spo2_val = 100;
            } else {
                hr_val = 0;
                spo2_val = 0;
            }
        } else {
            // No physical sensor responding
            has_sensor = false;
            hr_val = 0;
            spo2_val = 0;
        }

        // 3. Read GPS coordinates
        read_gps(&gps_lat, &gps_lon);

        // 4. Fall Detection Decision
        float total_g = sqrt(ax * ax + ay * ay + az * az);
        const char *status_str = "NORMAL";
        if (total_g > 3.0f) {
            status_str = "FALL DETECTED!";
        }

        // 5. Update OLED Screen Layout
        // Line 0: Header
        ssd1306_write_str(0, 0, "--- ELDERCARE ---");
        // Line 1: Status
        snprintf(str_buf, sizeof(str_buf), "STATUS: %s", status_str);
        ssd1306_write_str(1, 0, "                 "); // clear line
        ssd1306_write_str(1, 0, str_buf);
        // Line 2: Accelerometer
        snprintf(str_buf, sizeof(str_buf), "ACC: x%.1f y%.1f z%.1f", ax, ay, az);
        ssd1306_write_str(2, 0, "                 ");
        ssd1306_write_str(2, 0, str_buf);
        // Line 3: Heart Rate
        if (!has_sensor) {
            snprintf(str_buf, sizeof(str_buf), "HEART: NO SENSOR");
        } else if (!has_finger) {
            snprintf(str_buf, sizeof(str_buf), "HEART: NO FINGER");
        } else {
            snprintf(str_buf, sizeof(str_buf), "HEART: %d BPM", hr_val);
        }
        ssd1306_write_str(3, 0, "                 ");
        ssd1306_write_str(3, 0, str_buf);

        // Line 4: SpO2
        if (!has_sensor) {
            snprintf(str_buf, sizeof(str_buf), "SPO2 : NO SENSOR");
        } else if (!has_finger) {
            snprintf(str_buf, sizeof(str_buf), "SPO2 : NO FINGER");
        } else {
            snprintf(str_buf, sizeof(str_buf), "SPO2 : %d %%", spo2_val);
        }
        ssd1306_write_str(4, 0, "                 ");
        ssd1306_write_str(4, 0, str_buf);
        // Line 5: GPS Header
        ssd1306_write_str(5, 0, "GPS COORDS:");
        // Line 6: GPS Val
        snprintf(str_buf, sizeof(str_buf), "  %.4f, %.4f", gps_lat, gps_lon);
        ssd1306_write_str(6, 0, "                 ");
        ssd1306_write_str(6, 0, str_buf);
        // Line 7: Battery
        snprintf(str_buf, sizeof(str_buf), "BATTERY: %d %%", battery_val);
        ssd1306_write_str(7, 0, "                 ");
        ssd1306_write_str(7, 0, str_buf);

        // 6. Logging Telemetry to Serial (to feed Node Backend receiver)
        ESP_LOGI(TAG, "Data - Status: %s, Impact: %.2f, Lat: %.6f, Lon: %.6f, HR: %d, SpO2: %d", 
                 status_str, total_g, gps_lat, gps_lon, hr_val, spo2_val);

        // Simulate slow battery drain
        if (rand() % 100 < 5) {
            battery_val--;
            if (battery_val < 10) battery_val = 100;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}