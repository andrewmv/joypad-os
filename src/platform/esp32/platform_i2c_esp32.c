// platform_i2c_esp32.c - ESP32 I2C implementation
//
// Wraps ESP-IDF v5+ i2c_master driver for the platform I2C HAL.

#include "platform/platform_i2c.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <string.h>

#define MAX_I2C_BUSES 2
#define MAX_DEVICES_PER_BUS 4

static const char* TAG = "i2c_esp32";

// Per-device handle cache (seesaw + future devices)
typedef struct {
    uint8_t  addr;
    uint32_t freq_hz;  // Device-specific clock override (0 = use bus->freq_hz)
    i2c_master_dev_handle_t handle;
} device_entry_t;

static struct platform_i2c {
    i2c_master_bus_handle_t bus_handle;
    device_entry_t devices[MAX_DEVICES_PER_BUS];
    uint8_t device_count;
    uint32_t freq_hz;
    bool initialized;
} i2c_buses[MAX_I2C_BUSES];

// Get or create a device handle for the given address
static i2c_master_dev_handle_t get_device_handle(struct platform_i2c* bus, uint8_t addr)
{
    // Search existing
    for (int i = 0; i < bus->device_count; i++) {
        if (bus->devices[i].addr == addr) {
            return bus->devices[i].handle;
        }
    }

    // Add new device
    if (bus->device_count >= MAX_DEVICES_PER_BUS) {
        ESP_LOGE(TAG, "Too many I2C devices on bus");
        return NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = bus->freq_hz ? bus->freq_hz : 400000,
    };

    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(err));
        return NULL;
    }

    bus->devices[bus->device_count].addr = addr;
    bus->devices[bus->device_count].handle = dev_handle;
    bus->device_count++;

    return dev_handle;
}

platform_i2c_t platform_i2c_init(const platform_i2c_config_t* config)
{
    if (!config || config->bus >= MAX_I2C_BUSES) return NULL;

    struct platform_i2c* bus = &i2c_buses[config->bus];
    if (bus->initialized) return bus;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = config->bus,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus->bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus %d: %s", config->bus, esp_err_to_name(err));
        return NULL;
    }

    bus->device_count = 0;
    bus->freq_hz = config->freq_hz;
    bus->initialized = true;
    ESP_LOGI(TAG, "Bus %d initialized (SDA=%d, SCL=%d, %luHz)",
             config->bus, config->sda_pin, config->scl_pin,
             (unsigned long)config->freq_hz);

    return bus;
}

int platform_i2c_write(platform_i2c_t bus, uint8_t addr, const uint8_t* data, size_t len)
{
    if (!bus || !bus->initialized) return -1;
    i2c_master_dev_handle_t dev = get_device_handle(bus, addr);
    if (!dev) return -1;

    esp_err_t err = i2c_master_transmit(dev, data, len, 100);
    return (err == ESP_OK) ? 0 : -1;
}

int platform_i2c_read(platform_i2c_t bus, uint8_t addr, uint8_t* data, size_t len)
{
    if (!bus || !bus->initialized) return -1;
    i2c_master_dev_handle_t dev = get_device_handle(bus, addr);
    if (!dev) return -1;

    esp_err_t err = i2c_master_receive(dev, data, len, 100);
    return (err == ESP_OK) ? 0 : -1;
}

int platform_i2c_write_read(platform_i2c_t bus, uint8_t addr,
                            const uint8_t* wr, size_t wr_len,
                            uint8_t* rd, size_t rd_len)
{
    if (!bus || !bus->initialized) return -1;
    i2c_master_dev_handle_t dev = get_device_handle(bus, addr);
    if (!dev) return -1;

    esp_err_t err = i2c_master_transmit_receive(dev, wr, wr_len, rd, rd_len, 100);
    return (err == ESP_OK) ? 0 : -1;
}

int platform_i2c_set_device_freq(platform_i2c_t bus, uint8_t addr, uint32_t freq_hz)
{
    if (!bus || !bus->initialized || !freq_hz) return -1;

    // Check if device already registered — re-create handle at new frequency
    for (int i = 0; i < bus->device_count; i++) {
        if (bus->devices[i].addr == addr) {
            if (bus->devices[i].freq_hz == freq_hz) return 0;  // Already correct
            i2c_master_bus_rm_device(bus->devices[i].handle);
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address  = addr,
                .scl_speed_hz    = freq_hz,
            };
            esp_err_t err = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg,
                                                       &bus->devices[i].handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "set_device_freq re-add 0x%02X failed: %s",
                         addr, esp_err_to_name(err));
                bus->devices[i].handle = NULL;
                return -1;
            }
            bus->devices[i].freq_hz = freq_hz;
            ESP_LOGI(TAG, "Device 0x%02X speed set to %luHz", addr, (unsigned long)freq_hz);
            return 0;
        }
    }

    // Device not yet cached — pre-register it at the requested frequency
    if (bus->device_count >= MAX_DEVICES_PER_BUS) {
        ESP_LOGE(TAG, "Too many I2C devices on bus (set_device_freq)");
        return -1;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = freq_hz,
    };
    i2c_master_dev_handle_t dev_handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(bus->bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_device_freq add 0x%02X failed: %s",
                 addr, esp_err_to_name(err));
        return -1;
    }
    bus->devices[bus->device_count].addr    = addr;
    bus->devices[bus->device_count].freq_hz = freq_hz;
    bus->devices[bus->device_count].handle  = dev_handle;
    bus->device_count++;
    ESP_LOGI(TAG, "Device 0x%02X pre-registered at %luHz", addr, (unsigned long)freq_hz);
    return 0;
}
