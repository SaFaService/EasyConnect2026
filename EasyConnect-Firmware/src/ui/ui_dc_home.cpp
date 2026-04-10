/**
 * @file ui_dc_home.cpp
 * @brief Home screen Display Controller
 *
 * Layout (1024x600):
 *   +------------------------------------------------------------------+
 *   |  00:00:00   03 Apr 2026                          -- C   -- %RH  | <- header 3D 60px
 *   +------------------------------------------------------------------+
 *   |                                                                  |
 *   |          [ WiFi ]           [ Impostazioni ]                    |
 *   |                                                                  |
 *   |          [ Notifiche ]      [ Stato ]                           |
 *   |                                                                  |
 *   +------------------------------------------------------------------+
 *
 * Luminosita: IO_EXTENSION_Pwm_Output (0-255), slider 5-100 %.
 * Idle dim:   timeout configurabile (3/5/10/15 min, default 5).
 *             Porta la luminosita al 10 % e al primo tocco ripristina il valore precedente.
 */

#include "ui_dc_home.h"
#include "ui_dc_settings.h"
#include "ui_dc_network.h"
#include "ui_dc_clock.h"
#include "ui_notifications.h"
#include "rs485_network.h"
#include "icons/icons_index.h"
#include "display_port/io_extension.h"
#include <Preferences.h>
#include <Arduino.h>
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
#define HM_BG     lv_color_hex(0xEEF3F8)
#define HM_WHITE  lv_color_hex(0xFFFFFF)
#define HM_ORANGE lv_color_hex(0xE84820)
#define HM_TEXT   lv_color_hex(0x243447)
#define HM_DIM    lv_color_hex(0x7A92B0)
#define HM_SHADOW lv_color_hex(0xBBCCDD)

#define HEADER_H     60
#define IDLE_DIM_PCT 10                        // luminosita al dim (%)
#define IDLE_MIN_DEFAULT 5

static constexpr const char* k_default_plant_name = "Il mio Impianto";
static constexpr size_t k_plant_name_max_len = 48;

// ─── Stato globale luminosita (persiste tra le schermate) ────────────────────
static int         g_brightness       = 80;   // 5-100 %
static bool        g_dimmed           = false;
static int         g_saved_brightness = 80;
static lv_timer_t* g_idle_timer       = NULL;
static int         g_idle_minutes     = IDLE_MIN_DEFAULT;

static Preferences g_ui_pref;
static bool        g_ui_pref_ready    = false;
static bool        g_ui_loaded        = false;
static UiTempUnit  g_temp_unit        = UI_TEMP_C;
static int         g_ventilation_min_speed_pct = 20;
static int         g_ventilation_max_speed_pct = 100;
static int         g_ventilation_step_count = 0;

// ─── Stato orologio (locale alla schermata home) ──────────────────────────────
static lv_obj_t*   s_time_lbl    = NULL;
static lv_obj_t*   s_date_lbl    = NULL;
static lv_obj_t*   s_plant_lbl   = NULL;
static lv_timer_t* s_clock_timer = NULL;
static lv_timer_t* s_home_sync_timer = NULL;
static lv_obj_t*   s_temp_lbl    = NULL;
static lv_obj_t*   s_hum_lbl     = NULL;
static lv_obj_t*   s_notif_icon_lbl = NULL;
static lv_obj_t*   s_home_scr = NULL;
static lv_obj_t*   s_bypass_glow = NULL;
static lv_obj_t*   s_safety_popup_mask = NULL;
static lv_obj_t*   s_pressure_panel = NULL;

static bool  g_env_valid = false;
static float g_env_temp  = 0.0f;
static float g_env_hum   = 0.0f;
static lv_timer_t* s_tile_blink_timer = NULL;
static bool g_system_bypass_active = false;
static bool g_system_safety_trip_active = false;

enum class HomeTileKind : uint8_t {
    RELAY_LIGHT = 1,
    RELAY_UVC = 2,
    RELAY_ELECTRO = 3,
    RELAY_COMMAND = 5,
    AIR_INTAKE = 10,
    AIR_EXTRACTION = 11,
};

struct HomeTile {
    HomeTileKind kind;
    uint8_t group;
    bool is_relay;
    uint8_t addresses_count;
    uint8_t addresses[RS485_NET_MAX_DEVICES];
    uint16_t total_count;
    uint16_t on_count;
    const lv_img_dsc_t* icon_on;
    const lv_img_dsc_t* icon_off;
    lv_obj_t* icon_obj;
    lv_obj_t* warning_obj;
    lv_obj_t* speed_slider;
    lv_obj_t* speed_lbl;
    bool show_group_label;
    bool has_warning;
    uint16_t speed_sum;
    uint8_t speed_count;
    uint8_t speed_pct;
    char title[32];
};

static constexpr int k_max_pressure_groups = 6;
static constexpr int k_max_pressure_pairs = 6;

struct PressureGroupSummary {
    bool used;
    uint8_t group;
    uint8_t present_count;
    uint8_t online_count;
    uint8_t pressure_count;
    uint8_t temp_count;
    float pressure_sum;
    float temp_sum;
};

struct PressurePairSummary {
    uint8_t group_a;
    uint8_t group_b;
    bool has_delta_p;
    bool has_temp_a;
    bool has_temp_b;
    bool has_delta_t;
    float delta_p;
    float temp_a;
    float temp_b;
    float delta_t;
};

struct PressurePairWidgets {
    bool used;
    uint8_t group_a;
    uint8_t group_b;
    lv_obj_t* delta_p_lbl;
    lv_obj_t* temp_lbl;
    lv_obj_t* delta_t_lbl;
};

struct SavedRelayState {
    bool valid;
    uint8_t address;
    uint8_t relay_mode;
    bool desired_on;
};

static HomeTile s_home_tiles[RS485_NET_MAX_DEVICES];
static SavedRelayState s_saved_relay_states[RS485_NET_MAX_DEVICES];
static int s_saved_relay_count = 0;
static int s_home_tile_count = 0;
static int s_pressure_pair_count = 0;
static bool s_tile_blink_phase = false;
static char g_plant_name[k_plant_name_max_len] = "Il mio Impianto";
static PressurePairWidgets s_pressure_pair_widgets[k_max_pressure_pairs];

static void _refresh_env_labels();
static void _refresh_plant_name_label(bool devices_found);
static void _sync_home_tile_animation_state();
static void _sync_home_tiles_from_network_state();
static void _sync_notifications_from_network_state();
static void _refresh_notification_bell();
static void _handle_system_safety_state();
static void _refresh_pressure_panel();

// ─── Controllo luminosita ─────────────────────────────────────────────────────
static uint8_t _ui_pct_to_hw_pct(int ui_pct) {
    if (ui_pct < 5)   ui_pct = 5;
    if (ui_pct > 100) ui_pct = 100;
    // Backlight line is active-low: 100% UI brightness must produce low PWM.
    return (uint8_t)(100 - ui_pct);
}

static void _apply_brightness_hw(int ui_pct) {
    IO_EXTENSION_Pwm_Output(_ui_pct_to_hw_pct(ui_pct));
}

static uint32_t _idle_minutes_to_ms(int minutes) {
    return (uint32_t)minutes * 60UL * 1000UL;
}

static int _sanitize_idle_minutes(int minutes) {
    if (minutes == 3 || minutes == 5 || minutes == 10 || minutes == 15) {
        return minutes;
    }
    return IDLE_MIN_DEFAULT;
}

static UiTempUnit _sanitize_temp_unit(int raw) {
    return (raw == (int)UI_TEMP_F) ? UI_TEMP_F : UI_TEMP_C;
}

static int _sanitize_ventilation_min_speed(int pct) {
    return constrain(pct, 0, 90);
}

static int _sanitize_ventilation_max_speed(int pct) {
    return constrain(pct, 10, 100);
}

static int _sanitize_ventilation_step_count(int steps) {
    switch (steps) {
        case 0:
        case 2:
        case 3:
        case 5:
        case 7:
        case 10:
            return steps;
        default:
            return 0;
    }
}

static void _sanitize_plant_name(const char* raw, char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    out[0] = '\0';
    String value = raw ? String(raw) : String("");
    value.trim();
    if (value.length() == 0) value = k_default_plant_name;

    size_t write_len = value.length();
    if (write_len >= out_size) write_len = out_size - 1;
    memcpy(out, value.c_str(), write_len);
    out[write_len] = '\0';
}

static void _ensure_ui_settings_loaded() {
    if (!g_ui_pref_ready) {
        g_ui_pref_ready = g_ui_pref.begin("easy_disp", false);
    }
    if (g_ui_loaded) return;

    if (g_ui_pref_ready) {
        g_brightness = constrain((int)g_ui_pref.getUChar("br_pct", 80), 5, 100);
        g_idle_minutes = _sanitize_idle_minutes((int)g_ui_pref.getUChar("scr_min", IDLE_MIN_DEFAULT));
        g_temp_unit = _sanitize_temp_unit((int)g_ui_pref.getUChar("temp_u", (uint8_t)UI_TEMP_C));
        g_ventilation_min_speed_pct = _sanitize_ventilation_min_speed((int)g_ui_pref.getUChar("vent_min", 20));
        g_ventilation_max_speed_pct = _sanitize_ventilation_max_speed((int)g_ui_pref.getUChar("vent_max", 100));
        g_ventilation_step_count = _sanitize_ventilation_step_count((int)g_ui_pref.getUChar("vent_steps", 0));
        if (g_ventilation_max_speed_pct < g_ventilation_min_speed_pct) {
            g_ventilation_max_speed_pct = g_ventilation_min_speed_pct;
        }
        _sanitize_plant_name(g_ui_pref.getString("plant_name", k_default_plant_name).c_str(),
                             g_plant_name, sizeof(g_plant_name));
    } else {
        g_brightness = 80;
        g_idle_minutes = IDLE_MIN_DEFAULT;
        g_temp_unit = UI_TEMP_C;
        g_ventilation_min_speed_pct = 20;
        g_ventilation_max_speed_pct = 100;
        g_ventilation_step_count = 0;
        _sanitize_plant_name(k_default_plant_name, g_plant_name, sizeof(g_plant_name));
    }

    g_saved_brightness = g_brightness;
    _apply_brightness_hw(g_brightness);
    g_ui_loaded = true;
}

