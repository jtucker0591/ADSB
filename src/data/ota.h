#pragma once
#include <cstdint>

// ---- Boot-health / crash-loop safety net ----
// ESP32 always writes to whichever OTA partition ISN'T currently running,
// so the previous firmware stays untouched on disk through an update. This
// pairs a simple NVS boot-attempt counter with that fact: if the board
// can't get through setup() a few times in a row, we assume the last
// update was bad, point the bootloader back at the other partition, and
// restart. This is a best-effort software safety net (not the ESP-IDF
// bootloader's built-in rollback feature, which needs a Kconfig option not
// available under the stock Arduino build) -- test it deliberately once
// before trusting it in the field.

// Call once, early in setup() (right after Serial.begin). Increments the
// boot-attempt counter; if it has exceeded the crash-loop threshold,
// reverts to the other OTA partition and reboots (does not return in that
// case).
void ota_check_boot_health();

// Call once setup() has completed without crashing. Clears the
// boot-attempt counter and re-enables auto-apply if it had been paused
// after repeated failures.
void ota_confirm_boot_ok();

// True if auto-apply is currently paused after repeated boot failures.
bool ota_is_paused();

// ---- Update check/apply ----
// Call periodically from a task that already has WiFi up (e.g. the fetch
// loop). No-ops unless OTA_CHECK_INTERVAL_MS has elapsed since the last
// check, or if auto-apply is paused. Compares GitHub's latest release tag
// to VERSION; if newer, downloads and flashes it. On success this reboots
// into the new firmware and does not return.
void ota_check_and_apply_if_due();

// Millis timestamp of the last update check (0 if none yet this boot).
uint32_t ota_last_check_ms();
