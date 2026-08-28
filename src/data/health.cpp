#include "health.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>

#define HEALTH_PREFS_NS "health"

enum FatalCode { FATAL_NONE = 0, FATAL_MEMORY = 1, FATAL_OTA_STUCK = 2 };

// Written by fetch_task, read by the main task in health_check() --
// plain int/bool reads and writes are atomic on this architecture, so
// this single-writer/single-reader pattern is safe without a mutex (same
// approach fetcher.cpp already uses for _active_net).
static volatile int _consecutive_fails = 0;
static volatile int _consecutive_ok = 0;
static volatile bool _ota_in_progress = false;
static volatile uint32_t _ota_start_ms = 0;
static volatile uint32_t _ota_last_progress_ms = 0;
static volatile int _fatal_code = FATAL_NONE;

// Main-task-only state (all NVS access happens here, never from fetch_task,
// to avoid two tasks touching a Preferences/NVS transaction at once).
static uint32_t _fatal_since_ms = 0;
static int _restart_count = 0;
static bool _latched = false;

void health_init() {
    Preferences prefs;
    prefs.begin(HEALTH_PREFS_NS, false);
    _restart_count = prefs.getInt("restarts", 0);
    prefs.end();
}

void health_report_fetch_result(bool ok) {
    if (_fatal_code != FATAL_NONE) return;
    if (ok) {
        _consecutive_fails = 0;
        _consecutive_ok++;
    } else {
        _consecutive_ok = 0;
        _consecutive_fails++;
        if (_consecutive_fails >= HEALTH_MAX_CONSECUTIVE_FAILS) {
            _fatal_code = FATAL_MEMORY;
        }
    }
}

void health_report_ota_start() {
    _ota_start_ms = millis();
    _ota_last_progress_ms = _ota_start_ms;
    _ota_in_progress = true;
}

void health_report_ota_end() {
    _ota_in_progress = false;
}

void health_report_ota_progress() {
    _ota_last_progress_ms = millis();
}

bool health_check() {
    if (_fatal_code == FATAL_NONE && _ota_in_progress) {
        uint32_t now = millis();
        bool stalled = (now - _ota_last_progress_ms) > HEALTH_OTA_STALL_TIMEOUT_MS;
        bool ran_too_long = (now - _ota_start_ms) > HEALTH_OTA_ABSOLUTE_TIMEOUT_MS;
        if (stalled || ran_too_long) {
            _fatal_code = FATAL_OTA_STUCK;
        }
    }

    // Board has been running cleanly for a while -- clear the persisted
    // auto-restart streak so a single old failure doesn't count against it
    // indefinitely.
    if (_fatal_code == FATAL_NONE && _restart_count != 0 &&
        _consecutive_ok >= HEALTH_CONSECUTIVE_OK_TO_CLEAR_STREAK) {
        _restart_count = 0;
        Preferences prefs;
        prefs.begin(HEALTH_PREFS_NS, false);
        prefs.putInt("restarts", 0);
        prefs.end();
    }

    if (_fatal_code == FATAL_NONE) return false;

    if (!_latched) {
        _latched = true;
        _fatal_since_ms = millis();
        _restart_count++;
        Preferences prefs;
        prefs.begin(HEALTH_PREFS_NS, false);
        prefs.putInt("restarts", _restart_count);
        prefs.end();
    }

    if (_restart_count <= HEALTH_MAX_AUTO_RESTARTS &&
        (millis() - _fatal_since_ms) >= HEALTH_AUTO_RESTART_DELAY_MS) {
        delay(200);
        esp_restart();
    }

    return true;
}

const char *health_fatal_reason() {
    switch (_fatal_code) {
        case FATAL_MEMORY:    return "OUT OF MEMORY";
        case FATAL_OTA_STUCK: return "UPDATE STUCK";
        default:               return "";
    }
}

int32_t health_seconds_until_restart() {
    if (_fatal_code == FATAL_NONE) return -1;
    if (_restart_count > HEALTH_MAX_AUTO_RESTARTS) return -1;
    if (!_latched) return (int32_t)(HEALTH_AUTO_RESTART_DELAY_MS / 1000);
    uint32_t elapsed = millis() - _fatal_since_ms;
    if (elapsed >= HEALTH_AUTO_RESTART_DELAY_MS) return 0;
    return (int32_t)((HEALTH_AUTO_RESTART_DELAY_MS - elapsed) / 1000);
}