void ui_plant_name_set(const char* name) {
    _ensure_ui_settings_loaded();

    char sanitized[k_plant_name_max_len];
    _sanitize_plant_name(name, sanitized, sizeof(sanitized));
    if (strncmp(g_plant_name, sanitized, sizeof(g_plant_name)) == 0) return;

    strncpy(g_plant_name, sanitized, sizeof(g_plant_name) - 1);
    g_plant_name[sizeof(g_plant_name) - 1] = '\0';

    if (g_ui_pref_ready) {
        g_ui_pref.putString("plant_name", g_plant_name);
    }
}

void ui_plant_name_get(char* out, size_t out_size) {
    _ensure_ui_settings_loaded();
    if (!out || out_size == 0) return;

    strncpy(out, g_plant_name, out_size - 1);
    out[out_size - 1] = '\0';
}

void ui_ventilation_min_speed_set(int pct) {
    _ensure_ui_settings_loaded();
    const int sanitized = _sanitize_ventilation_min_speed(pct);
    if (g_ventilation_min_speed_pct == sanitized) return;

    g_ventilation_min_speed_pct = sanitized;
    if (g_ventilation_max_speed_pct < g_ventilation_min_speed_pct) {
        g_ventilation_max_speed_pct = g_ventilation_min_speed_pct;
        if (g_ui_pref_ready) g_ui_pref.putUChar("vent_max", (uint8_t)g_ventilation_max_speed_pct);
    }
    if (g_ui_pref_ready) {
        g_ui_pref.putUChar("vent_min", (uint8_t)g_ventilation_min_speed_pct);
    }
}

int ui_ventilation_min_speed_get(void) {
    _ensure_ui_settings_loaded();
    return g_ventilation_min_speed_pct;
}

void ui_ventilation_max_speed_set(int pct) {
    _ensure_ui_settings_loaded();
    int sanitized = _sanitize_ventilation_max_speed(pct);
    if (sanitized < g_ventilation_min_speed_pct) sanitized = g_ventilation_min_speed_pct;
    if (g_ventilation_max_speed_pct == sanitized) return;

    g_ventilation_max_speed_pct = sanitized;
    if (g_ui_pref_ready) {
        g_ui_pref.putUChar("vent_max", (uint8_t)g_ventilation_max_speed_pct);
    }
}

int ui_ventilation_max_speed_get(void) {
    _ensure_ui_settings_loaded();
    return g_ventilation_max_speed_pct;
}

void ui_ventilation_step_count_set(int steps) {
    _ensure_ui_settings_loaded();
    const int sanitized = _sanitize_ventilation_step_count(steps);
    if (g_ventilation_step_count == sanitized) return;

    g_ventilation_step_count = sanitized;
    if (g_ui_pref_ready) {
        g_ui_pref.putUChar("vent_steps", (uint8_t)g_ventilation_step_count);
    }
}

int ui_ventilation_step_count_get(void) {
    _ensure_ui_settings_loaded();
    return g_ventilation_step_count;
}

void ui_brightness_init(void) {
    _ensure_ui_settings_loaded();
}

void ui_brightness_set(int pct) {
    _ensure_ui_settings_loaded();
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    const int prev = g_brightness;
    g_brightness = pct;
    _apply_brightness_hw(pct);
    if (g_ui_pref_ready && prev != pct) {
        g_ui_pref.putUChar("br_pct", (uint8_t)pct);
    }
}

int ui_brightness_get(void) {
    _ensure_ui_settings_loaded();
    return g_brightness;
}

void ui_screensaver_minutes_set(int minutes) {
    _ensure_ui_settings_loaded();
    const int sanitized = _sanitize_idle_minutes(minutes);
    if (g_idle_minutes == sanitized) return;
    g_idle_minutes = sanitized;
    if (g_ui_pref_ready) {
        g_ui_pref.putUChar("scr_min", (uint8_t)g_idle_minutes);
    }
}

int ui_screensaver_minutes_get(void) {
    _ensure_ui_settings_loaded();
    return g_idle_minutes;
}

void ui_temperature_unit_set(UiTempUnit unit) {
    _ensure_ui_settings_loaded();
    const UiTempUnit sanitized = _sanitize_temp_unit((int)unit);
    if (g_temp_unit == sanitized) return;
    g_temp_unit = sanitized;
    if (g_ui_pref_ready) {
        g_ui_pref.putUChar("temp_u", (uint8_t)g_temp_unit);
    }
    _refresh_env_labels();
}

UiTempUnit ui_temperature_unit_get(void) {
    _ensure_ui_settings_loaded();
    return g_temp_unit;
}

static void _refresh_env_labels() {
    if (!s_temp_lbl || !s_hum_lbl) return;

    if (!g_env_valid) {
        lv_label_set_text(s_temp_lbl, (g_temp_unit == UI_TEMP_F) ? "-- F" : "-- C");
        lv_label_set_text(s_hum_lbl, "-- %RH");
        return;
    }

    const float temp_value = (g_temp_unit == UI_TEMP_F)
        ? ((g_env_temp * 9.0f / 5.0f) + 32.0f)
        : g_env_temp;

    const int temp_tenths = (int)(temp_value * 10.0f + (temp_value >= 0.0f ? 0.5f : -0.5f));
    const int hum_tenths = (int)(g_env_hum * 10.0f + 0.5f);

    const int temp_abs_tenths = (temp_tenths < 0) ? -temp_tenths : temp_tenths;
    const int hum_abs_tenths = (hum_tenths < 0) ? -hum_tenths : hum_tenths;

    char temp_buf[20];
    char hum_buf[20];
    lv_snprintf(temp_buf, sizeof(temp_buf), "%s%d.%d %c",
                (temp_tenths < 0) ? "-" : "",
                temp_abs_tenths / 10,
                temp_abs_tenths % 10,
                (g_temp_unit == UI_TEMP_F) ? 'F' : 'C');
    lv_snprintf(hum_buf, sizeof(hum_buf), "%d.%d %%RH",
                hum_abs_tenths / 10,
                hum_abs_tenths % 10);
    lv_label_set_text(s_temp_lbl, temp_buf);
    lv_label_set_text(s_hum_lbl, hum_buf);
}

static void _refresh_plant_name_label(bool devices_found) {
    if (!s_plant_lbl) return;

    if (!devices_found) {
        lv_obj_add_flag(s_plant_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(s_plant_lbl, g_plant_name);
    lv_obj_clear_flag(s_plant_lbl, LV_OBJ_FLAG_HIDDEN);
}

static bool _device_has_comm_issue(const Rs485Device& dev) {
    return dev.in_plant && !dev.online;
}

static bool _relay_has_safety_issue(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::RELAY) return false;
    if (dev.relay_mode != 2 && dev.relay_mode != 3) return false;
    if (!dev.online) return false;
    return !dev.relay_safety_closed;
}

static bool _relay_has_feedback_fault(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::RELAY) return false;
    if (dev.relay_mode != 2 && dev.relay_mode != 3) return false;
    if (!dev.online) return false;
    if (dev.relay_feedback_fault_latched) return true;

    String state = dev.relay_state;
    state.toUpperCase();
    return dev.relay_safety_closed &&
           !dev.relay_feedback_ok &&
           state.indexOf("FAULT") >= 0;
}

static bool _device_has_warning(const Rs485Device& dev) {
    if (!dev.data_valid) return true;
    if (_device_has_comm_issue(dev)) return true;
    if (_relay_has_safety_issue(dev)) return true;
    if (_relay_has_feedback_fault(dev)) return true;
    return false;
}

static bool _is_pressure_sensor(const Rs485Device& dev) {
    if (!dev.data_valid) return false;
    if (!dev.in_plant) return false;
    if (dev.type != Rs485DevType::SENSOR) return false;
    return dev.sensor_profile != Rs485SensorProfile::AIR_010;
}

static int _find_pressure_group(PressureGroupSummary* groups, int count, uint8_t group) {
    for (int i = 0; i < count; i++) {
        if (groups[i].used && groups[i].group == group) return i;
    }
    return -1;
}

static int _collect_pressure_groups(PressureGroupSummary* groups, int max_groups) {
    if (!groups || max_groups <= 0) return 0;
    memset(groups, 0, sizeof(PressureGroupSummary) * (size_t)max_groups);

    int count = 0;
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!_is_pressure_sensor(dev)) continue;
        if (dev.group == 0) continue;

        int idx = _find_pressure_group(groups, count, dev.group);
        if (idx < 0) {
            if (count >= max_groups) continue;
            idx = count++;
            groups[idx].used = true;
            groups[idx].group = dev.group;
        }

        PressureGroupSummary& grp = groups[idx];
        grp.present_count++;
        if (!dev.online) continue;

        grp.online_count++;
        grp.pressure_sum += dev.p;
        grp.pressure_count++;
        grp.temp_sum += dev.t;
        grp.temp_count++;
    }

    for (int i = 1; i < count; i++) {
        PressureGroupSummary key = groups[i];
        int j = i - 1;
        while (j >= 0 && groups[j].group > key.group) {
            groups[j + 1] = groups[j];
            j--;
        }
        groups[j + 1] = key;
    }
    return count;
}

static bool _pair_exists(const PressurePairSummary* pairs, int count, uint8_t group_a, uint8_t group_b) {
    for (int i = 0; i < count; i++) {
        if (pairs[i].group_a == group_a && pairs[i].group_b == group_b) return true;
    }
    return false;
}

static void _fill_pressure_pair(const PressureGroupSummary& a,
                                const PressureGroupSummary& b,
                                PressurePairSummary& out) {
    memset(&out, 0, sizeof(out));
    out.group_a = a.group;
    out.group_b = b.group;

    if (a.pressure_count > 0 && b.pressure_count > 0) {
        const float pressure_a = a.pressure_sum / (float)a.pressure_count;
        const float pressure_b = b.pressure_sum / (float)b.pressure_count;
        out.has_delta_p = true;
        out.delta_p = pressure_a - pressure_b;
    }

    if (a.temp_count > 0) {
        out.has_temp_a = true;
        out.temp_a = a.temp_sum / (float)a.temp_count;
    }
    if (b.temp_count > 0) {
        out.has_temp_b = true;
        out.temp_b = b.temp_sum / (float)b.temp_count;
    }
    if (out.has_temp_a && out.has_temp_b) {
        out.has_delta_t = true;
        out.delta_t = out.temp_a - out.temp_b;
    }
}

