#pragma once

// ---- Secrets (WiFi + location) ----
// Real values live in secrets.h, which is gitignored and never committed.
// Copy secrets.example.h to secrets.h and fill in your own values. If
// secrets.h is missing (e.g. building a shared/release image), generic
// placeholders are used instead so the build still succeeds.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "src/secrets.h not found -- using placeholder WiFi/location. Copy src/secrets.example.h to src/secrets.h and fill in real values."
#define WIFI_SSID1 ""
#define WIFI_PASS1 ""
#define WIFI_SSID2 ""
#define WIFI_PASS2 ""
#define WIFI_SSID3 ""
#define WIFI_PASS3 ""
#define HOME_LAT 0.0
#define HOME_LON 0.0
#define LOCATION_NAME "Unconfigured"
#define LOC1_NAME "Home"
#define LOC1_ABBR "HME"
#define LOC1_LAT 0.0
#define LOC1_LON 0.0
#define LOC2_NAME ""
#define LOC2_ABBR ""
#define LOC2_LAT 0.0
#define LOC2_LON 0.0
#define LOC3_NAME ""
#define LOC3_ABBR ""
#define LOC3_LAT 0.0
#define LOC3_LON 0.0
#define NUM_LOCATIONS 1
#endif

// ADS-B settings — reduced for CYD (no PSRAM, 320KB DRAM)
#define ADSB_RADIUS_NM 50
#define ADSB_POLL_INTERVAL_MS 5000
#define MAX_AIRCRAFT 80
#define TRAIL_LENGTH 15

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

// ---- OTA updates ----
// Public repo — no auth needed. Boards check GitHub's "latest release" API
// once a day and self-update via HTTPUpdate if the release tag differs
// from VERSION above. See README.md "Publishing an OTA update".
//
#define OTA_ENABLED 1
#define OTA_GITHUB_OWNER "jtucker0591"
#define OTA_GITHUB_REPO  "ADSB"
#define OTA_CHECK_INTERVAL_MS (24UL * 60 * 60 * 1000)  // once per day
