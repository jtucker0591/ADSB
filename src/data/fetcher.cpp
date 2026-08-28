#include "fetcher.h"
#include "error_log.h"
#include "health.h"
#include "http_mutex.h"
#include "ota.h"
#include "../config.h"
#include "../data/storage.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cmath>

static volatile NetType _active_net = NET_NONE;

static AircraftList *_aircraft_list = nullptr;
static uint32_t _last_update = 0;
static TaskHandle_t _fetch_task_handle = nullptr;
static TaskHandle_t _route_task_handle = nullptr;
static FetcherStats _fstats = {};

// Military alert dedup
#define ALERTED_MAX 64
static char _alerted_hexes[ALERTED_MAX][7];
static int _alerted_count = 0;
static int _alerted_write = 0;

static bool already_alerted(const char *hex) {
    for (int i = 0; i < _alerted_count; i++) {
        if (strcmp(_alerted_hexes[i], hex) == 0) return true;
    }
    return false;
}

static void mark_alerted(const char *hex) {
    strlcpy(_alerted_hexes[_alerted_write], hex, 7);
    _alerted_write = (_alerted_write + 1) % ALERTED_MAX;
    if (_alerted_count < ALERTED_MAX) _alerted_count++;
}

static bool check_military(const char *hex) {
    uint32_t h = strtoul(hex, nullptr, 16);
    if (h >= 0xADF7C8 && h <= 0xAFFFFF) return true;
    if (h >= 0xA00001 && h <= 0xA00FFF) return true;
    if (h >= 0x43C000 && h <= 0x43CFFF) return true;
    if (h >= 0x3B0000 && h <= 0x3BFFFF) return true;
    if (h >= 0x3F4000 && h <= 0x3F7FFF) return true;
    if (h >= 0xC0CDF9 && h <= 0xC0FFFF) return true;
    if (h >= 0x7C8000 && h <= 0x7CBFFF) return true;
    if (h >= 0x0A4000 && h <= 0x0A4FFF) return true;
    return false;
}

static bool check_emergency(uint16_t squawk) {
    return squawk == 7500 || squawk == 7600 || squawk == 7700;
}

static int find_aircraft(const char *hex) {
    for (int i = 0; i < _aircraft_list->count; i++) {
        if (strcmp(_aircraft_list->aircraft[i].icao_hex, hex) == 0)
            return i;
    }
    return -1;
}

// Simple planar (equirectangular) approximation -- accurate enough at the
// tens-of-nm ranges this tracker cares about, and cheap. Uses the CURRENT
// runtime tracking center (g_config), not the compile-time HOME_LAT/LON, so
// it stays correct if the board is cycled to a different location preset.
static float fetch_distance_nm(float lat, float lon) {
    float dlat = lat - g_config.home_lat;
    float dlon = lon - g_config.home_lon;
    float cos_lat = cosf(g_config.home_lat * (float)M_PI / 180.0f);
    float nm_north = dlat * 60.0f;
    float nm_east = dlon * 60.0f * cos_lat;
    return sqrtf(nm_north * nm_north + nm_east * nm_east);
}

struct ParsedEntry {
    char hex[7];
    char callsign[9];
    char registration[9];
    char type_code[5];
    char category[3];
    char desc[40];
    char owner_op[32];
    float lat, lon;
    int32_t altitude;
    int16_t speed, heading, vert_rate;
    uint16_t squawk;
    bool on_ground;
    float mach;
    int16_t ias, tas;
};