static int _build_pressure_pairs(PressurePairSummary* pairs, int max_pairs) {
    if (!pairs || max_pairs <= 0) return 0;
    memset(pairs, 0, sizeof(PressurePairSummary) * (size_t)max_pairs);

    PressureGroupSummary groups[k_max_pressure_groups];
    const int group_count = _collect_pressure_groups(groups, k_max_pressure_groups);
    if (group_count < 2) return 0;

    int pair_count = 0;
    for (int i = 0; i + 1 < group_count && pair_count < max_pairs; i++) {
        _fill_pressure_pair(groups[i], groups[i + 1], pairs[pair_count++]);
    }

    if (group_count >= 3 && pair_count < max_pairs) {
        const uint8_t first_group = groups[0].group;
        const uint8_t last_group = groups[group_count - 1].group;
        if (!_pair_exists(pairs, pair_count, first_group, last_group)) {
            _fill_pressure_pair(groups[0], groups[group_count - 1], pairs[pair_count++]);
        }
    }

    return pair_count;
}

static float _display_temperature_value(float temp_c) {
    return (g_temp_unit == UI_TEMP_F) ? ((temp_c * 9.0f / 5.0f) + 32.0f) : temp_c;
}

static float _display_delta_temperature_value(float delta_c) {
    return (g_temp_unit == UI_TEMP_F) ? (delta_c * 9.0f / 5.0f) : delta_c;
}

static void _format_signed_tenths(float value, char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    const bool negative = value < 0.0f;
    float abs_value = negative ? -value : value;
    long tenths = (long)(abs_value * 10.0f + 0.5f);
    const long integer_part = tenths / 10;
    const long decimal_part = tenths % 10;

    if (negative && tenths > 0) {
        lv_snprintf(out, (uint32_t)out_size, "-%ld.%ld", integer_part, decimal_part);
    } else {
        lv_snprintf(out, (uint32_t)out_size, "%ld.%ld", integer_part, decimal_part);
    }
}

static void _format_pressure_pair_title(const PressurePairSummary& pair, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    lv_snprintf(out, (uint32_t)out_size, "DeltaP G%u - G%u",
                (unsigned)pair.group_a, (unsigned)pair.group_b);
}

static void _format_pressure_pair_delta(const PressurePairSummary& pair, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!pair.has_delta_p) {
        lv_snprintf(out, (uint32_t)out_size, "DeltaP non disponibile");
        return;
    }
    char value[20];
    _format_signed_tenths(pair.delta_p, value, sizeof(value));
    lv_snprintf(out, (uint32_t)out_size, "%s Pa", value);
}

static void _format_pressure_pair_temp(const PressurePairSummary& pair, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const char unit = (g_temp_unit == UI_TEMP_F) ? 'F' : 'C';

    if (pair.has_temp_a && pair.has_temp_b) {
        char temp_a[20];
        char temp_b[20];
        _format_signed_tenths(_display_temperature_value(pair.temp_a), temp_a, sizeof(temp_a));
        _format_signed_tenths(_display_temperature_value(pair.temp_b), temp_b, sizeof(temp_b));
        lv_snprintf(out, (uint32_t)out_size, "T %u: %s%c   %u: %s%c",
                    (unsigned)pair.group_a, temp_a, unit,
                    (unsigned)pair.group_b, temp_b, unit);
        return;
    }
    if (pair.has_temp_a) {
        char temp_a[20];
        _format_signed_tenths(_display_temperature_value(pair.temp_a), temp_a, sizeof(temp_a));
        lv_snprintf(out, (uint32_t)out_size, "T G%u: %s%c",
                    (unsigned)pair.group_a, temp_a, unit);
        return;
    }
    if (pair.has_temp_b) {
        char temp_b[20];
        _format_signed_tenths(_display_temperature_value(pair.temp_b), temp_b, sizeof(temp_b));
        lv_snprintf(out, (uint32_t)out_size, "T G%u: %s%c",
                    (unsigned)pair.group_b, temp_b, unit);
        return;
    }
    lv_snprintf(out, (uint32_t)out_size, "Temperatura non disponibile");
}

static void _format_pressure_pair_delta_t(const PressurePairSummary& pair, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (!pair.has_delta_t) {
        lv_snprintf(out, (uint32_t)out_size, "DeltaT non disponibile");
        return;
    }
    char value[20];
    _format_signed_tenths(_display_delta_temperature_value(pair.delta_t), value, sizeof(value));
    lv_snprintf(out, (uint32_t)out_size, "DeltaT %s%c",
                value, (g_temp_unit == UI_TEMP_F) ? 'F' : 'C');
}

static bool _is_protected_tile_kind(HomeTileKind kind) {
    return kind == HomeTileKind::RELAY_UVC || kind == HomeTileKind::RELAY_ELECTRO;
}

static bool _is_protected_relay_mode(uint8_t relay_mode) {
    return relay_mode == 2 || relay_mode == 3;
}

static int _find_saved_relay_state(uint8_t address) {
    for (int i = 0; i < s_saved_relay_count; i++) {
        if (!s_saved_relay_states[i].valid) continue;
        if (s_saved_relay_states[i].address == address) return i;
    }
    return -1;
}

static void _upsert_saved_relay_state(uint8_t address, uint8_t relay_mode, bool desired_on) {
    if (address < 1 || address > 200) return;

    int idx = _find_saved_relay_state(address);
    if (idx < 0) {
        if (s_saved_relay_count >= RS485_NET_MAX_DEVICES) return;
        idx = s_saved_relay_count++;
    }

    SavedRelayState& entry = s_saved_relay_states[idx];
    entry.valid = true;
    entry.address = address;
    entry.relay_mode = relay_mode;
    entry.desired_on = desired_on;
}

static void _sync_saved_relay_states_from_runtime() {
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant) continue;
        if (dev.type != Rs485DevType::RELAY) continue;
        _upsert_saved_relay_state(dev.address, dev.relay_mode, dev.relay_on);
    }
}

static void _apply_saved_relay_states(bool include_protected) {
    for (int i = 0; i < s_saved_relay_count; i++) {
        const SavedRelayState& entry = s_saved_relay_states[i];
        if (!entry.valid) continue;

        const bool should_force_off = _is_protected_relay_mode(entry.relay_mode) && !include_protected;
        const char* action = should_force_off ? "OFF" : (entry.desired_on ? "ON" : "OFF");
        String raw;
        rs485_network_relay_command(entry.address, action, raw);
    }
}

static bool _has_protected_systems_in_plant(bool* has_uvc, bool* has_electro) {
    bool uvc = false;
    bool electro = false;

    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant) continue;
        if (dev.type != Rs485DevType::RELAY) continue;

        if (dev.relay_mode == 2) uvc = true;
        if (dev.relay_mode == 3) electro = true;
    }

    if (has_uvc) *has_uvc = uvc;
    if (has_electro) *has_electro = electro;
    return uvc || electro;
}

static bool _has_any_active_relay(void) {
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.online) continue;
        if (dev.type != Rs485DevType::RELAY) continue;
        if (dev.relay_on) return true;
    }
    return false;
}

static bool _has_active_protected_safety(bool* has_uvc, bool* has_electro) {
    bool uvc = false;
    bool electro = false;

    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant) continue;
        if (!_relay_has_safety_issue(dev)) continue;

        if (dev.relay_mode == 2) uvc = true;
        if (dev.relay_mode == 3) electro = true;
    }

    if (has_uvc) *has_uvc = uvc;
    if (has_electro) *has_electro = electro;
    return uvc || electro;
}

static void _format_protected_systems_text(bool has_uvc, bool has_electro,
                                           char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (has_uvc && has_electro) {
        lv_snprintf(out, (uint32_t)out_size, "UVC ed Elettrostatico");
        return;
    }
    if (has_uvc) {
        lv_snprintf(out, (uint32_t)out_size, "UVC");
        return;
    }
    if (has_electro) {
        lv_snprintf(out, (uint32_t)out_size, "Elettrostatico");
        return;
    }
    lv_snprintf(out, (uint32_t)out_size, "impianto");
}

static void _set_all_relay_tiles_off() {
    for (int i = 0; i < s_home_tile_count; i++) {
        HomeTile& tile = s_home_tiles[i];
        if (!tile.is_relay) continue;
        tile.on_count = 0;
    }
}

static void _shutdown_all_plant_relays() {
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.online) continue;
        if (dev.type != Rs485DevType::RELAY) continue;
        if (!dev.relay_on) continue;

        String raw;
        rs485_network_relay_command(dev.address, "OFF", raw);
    }
    _set_all_relay_tiles_off();
}

static void _refresh_bypass_glow() {
    if (!s_bypass_glow) return;
    if (g_system_bypass_active) lv_obj_clear_flag(s_bypass_glow, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bypass_glow, LV_OBJ_FLAG_HIDDEN);
}

static void _dismiss_safety_popup() {
    if (s_safety_popup_mask) {
        lv_obj_del(s_safety_popup_mask);
        s_safety_popup_mask = NULL;
    }
}

static void _safety_popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = lv_event_get_target(e);
    if (obj == s_safety_popup_mask) {
        s_safety_popup_mask = NULL;
    }
}

static void _safety_popup_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _dismiss_safety_popup();
}

static void _safety_popup_bypass_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    g_system_bypass_active = true;
    _apply_saved_relay_states(false);
    _refresh_bypass_glow();
    _dismiss_safety_popup();
}

