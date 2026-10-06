// config.h -- pins and build-time constants. Change pins HERE only.
// Board: ESP32-S3 dev board (ESP32-S3-WROOM-1). In the Arduino IDE pick
// Tools > Board > "ESP32S3 Dev Module".
// Never use on the S3: GPIO 19/20 (native USB), 26-32 (flash), 35-37 (PSRAM on
// N8R8/N16R8 modules), 0/3/45/46 (boot strapping). GPIO 22-25 do not exist.
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32) && CONFIG_IDF_TARGET_ESP32
#error "These pins are for the ESP32-S3: in Tools > Board pick ESP32S3 Dev Module. (GPIO 6-9 would crash a classic ESP32.)"
#endif

#define RS_FW_VERSION "1.4.0"

// ----------------------------- I2C buses -------------------------------------
// Bus 0: both MPU6050s (long wire to the shin -> run it at 100 kHz)
// Aman's tested breadboard (1 Oct): the OLED and the first MPU6050 share 8/9.
// That works as is: the OLED is found on this bus by the fallback below.
// For elbow flexion the same two sensors go on the upper arm (0x68) and the
// forearm (0x69).
#define PIN_IMU_SDA 8
#define PIN_IMU_SCL 9
#define IMU_I2C_HZ 100000

// Bus 1: the OLED on its OWN bus, if it is wired here. A full OLED refresh
// takes ~25 ms even at 400 kHz; on a separate bus it can never stall the
// 100 Hz sensor loop. If the OLED is not found here (e.g. it is wired to the
// IMU bus, 8/9), the firmware falls back to a shared, mutex-protected bus:
// it works, the screen just refreshes at 4 fps instead of 15.
// 17/18, not 18/19: GPIO 19 is the S3's native USB D- line.
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define OLED_I2C_HZ 400000
#define OLED_ADDR 0x3C

#define IMU_ADDR_THIGH 0x68  // AD0 -> GND (or floating). Thigh, or upper arm for elbow
#define IMU_ADDR_SHIN 0x69   // AD0 -> 3V3. Shin, or forearm for elbow

// ------------------------------- outputs -------------------------------------
// Motor on GPIO 5: the S3 has no GPIO 25 (the old classic-ESP32 pin).
#define PIN_MOTOR 5     // NPN base via 1k resistor, flyback diode across motor
#define PIN_BUZZER 4    // ACTIVE buzzer (+ to pin via NPN if it draws > 20 mA)

// ------------------------------- buttons -------------------------------------
// Wire each button between the pin and GND (internal pull-ups are used).
#define PIN_BTN_NEXT 15
#define PIN_BTN_OK 6
#define PIN_BTN_BACK 7

// ------------------------------- timing --------------------------------------
#define SENSOR_HZ 100
#define UI_REFRESH_MS 66          // ~15 fps on the dedicated OLED bus
#define UI_REFRESH_SHARED_MS 250  // slower if the OLED shares the IMU bus

// ------------------------------- network -------------------------------------
#define AP_SSID_PREFIX "RehabSense-"  // + last 4 hex digits of the MAC
#define AP_PASSWORD "rehab1234"       // >= 8 characters
#define THINGSPEAK_MIN_GAP_MS 16000   // free tier: ~1 update / 15 s
#define NTP_SERVER "pool.ntp.org"

// ------------------------------- storage -------------------------------------
#define SESSIONS_FILE "/sessions.csv"
#define SESSIONS_OLD_FILE "/sessions_old.csv"
#define SESSIONS_MAX_BYTES (96 * 1024)  // rotate the log beyond this size
// Clinical test results (chair stand, TUG, position sense): own log, own ids.
#define TESTS_FILE "/tests.csv"
#define TESTS_OLD_FILE "/tests_old.csv"
#define TESTS_MAX_BYTES (32 * 1024)
