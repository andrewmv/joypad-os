// app.c - WII2BLE App Entry Point
//
// Reads a Wii extension controller (Nunchuck / Classic / Classic Pro) over
// I2C and routes input to two simultaneous outputs on ESP32-S3:
//   - USB HID gamepad  (wired, via native USB OTG)
//   - BLE HID gamepad  (wireless, via BTstack HOGP peripheral)
//
// Both outputs are live at the same time via ROUTING_MODE_BROADCAST.
// BOOT button: single click cycles USB output mode; long hold clears BLE bonds.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/services/leds/leds.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/wii_ext/wii_ext_host.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "usb/usbd/usbd.h"
#ifdef OLED_I2C_DISPLAY
#include "core/services/display/display.h"
#endif
#include "bt/ble_output/ble_output.h"
#include "bt/transport/bt_transport.h"
#include "platform/platform.h"
#include "tusb.h"
#include "gap.h"
#include "ble/le_device_db.h"
#include <stdio.h>

// BTstack ESP32 transport (defined in bt_transport_esp32.c)
extern const bt_transport_t bt_transport_esp32;
extern void bt_esp32_set_post_init(void (*cb)(void));

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &wii_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

// BLE output listed first so it gets the primary output slot for profile
// selection (Classic Pro has no Home button to drive the USB mode cycle).
static const OutputInterface* output_interfaces[] = {
    &ble_output_interface,
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK: {
            // Cycle USB HID output mode (HID → XInput → SInput → …)
            printf("[app:wii2ble] Button click — cycling USB output mode\n");
            tud_task_ext(1, false);
            platform_sleep_ms(50);
            tud_task_ext(1, false);
            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:wii2ble] USB mode → %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Cycle BLE output mode (Standard ↔ Xbox BLE)
            ble_output_mode_t next = ble_output_get_next_mode();
            printf("[app:wii2ble] Double-click — BLE mode → %s\n",
                   ble_output_get_mode_name(next));
            ble_output_set_mode(next);
            break;
        }

        case BUTTON_EVENT_HOLD:
            // Clear all BLE bonds and restart advertising
            printf("[app:wii2ble] Long press — clearing BLE bonds\n");
            gap_delete_all_link_keys();
            for (int i = 0; i < le_device_db_max_count(); i++) {
                le_device_db_remove(i);
            }
            printf("[app:wii2ble] Bonds cleared, restarting advertising\n");
            gap_advertisements_enable(1);
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:wii2ble] Initializing WII2BLE v%s\n", APP_VERSION);
    printf("[app:wii2ble]   Wii I2C: SDA=%d SCL=%d @ %uHz\n",
           WII_PIN_SDA, WII_PIN_SCL, (unsigned)WII_I2C_FREQ_HZ);

    button_init();
    button_set_callback(on_button_event);

    // Initialize Wii extension I2C host (dual-port when second pins are defined).
    // Each port gets its own I2C bus (bus 0 / bus 1); all Wii accessories share
    // address 0x52 so they cannot share a bus.
#if defined(WII_PIN_SDA2) && WII_PIN_SDA2 != 255
    wii_host_init_dual_detected(WII_PIN_SDA,  WII_PIN_SCL,  WII_DETECT_GPIO,
                                WII_PIN_SDA2, WII_PIN_SCL2, WII_DETECT_GPIO2);
    printf("[app:wii2ble]   Player 2: SDA=%d SCL=%d detect=%d\n",
           WII_PIN_SDA2, WII_PIN_SCL2, WII_DETECT_GPIO2);
#else
    wii_host_init_pins_detected(WII_PIN_SDA, WII_PIN_SCL, WII_DETECT_GPIO);
#endif

    // Configure router: broadcast Wii input to both USB and BLE outputs
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE]     = USB_OUTPUT_PORTS,
            [OUTPUT_TARGET_BLE_PERIPHERAL] = MAX_PLAYER_SLOTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
    };
    router_init(&router_cfg);

    router_add_route(INPUT_SOURCE_NATIVE_WII, OUTPUT_TARGET_USB_DEVICE,     0);
    router_add_route(INPUT_SOURCE_NATIVE_WII, OUTPUT_TARGET_BLE_PERIPHERAL, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &wii2ble_profile_set,
    };
    profile_init(&profile_cfg);