static void _show_safety_popup(bool has_uvc, bool has_electro) {
    if (!s_home_scr) return;

    bool plant_has_uvc = false;
    bool plant_has_electro = false;
    char trigger_systems[48];
    char plant_systems[48];
    char body[520];
    _format_protected_systems_text(has_uvc, has_electro, trigger_systems, sizeof(trigger_systems));
    _has_protected_systems_in_plant(&plant_has_uvc, &plant_has_electro);
    _format_protected_systems_text(plant_has_uvc, plant_has_electro, plant_systems, sizeof(plant_systems));

    lv_snprintf(body, sizeof(body),
                "Il sistema ha rilevato una sicurezza aperta sulla periferica %s e ha spento l'impianto preventivamente.\n"
                "Verificare che gli sportelli di ispezione dell'impianto siano effettivamente chiusi e che non siano presenti operatori in manutenzione.\n"
                "Se si ritiene che questo sia un falso allarme, premere il tasto Bypass per avviare il motore.\n"
                "I/Il filtri/o %s rimarranno comunque disattivati preventivamente.",
                trigger_systems, plant_systems);

    _dismiss_safety_popup();

    lv_obj_t* mask = lv_obj_create(s_home_scr);
    s_safety_popup_mask = mask;
    lv_obj_add_event_cb(mask, _safety_popup_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_pos(mask, 0, 0);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(mask);
    lv_obj_set_size(card, 760, 340);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_pad_left(card, 28, 0);
    lv_obj_set_style_pad_right(card, 28, 0);
    lv_obj_set_style_pad_top(card, 24, 0);
    lv_obj_set_style_pad_bottom(card, 24, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 42, 42);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xE8EEF5), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xD8E1EC), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_radius(close_btn, 12, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, _safety_popup_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, HM_TEXT, 0);
    lv_obj_center(close_lbl);

    lv_obj_t* warn_icon = lv_img_create(card);
    lv_img_set_src(warn_icon, &warning);
    lv_img_set_zoom(warn_icon, 220);
    lv_obj_align(warn_icon, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "Sicurezza aperta rilevata");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, HM_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 78, 6);

    lv_obj_t* msg = lv_label_create(card);
    lv_label_set_text(msg, body);
    lv_obj_set_width(msg, 700);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(msg, HM_TEXT, 0);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 0, 72);

    lv_obj_t* bypass_btn = lv_btn_create(card);
    lv_obj_set_size(bypass_btn, 180, 52);
    lv_obj_align(bypass_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bypass_btn, HM_ORANGE, 0);
    lv_obj_set_style_bg_color(bypass_btn, lv_color_hex(0xB02810), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(bypass_btn, 0, 0);
    lv_obj_set_style_radius(bypass_btn, 12, 0);
    lv_obj_set_style_shadow_width(bypass_btn, 0, 0);
    lv_obj_add_event_cb(bypass_btn, _safety_popup_bypass_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* bypass_lbl = lv_label_create(bypass_btn);
    lv_label_set_text(bypass_lbl, "ByPass");
    lv_obj_set_style_text_font(bypass_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(bypass_lbl, lv_color_white(), 0);
    lv_obj_center(bypass_lbl);

    lv_obj_move_foreground(mask);
}

static const char* _tile_warning_title(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) {
        return (dev.relay_mode == 2) ? "UVC" :
               (dev.relay_mode == 3) ? "Elettrostatico" :
               "Relay";
    }
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        if (dev.group == 1) return "Aspirazione";
        if (dev.group == 2) return "Immissione";
        return "0/10V";
    }
    return "Periferica";
}

static void _format_notification_body(const Rs485Device& dev, const char* issue,
                                      char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    if (_device_has_comm_issue(dev)) {
        lv_snprintf(out, (uint32_t)out_size,
                    "Mancato rilevamento scheda IP %u, seriale %s.",
                    (unsigned)dev.address, dev.sn[0] ? dev.sn : "N/D");
        return;
    }

    if (_relay_has_safety_issue(dev)) {
        lv_snprintf(out, (uint32_t)out_size,
                    "Circuito di sicurezza attivo sulla periferica %u del gruppo %u. Uscite portate in OFF.",
                    (unsigned)dev.address, (unsigned)dev.group);
        return;
    }

    if (_relay_has_feedback_fault(dev)) {
        lv_snprintf(out, (uint32_t)out_size,
                    "Feedback non ricevuto sulla periferica %u del gruppo %u dopo i tentativi previsti.",
                    (unsigned)dev.address, (unsigned)dev.group);
        return;
    }

    lv_snprintf(out, (uint32_t)out_size, "%s", (issue && issue[0]) ? issue : "Anomalia rilevata.");
}

static void _refresh_notification_bell() {
    if (!s_notif_icon_lbl) return;

    const UiNotifSeverity severity = ui_notif_highest_severity();
    lv_color_t bell_color = lv_color_hex(0x28A745);
    if (severity == UI_NOTIF_INFO) bell_color = lv_color_hex(0xF1C40F);
    if (severity == UI_NOTIF_ALERT) bell_color = lv_color_hex(0xE74C3C);
    lv_obj_set_style_text_color(s_notif_icon_lbl, bell_color, 0);
}

void ui_dc_home_set_environment(float temp_c, float hum_rh, bool valid) {
    g_env_valid = valid;
    if (valid) {
        g_env_temp = temp_c;
        g_env_hum = hum_rh;
    }
    _refresh_env_labels();
}

// ─── Timer idle dim (ogni 2 s, non viene mai eliminato) ──────────────────────
static void _idle_cb(lv_timer_t* /*t*/) {
    _ensure_ui_settings_loaded();
    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
    const uint32_t idle_ms = _idle_minutes_to_ms(g_idle_minutes);

    if (!g_dimmed && inactive_ms > idle_ms) {
        // Safe dim: applica il dim automatico solo se il valore corrente e' >= 10 %.
        if (g_brightness >= IDLE_DIM_PCT) {
            g_saved_brightness = g_brightness;
            g_dimmed = true;
            _apply_brightness_hw(IDLE_DIM_PCT);
        }
    } else if (g_dimmed && inactive_ms < 2000UL) {
        // Touchscreen usato di nuovo: ripristina
        g_dimmed = false;
        ui_brightness_set(g_saved_brightness);
    }
}

// ─── Orologio (ogni secondo) ──────────────────────────────────────────────────
static void _clock_cb(lv_timer_t* /*t*/) {
    if (!s_time_lbl) return;
    char time_buf[16];
    char date_buf[20];
    ui_dc_clock_format_time_hms(time_buf, sizeof(time_buf));
    ui_dc_clock_format_date_home(date_buf, sizeof(date_buf));
    lv_label_set_text(s_time_lbl, time_buf);
    if (s_date_lbl) {
        lv_label_set_text(s_date_lbl, date_buf);
    }
}

// ─── Pulizia quando la home viene distrutta ───────────────────────────────────
static void _on_home_delete(lv_event_t* /*e*/) {
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    if (s_tile_blink_timer) {
        lv_timer_del(s_tile_blink_timer);
        s_tile_blink_timer = NULL;
    }
    s_home_tile_count = 0;
    s_tile_blink_phase = false;
    s_time_lbl = NULL;
    s_date_lbl = NULL;
    s_plant_lbl = NULL;
    s_notif_icon_lbl = NULL;
    s_temp_lbl = NULL;
    s_hum_lbl = NULL;
    s_home_scr = NULL;
    s_bypass_glow = NULL;
    s_safety_popup_mask = NULL;
    s_pressure_panel = NULL;
    s_pressure_pair_count = 0;
    memset(s_pressure_pair_widgets, 0, sizeof(s_pressure_pair_widgets));
    if (s_home_sync_timer) {
        lv_timer_del(s_home_sync_timer);
        s_home_sync_timer = NULL;
    }
    // g_idle_timer NON viene eliminato: deve continuare su tutte le schermate
}

// ─── Navigazione verso Impostazioni ──────────────────────────────────────────
static void _open_settings_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* settings = ui_dc_settings_create();
    lv_scr_load_anim(settings, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

// ─── Navigazione verso lista dispositivi RS485 ───────────────────────────────
static void _open_network_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* net = ui_dc_network_create();
    lv_scr_load_anim(net, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

static void _open_notifications_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_panel_toggle();
}

// ─── Helper: card pulsante home ───────────────────────────────────────────────
static const char* _tile_kind_text(HomeTileKind kind) {
    switch (kind) {
        case HomeTileKind::RELAY_LIGHT: return "Lampada";
        case HomeTileKind::RELAY_UVC: return "UVC";
        case HomeTileKind::RELAY_ELECTRO: return "Elettrostatico";
        case HomeTileKind::RELAY_COMMAND: return "Comando";
        case HomeTileKind::AIR_INTAKE: return "Immissione";
        case HomeTileKind::AIR_EXTRACTION: return "Aspirazione";
        default: return "Periferica";
    }
}

static void _tile_icons(HomeTileKind kind, const lv_img_dsc_t*& on_icon, const lv_img_dsc_t*& off_icon) {
    switch (kind) {
        case HomeTileKind::RELAY_LIGHT:   on_icon = &light_on; off_icon = &light_off; break;
        case HomeTileKind::RELAY_UVC:     on_icon = &uvc_on; off_icon = &uvc_off; break;
        case HomeTileKind::RELAY_ELECTRO: on_icon = &electrostatic_on; off_icon = &electrostatic_off; break;
        case HomeTileKind::RELAY_COMMAND: on_icon = &settings; off_icon = &settings; break;
        case HomeTileKind::AIR_INTAKE:    on_icon = &airintake_on; off_icon = &airintake_off; break;
        case HomeTileKind::AIR_EXTRACTION:on_icon = &airextraction_on; off_icon = &airextraction_off; break;
        default:                          on_icon = &settings; off_icon = &settings; break;
    }
}

static bool _device_to_tile_key(const Rs485Device& dev,
                                HomeTileKind& kind,
                                uint8_t& group,
                                bool& is_relay,
                                bool& is_on) {
    if (!dev.in_plant) return false;
    if (!dev.data_valid) return false;

    if (dev.type == Rs485DevType::RELAY) {
        if (dev.relay_mode == 4) return false; // GAS escluso dalla Home.
        if (dev.relay_mode == 1) kind = HomeTileKind::RELAY_LIGHT;
        else if (dev.relay_mode == 2) kind = HomeTileKind::RELAY_UVC;
        else if (dev.relay_mode == 3) kind = HomeTileKind::RELAY_ELECTRO;
        else if (dev.relay_mode == 5) kind = HomeTileKind::RELAY_COMMAND;
        else return false;
        group = dev.group;
        is_relay = true;
        is_on = dev.relay_on;
        return true;
    }

    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        group = dev.group;
        kind = (dev.group == 1) ? HomeTileKind::AIR_EXTRACTION : HomeTileKind::AIR_INTAKE;
        is_relay = false;
        is_on = dev.sensor_active;
        return true;
    }

    // Pressione e tutto il resto non viene mostrato in Home.
    return false;
}

