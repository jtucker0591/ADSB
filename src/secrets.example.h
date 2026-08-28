#pragma once
// Copy this file to secrets.h (same folder) and fill in your real values.
// secrets.h is listed in .gitignore and will never be committed — this
// example file is what ships in the repo so the shape is documented.
//
// This is everything specific to YOU and YOUR board(s): WiFi credentials
// and every location (home + the two cycle-through presets). None of it
// is needed for the shared/public OTA firmware to build or run generically
// — a release build compiles with placeholder values here, and each board
// keeps its real identity in its own flash (NVS), seeded once on first
// boot. See README.md "Secrets & multi-board setup" for the full story.

// WiFi credentials — tried in order until one connects
#define WIFI_SSID1 "YourWiFiName"
#define WIFI_PASS1 "YourWiFiPassword"
#define WIFI_SSID2 ""
#define WIFI_PASS2 ""
#define WIFI_SSID3 ""
#define WIFI_PASS3 ""

// Home location — this board's primary tracking center
#define HOME_LAT 0.0
#define HOME_LON 0.0

// API query radius in nautical miles. This device has no PSRAM, so near
// busy airspace (a major airport, a big city) a wide radius can return
// more aircraft data than it can reliably parse -- shows up as a
// "NoMemory" error in Serial and a splash screen that never advances.
// Start smaller (15-20) near heavy traffic; a rural spot can usually
// handle 50 fine.
#define ADSB_RADIUS_NM 30

// Splash screen label for this board
#define LOCATION_NAME "My Location"

// Location presets — LOC1 should match HOME_LAT/HOME_LON above.
// LOC2/LOC3 are optional alternates you can cycle to on-device (long-press
// center in Radar view). Leave a slot's NAME empty ("") to disable it and
// lower NUM_LOCATIONS to match.
//
// Like WiFi/home location, all of LOC1-3 are seeded into this board's NVS
// flash the first time it boots firmware built with this secrets.h, and
// from then on live there — not in whatever's compiled into a later shared
// OTA build (which always uses placeholder values, see release.yml). To
// change a board's presets after that first boot, you'd need to either
// change them on-device (no settings-screen UI for that yet) or erase NVS
// and reflash over USB with the updated secrets.h.
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
