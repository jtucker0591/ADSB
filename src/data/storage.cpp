#include "storage.h"
#include "../config.h"
#include <Preferences.h>
#include <cstring>

UserConfig g_config = {};
static Preferences _prefs;

UserConfig storage_load_config() {
    UserConfig cfg;

    // Compiled defaults
    strncpy(cfg.wifi_ssid, WIFI_SSID1, sizeof(cfg.wifi_ssid) - 1);
    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    strncpy(cfg.wifi_pass, WIFI_PASS1, sizeof(cfg.wifi_pass) - 1);
    cfg.wifi_pass[sizeof(cfg.wifi_pass) - 1] = '\0';
    strncpy(cfg.location_name, LOCATION_NAME, sizeof(cfg.location_name) - 1);
    cfg.location_name[sizeof(cfg.location_name) - 1] = '\0';
    strncpy(cfg.location_abbr, LOC1_ABBR, sizeof(cfg.location_abbr) - 1);
    cfg.location_abbr[sizeof(cfg.location_abbr) - 1] = '\0';
    cfg.home_lat = HOME_LAT;
    cfg.home_lon = HOME_LON;
    cfg.radius_nm = ADSB_RADIUS_NM;
    cfg.use_metric = false;
    cfg.use_ethernet = false; // WiFi by default
    cfg.watchlist_count = 0;
    cfg.alert_military = true;
    cfg.alert_emergency = true;
    cfg.alert_autofocus = true; // auto-switch to map on mil/emg alerts
    cfg.cycle_enabled = true;
    cfg.cycle_interval_s = 9999;
    cfg.cycle_inactivity_s = 60;
    cfg.trails_enabled = false;
    cfg.trail_max_points = 30;
    cfg.trail_style = 0;
    cfg.brightness = 255;
    cfg.night_mode = true;
    cfg.map_zoom_idx = 1;    // 50nm default
    cfg.radar_zoom_idx = 1;  // 20nm default
    cfg.arrivals_filter_idx = 4; // ALL default
    cfg.loc_idx = 0; // first location default

    _prefs.begin("adsb", true); // read-only

    // An EMPTY stored value is treated the same as a missing key, not
    // trusted as "the real identity is blank". A board's WiFi/location
    // should never legitimately be blank -- if NVS has an empty string
    // here, something went wrong writing it (e.g. a settings screen
    // action -- brightness, night mode, a toggle -- ran while a build with
    // no real secrets.h was in memory, and silently persisted that blanked
    // g_config back over the good saved values). Falling back to the
    // compiled default in that case, and re-saving it below, means a
    // corrupted entry self-heals the next time this board is flashed with
    // its real secrets.h, instead of staying broken until someone notices
    // and manually erases NVS.
    String nvs_ssid = _prefs.getString("ssid", "");
    String nvs_pass = _prefs.getString("pass", "");
    String nvs_loc_name = _prefs.getString("loc_name", "");
    String nvs_loc_abbr = _prefs.getString("loc_abbr", "");

    // Empty ssid or loc_name (missing OR blank) means this board doesn't
    // have a usable persisted identity yet -- once cfg below reflects the
    // best available values (NVS where non-empty, compiled default
    // otherwise), we persist it so a later shared OTA build (compiled with
    // generic placeholder secrets) can never overwrite it again.
    bool needs_seed = nvs_ssid.length() == 0 || nvs_loc_name.length() == 0;

    // Override with NVS values where they're actually non-empty
    if (nvs_ssid.length() > 0) strlcpy(cfg.wifi_ssid, nvs_ssid.c_str(), sizeof(cfg.wifi_ssid));
    if (nvs_pass.length() > 0) strlcpy(cfg.wifi_pass, nvs_pass.c_str(), sizeof(cfg.wifi_pass));
    if (nvs_loc_name.length() > 0) strlcpy(cfg.location_name, nvs_loc_name.c_str(), sizeof(cfg.location_name));
    if (nvs_loc_abbr.length() > 0) strlcpy(cfg.location_abbr, nvs_loc_abbr.c_str(), sizeof(cfg.location_abbr));
    cfg.home_lat = _prefs.getFloat("lat", cfg.home_lat);
    cfg.home_lon = _prefs.getFloat("lon", cfg.home_lon);
    cfg.radius_nm = _prefs.getInt("radius", cfg.radius_nm);
    cfg.use_metric = _prefs.getBool("metric", cfg.use_metric);
    cfg.use_ethernet = _prefs.getBool("use_eth", cfg.use_ethernet);
    cfg.alert_military = _prefs.getBool("alrt_mil", cfg.alert_military);
    cfg.alert_emergency = _prefs.getBool("alrt_emg", cfg.alert_emergency);
    cfg.alert_autofocus = _prefs.getBool("alrt_af", cfg.alert_autofocus);
    cfg.cycle_enabled = _prefs.getBool("cyc_on", cfg.cycle_enabled);
    cfg.cycle_interval_s = _prefs.getInt("cyc_int", cfg.cycle_interval_s);
    cfg.cycle_inactivity_s = _prefs.getInt("cyc_idle", cfg.cycle_inactivity_s);
    cfg.trails_enabled = _prefs.getBool("trail_on", cfg.trails_enabled);
    cfg.trail_max_points = _prefs.getInt("trail_pts", cfg.trail_max_points);
    cfg.trail_style = _prefs.getInt("trail_sty", cfg.trail_style);
    cfg.brightness = _prefs.getUChar("bright", cfg.brightness);
    cfg.night_mode = _prefs.getBool("night", cfg.night_mode);
    cfg.map_zoom_idx = _prefs.getInt("map_zoom", cfg.map_zoom_idx);
    cfg.radar_zoom_idx = _prefs.getInt("rdr_zoom", cfg.radar_zoom_idx);
    cfg.arrivals_filter_idx = _prefs.getInt("arr_filt", cfg.arrivals_filter_idx);

    _prefs.end();
    Serial.println("Storage: config loaded from NVS");

    if (needs_seed) {
        storage_save_config(cfg);
        Serial.println("Storage: saved/backfilled this board's WiFi/location identity to NVS");
    }

    return cfg;
}

