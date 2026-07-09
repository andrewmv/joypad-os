// wii_ext_host.h - Native Wii extension controller host driver
//
// Reads a Wii Nunchuck / Classic Controller / Classic Pro via PIO I2C
// (cut extension cable wired directly to the Pico) and submits input
// events to the router. Single port for v1; multi-port is a future
// extension (see .dev/docs/WII_EXTENSION_PLAN.md).

#ifndef WII_EXT_HOST_H
#define WII_EXT_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// Default GPIO pins. Overridable per-app (see src/apps/wii2usb/app.h).
#ifndef WII_PIN_SDA
#define WII_PIN_SDA   2
#endif
#ifndef WII_PIN_SCL
#define WII_PIN_SCL   3
#endif
// Target I2C clock. Wii extensions tolerate 100..400 kHz; 100 kHz is the
// most forgiving with long/cheap cables and clone accessories.
#ifndef WII_I2C_FREQ_HZ
#define WII_I2C_FREQ_HZ  100000
#endif

void wii_host_init(void);
void wii_host_init_pins(uint8_t sda, uint8_t scl);
void wii_host_task(void);
bool wii_host_is_connected(void);
bool wii_host_port_is_connected(uint8_t port);

// Current extension type as a raw wii_ext_type_t value (0 = none).
// Exposed as int to keep this header free of the lib's internal enum.
int  wii_host_get_ext_type(void);

// Dual-port support: two independent I2C buses, one controller each.
// All Wii extension accessories share slave address 0x52, so each port
// must be on its own bus.
#ifndef WII_PIN_SDA2
#define WII_PIN_SDA2  255  // disabled by default
#endif
#ifndef WII_PIN_SCL2
#define WII_PIN_SCL2  255
#endif

void wii_host_init_dual(uint8_t sda1, uint8_t scl1, uint8_t sda2, uint8_t scl2);

// Presence-detect variants: detect_gpio is driven high by the accessory's
// VCC pin via the socket; a board pull-down makes it low when the socket is
// empty.  Use WII_DETECT_NONE (255) when the pin is not wired.
#define WII_DETECT_NONE 255

void wii_host_init_pins_detected(uint8_t sda, uint8_t scl, uint8_t detect);
void wii_host_init_dual_detected(uint8_t sda1, uint8_t scl1, uint8_t det1,
                                 uint8_t sda2, uint8_t scl2, uint8_t det2);

// When false, the driver suppresses all LED color writes, letting the app own
// the status LED entirely. Default true preserves wii2usb/wii2gc behavior where
// the LED reflects detected controller type.
void wii_host_set_status_led(bool enabled);

extern const InputInterface wii_input_interface;

#endif // WII_EXT_HOST_H
