// profiles.h - WII2BLE App Profiles

#ifndef WII2BLE_PROFILES_H
#define WII2BLE_PROFILES_H

#include "core/services/profiles/profile.h"

static const profile_t wii2ble_profiles[] = {
    {
        .name = "default",
        .description = "Standard passthrough",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

static const profile_set_t wii2ble_profile_set = {
    .profiles = wii2ble_profiles,
    .profile_count = sizeof(wii2ble_profiles) / sizeof(wii2ble_profiles[0]),
    .default_index = 0,
};

#endif // WII2BLE_PROFILES_H