static int _find_home_tile(HomeTileKind kind, uint8_t group) {
    for (int i = 0; i < s_home_tile_count; i++) {
        if (s_home_tiles[i].kind == kind && s_home_tiles[i].group == group) return i;
    }
    return -1;
}

static int _count_home_tiles_for_kind(HomeTileKind kind) {
    int count = 0;
    for (int i = 0; i < s_home_tile_count; i++) {
        if (s_home_tiles[i].kind == kind) count++;
    }
    return count;
}

static void _home_tile_blink_cb(lv_timer_t* /*t*/) {
    s_tile_blink_phase = !s_tile_blink_phase;
    for (int i = 0; i < s_home_tile_count; i++) {
        HomeTile& tile = s_home_tiles[i];
        if (!tile.icon_obj) continue;
        if (tile.on_count == 0 || tile.on_count == tile.total_count) continue;
        if (tile.icon_on == tile.icon_off) continue;
        lv_img_set_src(tile.icon_obj, s_tile_blink_phase ? tile.icon_on : tile.icon_off);
    }
}

static const lv_img_dsc_t* _home_tile_current_icon(const HomeTile& tile) {
    if (tile.on_count >= tile.total_count) return tile.icon_on;
    if (tile.on_count == 0) return tile.icon_off;
    return s_tile_blink_phase ? tile.icon_on : tile.icon_off;
}

static bool _is_air_010_tile(const HomeTile& tile) {
    return !tile.is_relay &&
           (tile.kind == HomeTileKind::AIR_INTAKE || tile.kind == HomeTileKind::AIR_EXTRACTION);
}

static uint8_t _sanitize_speed_pct(int pct) {
    return (uint8_t)constrain(pct, 0, 100);
}

static int _vent_slider_to_output_pct(int slider_pct) {
    slider_pct = constrain(slider_pct, 0, 100);
    const int min_pct = ui_ventilation_min_speed_get();
    int max_pct = ui_ventilation_max_speed_get();
    if (max_pct < min_pct) max_pct = min_pct;
    return min_pct + ((slider_pct * (max_pct - min_pct) + 50) / 100);
}

static int _vent_output_to_slider_pct(int output_pct) {
    output_pct = constrain(output_pct, 0, 100);
    const int min_pct = ui_ventilation_min_speed_get();
    int max_pct = ui_ventilation_max_speed_get();
    if (max_pct < min_pct) max_pct = min_pct;
    if (output_pct <= min_pct) return 0;
    if (output_pct >= max_pct) return 100;
    if (max_pct == min_pct) return 100;
    return ((output_pct - min_pct) * 100 + ((max_pct - min_pct) / 2)) / (max_pct - min_pct);
}

static int _vent_snap_slider_pct(int slider_pct) {
    slider_pct = constrain(slider_pct, 0, 100);
    const int steps = ui_ventilation_step_count_get();
    if (steps < 2) return slider_pct;

    const int intervals = steps - 1;
    const int idx = ((slider_pct * intervals) + 50) / 100;
    return (idx * 100 + (intervals / 2)) / intervals;
}

static int _vent_slider_next_step_pct(int slider_pct, int direction) {
    slider_pct = _vent_snap_slider_pct(slider_pct);
    const int steps = ui_ventilation_step_count_get();
    if (steps < 2) return constrain(slider_pct + direction, 0, 100);

    const int intervals = steps - 1;
    int idx = ((slider_pct * intervals) + 50) / 100;
    idx = constrain(idx + direction, 0, intervals);
    return (idx * 100 + (intervals / 2)) / intervals;
}

static void _update_air_speed_label(HomeTile& tile, int output_pct) {
    if (!tile.speed_lbl) return;
    char buf[20];
    lv_snprintf(buf, sizeof(buf), "Uscita %d%%", constrain(output_pct, 0, 100));
    lv_label_set_text(tile.speed_lbl, buf);
}

static void _set_air_speed_widgets(HomeTile& tile, int output_pct, bool update_slider) {
    output_pct = constrain(output_pct, 0, 100);
    tile.speed_pct = (uint8_t)output_pct;
    if (update_slider && tile.speed_slider) {
        lv_slider_set_value(tile.speed_slider, _vent_output_to_slider_pct(output_pct), LV_ANIM_OFF);
    }
    _update_air_speed_label(tile, output_pct);
}

static bool _refresh_home_tile_state_from_network(HomeTile& tile) {
    if (!tile.is_relay || tile.addresses_count == 0) return false;

    uint16_t refreshed_on = 0;
    uint8_t refreshed_count = 0;
    for (uint8_t i = 0; i < tile.addresses_count; i++) {
        Rs485Device dev;
        String raw;
        if (!rs485_network_query_device(tile.addresses[i], dev, raw)) continue;
        refreshed_count++;
        if (dev.relay_on) refreshed_on++;
    }

    if (refreshed_count != tile.addresses_count) return false;
    tile.on_count = refreshed_on;
    return true;
}

static void _sync_home_tile_animation_state() {
    bool has_mixed_state = false;
    for (int i = 0; i < s_home_tile_count; i++) {
        const HomeTile& tile = s_home_tiles[i];
        if (tile.on_count > 0 && tile.on_count < tile.total_count && tile.icon_on != tile.icon_off) {
            has_mixed_state = true;
            break;
        }
    }

    if (has_mixed_state) {
        if (!s_tile_blink_timer) {
            s_tile_blink_phase = false;
            s_tile_blink_timer = lv_timer_create(_home_tile_blink_cb, 300, NULL);
        } else {
            lv_timer_set_period(s_tile_blink_timer, 300);
        }
    } else if (s_tile_blink_timer) {
        lv_timer_del(s_tile_blink_timer);
        s_tile_blink_timer = NULL;
        s_tile_blink_phase = false;
    }

    for (int i = 0; i < s_home_tile_count; i++) {
        HomeTile& tile = s_home_tiles[i];
        if (!tile.icon_obj) continue;
        lv_img_set_src(tile.icon_obj, _home_tile_current_icon(tile));
    }
}