#ifdef OLED_I2C_DISPLAY
    {
        display_i2c_config_t disp_cfg = { .addr = WII_DISPLAY_ADDR };
        display_init_sh1106_i2c(&disp_cfg);
        if (display_is_initialized()) {
            printf("[app:wii2ble]   OLED: SH1106 ready (bus %d, addr 0x%02X, 400kHz)\n",
                   OLED_I2C_BUS, WII_DISPLAY_ADDR);
        } else {
            printf("[app:wii2ble]   OLED: no display at 0x%02X (disabled)\n",
                   WII_DISPLAY_ADDR);
        }
    }
#endif

    // Initialize BLE output (loads mode from flash before BTstack starts)
    ble_output_init();

    // Start BTstack in peripheral mode; GATT/GAP setup runs after HCI ready
    bt_esp32_set_post_init(ble_output_late_init);
    bt_init(&bt_transport_esp32);

    printf("[app:wii2ble] Initialization complete\n");
    printf("[app:wii2ble]   Routing: Wii extension → USB HID + BLE HID\n");
}

// ============================================================================
// OLED STATUS RENDER
// ============================================================================

#ifdef OLED_I2C_DISPLAY
// Button order for the controller row display
static const uint32_t DISPLAY_BTN_MAP[] = {
    JP_BUTTON_DU, JP_BUTTON_DD, JP_BUTTON_DL, JP_BUTTON_DR,
    JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4,
    JP_BUTTON_L1, JP_BUTTON_R1, JP_BUTTON_S1, JP_BUTTON_S2,
};
#define DISPLAY_BTN_COUNT  12
#define DISPLAY_BTN_SIZE    7   // px (box width and height)
#define DISPLAY_BTN_STRIDE  9   // px (box + 2px gap)
#define DISPLAY_BTN_X0     14   // px (x position of first button)

static void render_display(void)
{
    if (!display_is_initialized()) return;

    display_clear();

    // ---- Controller rows (P1 / P2) ----
    // Each row: label + one 7×7 box per button (filled=pressed, outline=released)
    for (uint8_t p = 0; p < 2; p++) {
        uint8_t y = p * 11;
        char label[4];
        snprintf(label, sizeof(label), "P%u", p + 1u);
        display_text(0, y, label);

        if (!wii_host_port_is_connected(p)) {
            display_text(DISPLAY_BTN_X0, y, "---");
        } else {
            const input_event_t *ev =
                router_get_output(OUTPUT_TARGET_USB_DEVICE, p);
            uint32_t btns = ev ? ev->buttons : 0;
            for (int b = 0; b < DISPLAY_BTN_COUNT; b++) {
                uint8_t bx = DISPLAY_BTN_X0 + b * DISPLAY_BTN_STRIDE;
                bool pressed = (btns & DISPLAY_BTN_MAP[b]) != 0;
                if (pressed) display_fill_rect(bx, y, DISPLAY_BTN_SIZE, DISPLAY_BTN_SIZE, true);
                else         display_rect(bx, y, DISPLAY_BTN_SIZE, DISPLAY_BTN_SIZE);
            }
        }
    }

    // ---- Divider ----
    display_hline(0, 23, DISPLAY_WIDTH);

    // ---- BLE status ----
    display_text(0, 26, "BLE:");
    display_text(26, 26, ble_output_is_connected() ? "Paired      " : "Searching...");

    // ---- USB status ----
    display_text(0, 36, "USB:");
    display_text(26, 36, tud_mounted() ? "HID" : "Disconnected");

    // ---- Port presence indicators ----
    display_text(0, 50, "P1");
    if (wii_host_port_is_connected(0)) display_fill_rect(14, 50, 7, 7, true);
    else                               display_rect(14, 50, 7, 7);

    display_text(64, 50, "P2");
    if (wii_host_port_is_connected(1)) display_fill_rect(78, 50, 7, 7, true);
    else                               display_rect(78, 50, 7, 7);

    display_update();
}
#endif // OLED_I2C_DISPLAY

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    button_task();

    // Mirror USB output mode colour on the NeoPixel
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }

#ifdef OLED_I2C_DISPLAY
    static uint32_t last_display_ms = 0;
    uint32_t now_ms = platform_time_ms();
    if (now_ms - last_display_ms >= 100) {
        render_display();
        last_display_ms = now_ms;
    }
#endif
}
