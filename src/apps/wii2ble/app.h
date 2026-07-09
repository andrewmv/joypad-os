// app.h - WII2BLE App Manifest
// Wii extension controller (Nunchuck / Classic / Classic Pro) to
// USB HID + BLE HID adapter on ESP32-S3.
//
// Output modes:
//   USB  — Wired USB HID gamepad (primary; uses ESP32-S3 native USB OTG)
//   BLE  — Wireless BLE HID gamepad (HOGP peripheral; pairs to PC/HTPC)
//
// Both outputs are active simultaneously via the N:M router.
// BOOT button: single click cycles USB output mode; hold clears BLE bonds.
//
// Wiring (I2C — adjust WII_PIN_SDA / WII_PIN_SCL for your board):
//   Controller VCC  → 3V3
//   Controller GND  → GND
//   Controller SDA  → WII_PIN_SDA  (with 1.8–4.7 kΩ pull-up to 3V3)
//   Controller SCL  → WII_PIN_SCL  (with 1.8–4.7 kΩ pull-up to 3V3)
// Do NOT power from VBUS/5V — Wii extension accessories are 3.3 V parts.

#ifndef APP_WII2BLE_H
#define APP_WII2BLE_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME        "WII2BLE"
#define APP_VERSION     "0.1.0"
#define APP_DESCRIPTION "SNES Classic / Wii extension controller to USB HID + BLE HID adapter"
#define APP_AUTHOR      "joypad-os contributors"

// ============================================================================
// CORE FEATURES
// ============================================================================
#define REQUIRE_NATIVE_WII_HOST 1
#define WII_MAX_CONTROLLERS     2

#define REQUIRE_USB_DEVICE      1
#define USB_OUTPUT_PORTS        2

#define REQUIRE_BLE_OUTPUT      1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// I2C PIN CONFIGURATION
// ============================================================================
// Defaults for ESP32-S3-DevKitC-1 — change to match your PCB layout.
// Any GPIO pair works on ESP32-S3 (flexible GPIO matrix).
//
// Player 1 controller (I2C bus 0):
#define WII_PIN_SDA     10
#define WII_PIN_SCL     9
// Player 2 controller (I2C bus 1). Set WII_PIN_SDA2 to 255 to disable.
#define WII_PIN_SDA2    12
#define WII_PIN_SCL2    11

// OLED status display (SH1106 128x64, I2C).
// Shares the P2 bus (OLED_I2C_BUS=1 set in CMakeLists).
// Set WII_DISPLAY_ADDR to 255 to disable at runtime even when OLED_I2C_DISPLAY=1.
#define WII_DISPLAY_ADDR  0x3C   // 7-bit; 8-bit write address is 0x78

// Presence-detect GPIO for each socket (255 = pin not wired on this board).
// Connect the socket's VCC/detect pin through a pull-down resistor to a GPIO;
// the firmware reads high = controller inserted, low = socket empty.
#define WII_DETECT_GPIO   13
#define WII_DETECT_GPIO2  14

// I2C clock. Clone controllers are unreliable above ~100 kHz; 50 kHz is a
// conservative default. Increase to 400000 if your specific hardware allows.
#define WII_I2C_FREQ_HZ 50000

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
// SIMPLE mode: each physical Wii port is a distinct player slot.
// USB gets P1→slot0 and P2→slot1; BLE is capped to slot0 only via
// max_players_per_output in app.c.
#define ROUTING_MODE    ROUTING_MODE_SIMPLE

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     2
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_WII2BLE_H