static void apply_parsed(Aircraft &a, const ParsedEntry &p, bool is_new) {
    strlcpy(a.icao_hex, p.hex, sizeof(a.icao_hex));
    strlcpy(a.callsign, p.callsign, sizeof(a.callsign));
    strlcpy(a.registration, p.registration, sizeof(a.registration));
    strlcpy(a.type_code, p.type_code, sizeof(a.type_code));
    strlcpy(a.category, p.category, sizeof(a.category));
    strlcpy(a.desc, p.desc, sizeof(a.desc));
    strlcpy(a.owner_op, p.owner_op, sizeof(a.owner_op));
    a.lat = p.lat;
    a.lon = p.lon;
    a.altitude = p.altitude;
    a.speed = p.speed;
    a.heading = p.heading;
    a.vert_rate = p.vert_rate;
    a.squawk = p.squawk;
    a.on_ground = p.on_ground;
    a.mach = p.mach;
    a.ias = p.ias;
    a.tas = p.tas;
    a.is_military = check_military(a.icao_hex);
    a.is_emergency = check_emergency(a.squawk);
    a.is_watched = false;
    a.last_seen = millis();
    a.stale_since = 0;

    if (is_new) a.trail_count = 0;

    if (a.lat != 0.0f || a.lon != 0.0f) {
        if (a.trail_count < TRAIL_LENGTH) {
            a.trail[a.trail_count] = {a.lat, a.lon, a.altitude, a.last_seen};
            a.trail_count++;
        } else {
            memmove(&a.trail[0], &a.trail[1], (TRAIL_LENGTH - 1) * sizeof(TrailPoint));
            a.trail[TRAIL_LENGTH - 1] = {a.lat, a.lon, a.altitude, a.last_seen};
        }
    }
}