void storage_save_config(const UserConfig &cfg) {
    _prefs.begin("adsb", false); // read-write

    _prefs.putString("ssid", cfg.wifi_ssid);
    _prefs.putString("pass", cfg.wifi_pass);
    _prefs.putString("loc_name", cfg.location_name);
    _prefs.putString("loc_abbr", cfg.location_abbr);
    _prefs.putFloat("lat", cfg.home_lat);
    _prefs.putFloat("lon", cfg.home_lon);
    _prefs.putInt("radius", cfg.radius_nm);
    _prefs.putBool("metric", cfg.use_metric);
    _prefs.putBool("use_eth", cfg.use_ethernet);
    _prefs.putBool("alrt_mil", cfg.alert_military);
    _prefs.putBool("alrt_emg", cfg.alert_emergency);
    _prefs.putBool("alrt_af", cfg.alert_autofocus);
    _prefs.putBool("cyc_on", cfg.cycle_enabled);
    _prefs.putInt("cyc_int", cfg.cycle_interval_s);
    _prefs.putInt("cyc_idle", cfg.cycle_inactivity_s);
    _prefs.putBool("trail_on", cfg.trails_enabled);
    _prefs.putInt("trail_pts", cfg.trail_max_points);
    _prefs.putInt("trail_sty", cfg.trail_style);
    _prefs.putUChar("bright", cfg.brightness);
    _prefs.putBool("night", cfg.night_mode);
    _prefs.putInt("map_zoom", cfg.map_zoom_idx);
    _prefs.putInt("rdr_zoom", cfg.radar_zoom_idx);
    _prefs.putInt("arr_filt", cfg.arrivals_filter_idx);

    _prefs.end();
    Serial.println("Storage: config saved to NVS");
}
