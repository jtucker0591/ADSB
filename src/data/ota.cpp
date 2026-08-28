#include "ota.h"
#include "../config.h"
#include "error_log.h"
#include "health.h"
#include "http_mutex.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <cstring>
#include <esp_ota_ops.h>

#define OTA_MAX_BOOT_ATTEMPTS 3
#define OTA_PREFS_NS "ota"

static Preferences _prefs;
static uint32_t _last_check_ms = 0;
static bool _paused = false;

void ota_check_boot_health() {
    _prefs.begin(OTA_PREFS_NS, false);
    int tries = _prefs.getInt("boot_try", 0) + 1;
    _prefs.putInt("boot_try", tries);
    _paused = _prefs.getBool("paused", false);
    _prefs.end();

    Serial.printf("OTA: boot attempt %d/%d\n", tries, OTA_MAX_BOOT_ATTEMPTS);

    if (tries > OTA_MAX_BOOT_ATTEMPTS) {
        Serial.println("OTA: repeated boot failures -- reverting to the other firmware partition");
        error_log_add("OTA: crash-loop detected, reverting firmware");

        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
        if (other && running && other != running) {
            esp_ota_set_boot_partition(other);
        }

        // Pause auto-apply either way, so if this reboot lands back on the
        // same image (e.g. no valid other partition was found), it won't
        // just try updating again and re-loop.
        _prefs.begin(OTA_PREFS_NS, false);
        _prefs.putBool("paused", true);
        _prefs.putInt("boot_try", 0);
        _prefs.end();

        delay(200);
        esp_restart();
    }
}

void ota_confirm_boot_ok() {
    _prefs.begin(OTA_PREFS_NS, false);
    _prefs.putInt("boot_try", 0);
    if (_prefs.getBool("paused", false)) {
        _prefs.putBool("paused", false);
        Serial.println("OTA: board stable, auto-update re-enabled");
    }
    _prefs.end();
    _paused = false;
}

bool ota_is_paused() { return _paused; }
uint32_t ota_last_check_ms() { return _last_check_ms; }

static bool fetch_latest_release(char *tag_out, size_t tag_len, char *asset_url_out, size_t url_len) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);

    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/%s/releases/latest",
             OTA_GITHUB_OWNER, OTA_GITHUB_REPO);
    http.begin(client, url);
    http.addHeader("User-Agent", "ADSB-CYD-OTA");
    http.setTimeout(10000);

    int code = http.GET();
    bool ok = false;

    if (code == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["tag_name"] = true;
        JsonObject a = filter["assets"][0].to<JsonObject>();
        a["name"] = true;
        a["browser_download_url"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (!err) {
            strlcpy(tag_out, doc["tag_name"] | "", tag_len);
            for (JsonObject asset : doc["assets"].as<JsonArray>()) {
                const char *name = asset["name"] | "";
                if (strstr(name, ".bin")) {
                    strlcpy(asset_url_out, asset["browser_download_url"] | "", url_len);
                    break;
                }
            }
            ok = tag_out[0] != '\0' && asset_url_out[0] != '\0';
            if (!ok) error_log_add("OTA: release found but no .bin asset attached");
        } else {
            error_log_add("OTA: manifest JSON: %s", err.c_str());
        }
    } else {
        error_log_add("OTA: check HTTP %d", code);
    }

    http.end();
    return ok;
}

void ota_check_and_apply_if_due() {
#if !OTA_ENABLED
    return;
#endif
    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();
    if (_last_check_ms != 0 && (now - _last_check_ms) < OTA_CHECK_INTERVAL_MS) return;
    _last_check_ms = now;

    if (_paused) {
        Serial.println("OTA: auto-update is paused after a prior failure, skipping check");
        return;
    }

    // Same network lock the regular ADS-B fetch and route enrichment both
    // hold for their own HTTPS calls. Without it, this function's requests
    // (the release check, then the firmware download itself) could run
    // concurrently with one of those other tasks' TLS connections -- and on
    // a board this heap-constrained, two simultaneous TLS handshakes
    // competing for the same small contiguous free block is exactly what
    // produced "start_ssl_client: -1" failures during a real OTA update.
    if (!http_mutex_acquire(pdMS_TO_TICKS(15000))) {
        Serial.println("OTA: check skipped, couldn't get the network lock in time");
        return;
    }

    char tag[24] = {};
    char asset_url[256] = {};
    if (!fetch_latest_release(tag, sizeof(tag), asset_url, sizeof(asset_url))) {
        Serial.println("OTA: check failed (network or API)");
        http_mutex_release();
        return;
    }

    Serial.printf("OTA: latest release %s (running %s)\n", tag, VERSION);
    if (strcmp(tag, VERSION) == 0) {
        http_mutex_release();
        return; // already current
    }

    Serial.printf("OTA: updating to %s\n", tag);
    error_log_add("OTA: updating to %s", tag);

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(10);
    httpUpdate.rebootOnUpdate(true);
    // GitHub's release asset URL (browser_download_url) is itself a
    // redirect to the actual file on S3 (objects.githubusercontent.com).
    // HTTPUpdate doesn't follow redirects by default, so without this it
    // sees the 302 and reports it as a failure instead of chasing it to
    // the real binary.
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    health_report_ota_start();
    t_httpUpdate_return ret = httpUpdate.update(client, asset_url);
    health_report_ota_end(); // only reached on failure -- success reboots via rebootOnUpdate(true)
    http_mutex_release(); // only reached on failure too -- success reboots before this line
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            error_log_add("OTA failed: %s", httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("OTA: server reported no update (unexpected -- tag differed)");
            break;
        case HTTP_UPDATE_OK:
            // rebootOnUpdate(true) reboots automatically on success; we
            // shouldn't actually reach here.
            break;
    }
}