static void parse_aircraft_json(JsonDocument &doc) {
    JsonArray ac = doc["ac"].as<JsonArray>();
    Serial.printf("parse: ac array size=%d\n", ac.size());

    static ParsedEntry parsed[MAX_AIRCRAFT];
    static float parsed_dist[MAX_AIRCRAFT];
    int parsed_count = 0;

    // Keep the MAX_AIRCRAFT CLOSEST aircraft from this fetch, not just the
    // first MAX_AIRCRAFT in whatever order the API returned them. Once the
    // buffer is full, a new candidate only gets in by bumping out whichever
    // kept aircraft is currently farthest away -- so when there's more
    // traffic in range than we have room for, we consistently keep the
    // closest ones.
    for (JsonObject obj : ac) {
        float lat = obj["lat"] | 0.0f;
        float lon = obj["lon"] | 0.0f;
        if (lat == 0.0f && lon == 0.0f) continue;

        float d = fetch_distance_nm(lat, lon);

        int target_idx;
        if (parsed_count < MAX_AIRCRAFT) {
            target_idx = parsed_count++;
        } else {
            int farthest = 0;
            for (int i = 1; i < MAX_AIRCRAFT; i++)
                if (parsed_dist[i] > parsed_dist[farthest]) farthest = i;
            if (d >= parsed_dist[farthest]) continue; // farther than everything we're keeping
            target_idx = farthest;
        }

        ParsedEntry &p = parsed[target_idx];
        parsed_dist[target_idx] = d;
        strlcpy(p.hex, obj["hex"] | "", sizeof(p.hex));
        strlcpy(p.callsign, obj["flight"] | "", sizeof(p.callsign));
        for (int i = strlen(p.callsign) - 1; i >= 0 && p.callsign[i] == ' '; i--)
            p.callsign[i] = '\0';
        strlcpy(p.registration, obj["r"] | "", sizeof(p.registration));
        strlcpy(p.type_code, obj["t"] | "", sizeof(p.type_code));
        strlcpy(p.category, obj["category"] | "", sizeof(p.category));
        strlcpy(p.desc, obj["desc"] | "", sizeof(p.desc));
        strlcpy(p.owner_op, obj["ownOp"] | "", sizeof(p.owner_op));
        p.lat = lat;
        p.lon = lon;
        p.altitude = obj["alt_baro"].is<int>() ? obj["alt_baro"].as<int>() : 0;
        p.speed = (int16_t)(obj["gs"] | 0.0f);
        p.heading = (int16_t)(obj["track"] | 0.0f);
        p.vert_rate = (int16_t)(obj["baro_rate"] | 0.0f);
        p.squawk = strtoul(obj["squawk"] | "0", nullptr, 10);
        p.on_ground = obj["alt_baro"] == "ground";
        p.mach = obj["mach"] | 0.0f;
        p.ias = (int16_t)(obj["ias"] | 0.0f);
        p.tas = (int16_t)(obj["tas"] | 0.0f);
    }

    if (!_aircraft_list->aircraft || !_aircraft_list->lock()) return;

    uint32_t now = millis();
    bool seen[MAX_AIRCRAFT] = {};

    // Pass 1: update aircraft already being tracked that are present in this fetch.
    // apply_parsed() resets stale_since to 0 for these.
    for (int p = 0; p < parsed_count; p++) {
        int idx = find_aircraft(parsed[p].hex);
        if (idx >= 0) {
            apply_parsed(_aircraft_list->aircraft[idx], parsed[p], false);
            seen[idx] = true;
        }
    }

    // Any tracked aircraft NOT in this fetch just dropped off the feed — start its
    // ghost fade. Previously stale_since was only ever reset to 0 and never set, so
    // aircraft that left coverage stayed "fresh" forever, never faded on screen, and
    // permanently occupied a slot — which is why the list would jam up once it had
    // ever seen MAX_AIRCRAFT unique planes.
    for (int i = 0; i < _aircraft_list->count; i++) {
        if (!seen[i] && _aircraft_list->aircraft[i].stale_since == 0) {
            _aircraft_list->aircraft[i].stale_since = now;
        }
    }

    // Compact out aircraft that have fully faded (ghost timeout elapsed) to reclaim
    // their slots for new arrivals.
    int kept = 0;
    for (int r = 0; r < _aircraft_list->count; r++) {
        Aircraft &a = _aircraft_list->aircraft[r];
        bool expired = a.stale_since != 0 && (now - a.stale_since) >= GHOST_TIMEOUT_MS;
        if (expired) continue;
        if (kept != r) _aircraft_list->aircraft[kept] = a;
        kept++;
    }
    _aircraft_list->count = kept;

    // Pass 2: add aircraft that weren't already tracked. If the list is completely
    // full, evict the most evictable existing slot first — a long-stale aircraft, or
    // failing that the farthest-away still-fresh one — instead of silently dropping
    // new traffic for the rest of the session once MAX_AIRCRAFT has ever been
    // reached. Military/emergency aircraft are never evicted.
    for (int p = 0; p < parsed_count; p++) {
        if (find_aircraft(parsed[p].hex) >= 0) continue; // already handled in pass 1

        if (_aircraft_list->count >= MAX_AIRCRAFT) {
            int worst = -1;
            float worst_score = 0;
            for (int i = 0; i < _aircraft_list->count; i++) {
                Aircraft &cand = _aircraft_list->aircraft[i];
                if (cand.is_military || cand.is_emergency) continue; // never bump these
                // Any stale aircraft outranks any fresh one; among stale aircraft,
                // the longest-gone is evicted first. Among fresh aircraft, the
                // farthest-away one goes first, so the tracked list stays biased
                // toward the closest traffic when there's more in range than
                // MAX_AIRCRAFT slots.
                float score = (cand.stale_since != 0)
                    ? (float)(now - cand.stale_since) + 1000000.0f
                    : fetch_distance_nm(cand.lat, cand.lon);
                if (worst == -1 || score > worst_score) {
                    worst = i;
                    worst_score = score;
                }
            }
            if (worst == -1) continue; // every slot is protected — drop this new one

            for (int i = worst; i < _aircraft_list->count - 1; i++)
                _aircraft_list->aircraft[i] = _aircraft_list->aircraft[i + 1];
            _aircraft_list->count--;
        }

        int new_idx = _aircraft_list->count;
        _aircraft_list->aircraft[new_idx].clear();
        apply_parsed(_aircraft_list->aircraft[new_idx], parsed[p], true);
        _aircraft_list->count++;
    }

    _aircraft_list->unlock();
}

static bool network_connected() {
    if (WiFi.status() == WL_CONNECTED) {
        _active_net = NET_WIFI;
        return true;
    }
    _active_net = NET_NONE;
    return false;
}