static void _sync_home_tiles_from_network_state() {
    for (int i = 0; i < s_home_tile_count; i++) {
        HomeTile& tile = s_home_tiles[i];
        tile.on_count = 0;
        tile.has_warning = false;
        tile.speed_sum = 0;
        tile.speed_count = 0;

        for (uint8_t j = 0; j < tile.addresses_count; j++) {
            Rs485Device dev;
            if (!rs485_network_get_device_by_address(tile.addresses[j], dev)) continue;

            const bool is_on = tile.is_relay ? dev.relay_on : dev.sensor_active;
            if (is_on) tile.on_count++;
            if (_device_has_warning(dev)) tile.has_warning = true;
            if (_is_air_010_tile(tile)) {
                tile.speed_sum += _sanitize_speed_pct((int)(dev.h + 0.5f));
                tile.speed_count++;
            }
        }

        if (_is_air_010_tile(tile) && tile.speed_count > 0) {
            _set_air_speed_widgets(tile, (int)(tile.speed_sum / tile.speed_count), true);
        }

        if (tile.warning_obj) {
            if (tile.has_warning) lv_obj_clear_flag(tile.warning_obj, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(tile.warning_obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void _refresh_pressure_panel() {
    if (!s_pressure_panel) return;

    PressurePairSummary pairs[k_max_pressure_pairs];
    const int pair_count = _build_pressure_pairs(pairs, k_max_pressure_pairs);

    for (int i = 0; i < s_pressure_pair_count; i++) {
        PressurePairWidgets& widgets = s_pressure_pair_widgets[i];
        if (!widgets.used) continue;

        bool matched = false;
        for (int j = 0; j < pair_count; j++) {
            const PressurePairSummary& pair = pairs[j];
            if (widgets.group_a != pair.group_a || widgets.group_b != pair.group_b) continue;

            char delta_p[48];
            char temp_line[64];
            char delta_t[48];
            _format_pressure_pair_delta(pair, delta_p, sizeof(delta_p));
            _format_pressure_pair_temp(pair, temp_line, sizeof(temp_line));
            _format_pressure_pair_delta_t(pair, delta_t, sizeof(delta_t));
            if (widgets.delta_p_lbl) lv_label_set_text(widgets.delta_p_lbl, delta_p);
            if (widgets.temp_lbl) lv_label_set_text(widgets.temp_lbl, temp_line);
            if (widgets.delta_t_lbl) lv_label_set_text(widgets.delta_t_lbl, delta_t);
            matched = true;
            break;
        }

        if (!matched) {
            if (widgets.delta_p_lbl) lv_label_set_text(widgets.delta_p_lbl, "DeltaP non disponibile");
            if (widgets.temp_lbl) lv_label_set_text(widgets.temp_lbl, "Temperatura non disponibile");
            if (widgets.delta_t_lbl) lv_label_set_text(widgets.delta_t_lbl, "DeltaT non disponibile");
        }
    }
}

static void _sync_notifications_from_network_state() {
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant) continue;
        char key[32];
        char title[64];
        char body[160];

        lv_snprintf(key, sizeof(key), "comm_%u", (unsigned)dev.address);
        if (_device_has_comm_issue(dev)) {
            lv_snprintf(title, sizeof(title), "%s G%u - Comunicazione",
                        _tile_warning_title(dev), (unsigned)dev.group);
            _format_notification_body(dev, "Mancata comunicazione.", body, sizeof(body));
            ui_notif_push_or_update(key, UI_NOTIF_ALERT, title, body);
        } else {
            ui_notif_clear(key);
        }

        if (dev.type != Rs485DevType::RELAY) continue;
        if (dev.relay_mode != 2 && dev.relay_mode != 3) continue;

        lv_snprintf(key, sizeof(key), "relay_fb_%u", (unsigned)dev.address);
        if (_relay_has_feedback_fault(dev)) {
            lv_snprintf(title, sizeof(title), "%s G%u - Feedback",
                        _tile_warning_title(dev), (unsigned)dev.group);
            _format_notification_body(dev, "Feedback non ricevuto.", body, sizeof(body));
            ui_notif_push_or_update(key, UI_NOTIF_ALERT, title, body);
        } else {
            ui_notif_clear(key);
        }

        lv_snprintf(key, sizeof(key), "relay_safe_%u", (unsigned)dev.address);
        if (_relay_has_safety_issue(dev)) {
            lv_snprintf(title, sizeof(title), "%s G%u - Sicurezza",
                        _tile_warning_title(dev), (unsigned)dev.group);
            _format_notification_body(dev, "Circuito di sicurezza attivo.", body, sizeof(body));
            ui_notif_push_or_update(key, UI_NOTIF_ALERT, title, body);
        } else {
            ui_notif_clear(key);
        }
    }
}

static void _handle_system_safety_state() {
    bool has_uvc = false;
    bool has_electro = false;
    const bool safety_active = _has_active_protected_safety(&has_uvc, &has_electro);

    if (!safety_active) {
        if (g_system_safety_trip_active) {
            _apply_saved_relay_states(true);
        }
        g_system_safety_trip_active = false;
        g_system_bypass_active = false;
        _dismiss_safety_popup();
        _refresh_bypass_glow();
        ui_notif_clear("system_safety_trip");
        ui_notif_clear("system_bypass");
        _sync_saved_relay_states_from_runtime();
        return;
    }

    char systems[48];
    char body[220];
    _format_protected_systems_text(has_uvc, has_electro, systems, sizeof(systems));
    lv_snprintf(body, sizeof(body),
                "Sicurezza aperta rilevata su %s. Tutti i relay sono stati portati in OFF.",
                systems);
    ui_notif_push_or_update("system_safety_trip", UI_NOTIF_ALERT,
                            "Sicurezza impianto aperta", body);

    if (g_system_bypass_active) {
        lv_snprintf(body, sizeof(body),
                    "Modalita' ByPass attiva. %s restano disattivati fino al ripristino della sicurezza.",
                    systems);
        ui_notif_push_or_update("system_bypass", UI_NOTIF_INFO,
                                "ByPass attivo", body);
    } else {
        ui_notif_clear("system_bypass");
    }

    if (!g_system_safety_trip_active) {
        _sync_saved_relay_states_from_runtime();
        const bool had_active_relays = _has_any_active_relay();
        g_system_safety_trip_active = true;
        _shutdown_all_plant_relays();
        if (had_active_relays && !g_system_bypass_active) {
            _show_safety_popup(has_uvc, has_electro);
        }
    }

    _refresh_bypass_glow();
}

static void _home_sync_cb(lv_timer_t* /*t*/) {
    _sync_home_tiles_from_network_state();
    _refresh_pressure_panel();
    _handle_system_safety_state();
    _sync_home_tile_animation_state();
    _sync_notifications_from_network_state();
    _refresh_notification_bell();
}

static void _home_tile_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_home_tile_count) return;

    HomeTile& tile = s_home_tiles[idx];
    if (_is_air_010_tile(tile)) {
        if (tile.addresses_count == 0) {
            _open_network_cb(e);
            return;
        }

        const bool target_on = (tile.on_count < tile.total_count);
        int ok_count = 0;
        for (uint8_t i = 0; i < tile.addresses_count; i++) {
            String raw;
            if (rs485_network_motor_enable_command(tile.addresses[i], target_on, raw)) ok_count++;
        }
        if (ok_count == tile.addresses_count) {
            tile.on_count = target_on ? tile.total_count : 0;
        }
        _sync_home_tile_animation_state();
        return;
    }

    if (!tile.is_relay || tile.addresses_count == 0) {
        _open_network_cb(e);
        return;
    }

    bool has_uvc = false;
    bool has_electro = false;
    const bool safety_active = _has_active_protected_safety(&has_uvc, &has_electro);
    const bool target_on = (tile.on_count < tile.total_count);
    if (target_on && safety_active) {
        _shutdown_all_plant_relays();
        if (!g_system_bypass_active || _is_protected_tile_kind(tile.kind)) {
            _show_safety_popup(has_uvc, has_electro);
            _refresh_notification_bell();
            return;
        }
    }

    if (target_on && g_system_bypass_active && _is_protected_tile_kind(tile.kind)) {
        _show_safety_popup(has_uvc, has_electro);
        _refresh_notification_bell();
        return;
    }

    const char* action = target_on ? "ON" : "OFF";
    int ok_count = 0;
    for (uint8_t i = 0; i < tile.addresses_count; i++) {
        String raw;
        if (rs485_network_relay_command(tile.addresses[i], action, raw)) ok_count++;
    }

    if (ok_count == tile.addresses_count) {
        tile.on_count = target_on ? tile.total_count : 0;
        for (uint8_t i = 0; i < tile.addresses_count; i++) {
            const uint8_t relay_mode = (tile.kind == HomeTileKind::RELAY_UVC) ? 2 :
                                       (tile.kind == HomeTileKind::RELAY_ELECTRO) ? 3 :
                                       (tile.kind == HomeTileKind::RELAY_COMMAND) ? 5 : 1;
            _upsert_saved_relay_state(tile.addresses[i], relay_mode, target_on);
        }
    } else if (ok_count > 0) {
        (void)_refresh_home_tile_state_from_network(tile);
    }

    _sync_home_tile_animation_state();
}

static void _build_home_tiles_from_network() {
    s_home_tile_count = 0;
    memset(s_home_tiles, 0, sizeof(s_home_tiles));

    const int dev_count = rs485_network_device_count();
    Rs485Device dev;
    for (int i = 0; i < dev_count; i++) {
        if (!rs485_network_get_device(i, dev)) continue;

        HomeTileKind kind;
        uint8_t group = 0;
        bool is_relay = false;
        bool is_on = false;
        if (!_device_to_tile_key(dev, kind, group, is_relay, is_on)) continue;

        int idx = _find_home_tile(kind, group);
        if (idx < 0) {
            if (s_home_tile_count >= RS485_NET_MAX_DEVICES) continue;
            idx = s_home_tile_count++;
            HomeTile& t = s_home_tiles[idx];
            memset(&t, 0, sizeof(t));
            t.kind = kind;
            t.group = group;
            t.is_relay = is_relay;
            _tile_icons(kind, t.icon_on, t.icon_off);
        }

        HomeTile& tile = s_home_tiles[idx];
        tile.total_count++;
        if (is_on) tile.on_count++;
        if (_device_has_warning(dev)) tile.has_warning = true;
        if (_is_air_010_tile(tile)) {
            tile.speed_sum += _sanitize_speed_pct((int)(dev.h + 0.5f));
            tile.speed_count++;
        }
        if (tile.addresses_count < RS485_NET_MAX_DEVICES) {
            tile.addresses[tile.addresses_count++] = dev.address;
        }
    }

    for (int i = 0; i < s_home_tile_count; i++) {
        HomeTile& tile = s_home_tiles[i];
        tile.show_group_label = (_count_home_tiles_for_kind(tile.kind) > 1);
        if (tile.show_group_label) {
            lv_snprintf(tile.title, sizeof(tile.title), "%s G%u",
                        _tile_kind_text(tile.kind), (unsigned)tile.group);
        } else {
            tile.title[0] = '\0';
        }
        if (_is_air_010_tile(tile) && tile.speed_count > 0) {
            tile.speed_pct = (uint8_t)(tile.speed_sum / tile.speed_count);
        }
    }
}

static void _home_air_speed_slider_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_home_tile_count) return;

    HomeTile& tile = s_home_tiles[idx];
    if (!_is_air_010_tile(tile)) return;

    lv_obj_t* slider = lv_event_get_target(e);
    const int slider_pct = _vent_snap_slider_pct((int)lv_slider_get_value(slider));
    const int output_pct = _vent_slider_to_output_pct(slider_pct);
    if (code == LV_EVENT_VALUE_CHANGED) {
        if (ui_ventilation_step_count_get() >= 2) {
            lv_slider_set_value(slider, slider_pct, LV_ANIM_OFF);
        }
        _update_air_speed_label(tile, output_pct);
        return;
    }
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;

    int ok_count = 0;
    for (uint8_t i = 0; i < tile.addresses_count; i++) {
        String raw;
        if (rs485_network_motor_speed_command(tile.addresses[i], (uint8_t)output_pct, raw)) {
            ok_count++;
        }
    }

    if (ok_count > 0) {
        _set_air_speed_widgets(tile, output_pct, false);
    }
}

static void _home_air_speed_step_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const intptr_t raw = (intptr_t)lv_event_get_user_data(e);
    const int idx = (int)(raw / 10);
    const int direction = (raw % 10) == 1 ? 1 : -1;
    if (idx < 0 || idx >= s_home_tile_count) return;

    HomeTile& tile = s_home_tiles[idx];
    if (!_is_air_010_tile(tile) || !tile.speed_slider) return;

    const int current = (int)lv_slider_get_value(tile.speed_slider);
    const int slider_pct = _vent_slider_next_step_pct(current, direction);
    const int output_pct = _vent_slider_to_output_pct(slider_pct);
    lv_slider_set_value(tile.speed_slider, slider_pct, LV_ANIM_OFF);

    int ok_count = 0;
    for (uint8_t i = 0; i < tile.addresses_count; i++) {
        String raw_response;
        if (rs485_network_motor_speed_command(tile.addresses[i], (uint8_t)output_pct, raw_response)) {
            ok_count++;
        }
    }
    if (ok_count > 0) {
        _set_air_speed_widgets(tile, output_pct, false);
    } else {
        _update_air_speed_label(tile, output_pct);
    }
}

static lv_obj_t* make_action_card(lv_obj_t*   parent,
                                  const char* symbol,
                                  const char* label_text,
                                  lv_color_t  icon_color,
                                  lv_coord_t  w,
                                  lv_coord_t  h,
                                  bool        compact,
                                  lv_obj_t**  out_icon_lbl = nullptr) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, compact ? 18 : 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, compact ? 14 : 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, compact ? 4 : 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);

    lv_obj_t* ico = lv_label_create(card);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, compact ? &lv_font_montserrat_32 : &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, compact ? -16 : -22);
    if (out_icon_lbl) *out_icon_lbl = ico;

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, compact ? &lv_font_montserrat_14 : &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, HM_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, compact ? 32 : 52);
    return card;
}

