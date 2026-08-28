#pragma once
#include <cstdint>

// ---- Fatal-condition watchdog ----
// fetch_task and the OTA updater report signals here. main.cpp's loop()
// polls health_check() every iteration and, once tripped, takes over the
// screen with a hard "ERROR - RESTART NOW" instead of leaving the board
// silently frozen with no indication anything is wrong.
//
// This works even when fetch_task is stuck inside a blocking call (like a
// hung OTA download) because loop() runs as its own FreeRTOS task and keeps
// executing regardless -- it isn't blocked by fetch_task being stuck.

// Consecutive fetch failures (HTTP or JSON, any cause) before the board is
// treated as genuinely stuck rather than having a transient hiccup.
#define HEALTH_MAX_CONSECUTIVE_FAILS 8

// How many consecutive good fetches clears the auto-restart streak (see
// below), so one old failure doesn't count against the board forever.
#define HEALTH_CONSECUTIVE_OK_TO_CLEAR_STREAK 12

// OTA-stuck detection is stall-based (time since the LAST progress tick),
// not a single flat deadline from when the update started -- a real
// firmware download+flash (~1.2MB over WiFi/TLS) can legitimately take a
// couple minutes, and a flat timeout doesn't distinguish "slow but still
// moving" from "actually stuck". Hit this exact problem on v2.4: an update
// that was still progressing normally tripped a 3-minute flat deadline and
// put up the fatal-error screen mid-download -- which risks someone seeing
// "ERROR - RESTART NOW" and unplugging the board while a flash write is
// in progress. Same fix as the stream reader in fetcher.cpp: no progress
// for HEALTH_OTA_STALL_TIMEOUT_MS means genuinely stuck;
// HEALTH_OTA_ABSOLUTE_TIMEOUT_MS is still there as a backstop so a
// pathological trickle of tiny progress ticks can't stall this forever.
#define HEALTH_OTA_STALL_TIMEOUT_MS (60UL * 1000)
#define HEALTH_OTA_ABSOLUTE_TIMEOUT_MS (8UL * 60 * 1000)

// How long the error screen stays up before auto-restarting.
#define HEALTH_AUTO_RESTART_DELAY_MS (5UL * 60 * 1000)

// How many auto-restarts in a row (persisted in NVS, survives the reboot)
// are allowed before giving up on auto-restart and requiring a manual
// restart instead -- so a persistent bug can't loop forever invisibly on
// an unattended board.
#define HEALTH_MAX_AUTO_RESTARTS 3

// Call once, early in setup() (main/UI task only).
void health_init();

// fetch_task calls this after every fetch attempt, success or failure.
void health_report_fetch_result(bool ok);

// ota.cpp calls these immediately around the blocking httpUpdate.update()
// call (also from fetch_task).
void health_report_ota_start();
void health_report_ota_end();

// ota.cpp's HTTPUpdate progress callback calls this on every tick (also
// from fetch_task) -- resets the stall clock so a slow-but-moving download
// never gets mistaken for a stuck one.
void health_report_ota_progress();

// main.cpp's loop() calls this every iteration. This is the only function
// that touches NVS, so all Preferences access stays on the main task.
// Returns true once a fatal condition has latched for this boot; while
// true, also handles the auto-restart timing/cap.
bool health_check();

// Short reason string for the error screen. Valid once health_check()
// first returns true.
const char *health_fatal_reason();

// Seconds remaining before auto-restart fires, or -1 if auto-restart has
// been exhausted for this streak and only a manual restart will do.
int32_t health_seconds_until_restart();