static void update_ip_addr() {
    if (_active_net == NET_WIFI)
        strlcpy(_fstats.ip_addr, WiFi.localIP().toString().c_str(), sizeof(_fstats.ip_addr));
    else
        strlcpy(_fstats.ip_addr, "N/A", sizeof(_fstats.ip_addr));
}

// Wraps a WiFiClient for ArduinoJson's streaming deserializer, adding the
// same "wait for more data instead of giving up" tolerance the old manual
// buffered read had. A plain Stream reader treats a momentary stall (no
// bytes available yet, but the connection is still open and more data is on
// the way -- normal on WiFi/TLS) as end-of-input, which surfaces as an
// "IncompleteInput" parse error even though the response wasn't actually
// truncated.
//
// The timeout here is stall-based (time since the LAST byte arrived), not a
// single fixed budget for the whole download -- a busy fetch (50+ aircraft)
// can take a while to fully arrive even over a perfectly healthy connection,
// and a flat deadline punishes "slow but steady" the same as "actually
// stuck". STREAM_ABSOLUTE_TIMEOUT_MS is still there as a backstop so a
// pathological one-byte-at-a-time drip can't hang the task forever.
#define STREAM_STALL_TIMEOUT_MS 8000
#define STREAM_ABSOLUTE_TIMEOUT_MS 30000

struct WaitingStreamReader {
    WiFiClient *stream;
    uint32_t start;
    uint32_t last_progress;

    explicit WaitingStreamReader(WiFiClient *s)
        : stream(s), start(millis()), last_progress(millis()) {}

    bool timed_out() const {
        uint32_t now = millis();
        return (int32_t)(now - last_progress) >= STREAM_STALL_TIMEOUT_MS ||
               (int32_t)(now - start) >= STREAM_ABSOLUTE_TIMEOUT_MS;
    }

    int read() {
        while (!timed_out()) {
            if (stream->available() > 0) {
                last_progress = millis();
                return stream->read();
            }
            if (!stream->connected()) return -1;
            vTaskDelay(1);
        }
        return -1;
    }

    size_t readBytes(char *buffer, size_t length) {
        size_t got = 0;
        while (got < length && !timed_out()) {
            int avail = stream->available();
            if (avail > 0) {
                size_t n = stream->readBytes(buffer + got, min((size_t)avail, length - got));
                if (n > 0) {
                    got += n;
                    last_progress = millis();
                }
            } else if (!stream->connected()) {
                break;
            } else {
                vTaskDelay(1);
            }
        }
        return got;
    }
};

