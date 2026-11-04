# Seeed Studio XIAO ESP32-S3: Production Firmware & Flashing Guide

This directory contains the production-grade, dual-core TinyML & ASCON-128 AEAD firmware configured specifically for the **Seeed Studio XIAO ESP32-S3** wearable form-factor.

---

## 1. Verified Hardware Audit
- **Detected Serial Port:** `/dev/cu.usbmodem101` (macOS)
- **Target Silicon:** Espressif ESP32-S3 (QFN56 revision v0.2)
- **CPU Architecture:** Xtensa Dual-Core 32-bit LX7 @ 240 MHz
- **Memory:** 512 KB Internal SRAM + **8 MB Embedded Octal OPI PSRAM**
- **Hardware MAC Address:** `68:EE:8F:50:62:74`
- **USB Interface:** Native USB-Serial/JTAG CDC (Type-C)

---

## 2. Pinout & Hardware Wiring Matrix

> **IMPORTANT CAUTION:**  
> Do **NOT** use standard ESP32 30-pin DevKit GPIOs (such as GPIO 21 for SDA or GPIO 22 for SCL). On the XIAO ESP32-S3 footprint, those pins are not exposed. Connect strictly according to the table below:

| Peripheral / Module | Sensor Pin | XIAO ESP32-S3 Physical Pin | XIAO GPIO Number | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **MPU6050 IMU** | `VCC` | `3V3` or `5V` | Power | Connect to 3.3V or 5V (if module has regulator) |
| | `GND` | `GND` | Ground | Common system ground |
| | `SDA` | **`D4`** | **`GPIO 5`** | Native Hardware I2C SDA |
| | `SCL` | **`D5`** | **`GPIO 6`** | Native Hardware I2C SCL |
| | `AD0` | `GND` | - | Pull to GND for I2C address `0x68` |
| **SSD1306 OLED (Optional)** | `VCC` | `3V3` | Power | 3.3V display power |
| | `GND` | `GND` | Ground | Common ground |
| | `SDA` | **`D4`** | **`GPIO 5`** | Shared I2C bus (Address `0x3C`) |
| | `SCL` | **`D5`** | **`GPIO 6`** | Shared I2C bus |
| **MAX30102 PPG (Optional)** | `VCC` | `3V3` | Power | 3.3V sensor power |
| | `GND` | `GND` | Ground | Common ground |
| | `SDA` | **`D4`** | **`GPIO 5`** | Shared I2C bus (Address `0x57`) |
| | `SCL` | **`D5`** | **`GPIO 6`** | Shared I2C bus |
| **GPS Module (Optional)** | `TX` | **`D7`** | **`GPIO 44`** | GPS TX -> XIAO RX (UART1) |
| | `RX` | **`D6`** | **`GPIO 43`** | GPS RX -> XIAO TX (UART1) |
| **User Status LED** | Built-in | On-board | `GPIO 21` | Yellow LED (Active LOW: Low=ON, High=OFF) |

---

## 3. Method A: Flashing via Arduino IDE (Recommended GUI Method)

### Step 1: Install ESP32 Board Package
1. Open **Arduino IDE** -> **Settings / Preferences**.
2. In **Additional Boards Manager URLs**, paste:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools** -> **Board** -> **Boards Manager...**, search for `esp32` by Espressif Systems, and ensure it is installed (version 2.0.14+ or 3.0+).

### Step 2: Configure Board Settings for XIAO ESP32-S3
Under the **Tools** menu, select the following exact parameters:
- **Board:** `XIAO_ESP32S3` (or `Seeed Studio XIAO ESP32S3`)
- **Port:** `/dev/cu.usbmodem101`
- **USB CDC On Boot:** **`Enabled`** *(CRITICAL: Required for Serial to print over USB-C)*
- **USB Mode:** `Hardware CDC and JTAG`
- **CPU Frequency:** `240MHz (WiFi)`
- **PSRAM:** **`OPI PSRAM`** *(Enables the on-board 8MB high-speed RAM)*
- **Flash Mode:** `QIO 80MHz`
- **Flash Size:** `8MB (64Mb)`
- **Partition Scheme:** `Default 8MB with SPIFFS` (or `Huge APP 3MB`)

### Step 3: Flash the Firmware
1. Open [`xiao_esp32s3_firmware.ino`](./xiao_esp32s3_firmware.ino).
2. Click the **Upload (→)** button.
3. Open **Serial Monitor** at **`115200 baud`**. You will immediately see the initialization banner, 50 Hz IMU diagnostics, and ASCON-128 AEAD encrypted packets.

---

## 4. Method B: Flashing via PlatformIO CLI / VS Code

1. Navigate to the firmware directory:
   ```bash
   cd "/Users/kodevenkatdhheeraj/c/xiao_esp32s3_firmware"
   ```
2. Build and upload directly using the configured `platformio.ini`:
   ```bash
   pio run -e seeed_xiao_esp32s3 --target upload
   ```
3. Open the serial console:
   ```bash
   pio device monitor -b 115200
   ```

---

## 5. Method C: Flashing via ESP-IDF v5.2.7

The machine already has ESP-IDF v5.2.7 installed at `~/.espressif/v5.2.7/esp-idf/`.

To flash directly using native ESP-IDF tools:
```bash
# 1. Activate the ESP-IDF environment
. ~/.espressif/v5.2.7/esp-idf/export.sh

# 2. Set target to ESP32-S3
idf.py set-target esp32s3

# 3. Build, flash to /dev/cu.usbmodem101, and open serial monitor
idf.py -p /dev/cu.usbmodem101 flash monitor
```

---

## 6. Verification Checklist Before First Power-Up

- [x] **USB Port Verified:** `/dev/cu.usbmodem101` detected.
- [ ] **I2C Pull-Up Resistors:** MPU6050 breakout boards typically include 4.7kΩ pull-ups on SDA & SCL. Ensure no duplicate conflicting external pull-ups are added.
- [ ] **Wi-Fi Credentials:** Configured in `config.h` as `SSID: "Dhheeraj"`, `Pass: "onetwoeight"`. Modify in `config.h` if testing on a different local network.
- [ ] **Cloud Backend:** Live Firebase RTDB (`https://elderly-fall-detection-a0f34-default-rtdb.firebaseio.com`) and Render dashboard (`https://elderly-fall-detection.onrender.com`).
- [ ] **Safe Fall Testing:** Hold the XIAO S3 flat to verify ~1.0g gravity baseline. A sharp rotational drop onto a cushioned surface will trigger Stage 1 ($> 2.30\text{g}$) and Stage 2 Random Forest inference, immediately encrypting the payload with ASCON-128 and transmitting to your live dashboard.
