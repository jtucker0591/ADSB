#pragma once
// LEGACY (pre-2.0): this file used to be copied over config.h to build
// this board's firmware directly, back when each site was a fully separate
// compile. As of 2.0, real secrets live in a local, gitignored secrets.h
// on whichever machine builds for this board (see secrets.example.h), and
// this board's identity (WiFi + location) was seeded into its own on-device
// flash on first 2.0 boot, so it's no longer touched by shared OTA updates
// going forward. Real values previously here have been redacted before
// this repo went public — kept only as a historical record of what this
// board's settings were.

// WiFi credentials — see local secrets.h (not committed)
#define WIFI_SSID1 "REDACTED"
#define WIFI_PASS1 "REDACTED"
#define WIFI_SSID2 ""
#define WIFI_PASS2 ""
#define WIFI_SSID3 ""
#define WIFI_PASS3 ""

// Home location — see local secrets.h (not committed)
#define HOME_LAT 0.0
#define HOME_LON 0.0

// ADS-B settings — reduced for CYD (no PSRAM, 320KB DRAM)
#define ADSB_RADIUS_NM 20
#define ADSB_POLL_INTERVAL_MS 5000
#define MAX_AIRCRAFT 80
#define TRAIL_LENGTH 5

// CYD display
#define LCD_H_RES 320
#define LCD_V_RES 240

// CYD touch calibration (landscape rotation 1)
#define TOUCH_X_MIN 2600
#define TOUCH_X_MAX 1100
#define TOUCH_Y_MIN 3950
#define TOUCH_Y_MAX 280

// Loading Page
#define VERSION "v2.0"
#define BUILD_DATE "08/24/2026"
#define LOCATION_NAME "Hillsborough, NC"

// Location presets — see local secrets.h (not committed)
#define LOC1_NAME "Hillsborough"
#define LOC1_ABBR "HSB"
#define LOC1_LAT 0.0
#define LOC1_LON 0.0

#define LOC2_NAME "RDU"
#define LOC2_ABBR "RDU"
#define LOC2_LAT 35.8801
#define LOC2_LON -78.7880

#define LOC3_NAME ""
#define LOC3_ABBR ""
#define LOC3_LAT 0.0
#define LOC3_LON 0.0

#define NUM_LOCATIONS 2