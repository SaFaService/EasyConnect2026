#include "dc_settings.h"
#include "dc_data_model.h"
#include <Preferences.h>
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include "display_port/io_extension.h"

static Preferences s_pref_disp;
static Preferences s_pref_easy;
static Preferences s_pref_sys;
static bool s_loaded          = false;
static bool s_pref_disp_ready = false;
static bool s_pref_easy_ready = false;
static bool s_pref_sys_ready  = false;
static char s_system_pin_hash[65] = "";

// ─── Sanitize ────────────────────────────────────────────────────────────────

static int _san_brightness(int pct)  { return constrain(pct, 5, 100); }
static int _san_screensaver(int min) {
    if (min == 3 || min == 5 || min == 10 || min == 15) return min;
    return 5;
}
static DcTempUnit _san_temp_unit(int raw) {
    return (raw == (int)DC_TEMP_F) ? DC_TEMP_F : DC_TEMP_C;
}
static void _san_plant_name(const char* raw, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    String v = raw ? String(raw) : String("");
    v.trim();
    if (v.length() == 0) v = "Il mio Impianto";
    size_t len = v.length();
    if (len >= out_size) len = out_size - 1;
    memcpy(out, v.c_str(), len);
    out[len] = '\0';
}
static int _san_vent_min(int pct)    { return constrain(pct, 0, 90); }
static int _san_vent_max(int pct)    { return constrain(pct, 10, 100); }
static int _san_vent_steps(int s) {
    switch (s) { case 0: case 2: case 3: case 5: case 7: case 10: return s; default: return 0; }
}
static int _san_intake_diff(int pct) { return constrain(pct, 25, 90); }
static int _san_sg_temp(int t)       { return constrain(t, 30, 120); }
static int _san_sg_hum(int h)        { return constrain(h, 40, 100); }

static bool _is_pin6_valid(const char* pin6) {
    if (!pin6) return false;
    for (int i = 0; i < 6; i++) {
        const char c = pin6[i];
        if (c < '0' || c > '9') return false;
    }
    return pin6[6] == '\0';
}

static bool _sha256_hex(const char* input, char* out, size_t out_size) {
    if (!input || !out || out_size < 65) return false;

    uint8_t digest[32] = {};
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    const int start_rc = mbedtls_sha256_starts(&ctx, 0);
    const int update_rc = (start_rc == 0)
                        ? mbedtls_sha256_update(&ctx,
                                                reinterpret_cast<const unsigned char*>(input),
                                                strlen(input))
                        : -1;
    const int finish_rc = (update_rc == 0) ? mbedtls_sha256_finish(&ctx, digest) : -1;
    mbedtls_sha256_free(&ctx);

    if (start_rc != 0 || update_rc != 0 || finish_rc != 0) {
        out[0] = '\0';
        return false;
    }

    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(out + (i * 2), out_size - (i * 2), "%02x", digest[i]);
    }
    out[64] = '\0';
    return true;
}

static void _load_system_pin_hash_cache(void) {
    s_system_pin_hash[0] = '\0';
    if (!s_pref_sys_ready) return;

    const String hash = s_pref_sys.getString("sys_pin_hash", "");
    if (hash.length() != 64) return;

    hash.toCharArray(s_system_pin_hash, sizeof(s_system_pin_hash));
}