static lv_obj_t* make_device_card(lv_obj_t* parent, int tile_idx, HomeTile& tile) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 172, 188);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 20, 0);
    lv_obj_set_style_shadow_ofs_y(card, 5, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* badge = lv_obj_create(card);
    lv_obj_set_size(badge, 88, 88);
    lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF4F8FC), 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0xDDE5EE), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon = lv_img_create(badge);
    lv_img_set_src(icon, _home_tile_current_icon(tile));
    lv_img_set_zoom(icon, 430);
    lv_obj_center(icon);
    tile.icon_obj = icon;

    lv_obj_t* warning_icon = lv_img_create(card);
    lv_img_set_src(warning_icon, &warning);
    lv_obj_align_to(warning_icon, badge, LV_ALIGN_OUT_BOTTOM_RIGHT, 10, 2);
    if (!tile.has_warning) {
        lv_obj_add_flag(warning_icon, LV_OBJ_FLAG_HIDDEN);
    }
    tile.warning_obj = warning_icon;

    if (tile.show_group_label) {
        lv_obj_t* title_lbl = lv_label_create(card);
        lv_label_set_text(title_lbl, tile.title);
        lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title_lbl, HM_ORANGE, 0);
        lv_obj_set_style_text_align(title_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(title_lbl, 154);
        lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, 100);
    }

    lv_obj_t* sn_lbl = lv_label_create(card);
    lv_label_set_text(sn_lbl, _tile_kind_text(tile.kind));
    lv_obj_set_style_text_font(sn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sn_lbl, HM_TEXT, 0);
    lv_obj_set_style_text_align(sn_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sn_lbl, 154);
    lv_obj_align(sn_lbl, LV_ALIGN_TOP_MID, 0, tile.show_group_label ? 124 : 114);

    lv_obj_add_event_cb(card, _home_tile_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)tile_idx);
    return card;
}

