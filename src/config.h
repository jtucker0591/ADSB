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
#define ADSB_RADIUS_NM 20
#endif

// ADS-B settings — reduced for CYD (no PSRAM, 320KB DRAM). ADSB_RADIUS_NM
// is set in secrets.h (or above, if secrets.h is missing) since it's
// board-specific: a busy-airspace board near a major airport needs a
// smaller radius than a rural one, or the JSON response can be too big
// for this device's limited memory to parse (shows up as a "NoMemory"
// error in Serial and a splash screen that never advances).
#define ADSB_POLL_INTERVAL_MS 5000
// 50 is plenty of headroom for real traffic while keeping the static
// aircraft-list allocation smaller. When more aircraft are in range than
// this, the tracker keeps the CLOSEST ones (see fetcher.cpp) rather than
// whichever showed up first in the API response.
#define MAX_AIRCRAFT 50
// Trails (the fading line behind each aircraft) are a cosmetic extra --
// kept minimal here to free memory for parsing. The heading arrow shown on
// every aircraft is computed independently from live heading data, not
// from this trail history, so it's unaffected by this value. Trails can
// still be toggled on/off at runtime from the Settings screen.
#define TRAIL_LENGTH 2

// CYD display
#define LCD_H_RES 320
#define LCD_V_RES 240

// CYD touch calibration (landscape rotation 1)
#define TOUCH_X_MIN 2600
#define TOUCH_X_MAX 1100
#define TOUCH_Y_MIN 3950
#define TOUCH_Y_MAX 280

// Loading Page
#define VERSION "v2.5"
#define BUILD_DATE "08/28/2026"

// ---- OTA updates ----
// Public repo — no auth needed. Boards check GitHub's "latest release" API
// once a day and self-update via HTTPUpdate if the release tag differs
// from VERSION above. See README.md "Publishing an OTA update".
//
#define OTA_ENABLED 1
#define OTA_GITHUB_OWNER "jtucker0591"
#define OTA_GITHUB_REPO  "ADSB"
#define OTA_CHECK_INTERVAL_MS (24UL * 60 * 60 * 1000)  // once per day
