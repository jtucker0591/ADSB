#pragma once
#include <cstdint>

// One cyclable location preset (name/abbr/coordinates). LOC1-3 in
// secrets.h seed these on first boot with this feature -- see
// "Location presets" in storage.cpp for why they're persisted here at all
// instead of just read from the LOC1_NAME/LOC2_NAME/... macros directly.
#define MAX_LOCATION_PRESETS 3
struct LocationPreset {
    char name[24];
    char abbr[4];
    float lat;
    float lon;
};

struct UserConfig {
    char wifi_ssid[33];
    char wifi_pass[65];
    char location_name[32]; // e.g. "Hillsborough, NC" -- persisted for the
                             // same reason wifi_ssid/wifi_pass are: it must
                             // survive a shared OTA build compiled with a
                             // generic placeholder secrets.h.
    char location_abbr[4];  // e.g. "HSB"
    float home_lat;
    float home_lon;
    int radius_nm;
    bool use_metric;
    bool use_ethernet;       // true=Ethernet, false=WiFi (default: WiFi)
    char watchlist[10][7]; // up to 10 ICAO hex codes
    int watchlist_count;

    // View cycle settings
    bool cycle_enabled;
    int cycle_interval_s;    // seconds between auto-advance (default 30)
    int cycle_inactivity_s;  // seconds before resuming cycling after touch (default 60)

    // Alert settings
    bool alert_military;     // show popup for military aircraft
    bool alert_emergency;    // show popup for squawk 7500/7600/7700
    bool alert_autofocus;    // auto-switch to map on military/emergency alerts

    // Trail settings
    bool trails_enabled;
    int trail_max_points;    // 10-60 (default 30)
    int trail_style;         // 0=line, 1=dots

    // Display settings
    uint8_t brightness;      // PWM brightness 32-255 (default 255)
    bool night_mode;         // amber/red palette (default false)

    // Zoom indices (persisted per-view)
    int map_zoom_idx;
    int radar_zoom_idx;
    int arrivals_filter_idx; // distance filter index for arrivals

    // Location cycle presets. Seeded from LOC1_NAME/LOC1_LAT/... (secrets.h)
    // into NVS on first boot with this feature, exactly like wifi_ssid/
    // location_name above -- so a later shared/OTA build (compiled with
    // placeholder secrets) can never collapse a board back down to one
    // blank preset. locations[0] is always this board's primary/home site.
    LocationPreset locations[MAX_LOCATION_PRESETS];
    int num_locations;       // how many of locations[] are populated (1-3)
    int loc_idx;             // active location preset index (0..num_locations-1), persisted
};

// Load config from NVS. Returns defaults if not found.
UserConfig storage_load_config();

// Save config to NVS
void storage_save_config(const UserConfig &cfg);

// Global runtime config — loaded at boot, updated on settings save
extern UserConfig g_config;