static lv_obj_t* _make_pressure_pair_card(lv_obj_t* parent,
                                          const PressurePairSummary& pair,
                                          int widget_idx,
                                          lv_coord_t card_w,
                                          lv_coord_t card_h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_ofs_y(card, 4, 0);
    lv_obj_set_style_pad_left(card, 18, 0);
    lv_obj_set_style_pad_right(card, 18, 0);
    lv_obj_set_style_pad_top(card, 14, 0);
    lv_obj_set_style_pad_bottom(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon_wrap = lv_obj_create(card);
    lv_obj_set_size(icon_wrap, 64, 64);
    lv_obj_align(icon_wrap, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(icon_wrap, lv_color_hex(0xF4F8FC), 0);
    lv_obj_set_style_border_color(icon_wrap, lv_color_hex(0xDDE5EE), 0);
    lv_obj_set_style_border_width(icon_wrap, 1, 0);
    lv_obj_set_style_radius(icon_wrap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(icon_wrap, 0, 0);
    lv_obj_clear_flag(icon_wrap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon = lv_img_create(icon_wrap);
    lv_img_set_src(icon, &pressure);
    lv_img_set_zoom(icon, 360);
    lv_obj_center(icon);

    lv_obj_t* text_col = lv_obj_create(card);
    lv_obj_set_size(text_col, card_w - 112, card_h - 12);
    lv_obj_align(text_col, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(text_col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_col, 0, 0);
    lv_obj_set_style_pad_all(text_col, 0, 0);
    lv_obj_set_style_pad_row(text_col, 2, 0);
    lv_obj_set_layout(text_col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    char title[32];
    char delta_p[48];
    char temp_line[64];
    char delta_t[48];
    _format_pressure_pair_title(pair, title, sizeof(title));
    _format_pressure_pair_delta(pair, delta_p, sizeof(delta_p));
    _format_pressure_pair_temp(pair, temp_line, sizeof(temp_line));
    _format_pressure_pair_delta_t(pair, delta_t, sizeof(delta_t));

    lv_obj_t* title_lbl = lv_label_create(text_col);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title_lbl, HM_ORANGE, 0);

    lv_obj_t* delta_p_lbl = lv_label_create(text_col);
    lv_label_set_text(delta_p_lbl, delta_p);
    lv_obj_set_style_text_font(delta_p_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(delta_p_lbl, HM_TEXT, 0);

    lv_obj_t* temp_lbl = lv_label_create(text_col);
    lv_label_set_text(temp_lbl, temp_line);
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(temp_lbl, HM_DIM, 0);

    lv_obj_t* delta_t_lbl = lv_label_create(text_col);
    lv_label_set_text(delta_t_lbl, delta_t);
    lv_obj_set_style_text_font(delta_t_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(delta_t_lbl, HM_DIM, 0);

    PressurePairWidgets& widgets = s_pressure_pair_widgets[widget_idx];
    widgets.used = true;
    widgets.group_a = pair.group_a;
    widgets.group_b = pair.group_b;
    widgets.delta_p_lbl = delta_p_lbl;
    widgets.temp_lbl = temp_lbl;
    widgets.delta_t_lbl = delta_t_lbl;
    return card;
}

static lv_obj_t* _make_pressure_placeholder_card(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, card_w, card_h);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_ofs_y(card, 4, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* icon = lv_img_create(card);
    lv_img_set_src(icon, &pressure);
    lv_img_set_zoom(icon, 360);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 6);

    lv_obj_t* title_lbl = lv_label_create(card);
    lv_label_set_text(title_lbl, "Sensori pressione");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title_lbl, HM_ORANGE, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 74, 8);

    lv_obj_t* body_lbl = lv_label_create(card);
    lv_label_set_text(body_lbl,
                      "Sono presenti sensori pressione, ma servono almeno due gruppi online per calcolare il DeltaP.");
    lv_obj_set_width(body_lbl, card_w - 108);
    lv_label_set_long_mode(body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(body_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(body_lbl, HM_TEXT, 0);
    lv_obj_align(body_lbl, LV_ALIGN_TOP_LEFT, 74, 36);
    return card;
}

static lv_obj_t* _make_pressure_panel(lv_obj_t* parent, lv_coord_t panel_w, lv_coord_t panel_h) {
    s_pressure_panel = lv_obj_create(parent);
    lv_obj_set_size(s_pressure_panel, panel_w, panel_h);
    lv_obj_set_style_bg_opa(s_pressure_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pressure_panel, 0, 0);
    lv_obj_set_style_pad_all(s_pressure_panel, 8, 0);
    lv_obj_set_style_pad_row(s_pressure_panel, 12, 0);
    lv_obj_set_layout(s_pressure_panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_pressure_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pressure_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_pressure_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(s_pressure_panel);
    lv_label_set_text(title, "Pressioni Differenziali");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, HM_TEXT, 0);

    lv_obj_t* subtitle = lv_label_create(s_pressure_panel);
    lv_label_set_text(subtitle, "DeltaP e DeltaT tra i gruppi pressione");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subtitle, HM_DIM, 0);

    PressurePairSummary pairs[k_max_pressure_pairs];
    s_pressure_pair_count = _build_pressure_pairs(pairs, k_max_pressure_pairs);
    memset(s_pressure_pair_widgets, 0, sizeof(s_pressure_pair_widgets));

    const lv_coord_t card_w = panel_w - 16;
    if (s_pressure_pair_count <= 0) {
        _make_pressure_placeholder_card(s_pressure_panel, card_w, 160);
        return s_pressure_panel;
    }

    lv_coord_t card_h = 118;
    if (s_pressure_pair_count == 1) card_h = 148;
    if (s_pressure_pair_count >= 3) card_h = 106;

    for (int i = 0; i < s_pressure_pair_count; i++) {
        _make_pressure_pair_card(s_pressure_panel, pairs[i], i, card_w, card_h);
    }
    return s_pressure_panel;
}

// ─── Costruzione Home ─────────────────────────────────────────────────────────
static int _count_air_tiles() {
    int count = 0;
    for (int i = 0; i < s_home_tile_count; i++) {
        if (_is_air_010_tile(s_home_tiles[i])) count++;
    }
    return count;
}

static lv_obj_t* _make_air_speed_control(lv_obj_t* parent, int tile_idx, lv_coord_t control_w, lv_coord_t control_h) {
    HomeTile& tile = s_home_tiles[tile_idx];

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, control_w, control_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name_lbl = lv_label_create(row);
    lv_label_set_text(name_lbl, _tile_kind_text(tile.kind));
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(name_lbl, HM_TEXT, 0);
    lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, -15);

    lv_obj_t* value_lbl = lv_label_create(row);
    lv_obj_set_style_text_font(value_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(value_lbl, HM_ORANGE, 0);
    lv_obj_set_style_text_align(value_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(value_lbl, 120);
    lv_obj_align(value_lbl, LV_ALIGN_LEFT_MID, 0, 16);
    tile.speed_lbl = value_lbl;
    _update_air_speed_label(tile, tile.speed_pct);

    lv_obj_t* minus_btn = lv_btn_create(row);
    lv_obj_set_size(minus_btn, 54, 54);
    lv_obj_set_style_radius(minus_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(minus_btn, HM_WHITE, 0);
    lv_obj_set_style_bg_color(minus_btn, lv_color_hex(0xE6EEF6), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(minus_btn, lv_color_hex(0xDDE5EE), 0);
    lv_obj_set_style_border_width(minus_btn, 1, 0);
    lv_obj_set_style_shadow_width(minus_btn, 0, 0);
    lv_obj_align(minus_btn, LV_ALIGN_LEFT_MID, 150, 0);
    lv_obj_t* minus_lbl = lv_label_create(minus_btn);
    lv_label_set_text(minus_lbl, "-");
    lv_obj_set_style_text_font(minus_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(minus_lbl, HM_TEXT, 0);
    lv_obj_center(minus_lbl);
    lv_obj_add_event_cb(minus_btn, _home_air_speed_step_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(tile_idx * 10));

    lv_obj_t* plus_btn = lv_btn_create(row);
    lv_obj_set_size(plus_btn, 54, 54);
    lv_obj_set_style_radius(plus_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(plus_btn, HM_ORANGE, 0);
    lv_obj_set_style_bg_color(plus_btn, lv_color_hex(0xC93516), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(plus_btn, 0, 0);
    lv_obj_align(plus_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* plus_lbl = lv_label_create(plus_btn);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_set_style_text_font(plus_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(plus_lbl, lv_color_white(), 0);
    lv_obj_center(plus_lbl);
    lv_obj_add_event_cb(plus_btn, _home_air_speed_step_cb, LV_EVENT_CLICKED,
                        (void*)(intptr_t)(tile_idx * 10 + 1));

    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_set_size(slider, control_w - 310, 18);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, _vent_output_to_slider_pct(tile.speed_pct), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xDDE5EE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, HM_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, HM_ORANGE, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, 9, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 9, LV_PART_INDICATOR);
    lv_obj_align(slider, LV_ALIGN_LEFT_MID, 222, 0);
    lv_obj_add_event_cb(slider, _home_air_speed_slider_cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)tile_idx);
    lv_obj_add_event_cb(slider, _home_air_speed_slider_cb, LV_EVENT_RELEASED, (void*)(intptr_t)tile_idx);
    lv_obj_add_event_cb(slider, _home_air_speed_slider_cb, LV_EVENT_PRESS_LOST, (void*)(intptr_t)tile_idx);
    tile.speed_slider = slider;
    return row;
}

static lv_obj_t* _make_air_speed_panel(lv_obj_t* parent, lv_coord_t panel_w, lv_coord_t panel_h) {
    const int air_count = _count_air_tiles();
    if (air_count <= 0) return NULL;

    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_size(panel, panel_w, panel_h);
    lv_obj_set_style_bg_color(panel, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_shadow_color(panel, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(panel, 20, 0);
    lv_obj_set_style_shadow_ofs_y(panel, 5, 0);
    lv_obj_set_style_pad_left(panel, 22, 0);
    lv_obj_set_style_pad_right(panel, 22, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t control_h = (air_count > 1) ? ((panel_h - 30) / air_count) : (panel_h - 24);
    for (int i = 0; i < s_home_tile_count; i++) {
        if (!_is_air_010_tile(s_home_tiles[i])) continue;
        _make_air_speed_control(panel, i, panel_w - 44, control_h);
    }
    return panel;
}

lv_obj_t* ui_dc_home_create(void) {
    _ensure_ui_settings_loaded();

    lv_obj_t* scr = lv_obj_create(NULL);
    s_home_scr = scr;
    lv_obj_set_style_bg_color(scr, HM_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(scr, _on_home_delete, LV_EVENT_DELETE, NULL);

    // ── Timer idle (creato una volta sola, persiste) ──────────────────────────
    if (!g_idle_timer) {
        g_idle_timer = lv_timer_create(_idle_cb, 2000, NULL);
    }

    // ── Header bar effetto 3D ─────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, HM_WHITE, 0);
    lv_obj_set_style_bg_grad_color(hdr, lv_color_hex(0xD8E4EE), 0);
    lv_obj_set_style_bg_grad_dir(hdr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_shadow_color(hdr, lv_color_hex(0x90A8C0), 0);
    lv_obj_set_style_shadow_width(hdr, 20, 0);
    lv_obj_set_style_shadow_ofs_y(hdr, 5, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ui_notif_panel_init(scr, hdr);

    s_time_lbl = lv_label_create(hdr);
    lv_label_set_text(s_time_lbl, "00:00:00");
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_time_lbl, HM_TEXT, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_LEFT_MID, 24, -7);

    s_date_lbl = lv_label_create(hdr);
    lv_label_set_text(s_date_lbl, "-- --- ----");
    lv_obj_set_style_text_font(s_date_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_date_lbl, HM_DIM, 0);
    lv_obj_align(s_date_lbl, LV_ALIGN_LEFT_MID, 24, 13);

    s_plant_lbl = lv_label_create(hdr);
    lv_label_set_text(s_plant_lbl, g_plant_name);
    lv_obj_set_width(s_plant_lbl, 420);
    lv_label_set_long_mode(s_plant_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_plant_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_plant_lbl, HM_TEXT, 0);
    lv_obj_set_style_text_align(s_plant_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_plant_lbl, LV_ALIGN_CENTER, 0, -2);
    lv_obj_add_flag(s_plant_lbl, LV_OBJ_FLAG_HIDDEN);

    // Separatore verticale
    lv_obj_t* sep = lv_obj_create(hdr);
    lv_obj_set_size(sep, 1, 36);
    lv_obj_align(sep, LV_ALIGN_LEFT_MID, 158, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xC0D0E0), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Temperatura
    s_temp_lbl = lv_label_create(hdr);
    lv_label_set_text(s_temp_lbl, "-- C");
    lv_obj_set_style_text_font(s_temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_temp_lbl, HM_TEXT, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_RIGHT_MID, -24, -7);

    // Umidita
    s_hum_lbl = lv_label_create(hdr);
    lv_label_set_text(s_hum_lbl, "-- %RH");
    lv_obj_set_style_text_font(s_hum_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_hum_lbl, HM_DIM, 0);
    lv_obj_align(s_hum_lbl, LV_ALIGN_RIGHT_MID, -24, 13);

    _refresh_env_labels();

    // ── Timer orologio ────────────────────────────────────────────────────────
    s_clock_timer = lv_timer_create(_clock_cb, 1000, NULL);
    _clock_cb(NULL);
    s_home_sync_timer = lv_timer_create(_home_sync_cb, 1000, NULL);

    // ── Griglia 2x2 pulsanti ──────────────────────────────────────────────────
    _build_home_tiles_from_network();
    const int tile_count = s_home_tile_count;
    PressureGroupSummary pressure_groups[k_max_pressure_groups];
    const int pressure_group_count = _collect_pressure_groups(pressure_groups, k_max_pressure_groups);
    const bool has_pressure_panel = pressure_group_count > 0;
    const bool devices_found = rs485_network_device_count() > 0;
    const bool has_air_speed_panel = _count_air_tiles() > 0;
    _refresh_plant_name_label(devices_found);
    if (tile_count > 0 || has_pressure_panel) {
        lv_obj_t* body = lv_obj_create(scr);
        lv_obj_set_size(body, 1024, 540);
        lv_obj_set_pos(body, 0, HEADER_H);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 0, 0);
        lv_obj_set_style_radius(body, 0, 0);
        lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

        const lv_coord_t right_w = 220;
        const lv_coord_t left_w = has_pressure_panel ? 448 : 768;
        const lv_coord_t pressure_w = has_pressure_panel ? ((tile_count > 0) ? 300 : 768) : 0;
        const lv_coord_t content_h = has_air_speed_panel ? 400 : 500;
        const lv_coord_t nav_card_h = has_air_speed_panel ? 116 : 150;

        if (tile_count > 0) {
            lv_obj_t* left = lv_obj_create(body);
            lv_obj_set_size(left, left_w, content_h);
            lv_obj_set_pos(left, 12, 0);
            lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(left, 0, 0);
            lv_obj_set_style_pad_all(left, 8, 0);
            lv_obj_set_style_pad_row(left, 14, 0);
            lv_obj_set_style_pad_column(left, 14, 0);
            lv_obj_set_layout(left, LV_LAYOUT_FLEX);
            lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

            for (int i = 0; i < tile_count; i++) {
                HomeTile& tile = s_home_tiles[i];
                make_device_card(left, i, tile);
            }
        }
        _sync_home_tile_animation_state();

        if (has_pressure_panel) {
            lv_obj_t* center = _make_pressure_panel(body, pressure_w, content_h);
            if (tile_count > 0) {
                lv_obj_set_pos(center, left_w + 20, 0);
            } else {
                lv_obj_set_pos(center, 12, 0);
            }
        }

        lv_obj_t* right = lv_obj_create(body);
        lv_obj_set_size(right, right_w, content_h);
        lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -12, 0);
        lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(right, 0, 0);
        lv_obj_set_style_pad_all(right, 8, 0);
        lv_obj_set_style_pad_row(right, 14, 0);
        lv_obj_set_layout(right, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* settings_card = make_action_card(right, LV_SYMBOL_SETTINGS, "Impostazioni",
                                                   lv_color_hex(0x3A6BC8), 200, nav_card_h, true);
        lv_obj_add_event_cb(settings_card, _open_settings_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* stato_card = make_action_card(right, LV_SYMBOL_LIST, "Stato",
                                                lv_color_hex(0x8C44B8), 200, nav_card_h, true);
        lv_obj_add_event_cb(stato_card, _open_network_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* notif_card = make_action_card(right, LV_SYMBOL_BELL, "Notifiche",
                                                lv_color_hex(0x28A745), 200, nav_card_h, true,
                                                &s_notif_icon_lbl);
        lv_obj_add_event_cb(notif_card, _open_notifications_cb, LV_EVENT_CLICKED, NULL);

        if (has_air_speed_panel) {
            lv_obj_t* air_panel = _make_air_speed_panel(body, 1000, 112);
            if (air_panel) lv_obj_set_pos(air_panel, 12, 416);
        }
    } else {
        lv_obj_t* row = lv_obj_create(scr);
        lv_obj_set_size(row, 820, 260);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, 36);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 34, 0);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* settings_card = make_action_card(row, LV_SYMBOL_SETTINGS, "Impostazioni",
                                                   lv_color_hex(0x3A6BC8), 230, 210, false);
        lv_obj_add_event_cb(settings_card, _open_settings_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* notif_card = make_action_card(row, LV_SYMBOL_BELL, "Notifiche",
                                                lv_color_hex(0x28A745), 230, 210, false,
                                                &s_notif_icon_lbl);
        lv_obj_add_event_cb(notif_card, _open_notifications_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* stato_card = make_action_card(row, LV_SYMBOL_LIST, "Stato",
                                                lv_color_hex(0x8C44B8), 230, 210, false);
        lv_obj_add_event_cb(stato_card, _open_network_cb, LV_EVENT_CLICKED, NULL);
    }

    s_bypass_glow = lv_obj_create(scr);
    lv_obj_set_size(s_bypass_glow, 1024, 600);
    lv_obj_set_pos(s_bypass_glow, 0, 0);
    lv_obj_set_style_bg_opa(s_bypass_glow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_bypass_glow, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_border_width(s_bypass_glow, 6, 0);
    lv_obj_set_style_radius(s_bypass_glow, 0, 0);
    lv_obj_set_style_shadow_color(s_bypass_glow, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_shadow_width(s_bypass_glow, 28, 0);
    lv_obj_set_style_shadow_spread(s_bypass_glow, 2, 0);
    lv_obj_set_style_shadow_opa(s_bypass_glow, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(s_bypass_glow, 0, 0);
    lv_obj_clear_flag(s_bypass_glow, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bypass_glow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_bypass_glow);

    _home_sync_cb(NULL);
    return scr;
}