// Active-low backlight: 100% UI brightness → 0% PWM duty
static uint8_t _pct_to_hw(int ui_pct) {
    return (uint8_t)(100 - constrain(ui_pct, 5, 100));
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void dc_settings_load(void) {
    if (s_loaded) return;

    // RAM defaults first — model is always coherent even when NVS fails
    DcSettings& s = g_dc_model.settings;
    s.brightness_pct       = 80;
    s.screensaver_min      = 5;
    s.temp_unit            = DC_TEMP_C;
    s.ui_theme_id          = 0;
    strncpy(s.plant_name, "Il mio Impianto", sizeof(s.plant_name) - 1);
    s.plant_name[sizeof(s.plant_name) - 1] = '\0';
    s.vent_min_pct         = 20;
    s.vent_max_pct         = 100;
    s.vent_steps           = 0;
    s.intake_bar_enabled   = true;
    s.intake_diff_pct      = 25;
    s.safeguard_enabled    = false;
    s.safeguard_temp_max_c = 75;
    s.safeguard_hum_max_rh = 85;
    s.wifi_enabled         = false;
    s.wifi_ssid[0]         = '\0';
    s.api_customer_enabled = false;
    s.api_customer_url[0]  = '\0';
    s.api_factory_enabled  = false;
    s.ota_auto_enabled     = false;
    strncpy(s.ota_channel, "stable", sizeof(s.ota_channel) - 1);
    s.ota_channel[sizeof(s.ota_channel) - 1] = '\0';
    s.plant_configured     = false;

    s_pref_disp_ready = s_pref_disp.begin("easy_disp", false);
    if (s_pref_disp_ready) {
        s.brightness_pct     = _san_brightness((int)s_pref_disp.getUChar("br_pct", 80));
        s.screensaver_min    = _san_screensaver((int)s_pref_disp.getUChar("scr_min", 5));
        s.temp_unit          = _san_temp_unit((int)s_pref_disp.getUChar("temp_u", (uint8_t)DC_TEMP_C));
        s.ui_theme_id        = s_pref_disp.getUChar("ui_theme", 0);
        _san_plant_name(s_pref_disp.getString("plant_name", "Il mio Impianto").c_str(),
                        s.plant_name, sizeof(s.plant_name));
        s.vent_min_pct       = _san_vent_min((int)s_pref_disp.getUChar("vent_min", 20));
        s.vent_max_pct       = _san_vent_max((int)s_pref_disp.getUChar("vent_max", 100));
        s.vent_steps         = _san_vent_steps((int)s_pref_disp.getUChar("vent_steps", 0));
        s.intake_bar_enabled = s_pref_disp.getBool("imm_bar", true);
        s.intake_diff_pct    = _san_intake_diff((int)s_pref_disp.getUChar("imm_pct", 25));
        if (s.vent_max_pct < s.vent_min_pct) s.vent_max_pct = s.vent_min_pct;
        s.safeguard_enabled    = s_pref_disp.getBool("sg_en", false);
        s.safeguard_temp_max_c = _san_sg_temp((int)s_pref_disp.getUChar("sg_tmax", 75));
        s.safeguard_hum_max_rh = _san_sg_hum((int)s_pref_disp.getUChar("sg_hmax", 85));
        s.plant_configured     = s_pref_disp.getBool("plant_cfgd", false);
        // Auto-detect per impianti esistenti: nome diverso dal default → configurato
        if (!s.plant_configured && strcmp(s.plant_name, "Il mio Impianto") != 0) {
            s.plant_configured = true;
            s_pref_disp.putBool("plant_cfgd", true);
        }
    } else {
        Serial.println("[dc_settings] WARNING: easy_disp NVS unavailable, using defaults");
    }

    s_pref_easy_ready = s_pref_easy.begin("easy", false);
    if (s_pref_easy_ready) {
        s.wifi_enabled = s_pref_easy.getBool("dc_wifi_enabled", false);
        s_pref_easy.getString("ssid", "").toCharArray(s.wifi_ssid, sizeof(s.wifi_ssid));
        s.api_customer_enabled = s_pref_easy.getBool("cust_en", false);
        s_pref_easy.getString("cust_url", "").toCharArray(s.api_customer_url, sizeof(s.api_customer_url));
    } else {
        Serial.println("[dc_settings] WARNING: easy NVS unavailable, using defaults");
    }

    s_pref_sys_ready = s_pref_sys.begin("easy_sys", false);
    if (s_pref_sys_ready) {
        _load_system_pin_hash_cache();
    } else {
        Serial.println("[dc_settings] WARNING: easy_sys NVS unavailable, system PIN disabled");
    }

    s_loaded = true;
}

// ─── Display ─────────────────────────────────────────────────────────────────

void dc_settings_brightness_set(int pct) {
    pct = _san_brightness(pct);
    if (g_dc_model.settings.brightness_pct == pct) return;
    g_dc_model.settings.brightness_pct = pct;
    if (s_pref_disp_ready) s_pref_disp.putUChar("br_pct", (uint8_t)pct);
}

int dc_settings_brightness_get(void) {
    return g_dc_model.settings.brightness_pct;
}

void dc_settings_brightness_apply_hw(void) {
    IO_EXTENSION_Pwm_Output(_pct_to_hw(g_dc_model.settings.brightness_pct));
}

void dc_settings_screensaver_set(int minutes) {
    minutes = _san_screensaver(minutes);
    if (g_dc_model.settings.screensaver_min == minutes) return;
    g_dc_model.settings.screensaver_min = minutes;
    if (s_pref_disp_ready) s_pref_disp.putUChar("scr_min", (uint8_t)minutes);
}

int dc_settings_screensaver_get(void) {
    return g_dc_model.settings.screensaver_min;
}

void dc_settings_temp_unit_set(DcTempUnit unit) {
    unit = _san_temp_unit((int)unit);
    if (g_dc_model.settings.temp_unit == unit) return;
    g_dc_model.settings.temp_unit = unit;
    if (s_pref_disp_ready) s_pref_disp.putUChar("temp_u", (uint8_t)unit);
}

DcTempUnit dc_settings_temp_unit_get(void) {
    return g_dc_model.settings.temp_unit;
}

void dc_settings_plant_name_set(const char* name) {
    char san[48];
    _san_plant_name(name, san, sizeof(san));
    if (strncmp(g_dc_model.settings.plant_name, san, sizeof(g_dc_model.settings.plant_name)) == 0) return;
    strncpy(g_dc_model.settings.plant_name, san, sizeof(g_dc_model.settings.plant_name) - 1);
    g_dc_model.settings.plant_name[sizeof(g_dc_model.settings.plant_name) - 1] = '\0';
    if (s_pref_disp_ready) s_pref_disp.putString("plant_name", g_dc_model.settings.plant_name);
}

void dc_settings_plant_name_get(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    strncpy(out, g_dc_model.settings.plant_name, out_size - 1);
    out[out_size - 1] = '\0';
}

void dc_settings_theme_set(uint8_t theme_id) {
    if (g_dc_model.settings.ui_theme_id == theme_id) return;
    g_dc_model.settings.ui_theme_id = theme_id;
    if (s_pref_disp_ready) s_pref_disp.putUChar("ui_theme", theme_id);
}

uint8_t dc_settings_theme_get(void) {
    return g_dc_model.settings.ui_theme_id;
}

void dc_settings_plant_configured_set(bool configured) {
    if (g_dc_model.settings.plant_configured == configured) return;
    g_dc_model.settings.plant_configured = configured;
    if (s_pref_disp_ready) s_pref_disp.putBool("plant_cfgd", configured);
}

bool dc_settings_plant_configured_get(void) {
    return g_dc_model.settings.plant_configured;
}

// ─── Ventilazione ─────────────────────────────────────────────────────────────

void dc_settings_vent_min_set(int pct) {
    pct = _san_vent_min(pct);
    if (g_dc_model.settings.vent_min_pct == pct) return;
    g_dc_model.settings.vent_min_pct = pct;
    if (g_dc_model.settings.vent_max_pct < pct) {
        g_dc_model.settings.vent_max_pct = pct;
        if (s_pref_disp_ready) s_pref_disp.putUChar("vent_max", (uint8_t)pct);
    }
    if (s_pref_disp_ready) s_pref_disp.putUChar("vent_min", (uint8_t)pct);
}

int dc_settings_vent_min_get(void) { return g_dc_model.settings.vent_min_pct; }

void dc_settings_vent_max_set(int pct) {
    pct = _san_vent_max(pct);
    if (pct < g_dc_model.settings.vent_min_pct) pct = g_dc_model.settings.vent_min_pct;
    if (g_dc_model.settings.vent_max_pct == pct) return;
    g_dc_model.settings.vent_max_pct = pct;
    if (s_pref_disp_ready) s_pref_disp.putUChar("vent_max", (uint8_t)pct);
}

int dc_settings_vent_max_get(void) { return g_dc_model.settings.vent_max_pct; }

void dc_settings_vent_steps_set(int steps) {
    steps = _san_vent_steps(steps);
    if (g_dc_model.settings.vent_steps == steps) return;
    g_dc_model.settings.vent_steps = steps;
    if (s_pref_disp_ready) s_pref_disp.putUChar("vent_steps", (uint8_t)steps);
}

int dc_settings_vent_steps_get(void) { return g_dc_model.settings.vent_steps; }

void dc_settings_intake_bar_set(bool enabled) {
    if (g_dc_model.settings.intake_bar_enabled == enabled) return;
    g_dc_model.settings.intake_bar_enabled = enabled;
    if (s_pref_disp_ready) s_pref_disp.putBool("imm_bar", enabled);
}

bool dc_settings_intake_bar_get(void) { return g_dc_model.settings.intake_bar_enabled; }

void dc_settings_intake_diff_set(int pct) {
    pct = _san_intake_diff(pct);
    if (g_dc_model.settings.intake_diff_pct == pct) return;
    g_dc_model.settings.intake_diff_pct = pct;
    if (s_pref_disp_ready) s_pref_disp.putUChar("imm_pct", (uint8_t)pct);
}

int dc_settings_intake_diff_get(void) { return g_dc_model.settings.intake_diff_pct; }

// ─── Air safeguard ────────────────────────────────────────────────────────────

void dc_settings_safeguard_enabled_set(bool enabled) {
    if (g_dc_model.settings.safeguard_enabled == enabled) return;
    g_dc_model.settings.safeguard_enabled = enabled;
    if (s_pref_disp_ready) s_pref_disp.putBool("sg_en", enabled);
}

bool dc_settings_safeguard_enabled_get(void) { return g_dc_model.settings.safeguard_enabled; }

void dc_settings_safeguard_temp_max_set(int temp_c) {
    temp_c = _san_sg_temp(temp_c);
    if (g_dc_model.settings.safeguard_temp_max_c == temp_c) return;
    g_dc_model.settings.safeguard_temp_max_c = temp_c;
    if (s_pref_disp_ready) s_pref_disp.putUChar("sg_tmax", (uint8_t)temp_c);
}

int dc_settings_safeguard_temp_max_get(void) { return g_dc_model.settings.safeguard_temp_max_c; }

void dc_settings_safeguard_hum_max_set(int hum_rh) {
    hum_rh = _san_sg_hum(hum_rh);
    if (g_dc_model.settings.safeguard_hum_max_rh == hum_rh) return;
    g_dc_model.settings.safeguard_hum_max_rh = hum_rh;
    if (s_pref_disp_ready) s_pref_disp.putUChar("sg_hmax", (uint8_t)hum_rh);
}

int dc_settings_safeguard_hum_max_get(void) { return g_dc_model.settings.safeguard_hum_max_rh; }

// ─── Rete WiFi ───────────────────────────────────────────────────────────────

void dc_settings_wifi_set(bool enabled, const char* ssid, const char* pass) {
    g_dc_model.settings.wifi_enabled = enabled;
    strncpy(g_dc_model.settings.wifi_ssid, ssid ? ssid : "", sizeof(g_dc_model.settings.wifi_ssid) - 1);
    g_dc_model.settings.wifi_ssid[sizeof(g_dc_model.settings.wifi_ssid) - 1] = '\0';
    if (s_pref_easy_ready) {
        s_pref_easy.putBool("dc_wifi_enabled", enabled);
        s_pref_easy.putString("ssid", ssid ? ssid : "");
        if (pass) {
            if (pass[0] != '\0') s_pref_easy.putString("pass", pass);
            else                 s_pref_easy.remove("pass");
        }
    }
}

bool dc_settings_wifi_enabled_get(void) { return g_dc_model.settings.wifi_enabled; }

void dc_settings_wifi_ssid_get(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    strncpy(out, g_dc_model.settings.wifi_ssid, out_size - 1);
    out[out_size - 1] = '\0';
}

// ─── API customer ─────────────────────────────────────────────────────────────

void dc_settings_api_customer_set(bool enabled, const char* url, const char* key) {
    g_dc_model.settings.api_customer_enabled = enabled;
    strncpy(g_dc_model.settings.api_customer_url, url ? url : "", sizeof(g_dc_model.settings.api_customer_url) - 1);
    g_dc_model.settings.api_customer_url[sizeof(g_dc_model.settings.api_customer_url) - 1] = '\0';
    if (s_pref_easy_ready) {
        s_pref_easy.putBool("cust_en", enabled);
        s_pref_easy.putString("cust_url", url ? url : "");
        if (key) {
            if (key[0] != '\0') s_pref_easy.putString("cust_key", key);
            else                s_pref_easy.remove("cust_key");
        }
    }
}

bool dc_settings_api_customer_enabled_get(void) { return g_dc_model.settings.api_customer_enabled; }

void dc_settings_api_customer_url_get(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    strncpy(out, g_dc_model.settings.api_customer_url, out_size - 1);
    out[out_size - 1] = '\0';
}

bool dc_settings_system_pin_is_set(void) {
    return s_system_pin_hash[0] != '\0';
}

bool dc_settings_system_pin_verify(const char* pin6) {
    if (!_is_pin6_valid(pin6) || s_system_pin_hash[0] == '\0') return false;

    char hash[65];
    if (!_sha256_hex(pin6, hash, sizeof(hash))) return false;
    return strcmp(hash, s_system_pin_hash) == 0;
}

bool dc_settings_system_pin_set(const char* pin6) {
    if (!_is_pin6_valid(pin6) || !s_pref_sys_ready) return false;

    char hash[65];
    if (!_sha256_hex(pin6, hash, sizeof(hash))) return false;

    if (!s_pref_sys.putString("sys_pin_hash", hash)) return false;

    strncpy(s_system_pin_hash, hash, sizeof(s_system_pin_hash) - 1);
    s_system_pin_hash[sizeof(s_system_pin_hash) - 1] = '\0';
    return true;
}