static void fetch_task(void *param) {
    // Wait for WiFi with retry and radio recycle
    Serial.print("Fetcher: waiting for WiFi");
    int wait_cycles = 0;
    while (!network_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
        wait_cycles++;
        // After 20s (40 cycles), recycle radio and retry
        if (wait_cycles % 40 == 0) {
            const char* ssids[] = {WIFI_SSID1, WIFI_SSID2, WIFI_SSID3};
            const char* passes[] = {WIFI_PASS1, WIFI_PASS2, WIFI_PASS3};
            int net_idx = (wait_cycles / 40) % 3;
            Serial.printf("\nWiFi retry — trying [%s] (attempt %d)\n", ssids[net_idx], wait_cycles / 40 + 1);
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
            vTaskDelay(pdMS_TO_TICKS(500));
            WiFi.mode(WIFI_STA);
            WiFi.begin(ssids[net_idx], passes[net_idx]);
        }
    }
    update_ip_addr();
    Serial.printf("\nWiFi connected, IP: %s\n", _fstats.ip_addr);

    // The filter is small and constant-size (its shape never changes), so it's
    // built ONCE here and reused every poll cycle. The parse JsonDocument
    // (doc) is deliberately NOT reused the same way -- see the comment where
    // it's declared, inside the loop below, for why.
    JsonDocument filter;
    JsonObject af = filter["ac"][0].to<JsonObject>();
    af["hex"] = true;
    af["flight"] = true;
    af["r"] = true;
    af["t"] = true;
    af["category"] = true;
    af["desc"] = true;
    af["ownOp"] = true;
    af["lat"] = true;
    af["lon"] = true;
    af["alt_baro"] = true;
    af["gs"] = true;
    af["track"] = true;
    af["baro_rate"] = true;
    af["squawk"] = true;
    af["mach"] = true;
    af["ias"] = true;
    af["tas"] = true;
    // nav_altitude_mcp / roll / nav_qnh are deliberately NOT requested --
    // they're parsed and stored but never shown anywhere in the UI, so
    // there's no reason to spend parse-tree memory on them.

while (true) {
        if (network_connected()) {
            if (http_mutex_acquire(pdMS_TO_TICKS(15000))) {
                char url[128];
                snprintf(url, sizeof(url), "https://api.airplanes.live/v2/point/%.4f/%.4f/%d",
                         g_config.home_lat, g_config.home_lon, g_config.radius_nm);
                Serial.printf("Fetching coords: %.4f, %.4f, radius=%dnm\n",
                    g_config.home_lat, g_config.home_lon, g_config.radius_nm);
                WiFiClientSecure client;
                client.setInsecure();
                client.setHandshakeTimeout(10);

                HTTPClient http;
                http.begin(client, url);
                http.addHeader("User-Agent", "Mozilla/5.0 (compatible; ADSB-CYD/1.0)");
                http.setTimeout(10000);
                uint32_t t0 = millis();
                int httpCode = http.GET();
                Serial.printf("Fetch: HTTP %d, %lums, heap=%lu, largest_free=%lu\n",
                    httpCode, (unsigned long)(millis() - t0),
                    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                    (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

                if (httpCode == HTTP_CODE_OK) {
                    _fstats.last_fetch_ms = millis() - t0;

                    // Parse directly from the HTTP stream with the filter applied,
                    // rather than buffering the full raw response into RAM first.
                    // Buffering the whole response (previously up to 64KB, or
                    // whatever Content-Length reported -- 40KB+ near RDU even at a
                    // 10nm radius) just to filter most of it straight back out was
                    // the actual problem: the TLS handshake alone already
                    // fragments this board's heap down to a ~45KB largest
                    // contiguous block, so a 40KB buffer left almost nothing for
                    // either the JSON parse itself or (if the buffer were kept
                    // around) the next cycle's TLS handshake. Streaming +
                    // filtering together means memory use scales with the fields
                    // we keep per aircraft, not with how much traffic is in range.
                    int reported_len = http.getSize();
                    if (reported_len > 0) _fstats.bytes_received += (uint32_t)reported_len;
                    WaitingStreamReader reader(http.getStreamPtr());
                    uint32_t parse_t0 = millis();

                    // doc is deliberately a fresh, block-scoped object every cycle
                    // rather than reused via .clear(): its internal pool grows to
                    // match however many distinct aircraft are in a response, and
                    // ArduinoJson's clear() resets contents WITHOUT releasing that
                    // pool capacity back to the heap. A reused doc would grow
                    // toward its peak size over a long-running, busy session and
                    // then never shrink -- permanently reserving a chunk that
                    // eventually collides with the next cycle's TLS handshake,
                    // the same failure mode the read buffer had before. Being a
                    // true stack local here means its destructor -- and its
                    // memory -- releases fully the instant this block ends.
                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, reader, DeserializationOption::Filter(filter));

                    if (!err) {
                        _fstats.fetch_ok++;
                        health_report_fetch_result(true);
                        Serial.printf("JSON OK, ac array ptr=%p, heap=%lu\n",
                            (void*)_aircraft_list->aircraft,
                            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                        parse_aircraft_json(doc);
                        _last_update = millis();
                        Serial.printf("Fetched %d ac, heap=%lu\n",
                            _aircraft_list->count,
                            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                    } else {
                        _fstats.fetch_fail++;
                        health_report_fetch_result(false);
                        error_log_add("JSON: %s", err.c_str());
                        Serial.printf("JSON error: %s, parse_ms=%lu, heap=%lu, largest_free=%lu\n",
                            err.c_str(), (unsigned long)(millis() - parse_t0),
                            (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                            (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                    }
                } else {
                    _fstats.fetch_fail++;
                    health_report_fetch_result(false);
                    error_log_add("HTTP %d", httpCode);
                }
                http.end();
                http_mutex_release();
            }
        } else {
            error_log_add("Network down");
            WiFi.reconnect();
        }

        ota_check_and_apply_if_due(); // no-op unless the check interval has elapsed

        vTaskDelay(pdMS_TO_TICKS(ADSB_POLL_INTERVAL_MS));
    }
}

static void route_enrich_task(void *param) {
    while (!network_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (true) {
        char callsign[9] = {};
        char icao_hex[7] = {};
        bool found = false;

        if (_aircraft_list->lock(pdMS_TO_TICKS(100))) {
            for (int i = 0; i < _aircraft_list->count; i++) {
                Aircraft &a = _aircraft_list->aircraft[i];
                if (a.callsign[0] && !a.origin[0] && a.stale_since == 0) {
                    strlcpy(callsign, a.callsign, sizeof(callsign));
                    strlcpy(icao_hex, a.icao_hex, sizeof(icao_hex));
                    found = true;
                    break;
                }
            }
            _aircraft_list->unlock();
        }

        if (!found || !network_connected()) {
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        char origin[5] = {};
        char dest[5] = {};

        if (http_mutex_acquire(pdMS_TO_TICKS(12000))) {
            char url[128];
            snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);
            WiFiClientSecure client;
            client.setInsecure();
            client.setHandshakeTimeout(8);
            HTTPClient http;
            http.begin(client, url);
            http.setTimeout(8000);
            int code = http.GET();

            if (code == HTTP_CODE_OK) {
                WiFiClient *stream = http.getStreamPtr();
                JsonDocument doc;
                if (!deserializeJson(doc, *stream)) {
                    JsonObject route = doc["response"]["flightroute"];
                    strlcpy(origin, route["origin"]["iata_code"] | "", sizeof(origin));
                    strlcpy(dest, route["destination"]["iata_code"] | "", sizeof(dest));
                    _fstats.enrich_ok++;
                } else {
                    _fstats.enrich_fail++;
                }
            } else {
                _fstats.enrich_fail++;
            }
            http.end();
            http_mutex_release();
        }

        if (_aircraft_list->lock(pdMS_TO_TICKS(100))) {
            int idx = find_aircraft(icao_hex);
            if (idx >= 0) {
                Aircraft &a = _aircraft_list->aircraft[idx];
                if (origin[0]) strlcpy(a.origin, origin, sizeof(a.origin));
                else strlcpy(a.origin, "-", sizeof(a.origin));
                if (dest[0]) strlcpy(a.dest, dest, sizeof(a.dest));
                else strlcpy(a.dest, "-", sizeof(a.dest));
                Serial.printf("Route: %s %s->%s\n", callsign, a.origin, a.dest);
            }
            _aircraft_list->unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void fetcher_init(AircraftList *list) {
    _aircraft_list = list;
    http_mutex_init();

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    // Start WiFi non-blocking — fetch_task handles retry/wait
    const char* ssids[] = {WIFI_SSID1, WIFI_SSID2, WIFI_SSID3};
    const char* passes[] = {WIFI_PASS1, WIFI_PASS2, WIFI_PASS3};
    Serial.printf("WiFi trying: [%s]\n", ssids[0]);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssids[0], passes[0]);

    xTaskCreatePinnedToCore(fetch_task, "adsb_fetch", 32768, nullptr, 1, &_fetch_task_handle, 1);
    xTaskCreatePinnedToCore(route_enrich_task, "route_enrich", 8192, nullptr, 0, &_route_task_handle, 1);
}

bool fetcher_wifi_connected() {
    return network_connected();
}

NetType fetcher_connection_type() {
    return _active_net;
}

uint32_t fetcher_last_update() {
    return _last_update;
}

const FetcherStats* fetcher_get_stats() {
    return &_fstats;
}
