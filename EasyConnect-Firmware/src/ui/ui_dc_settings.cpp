/**
 * @file ui_dc_settings.cpp
 * @brief Pagina Impostazioni Display Controller
 */

#include "ui_dc_settings.h"
#include "ui_dc_home.h"
#include "ui_dc_clock.h"
#include "ui_dc_maintenance.h"
#include "display_port/rgb_lcd_port.h"
#include "rs485_network.h"
#include "DisplayApi_Manager.h"
#include "wifi_boot.h"
#include "lvgl.h"

#include <Network.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_err.h>
#include "esp_heap_caps.h"
#include <esp_wifi.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "ui_filter_calib.h"

// Layout
#define HEADER_H   60
#define LEFT_W     300
#define RIGHT_W    724
#define CONTENT_H  540
#define ITEM_H     (CONTENT_H / 7)

// Palette
#define ST_BG          lv_color_hex(0xEEF3F8)
#define ST_WHITE       lv_color_hex(0xFFFFFF)
#define ST_ORANGE      lv_color_hex(0xE84820)
#define ST_ORANGE2     lv_color_hex(0xB02810)
#define ST_TEXT        lv_color_hex(0x243447)
#define ST_DIM         lv_color_hex(0x7A92B0)
#define ST_BORDER      lv_color_hex(0xDDE5EE)
#define ST_LEFT_BG     lv_color_hex(0xF5F8FB)
#define ST_ACCENT_IDLE lv_color_hex(0xDDE5EE)

// Menu
static const char* const k_menu_labels[] = {
    "Profilo\nDisplay",
    "Data e\nOra",
    "WiFi e\nRete Internet",
    "Rete e\nSistema",
    "Ventilazione\nImpianto",
    "Filtraggio\nAria",
    "Sensori\nDiagnostica",
};

static const char* const k_menu_icons[] = {
    LV_SYMBOL_EDIT,
    LV_SYMBOL_BELL,
    LV_SYMBOL_WIFI,
    LV_SYMBOL_SETTINGS,
    LV_SYMBOL_REFRESH,
    LV_SYMBOL_SHUFFLE,
    LV_SYMBOL_EYE_OPEN,
};

static const uint32_t k_icon_clr[] = {
    0x3A6BC8,
    0xD08A1A,
    0x1985D1,
    0xE84820,
    0x0FA8A8,
    0x28A745,
    0x8C44B8,
};

static constexpr int k_menu_n = 7;

// Stateful pointers
static lv_obj_t* s_right_panel = NULL;
static int s_active = 0;

static lv_obj_t* s_btn[k_menu_n] = {};
static lv_obj_t* s_accent[k_menu_n] = {};
static lv_obj_t* s_ico[k_menu_n] = {};
static lv_obj_t* s_lbl[k_menu_n] = {};

// User settings maps
static const char* k_map_tema[] = {"Chiaro", "Scuro", ""};
static const char* k_map_gradi[] = {"Celsius", "Fahrenheit", ""};

// Date/time settings controls
static lv_timer_t* s_dt_timer = NULL;
static lv_obj_t* s_dt_row_auto = NULL;
static lv_obj_t* s_dt_row_tz = NULL;
static lv_obj_t* s_dt_row_time = NULL;
static lv_obj_t* s_dt_row_date = NULL;
static lv_obj_t* s_dt_auto_sw = NULL;
static lv_obj_t* s_dt_tz_dd = NULL;
static lv_obj_t* s_dt_time_btn = NULL;
static lv_obj_t* s_dt_date_btn = NULL;
static lv_obj_t* s_dt_time_lbl = NULL;
static lv_obj_t* s_dt_date_lbl = NULL;

// WiFi settings controls
static lv_obj_t* s_wifi_row = NULL;
static lv_obj_t* s_wifi_sw = NULL;
static lv_obj_t* s_wifi_status_lbl = NULL;
static lv_obj_t* s_wifi_ip_lbl = NULL;
static lv_obj_t* s_wifi_scan_lbl = NULL;
static lv_obj_t* s_wifi_list = NULL;
static lv_timer_t* s_wifi_status_timer = NULL;
static lv_timer_t* s_wifi_scan_timer = NULL;
static bool s_wifi_scan_pending = false;
static String s_wifi_target_ssid;
static bool s_wifi_connect_pending = false;
static unsigned long s_wifi_connect_guard_start_ms = 0;
static bool s_wifi_display_guard_active = false;
static bool s_wifi_api_section_visible = false;
static bool s_wifi_waiting_for_selection = false;
static bool s_wifi_scan_error = false;
static unsigned long s_wifi_scan_started_ms = 0;

static constexpr int k_wifi_max_networks = 16;
static constexpr unsigned long k_wifi_connect_guard_timeout_ms = 30000UL;
static constexpr uint8_t k_wifi_scan_soft_retries = 3;
static constexpr uint8_t k_wifi_scan_hard_retries = 5;
static constexpr uint32_t k_wifi_scan_retry_delay_ms = 180;
struct WifiScanEntry {
    String ssid;
    int32_t rssi;
    bool secure;
};
static WifiScanEntry s_wifi_scan_entries[k_wifi_max_networks];
static int s_wifi_scan_count = 0;

struct WifiPasswordPopupCtx {
    lv_obj_t* mask;
    lv_obj_t* ta;
    lv_obj_t* eye_btn;
    bool pw_visible;
    char ssid[33];
};
static WifiPasswordPopupCtx* s_wifi_popup_ctx = NULL;

struct PlantNamePopupCtx {
    lv_obj_t* mask;
    lv_obj_t* ta;
};

struct ApiTextPopupCtx {
    lv_obj_t* mask;
    lv_obj_t* ta;
    lv_obj_t* eye_btn;
    lv_obj_t* value_lbl;
    bool key_field;
    bool pw_visible;
    char pref_key[16];
};

struct Air010Presence {
    int extraction_count;
    int intake_count;
};

static PlantNamePopupCtx* s_plant_popup_ctx = NULL;
static ApiTextPopupCtx* s_api_popup_ctx = NULL;
static lv_obj_t* s_plant_name_btn = NULL;
static lv_obj_t* s_plant_name_lbl = NULL;

// Stato pannello "Rete e Sistema" (RS485)
static lv_obj_t*   s_sys_count_lbl = NULL;  // label "Periferiche rilevate" valore
static lv_obj_t*   s_sys_plant_lbl = NULL;  // label "Periferiche impianto" valore
static lv_obj_t*   s_sys_scan_btn  = NULL;  // pulsante Scansiona (per disabilitarlo)
static lv_obj_t*   s_sys_save_btn  = NULL;  // pulsante Salva impianto
static lv_timer_t* s_sys_timer     = NULL;  // timer polling stato scan

// Stato pannello "Filtraggio Aria"
static lv_obj_t*   s_filt_status_lbl = NULL;
static lv_obj_t*   s_filt_dp_lbl     = NULL;
static lv_obj_t*   s_filt_mon_sw     = NULL;
static lv_timer_t* s_filt_timer      = NULL;

enum class WizStep : uint8_t {
    MODE_SELECT,
    MANUAL_MOVE, MANUAL_SAMPLING, MANUAL_CONFIRM,
    AUTO_RUNNING,
    DONE
};

enum class AutoPhase : uint8_t { WARMUP, STABILIZING };

static constexpr int   k_auto_warmup_ticks   = 14;   // 14 × 500 ms = 7 s
static constexpr int   k_auto_timeout_ticks  = 120;  // 120 × 500 ms = 60 s
static constexpr int   k_auto_stab_hold      = 4;    // 4 finestre stabili consecutive
static constexpr float k_auto_stab_thr       = 5.0f; // Pa max range finestra stabile
static constexpr int   k_auto_stab_n         = 8;    // campioni rolling window
static constexpr int   k_manual_sample_ticks = 20;   // 20 × 500 ms = 10 s

struct CalibWizCtx {
    lv_obj_t*   mask;
    lv_obj_t*   subtitle_lbl;
    lv_obj_t*   body_lbl;
    lv_obj_t*   value_lbl;
    lv_obj_t*   speed_cont;
    lv_obj_t*   speed_lbl;
    lv_obj_t*   spinner;
    lv_obj_t*   btn_left;
    lv_obj_t*   btn_right;
    lv_timer_t* sample_timer;

    WizStep   step;
    bool      auto_mode;
    int       point_idx;
    int       total_points;
    uint8_t   speeds[UI_FILTER_MAX_POINTS];
    uint8_t   current_speed;

    // Campionamento manuale
    int   sample_tick;
    float sample_sum;
    int   sample_count;
    float sampled_dp[UI_FILTER_MAX_POINTS];

    // Stato modalita automatica
    AutoPhase auto_phase;
    int       auto_phase_tick;
    float     auto_stab_buf[k_auto_stab_n];
    int       auto_stab_idx;
    int       auto_stab_consec;
    float     auto_cur_sum;
    int       auto_cur_count;
};
static CalibWizCtx* s_wiz_ctx = NULL;

// Forward
static void _show_content(int idx);
static void _clear_network_state();
static void _filt_wiz_render(CalibWizCtx* ctx);
static void _filt_wiz_auto_advance(CalibWizCtx* ctx);
static void _filt_wiz_send_speed(uint8_t pct);

static Air010Presence _air010_presence() {
    Air010Presence presence = {};
    const int dev_count = rs485_network_device_count();
    for (int i = 0; i < dev_count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.data_valid) continue;
        if (dev.type != Rs485DevType::SENSOR) continue;
        if (dev.sensor_profile != Rs485SensorProfile::AIR_010) continue;

        if (dev.group == 1) presence.extraction_count++;
        else if (dev.group == 2) presence.intake_count++;
    }
    return presence;
}

static void _wifi_display_guard_apply(bool enable) {
    if (s_wifi_display_guard_active == enable) return;
    s_wifi_display_guard_active = enable;
    if (enable) {
        waveshare_rgb_lcd_activity_guard_acquire();
    } else {
        waveshare_rgb_lcd_activity_guard_release();
    }
}

static void _wifi_display_guard_refresh() {
    const bool need_guard = s_wifi_scan_pending || s_wifi_connect_pending;
    _wifi_display_guard_apply(need_guard);
}

static const char* _wifi_status_name(wl_status_t status) {
    switch (status) {
        case WL_NO_SHIELD: return "NO_SHIELD";
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "?";
    }
}

static const char* _wifi_mode_name(wifi_mode_t mode) {
    switch (mode) {
        case WIFI_MODE_NULL: return "OFF";
        case WIFI_MODE_STA: return "STA";
        case WIFI_MODE_AP: return "AP";
        case WIFI_MODE_APSTA: return "AP_STA";
        default: return "?";
    }
}

static void _wifi_log_state(const char* tag) {
    wifi_mode_t esp_mode = WIFI_MODE_NULL;
    const esp_err_t mode_err = esp_wifi_get_mode(&esp_mode);

    wifi_ps_type_t ps = WIFI_PS_NONE;
    const esp_err_t ps_err = esp_wifi_get_ps(&ps);

    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    const esp_err_t channel_err = esp_wifi_get_channel(&channel, &second);

    const wl_status_t status = WiFi.status();
    const wifi_mode_t arduino_mode = WiFi.getMode();

    Serial.printf("[WIFI-DIAG] t=%lu tag=%s mode=%d/%s esp_mode=%d/%s(%s) status=%d/%s auto=%d connected=%d ssid='%s' ps=%d(%s) ch=%u(%s) heap_int=%u heap_dma=%u dma_big=%u\n",
                  millis(),
                  tag ? tag : "?",
                  (int)arduino_mode,
                  _wifi_mode_name(arduino_mode),
                  (int)esp_mode,
                  _wifi_mode_name(esp_mode),
                  esp_err_to_name(mode_err),
                  (int)status,
                  _wifi_status_name(status),
                  WiFi.getAutoReconnect() ? 1 : 0,
                  WiFi.isConnected() ? 1 : 0,
                  WiFi.SSID().c_str(),
                  (int)ps,
                  esp_err_to_name(ps_err),
                  (unsigned)channel,
                  esp_err_to_name(channel_err),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

static void _wifi_log_scan_stop(const char* phase) {
    const esp_err_t err = esp_wifi_scan_stop();
    Serial.printf("[WIFI-DIAG] scan_stop phase=%s err=%s(%d)\n",
                  phase ? phase : "?",
                  esp_err_to_name(err),
                  (int)err);
}

static bool _wifi_try_start_scan_async(const char* phase) {
    _wifi_log_state(phase ? phase : "scan-start");
    const int16_t rc = WiFi.scanNetworks(true, true);
    Serial.printf("[WIFI-DIAG] scanNetworks phase=%s rc=%d (%s)\n",
                  phase ? phase : "?",
                  (int)rc,
                  rc == WIFI_SCAN_RUNNING ? "RUNNING" :
                  rc == WIFI_SCAN_FAILED ? "FAILED" : "DONE_SYNC");
    if (rc == WIFI_SCAN_FAILED) {
        _wifi_log_state("scan-start-failed");
    }
    return rc != WIFI_SCAN_FAILED;
}

static bool _wifi_start_scan_with_recovery() {
    _wifi_log_state("scan-recovery-begin");
    WiFi.useStaticBuffers(true);
    // Do not call WiFi.begin() here: with Arduino-ESP32 3.3.x it can reuse
    // stale credentials from NVS and leave the driver connecting while we scan.
    // We call it without credentials so it either uses NVS or idles — then we
    // scanNetworks() starts STA through enableSTA() when needed.
    if ((WiFi.getMode() & WIFI_STA) == 0 && !WiFi.mode(WIFI_STA)) {
        Serial.println("[WIFI-DIAG] unable to enable STA mode");
        _wifi_log_state("scan-sta-enable-failed");
        return false;
    }
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);

    WiFi.scanDelete();
    if (_wifi_try_start_scan_async("initial")) return true;

    // If a connect attempt is in progress, abort it and retry scan.
    _wifi_log_scan_stop("after-initial-fail");
    WiFi.disconnect(false, false);
    for (uint8_t attempt = 0; attempt < k_wifi_scan_soft_retries; attempt++) {
        delay(k_wifi_scan_retry_delay_ms);
        WiFi.scanDelete();
        char phase[24];
        snprintf(phase, sizeof(phase), "disconnect-%u", (unsigned)(attempt + 1));
        if (_wifi_try_start_scan_async(phase)) return true;
    }

    // Last-resort recovery without deinitializing WiFi: after LVGL is up,
    // deinit/reinit can fail with ESP_ERR_NO_MEM because internal DMA heap is low.
    _wifi_log_state("scan-restart-sta-without-deinit");
    WiFi.disconnect(false, false);
    delay(150);
    if ((WiFi.getMode() & WIFI_STA) == 0 && !WiFi.mode(WIFI_STA)) {
        Serial.println("[WIFI-DIAG] unable to keep STA mode active");
        _wifi_log_state("scan-keep-sta-failed");
        return false;
    }
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(false);
    delay(250);
    _wifi_log_state("scan-restart-sta-ready");
    for (uint8_t attempt = 0; attempt < k_wifi_scan_hard_retries; attempt++) {
        _wifi_log_scan_stop("restart-loop");
        WiFi.scanDelete();
        char phase[20];
        snprintf(phase, sizeof(phase), "restart-%u", (unsigned)(attempt + 1));
        if (_wifi_try_start_scan_async(phase)) return true;
        delay(k_wifi_scan_retry_delay_ms);
    }
    _wifi_log_state("scan-recovery-failed");
    return false;
}

static int _days_in_month(int year, int month) {
    static const int k_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;
    const bool leap = ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0));
    if (month == 2 && leap) return 29;
    return k_days[month - 1];
}

static void _build_numeric_options(int from, int to, char* out, size_t out_sz, bool two_digits) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    size_t used = 0;
    for (int n = from; n <= to; n++) {
        const int remaining = (int)(out_sz - used);
        if (remaining <= 1) break;

        int written = 0;
        if (two_digits) {
            written = lv_snprintf(out + used, (uint32_t)remaining, "%02d", n);
        } else {
            written = lv_snprintf(out + used, (uint32_t)remaining, "%d", n);
        }
        if (written < 0) break;
        used += (size_t)written;

        if (n != to && used + 1 < out_sz) {
            out[used++] = '\n';
            out[used] = '\0';
        }
    }
}

// Navigation
static void _back_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* home = ui_dc_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

static void _apply_menu_style(int idx, bool selected) {
    if (!s_btn[idx]) return;

    lv_color_t bg = selected ? ST_ORANGE : ST_LEFT_BG;
    lv_color_t accent = selected ? ST_ORANGE2 : ST_ACCENT_IDLE;
    lv_color_t ico_clr = selected ? lv_color_white() : lv_color_hex(k_icon_clr[idx]);
    lv_color_t txt_clr = selected ? lv_color_white() : ST_TEXT;

    lv_obj_set_style_bg_color(s_btn[idx], bg, 0);
    lv_obj_set_style_bg_color(s_accent[idx], accent, 0);
    lv_obj_set_style_text_color(s_ico[idx], ico_clr, 0);
    lv_obj_set_style_text_color(s_lbl[idx], txt_clr, 0);
}

static void _menu_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_active) return;

    _apply_menu_style(s_active, false);
    s_active = idx;
    _apply_menu_style(s_active, true);
    _show_content(idx);
}

// Shared UI rows/helpers
static lv_obj_t* make_row(lv_obj_t* parent, const char* label_text) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 62);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, ST_BORDER, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 24, 0);
    lv_obj_set_style_pad_right(row, 24, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
    return row;
}

static void _set_row_enabled(lv_obj_t* row, bool enabled) {
    if (!row) return;
    lv_obj_t* lbl = lv_obj_get_child(row, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, enabled ? ST_TEXT : ST_DIM, 0);
    }
}

static void _set_control_enabled(lv_obj_t* obj, bool enabled) {
    if (!obj) return;
    if (enabled) {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}


static void _seg2p_update(lv_obj_t* cont, int sel) {
    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_obj_get_child(cont, i);
        if (!btn) continue;
        const bool active = (i == sel);
        lv_obj_set_style_bg_color(btn, active ? ST_ORANGE : ST_WHITE, 0);
        lv_obj_t* lbl = lv_obj_get_child(btn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, active ? lv_color_white() : ST_TEXT, 0);
    }
    lv_obj_invalidate(cont);
}

static void _seg2p_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* btn  = lv_event_get_target(e);
    lv_obj_t* cont = lv_obj_get_parent(btn);
    const int idx  = (lv_obj_get_child(cont, 0) == btn) ? 0 : 1;
    lv_obj_set_user_data(cont, (void*)(uintptr_t)idx);
    _seg2p_update(cont, idx);
    lv_event_send(cont, LV_EVENT_VALUE_CHANGED, NULL);
}

static lv_obj_t* make_seg2_prominent(lv_obj_t* row, const char** map, int checked_idx) {
    lv_obj_t* cont = lv_obj_create(row);
    lv_obj_set_size(cont, 280, 44);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(cont, ST_ORANGE, 0);
    lv_obj_set_style_border_width(cont, 2, 0);
    lv_obj_set_style_radius(cont, 10, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_gap(cont, 0, 0);
    lv_obj_set_style_clip_corner(cont, true, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    for (int i = 0; i < 2; i++) {
        lv_obj_t* btn = lv_btn_create(cont);
        lv_obj_set_size(btn, 140, 44);
        lv_obj_set_style_bg_color(btn, (i == checked_idx) ? ST_ORANGE : ST_WHITE, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, map[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, (i == checked_idx) ? lv_color_white() : ST_TEXT, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, _seg2p_click_cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_set_user_data(cont, (void*)(uintptr_t)checked_idx);
    lv_obj_align(cont, LV_ALIGN_RIGHT_MID, 0, 0);
    return cont;
}

static void _brightness_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_brightness_set(val);
}

static void _vent_min_speed_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_ventilation_min_speed_set(val);
}

static void _vent_max_speed_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_ventilation_max_speed_set(val);
}

static void _vent_intake_percentage_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_ventilation_intake_percentage_set(val);
}

static void _vent_rebuild_timer_cb(lv_timer_t* t) {
    lv_timer_del(t);
    if (s_active == 4 && s_right_panel) {
        _show_content(4);
    }
}

static void _vent_intake_bar_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* sw = lv_event_get_target(e);
    ui_ventilation_intake_bar_enabled_set(lv_obj_has_state(sw, LV_STATE_CHECKED));
    (void)lv_timer_create(_vent_rebuild_timer_cb, 1, NULL);
}

static void _air_safeguard_enabled_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* sw = lv_event_get_target(e);
    ui_air_safeguard_enabled_set(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void _air_safeguard_temp_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d C", val);
    lv_label_set_text(lbl, buf);
    ui_air_safeguard_temp_max_set(val);
}

static void _air_safeguard_hum_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_air_safeguard_humidity_max_set(val);
}

static int _vent_step_count_to_index(int steps) {
    switch (steps) {
        case 2: return 1;
        case 3: return 2;
        case 5: return 3;
        case 7: return 4;
        case 10: return 5;
        default: return 0;
    }
}

static int _vent_step_index_to_count(int idx) {
    static const int k_steps[] = {0, 2, 3, 5, 7, 10};
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return k_steps[idx];
}

static void _vent_step_mode_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* dd = lv_event_get_target(e);
    ui_ventilation_step_count_set(_vent_step_index_to_count((int)lv_dropdown_get_selected(dd)));
}

static int _screensaver_minutes_to_index(int minutes) {
    switch (minutes) {
        case 3: return 0;
        case 5: return 1;
        case 10: return 2;
        case 15: return 3;
        default: return 1;
    }
}

static int _screensaver_index_to_minutes(int index) {
    static const int k_minutes[4] = {3, 5, 10, 15};
    if (index < 0) index = 0;
    if (index > 3) index = 3;
    return k_minutes[index];
}

static void _screensaver_cb(lv_event_t* e) {
    lv_obj_t* sl = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int idx = (int)lv_slider_get_value(sl);
    const int minutes = _screensaver_index_to_minutes(idx);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d min", minutes);
    lv_label_set_text(lbl, buf);
    ui_screensaver_minutes_set(minutes);
}

static void make_brightness_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Luminosita");

    lv_obj_t* pct = lv_label_create(row);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 5, 100);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _brightness_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_vent_min_speed_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Velocita minima motore");

    lv_obj_t* pct = lv_label_create(row);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 0, 90);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _vent_min_speed_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_vent_max_speed_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Velocita massima motore");

    lv_obj_t* pct = lv_label_create(row);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 10, 100);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _vent_max_speed_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_vent_intake_percentage_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Percentuale Immissione");

    lv_obj_t* pct = lv_label_create(row);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 25, 90);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _vent_intake_percentage_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_air_safeguard_temp_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Temperatura max G1");

    lv_obj_t* pct = lv_label_create(row);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d C", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 30, 120);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _air_safeguard_temp_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_air_safeguard_hum_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Umidita max G1");

    lv_obj_t* pct = lv_label_create(row);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 40, 100);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _air_safeguard_hum_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void make_screensaver_row(lv_obj_t* parent, int minutes) {
    lv_obj_t* row = make_row(parent, "Tempo Screensaver");

    lv_obj_t* val = lv_label_create(row);
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%d min", minutes);
    lv_label_set_text(val, buf);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, ST_ORANGE, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 0, 3);
    lv_slider_set_value(sl, _screensaver_minutes_to_index(minutes), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _screensaver_cb, LV_EVENT_VALUE_CHANGED, val);
}

static lv_obj_t* make_switch_row(lv_obj_t* parent, const char* label_text, bool on) {
    const uint32_t sel = (uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED;
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 56, 28);
    lv_obj_set_style_bg_color(sw, ST_ORANGE, sel);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    return sw;
}

static void make_info_row(lv_obj_t* parent, const char* label_text, const char* value_text) {
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, value_text);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, ST_DIM, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
}

static lv_obj_t* make_action_row_button(lv_obj_t* parent, const char* label_text, const char* btn_text) {
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* btn = lv_btn_create(row);
    lv_obj_set_size(btn, 170, 38);
    lv_obj_set_style_bg_color(btn, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(btn, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, btn_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    return btn;
}

static void make_action_row(lv_obj_t* parent, const char* label_text, const char* btn_text) {
    (void)make_action_row_button(parent, label_text, btn_text);
}

static void style_dropdown_soft_list(lv_obj_t* dd) {
    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (!list) return;

    lv_obj_set_style_bg_color(list, ST_WHITE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, ST_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(list, ST_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, LV_PART_MAIN);

    lv_obj_set_style_bg_color(list, lv_color_hex(0xEAF0F7), LV_PART_SELECTED);
    lv_obj_set_style_text_color(list, ST_TEXT, LV_PART_SELECTED);
}

static lv_obj_t* make_dropdown_row(lv_obj_t* parent, const char* label_text,
                                   const char* options, int sel) {
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* dd = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, (uint16_t)sel);
    lv_obj_set_size(dd, 200, 38);
    lv_obj_set_style_bg_color(dd, ST_WHITE, 0);
    lv_obj_set_style_border_color(dd, ST_BORDER, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_16, 0);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    style_dropdown_soft_list(dd);
    return dd;
}

static lv_obj_t* make_value_button_row(lv_obj_t* parent, const char* label_text,
                                       const char* value_text, lv_obj_t** out_btn,
                                       lv_obj_t** out_lbl) {
    lv_obj_t* row = make_row(parent, label_text);

    lv_obj_t* btn = lv_btn_create(row);
    lv_obj_set_size(btn, 220, 38);
    lv_obj_set_style_bg_color(btn, ST_WHITE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xEFF3F8), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, ST_LEFT_BG, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, ST_BORDER, 0);
    lv_obj_set_style_border_color(btn, ST_BORDER, LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, value_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
    lv_obj_set_style_text_color(lbl, ST_DIM, LV_STATE_DISABLED);
    lv_obj_center(lbl);

    if (out_btn) *out_btn = btn;
    if (out_lbl) *out_lbl = lbl;
    return row;
}

static lv_obj_t* make_scroll_list(lv_obj_t* parent) {
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return list;
}

static void make_placeholder(lv_obj_t* parent, const char* msg) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl, ST_DIM, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
}

// Date/time section logic
static void _clear_datetime_state() {
    if (s_dt_timer) {
        lv_timer_del(s_dt_timer);
        s_dt_timer = NULL;
    }
    s_dt_row_auto = NULL;
    s_dt_row_tz = NULL;
    s_dt_row_time = NULL;
    s_dt_row_date = NULL;
    s_dt_auto_sw = NULL;
    s_dt_tz_dd = NULL;
    s_dt_time_btn = NULL;
    s_dt_date_btn = NULL;
    s_dt_time_lbl = NULL;
    s_dt_date_lbl = NULL;
}

static void _refresh_datetime_value_labels() {
    if (s_dt_time_lbl) {
        char time_buf[16];
        ui_dc_clock_format_time_hms(time_buf, sizeof(time_buf));
        lv_label_set_text(s_dt_time_lbl, time_buf);
    }
    if (s_dt_date_lbl) {
        char date_buf[20];
        ui_dc_clock_format_date_numeric(date_buf, sizeof(date_buf));
        lv_label_set_text(s_dt_date_lbl, date_buf);
    }
}

static void _update_datetime_controls_state() {
    const bool has_rtc = ui_dc_clock_has_rtc();
    const bool auto_on = ui_dc_clock_is_auto_enabled();

    if (s_dt_auto_sw) {
        if (auto_on) lv_obj_add_state(s_dt_auto_sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_dt_auto_sw, LV_STATE_CHECKED);
        _set_control_enabled(s_dt_auto_sw, has_rtc);
    }

    _set_control_enabled(s_dt_tz_dd, has_rtc && auto_on);
    _set_control_enabled(s_dt_time_btn, !auto_on);
    _set_control_enabled(s_dt_date_btn, !auto_on);

    _set_row_enabled(s_dt_row_auto, has_rtc);
    _set_row_enabled(s_dt_row_tz, has_rtc && auto_on);
    _set_row_enabled(s_dt_row_time, !auto_on);
    _set_row_enabled(s_dt_row_date, !auto_on);
}

struct DtPopupCtx {
    lv_obj_t* mask;
    lv_obj_t* r1;
    lv_obj_t* r2;
    lv_obj_t* r3;
    bool is_date;
};

static void _popup_mask_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    DtPopupCtx* ctx = static_cast<DtPopupCtx*>(lv_event_get_user_data(e));
    delete ctx;
}

static void _popup_close(DtPopupCtx* ctx) {
    if (!ctx) return;
    if (ctx->mask) lv_obj_del(ctx->mask);
}

static void _popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    DtPopupCtx* ctx = static_cast<DtPopupCtx*>(lv_event_get_user_data(e));
    _popup_close(ctx);
}

static void _popup_save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    DtPopupCtx* ctx = static_cast<DtPopupCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;

    struct tm tm_now = {};
    if (!ui_dc_clock_get_local_tm(&tm_now)) {
        _popup_close(ctx);
        return;
    }

    bool ok = false;
    if (ctx->is_date) {
        int day = (int)lv_roller_get_selected(ctx->r1) + 1;
        int month = (int)lv_roller_get_selected(ctx->r2) + 1;
        int year = 2020 + (int)lv_roller_get_selected(ctx->r3);
        const int max_day = _days_in_month(year, month);
        if (day > max_day) day = max_day;
        ok = ui_dc_clock_set_manual_local(year, month, day,
                                          tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
    } else {
        int hour = (int)lv_roller_get_selected(ctx->r1);
        int minute = (int)lv_roller_get_selected(ctx->r2);
        ok = ui_dc_clock_set_manual_local(tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                                          hour, minute, 0);
    }

    if (ok) {
        _refresh_datetime_value_labels();
        _update_datetime_controls_state();
    }
    _popup_close(ctx);
}

static void _open_datetime_popup(bool is_date) {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) return;

    DtPopupCtx* ctx = new DtPopupCtx();
    if (!ctx) return;
    ctx->mask = NULL;
    ctx->r1 = NULL;
    ctx->r2 = NULL;
    ctx->r3 = NULL;
    ctx->is_date = is_date;

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    lv_obj_add_event_cb(mask, _popup_mask_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(mask);
    lv_obj_set_size(card, is_date ? 560 : 460, 310);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, ST_WHITE, 0);
    lv_obj_set_style_border_color(card, ST_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, is_date ? "Imposta Data" : "Imposta Ora");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, ST_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    struct tm tm_now = {};
    if (!ui_dc_clock_get_local_tm(&tm_now)) {
        tm_now.tm_year = 126;
        tm_now.tm_mon = 0;
        tm_now.tm_mday = 1;
        tm_now.tm_hour = 0;
        tm_now.tm_min = 0;
    }

    char opts_a[256];
    char opts_b[256];
    char opts_c[512];

    if (is_date) {
        _build_numeric_options(1, 31, opts_a, sizeof(opts_a), true);
        _build_numeric_options(1, 12, opts_b, sizeof(opts_b), true);
        _build_numeric_options(2020, 2099, opts_c, sizeof(opts_c), false);
    } else {
        _build_numeric_options(0, 23, opts_a, sizeof(opts_a), true);
        _build_numeric_options(0, 59, opts_b, sizeof(opts_b), true);
        opts_c[0] = '\0';
    }

    const int roller_y = 68;
    const int roller_w = 110;
    const int roller_h = 130;

    lv_obj_t* r1 = lv_roller_create(card);
    lv_roller_set_options(r1, opts_a, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(r1, roller_w, roller_h);
    lv_obj_align(r1, LV_ALIGN_TOP_LEFT, is_date ? 40 : 90, roller_y);
    lv_roller_set_selected(r1, is_date ? (uint16_t)(tm_now.tm_mday - 1) : (uint16_t)tm_now.tm_hour, LV_ANIM_OFF);

    lv_obj_t* r2 = lv_roller_create(card);
    lv_roller_set_options(r2, opts_b, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(r2, roller_w, roller_h);
    lv_obj_align(r2, LV_ALIGN_TOP_LEFT, is_date ? 185 : 255, roller_y);
    lv_roller_set_selected(r2, is_date ? (uint16_t)tm_now.tm_mon : (uint16_t)tm_now.tm_min, LV_ANIM_OFF);

    lv_obj_t* r3 = NULL;
    if (is_date) {
        r3 = lv_roller_create(card);
        lv_roller_set_options(r3, opts_c, LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(r3, 140, roller_h);
        lv_obj_align(r3, LV_ALIGN_TOP_LEFT, 330, roller_y);
        int year_idx = (tm_now.tm_year + 1900) - 2020;
        if (year_idx < 0) year_idx = 0;
        if (year_idx > 79) year_idx = 79;
        lv_roller_set_selected(r3, (uint16_t)year_idx, LV_ANIM_OFF);
    }

    ctx->r1 = r1;
    ctx->r2 = r2;
    ctx->r3 = r3;

    lv_obj_t* btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, 160, 42);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 24, -16);
    lv_obj_set_style_bg_color(btn_cancel, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(btn_cancel, ST_BORDER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, _popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, ST_TEXT, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_save = lv_btn_create(card);
    lv_obj_set_size(btn_save, 160, 42);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -24, -16);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, _popup_save_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Salva");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);
}

static void _dt_time_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_dc_clock_is_auto_enabled()) return;
    _open_datetime_popup(false);
}

static void _dt_date_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (ui_dc_clock_is_auto_enabled()) return;
    _open_datetime_popup(true);
}

static void _dt_auto_switch_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* sw = lv_event_get_target(e);
    const bool checked = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_dc_clock_set_auto_enabled(checked);
    _update_datetime_controls_state();
    _refresh_datetime_value_labels();
}

static void _dt_timezone_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* dd = lv_event_get_target(e);
    const int idx = (int)lv_dropdown_get_selected(dd);
    ui_dc_clock_timezone_index_set(idx);
    _refresh_datetime_value_labels();
}

static void _dt_timer_cb(lv_timer_t* /*t*/) {
    _refresh_datetime_value_labels();
}

static void _temp_unit_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t* cont = lv_event_get_target(e);
    const int idx = (int)(uintptr_t)lv_obj_get_user_data(cont);
    ui_temperature_unit_set(idx == 1 ? UI_TEMP_F : UI_TEMP_C);
}

static void _refresh_plant_name_row() {
    if (!s_plant_name_lbl) return;

    char plant_name[48];
    ui_plant_name_get(plant_name, sizeof(plant_name));
    lv_label_set_text(s_plant_name_lbl, plant_name);
}

static void _plant_popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    PlantNamePopupCtx* ctx = static_cast<PlantNamePopupCtx*>(lv_event_get_user_data(e));
    if (ctx == s_plant_popup_ctx) {
        s_plant_popup_ctx = NULL;
    }
    delete ctx;
}

static void _plant_popup_close(PlantNamePopupCtx* ctx) {
    if (!ctx || !ctx->mask) return;
    lv_obj_del(ctx->mask);
}

static void _plant_popup_submit(PlantNamePopupCtx* ctx) {
    if (!ctx || !ctx->ta) return;
    ui_plant_name_set(lv_textarea_get_text(ctx->ta));
    _refresh_plant_name_row();
    _plant_popup_close(ctx);
}

static void _plant_popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    PlantNamePopupCtx* ctx = static_cast<PlantNamePopupCtx*>(lv_event_get_user_data(e));
    _plant_popup_close(ctx);
}

static void _plant_popup_save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    PlantNamePopupCtx* ctx = static_cast<PlantNamePopupCtx*>(lv_event_get_user_data(e));
    _plant_popup_submit(ctx);
}

static void _plant_popup_keyboard_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    PlantNamePopupCtx* ctx = static_cast<PlantNamePopupCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_READY) {
        _plant_popup_submit(ctx);
    } else if (code == LV_EVENT_CANCEL) {
        _plant_popup_close(ctx);
    }
}

static void _open_plant_name_popup() {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) return;

    if (s_plant_popup_ctx && s_plant_popup_ctx->mask) {
        lv_obj_del(s_plant_popup_ctx->mask);
    }
    if (s_api_popup_ctx && s_api_popup_ctx->mask) {
        lv_obj_del(s_api_popup_ctx->mask);
    }

    PlantNamePopupCtx* ctx = new PlantNamePopupCtx();
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    s_plant_popup_ctx = ctx;
    lv_obj_add_event_cb(mask, _plant_popup_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* top = lv_obj_create(mask);
    lv_obj_set_size(top, 1024, 128);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, ST_WHITE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 20, 0);
    lv_obj_set_style_pad_right(top, 20, 0);
    lv_obj_set_style_pad_top(top, 14, 0);
    lv_obj_set_style_pad_bottom(top, 14, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, "Nome Impianto");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, ST_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(top);
    lv_label_set_text(hint, "Questo nome viene mostrato nella home quando sono presenti dispositivi.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, ST_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t* btn_cancel = lv_btn_create(top);
    lv_obj_set_size(btn_cancel, 150, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -190, 0);
    lv_obj_set_style_bg_color(btn_cancel, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(btn_cancel, ST_BORDER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, _plant_popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, ST_TEXT, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_save = lv_btn_create(top);
    lv_obj_set_size(btn_save, 170, 44);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, _plant_popup_save_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Salva");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);

    lv_obj_t* ta = lv_textarea_create(top);
    ctx->ta = ta;
    lv_obj_set_size(ta, 984, 56);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, 47);
    lv_textarea_set_placeholder_text(ta, "Il mio Impianto");

    char plant_name[48];
    ui_plant_name_get(plant_name, sizeof(plant_name));
    lv_textarea_set_text(ta, plant_name);

    lv_obj_t* kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 1024, 472);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, ST_LEFT_BG, 0);
    lv_obj_set_style_anim_time(kb, 0, LV_PART_ITEMS);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, _plant_popup_keyboard_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, _plant_popup_keyboard_cb, LV_EVENT_CANCEL, ctx);

    lv_obj_move_foreground(mask);
}

static void _plant_name_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _open_plant_name_popup();
}

static String _api_user_url_label() {
    DisplayApiConfig cfg = displayApiLoadConfig();
    cfg.customerUrl.trim();
    return cfg.customerUrl.length() > 0 ? cfg.customerUrl : "NON IMPOSTATO";
}

static String _api_user_key_label() {
    DisplayApiConfig cfg = displayApiLoadConfig();
    cfg.customerKey.trim();
    return cfg.customerKey.length() > 0 ? "Impostata" : "NON IMPOSTATA";
}

static void _api_popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    ApiTextPopupCtx* ctx = static_cast<ApiTextPopupCtx*>(lv_event_get_user_data(e));
    if (ctx == s_api_popup_ctx) {
        s_api_popup_ctx = NULL;
    }
    delete ctx;
}

static void _api_popup_close(ApiTextPopupCtx* ctx) {
    if (!ctx || !ctx->mask) return;
    lv_obj_del(ctx->mask);
}

static void _api_popup_submit(ApiTextPopupCtx* ctx) {
    if (!ctx || !ctx->ta) return;
    const String value = String(lv_textarea_get_text(ctx->ta));
    if (strcmp(ctx->pref_key, "custApiUrl") == 0) {
        displayApiSetCustomerUrl(value);
        if (ctx->value_lbl) {
            const String label = _api_user_url_label();
            lv_label_set_text(ctx->value_lbl, label.c_str());
        }
    } else if (strcmp(ctx->pref_key, "custApiKey") == 0) {
        displayApiSetCustomerKey(value);
        if (ctx->value_lbl) {
            const String label = _api_user_key_label();
            lv_label_set_text(ctx->value_lbl, label.c_str());
        }
    }
    _api_popup_close(ctx);
}

static void _api_popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ApiTextPopupCtx* ctx = static_cast<ApiTextPopupCtx*>(lv_event_get_user_data(e));
    _api_popup_close(ctx);
}

static void _api_popup_save_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ApiTextPopupCtx* ctx = static_cast<ApiTextPopupCtx*>(lv_event_get_user_data(e));
    _api_popup_submit(ctx);
}

static void _api_popup_keyboard_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    ApiTextPopupCtx* ctx = static_cast<ApiTextPopupCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_READY) {
        _api_popup_submit(ctx);
    } else if (code == LV_EVENT_CANCEL) {
        _api_popup_close(ctx);
    }
}

static void _api_popup_eye_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ApiTextPopupCtx* ctx = static_cast<ApiTextPopupCtx*>(lv_event_get_user_data(e));
    ctx->pw_visible = !ctx->pw_visible;
    lv_textarea_set_password_mode(ctx->ta, !ctx->pw_visible);
    lv_obj_t* lbl = lv_obj_get_child(ctx->eye_btn, 0);
    lv_label_set_text(lbl, ctx->pw_visible ? "Nascondi" : "Mostra");
}

static void _open_api_text_popup(const char* title, const char* placeholder,
                                 const char* initial_value, const char* pref_key,
                                 bool key_field, lv_obj_t* value_lbl) {
    lv_obj_t* parent = lv_scr_act();
    if (!parent || !pref_key) return;

    if (s_api_popup_ctx && s_api_popup_ctx->mask) {
        lv_obj_del(s_api_popup_ctx->mask);
    }

    ApiTextPopupCtx* ctx = new ApiTextPopupCtx();
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->pref_key, pref_key, sizeof(ctx->pref_key) - 1);
    ctx->value_lbl = value_lbl;
    ctx->key_field = key_field;

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    s_api_popup_ctx = ctx;
    lv_obj_add_event_cb(mask, _api_popup_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* top = lv_obj_create(mask);
    lv_obj_set_size(top, 1024, 128);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, ST_WHITE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 20, 0);
    lv_obj_set_style_pad_right(top, 20, 0);
    lv_obj_set_style_pad_top(top, 14, 0);
    lv_obj_set_style_pad_bottom(top, 14, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_lbl = lv_label_create(top);
    lv_label_set_text(title_lbl, title ? title : "Imposta API");
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title_lbl, ST_TEXT, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* btn_cancel = lv_btn_create(top);
    lv_obj_set_size(btn_cancel, 150, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -190, 0);
    lv_obj_set_style_bg_color(btn_cancel, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(btn_cancel, ST_BORDER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, _api_popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, ST_TEXT, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_save = lv_btn_create(top);
    lv_obj_set_size(btn_save, 170, 44);
    lv_obj_align(btn_save, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_save, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_save, 0, 0);
    lv_obj_set_style_shadow_width(btn_save, 0, 0);
    lv_obj_add_event_cb(btn_save, _api_popup_save_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "Salva");
    lv_obj_set_style_text_color(lbl_save, lv_color_white(), 0);
    lv_obj_center(lbl_save);

    lv_obj_t* ta = lv_textarea_create(top);
    ctx->ta = ta;
    lv_obj_set_size(ta, key_field ? 896 : 984, 56);
    lv_obj_align(ta, key_field ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, key_field ? 64 : 127);
    lv_textarea_set_password_mode(ta, key_field);
    lv_textarea_set_password_show_time(ta, 0);
    lv_textarea_set_placeholder_text(ta, placeholder ? placeholder : "");
    if (initial_value && initial_value[0] != '\0') {
        lv_textarea_set_text(ta, initial_value);
    }

    if (key_field) {
        lv_obj_t* eye_btn = lv_btn_create(top);
        ctx->eye_btn = eye_btn;
        lv_obj_set_size(eye_btn, 84, 44);
        lv_obj_align(eye_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(eye_btn, ST_LEFT_BG, 0);
        lv_obj_set_style_bg_color(eye_btn, ST_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(eye_btn, ST_BORDER, 0);
        lv_obj_set_style_border_width(eye_btn, 1, 0);
        lv_obj_set_style_shadow_width(eye_btn, 0, 0);
        lv_obj_add_event_cb(eye_btn, _api_popup_eye_cb, LV_EVENT_CLICKED, ctx);
        lv_obj_t* eye_lbl = lv_label_create(eye_btn);
        lv_label_set_text(eye_lbl, "Mostra");
        lv_obj_set_style_text_color(eye_lbl, ST_TEXT, 0);
        lv_obj_set_style_text_font(eye_lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(eye_lbl);
    }

    lv_obj_t* kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 1024, 472);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, ST_LEFT_BG, 0);
    lv_obj_set_style_anim_time(kb, 0, LV_PART_ITEMS);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, _api_popup_keyboard_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, _api_popup_keyboard_cb, LV_EVENT_CANCEL, ctx);

    lv_obj_move_foreground(mask);
}

static void _api_customer_url_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* value_lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    DisplayApiConfig cfg = displayApiLoadConfig();
    _open_api_text_popup("URL API Utente", "https://.../api.php",
                         cfg.customerUrl.c_str(), "custApiUrl", false, value_lbl);
}

static void _api_customer_key_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* value_lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    DisplayApiConfig cfg = displayApiLoadConfig();
    _open_api_text_popup("API Key Utente", "Chiave API Utente",
                         cfg.customerKey.c_str(), "custApiKey", true, value_lbl);
}

static bool _wifi_pref_enabled_get() {
    Preferences pref;
    if (!pref.begin("easy", true)) return false;
    const bool enabled = pref.getBool("dc_wifi_enabled", false);
    pref.end();
    return enabled;
}

static void _wifi_pref_enabled_set(bool enabled) {
    Preferences pref;
    if (!pref.begin("easy", false)) return;
    pref.putBool("dc_wifi_enabled", enabled);
    pref.end();
}

static void _wifi_pref_load_credentials(String& ssid, String& pass) {
    Preferences pref;
    ssid = "";
    pass = "";
    if (!pref.begin("easy", true)) return;
    ssid = pref.getString("ssid", "");
    pass = pref.getString("pass", "");
    pref.end();
}

static void _wifi_pref_save_credentials(const char* ssid, const char* pass) {
    Preferences pref;
    if (!pref.begin("easy", false)) return;
    pref.putString("ssid", ssid ? ssid : "");
    pref.putString("pass", pass ? pass : "");
    pref.end();
}

static void _wifi_resume_saved_connection_after_scan() {
    if (!s_wifi_sw || !lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED)) return;
    if (s_wifi_connect_pending) return;

    String saved_ssid;
    String saved_pass;
    _wifi_pref_load_credentials(saved_ssid, saved_pass);
    if (saved_ssid.length() == 0) {
        s_wifi_waiting_for_selection = true;
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        return;
    }

    s_wifi_target_ssid = saved_ssid;
    s_wifi_waiting_for_selection = true;
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    Serial.printf("[WIFI] Scansione completata. Rete salvata='%s'. In attesa scelta utente.\n",
                  saved_ssid.c_str());
}

static void _wifi_update_status_labels() {
    if (!s_wifi_status_lbl || !s_wifi_ip_lbl) return;

    if (!s_wifi_sw || !lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED) || WiFi.getMode() == WIFI_OFF) {
        lv_label_set_text(s_wifi_status_lbl, "Disattivato");
        lv_label_set_text(s_wifi_ip_lbl, "--");
        return;
    }

    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        const String txt = "Connesso a " + WiFi.SSID();
        lv_label_set_text(s_wifi_status_lbl, txt.c_str());
        lv_label_set_text(s_wifi_ip_lbl, WiFi.localIP().toString().c_str());
        return;
    }

    if (s_wifi_scan_pending) {
        lv_label_set_text(s_wifi_status_lbl, "Scansione in corso...");
    } else if (s_wifi_waiting_for_selection) {
        lv_label_set_text(s_wifi_status_lbl, "Scegli una rete WiFi");
    } else if (st == WL_CONNECT_FAILED) {
        lv_label_set_text(s_wifi_status_lbl, "Connessione fallita");
    } else if (st == WL_NO_SSID_AVAIL) {
        lv_label_set_text(s_wifi_status_lbl, "Rete non disponibile");
    } else {
        if (s_wifi_target_ssid.length() > 0) {
            const String txt = "Connessione in corso a " + s_wifi_target_ssid + "...";
            lv_label_set_text(s_wifi_status_lbl, txt.c_str());
        } else {
            lv_label_set_text(s_wifi_status_lbl, "Non connesso");
        }
    }
    lv_label_set_text(s_wifi_ip_lbl, "--");
}

static void _wifi_render_network_list();
static void _wifi_start_scan();
static void _wifi_connect_start(const char* ssid, const char* pass);

static void _wifi_popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    WifiPasswordPopupCtx* ctx = static_cast<WifiPasswordPopupCtx*>(lv_event_get_user_data(e));
    if (ctx == s_wifi_popup_ctx) {
        s_wifi_popup_ctx = NULL;
    }
    delete ctx;
}

static void _wifi_popup_close(WifiPasswordPopupCtx* ctx) {
    if (!ctx || !ctx->mask) return;
    lv_obj_del(ctx->mask);
}

static void _wifi_popup_submit(WifiPasswordPopupCtx* ctx) {
    if (!ctx || !ctx->ta) return;
    const char* pass = lv_textarea_get_text(ctx->ta);
    _wifi_connect_start(ctx->ssid, pass);
    _wifi_popup_close(ctx);
}

static void _wifi_popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    WifiPasswordPopupCtx* ctx = static_cast<WifiPasswordPopupCtx*>(lv_event_get_user_data(e));
    _wifi_popup_close(ctx);
}

static void _wifi_popup_connect_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    WifiPasswordPopupCtx* ctx = static_cast<WifiPasswordPopupCtx*>(lv_event_get_user_data(e));
    _wifi_popup_submit(ctx);
}

static void _wifi_popup_keyboard_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    WifiPasswordPopupCtx* ctx = static_cast<WifiPasswordPopupCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_READY) {
        _wifi_popup_submit(ctx);
    } else if (code == LV_EVENT_CANCEL) {
        _wifi_popup_close(ctx);
    }
}

static void _wifi_popup_eye_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    WifiPasswordPopupCtx* ctx = static_cast<WifiPasswordPopupCtx*>(lv_event_get_user_data(e));
    ctx->pw_visible = !ctx->pw_visible;
    lv_textarea_set_password_mode(ctx->ta, !ctx->pw_visible);
    lv_obj_t* lbl = lv_obj_get_child(ctx->eye_btn, 0);
    lv_label_set_text(lbl, ctx->pw_visible ? "Nascondi" : "Mostra");
}

static void _wifi_open_password_popup(const char* ssid) {
    lv_obj_t* parent = lv_scr_act();
    if (!parent || !ssid || ssid[0] == '\0') return;

    if (s_wifi_popup_ctx && s_wifi_popup_ctx->mask) {
        lv_obj_del(s_wifi_popup_ctx->mask);
    }
    if (s_api_popup_ctx && s_api_popup_ctx->mask) {
        lv_obj_del(s_api_popup_ctx->mask);
    }

    WifiPasswordPopupCtx* ctx = new WifiPasswordPopupCtx();
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->ssid, ssid, sizeof(ctx->ssid) - 1);

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    s_wifi_popup_ctx = ctx;
    lv_obj_add_event_cb(mask, _wifi_popup_delete_cb, LV_EVENT_DELETE, ctx);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* top = lv_obj_create(mask);
    lv_obj_set_size(top, 1024, 128);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, ST_WHITE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 20, 0);
    lv_obj_set_style_pad_right(top, 20, 0);
    lv_obj_set_style_pad_top(top, 14, 0);
    lv_obj_set_style_pad_bottom(top, 14, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, "Inserisci Password WiFi");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, ST_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* ssid_lbl = lv_label_create(top);
    const String ssid_text = String("Rete: ") + ctx->ssid;
    lv_label_set_text(ssid_lbl, ssid_text.c_str());
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_lbl, ST_DIM, 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t* btn_cancel = lv_btn_create(top);
    lv_obj_set_size(btn_cancel, 150, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -190, 0);
    lv_obj_set_style_bg_color(btn_cancel, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(btn_cancel, ST_BORDER, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_add_event_cb(btn_cancel, _wifi_popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, ST_TEXT, 0);
    lv_obj_center(lbl_cancel);

    lv_obj_t* btn_connect = lv_btn_create(top);
    lv_obj_set_size(btn_connect, 170, 44);
    lv_obj_align(btn_connect, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_connect, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_connect, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_connect, 0, 0);
    lv_obj_set_style_shadow_width(btn_connect, 0, 0);
    lv_obj_add_event_cb(btn_connect, _wifi_popup_connect_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, "Connetti");
    lv_obj_set_style_text_color(lbl_connect, lv_color_white(), 0);
    lv_obj_center(lbl_connect);

    lv_obj_t* ta = lv_textarea_create(top);
    ctx->ta = ta;
    lv_obj_set_size(ta, 896, 56);
    lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_20, 0);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_password_show_time(ta, 0);
    lv_textarea_set_placeholder_text(ta, "Password rete WiFi");

    lv_obj_t* eye_btn = lv_btn_create(top);
    ctx->eye_btn = eye_btn;
    lv_obj_set_size(eye_btn, 84, 44);
    lv_obj_align(eye_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(eye_btn, ST_LEFT_BG, 0);
    lv_obj_set_style_bg_color(eye_btn, ST_BORDER, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(eye_btn, ST_BORDER, 0);
    lv_obj_set_style_border_width(eye_btn, 1, 0);
    lv_obj_set_style_shadow_width(eye_btn, 0, 0);
    lv_obj_add_event_cb(eye_btn, _wifi_popup_eye_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* eye_lbl = lv_label_create(eye_btn);
    lv_label_set_text(eye_lbl, "Mostra");
    lv_obj_set_style_text_color(eye_lbl, ST_TEXT, 0);
    lv_obj_set_style_text_font(eye_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(eye_lbl);

    String saved_ssid;
    String saved_pass;
    _wifi_pref_load_credentials(saved_ssid, saved_pass);
    if (saved_ssid == String(ctx->ssid) && saved_pass.length() > 0) {
        lv_textarea_set_text(ta, saved_pass.c_str());
    }

    lv_obj_t* kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 1024, 472);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(kb, ST_LEFT_BG, 0);
    lv_obj_set_style_anim_time(kb, 0, LV_PART_ITEMS);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_event_cb(kb, _wifi_popup_keyboard_cb, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, _wifi_popup_keyboard_cb, LV_EVENT_CANCEL, ctx);

    lv_obj_move_foreground(mask);
}

static void _wifi_network_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_wifi_scan_count) return;
    if (!s_wifi_scan_entries[idx].secure) {
        _wifi_connect_start(s_wifi_scan_entries[idx].ssid.c_str(), "");
        return;
    }
    _wifi_open_password_popup(s_wifi_scan_entries[idx].ssid.c_str());
}

static void _wifi_render_network_list() {
    if (!s_wifi_list) return;
    lv_obj_clean(s_wifi_list);

    if (!s_wifi_sw || !lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED)) {
        lv_obj_t* lbl = lv_label_create(s_wifi_list);
        lv_label_set_text(lbl, "Attiva il WiFi per cercare le reti disponibili.");
        lv_obj_set_style_text_color(lbl, ST_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
        return;
    }

    if (s_wifi_scan_pending) {
        lv_obj_t* lbl = lv_label_create(s_wifi_list);
        lv_label_set_text(lbl, "Scansione reti in corso...");
        lv_obj_set_style_text_color(lbl, ST_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
        return;
    }

    if (s_wifi_scan_count <= 0) {
        lv_obj_t* lbl = lv_label_create(s_wifi_list);
        const char* msg;
        if (s_wifi_scan_error) {
            msg = "Errore scansione. Premi Aggiorna per riprovare.";
        } else if (WiFi.status() == WL_CONNECTED) {
            msg = "Premi Aggiorna per cercare altre reti.";
        } else {
            msg = "Nessuna rete trovata.";
        }
        lv_label_set_text(lbl, msg);
        lv_obj_set_style_text_color(lbl, ST_DIM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
        return;
    }

    for (int i = 0; i < s_wifi_scan_count; i++) {
        lv_obj_t* btn = lv_btn_create(s_wifi_list);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, 52);
        lv_obj_set_style_bg_color(btn, ST_WHITE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xEFF3F8), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, ST_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_left(btn, 14, 0);
        lv_obj_set_style_pad_right(btn, 14, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, _wifi_network_btn_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        const String left_text = s_wifi_scan_entries[i].secure
            ? s_wifi_scan_entries[i].ssid + " (protetta)"
            : s_wifi_scan_entries[i].ssid;

        lv_obj_t* lbl_name = lv_label_create(btn);
        lv_label_set_text(lbl_name, left_text.c_str());
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl_name, ST_TEXT, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t* lbl_rssi = lv_label_create(btn);
        char rssi_buf[16];
        lv_snprintf(rssi_buf, sizeof(rssi_buf), "%d dBm", (int)s_wifi_scan_entries[i].rssi);
        lv_label_set_text(lbl_rssi, rssi_buf);
        lv_obj_set_style_text_font(lbl_rssi, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_rssi, ST_DIM, 0);
        lv_obj_align(lbl_rssi, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void _wifi_scan_timer_cb(lv_timer_t* /*t*/) {
    if (!s_wifi_scan_pending) return;
    const int16_t found = WiFi.scanComplete();
    if (found == WIFI_SCAN_RUNNING) return;

    const unsigned long elapsed = millis() - s_wifi_scan_started_ms;
    Serial.printf("[WIFI-DIAG] scanComplete result=%d (%s) elapsed=%lu ms\n",
                  (int)found,
                  found == WIFI_SCAN_FAILED ? "FAILED" : "DONE",
                  elapsed);
    _wifi_log_state(found == WIFI_SCAN_FAILED ? "scan-complete-failed" : "scan-complete-done");

    s_wifi_scan_pending = false;
    _wifi_display_guard_refresh();
    s_wifi_scan_count = 0;

    if (found == WIFI_SCAN_FAILED) {
        s_wifi_scan_error = true;
        WiFi.scanDelete();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        if (s_wifi_scan_lbl) lv_label_set_text(s_wifi_scan_lbl, "Errore scansione");
        _wifi_update_status_labels();
        _wifi_render_network_list();
        return;
    }

    s_wifi_scan_error = false;
    if (found > 0) {
        for (int i = 0; i < found && s_wifi_scan_count < k_wifi_max_networks; i++) {
            String ssid = WiFi.SSID(i);
            ssid.trim();
            if (ssid.length() == 0) continue;

            bool duplicated = false;
            for (int j = 0; j < s_wifi_scan_count; j++) {
                if (s_wifi_scan_entries[j].ssid == ssid) {
                    duplicated = true;
                    break;
                }
            }
            if (duplicated) continue;

            s_wifi_scan_entries[s_wifi_scan_count].ssid = ssid;
            s_wifi_scan_entries[s_wifi_scan_count].rssi = WiFi.RSSI(i);
            s_wifi_scan_entries[s_wifi_scan_count].secure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            s_wifi_scan_count++;
        }
    }
    Serial.printf("[WIFI-DIAG] scan parsed raw=%d visible_unique=%d\n",
                  (int)found,
                  s_wifi_scan_count);
    WiFi.scanDelete();
    _wifi_resume_saved_connection_after_scan();

    if (s_wifi_scan_lbl) {
        if (s_wifi_scan_count > 0) {
            char msg[48];
            if (s_wifi_waiting_for_selection) {
                lv_snprintf(msg, sizeof(msg), "%d reti trovate - scegli rete", s_wifi_scan_count);
            } else {
                lv_snprintf(msg, sizeof(msg), "%d reti trovate", s_wifi_scan_count);
            }
            lv_label_set_text(s_wifi_scan_lbl, msg);
        } else {
            lv_label_set_text(s_wifi_scan_lbl, s_wifi_waiting_for_selection
                ? "Rete salvata non trovata"
                : "Nessuna rete trovata");
        }
    }
    _wifi_update_status_labels();
    _wifi_render_network_list();
}

static void _wifi_start_scan() {
    if (!s_wifi_sw || !lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED)) return;

    // Interrompe il tentativo di connessione automatica al boot solo se non già connessi,
    // per evitare conflitti con la scansione e possibili crash di memoria.
    if (WiFi.status() != WL_CONNECTED) {
        wifi_boot_abort();
    }

    Serial.println("[WIFI-DIAG] scan request from UI");
    _wifi_log_state("scan-request");

    // Se l'API sta inviando dati, aspettiamo che finisca per evitare conflitti WiFi.
    if (displayApiIsBusy()) {
        Serial.println("[WIFI-DIAG] Display API busy before scan, waiting up to 4500 ms");
        const unsigned long wait_start = millis();
        for (int i = 0; i < 90 && displayApiIsBusy(); i++) {
            delay(50);
        }
        Serial.printf("[WIFI-DIAG] Display API wait done elapsed=%lu ms busy=%d\n",
                      millis() - wait_start,
                      displayApiIsBusy() ? 1 : 0);
    }

    // A connect attempt often makes scan fail on ESP32; stop it before scanning.
    const wl_status_t scan_start_status = WiFi.status();
    if (s_wifi_connect_pending || scan_start_status != WL_CONNECTED) {
        Serial.printf("[WIFI-DIAG] abort connect before scan pending=%d status=%d/%s\n",
                      s_wifi_connect_pending ? 1 : 0,
                      (int)scan_start_status,
                      _wifi_status_name(scan_start_status));
        s_wifi_connect_pending = false;
        _wifi_display_guard_refresh();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        delay(120);
        _wifi_log_state("after-disconnect-before-scan");
    }

    s_wifi_waiting_for_selection = false;
    s_wifi_scan_error = false;
    s_wifi_scan_count = 0;
    s_wifi_scan_pending = true;
    s_wifi_scan_started_ms = millis();
    _wifi_display_guard_refresh();
    if (!_wifi_start_scan_with_recovery()) {
        WiFi.mode(WIFI_STA);  // Ensure WiFi stays enabled even if all retries failed
        s_wifi_scan_pending = false;
        _wifi_display_guard_refresh();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        s_wifi_scan_error = true;
        s_wifi_waiting_for_selection = true;
        if (s_wifi_scan_lbl) lv_label_set_text(s_wifi_scan_lbl, "Errore scansione");
        _wifi_log_state("scan-start-all-retries-failed");
    } else {
        if (s_wifi_scan_lbl) lv_label_set_text(s_wifi_scan_lbl, "Scansione in corso...");
        _wifi_log_state("scan-started");
    }

    if (!s_wifi_scan_timer) {
        s_wifi_scan_timer = lv_timer_create(_wifi_scan_timer_cb, 300, NULL);
    }
    _wifi_render_network_list();
}

static void _wifi_connect_start(const char* ssid, const char* pass) {
    if (!ssid || ssid[0] == '\0') return;

    _wifi_pref_save_credentials(ssid, pass ? pass : "");
    _wifi_pref_enabled_set(true);
    s_wifi_target_ssid = ssid;
    s_wifi_waiting_for_selection = false;
    s_wifi_scan_error = false;
    if (s_wifi_sw) lv_obj_add_state(s_wifi_sw, LV_STATE_CHECKED);

    WiFi.useStaticBuffers(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.scanDelete();
    WiFi.disconnect(false, false);
    delay(150);
    s_wifi_connect_pending = true;
    s_wifi_connect_guard_start_ms = millis();
    _wifi_display_guard_refresh();
    Serial.printf("[WIFI-DIAG] connect_start ssid='%s' ssid_len=%u pass_len=%u heap_int=%u heap_dma=%u dma_big=%u\n",
                  ssid,
                  (unsigned)strlen(ssid),
                  (unsigned)strlen(pass ? pass : ""),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    WiFi.begin(ssid, pass ? pass : "");

    if (s_wifi_scan_lbl) {
        const String text = String("Connessione a ") + ssid + "...";
        lv_label_set_text(s_wifi_scan_lbl, text.c_str());
    }
    _wifi_update_status_labels();

    const bool api_visible_now =
        s_wifi_sw && lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED) && WiFi.status() == WL_CONNECTED;
    if (s_active == 2 && s_right_panel && api_visible_now != s_wifi_api_section_visible &&
        !s_wifi_scan_pending && !s_wifi_connect_pending &&
        !(s_wifi_popup_ctx && s_wifi_popup_ctx->mask) &&
        !(s_api_popup_ctx && s_api_popup_ctx->mask)) {
        _show_content(2);
    }
}

static void _wifi_switch_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t* sw = lv_event_get_target(e);
    const bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    _wifi_pref_enabled_set(enabled);

    if (!enabled) {
        s_wifi_target_ssid = "";
        s_wifi_scan_pending = false;
        s_wifi_connect_pending = false;
        s_wifi_waiting_for_selection = false;
        s_wifi_scan_error = false;
        s_wifi_scan_count = 0;
        _wifi_display_guard_refresh();
        WiFi.scanDelete();
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        if (s_wifi_scan_lbl) lv_label_set_text(s_wifi_scan_lbl, "WiFi spento");
        if (s_wifi_popup_ctx && s_wifi_popup_ctx->mask) {
            lv_obj_del(s_wifi_popup_ctx->mask);
        }
        _wifi_render_network_list();
        _wifi_update_status_labels();
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);

    String saved_ssid;
    String saved_pass;
    _wifi_pref_load_credentials(saved_ssid, saved_pass);
    (void)saved_pass;
    if (saved_ssid.length() > 0) {
        s_wifi_target_ssid = saved_ssid;
    }

    s_wifi_waiting_for_selection = false;
    s_wifi_scan_error = false;
    _wifi_start_scan();
    _wifi_update_status_labels();
}

static void _wifi_rescan_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!s_wifi_sw || !lv_obj_has_state(s_wifi_sw, LV_STATE_CHECKED)) return;
    // Se connessi, disconnetti prima di scansionare (libera memoria DMA per la scan).
    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        delay(120);
    }
    _wifi_start_scan();
}

static void _wifi_status_timer_cb(lv_timer_t* /*t*/) {
    if (s_wifi_connect_pending) {
        const wl_status_t st = WiFi.status();
        const bool done =
            (st == WL_CONNECTED) ||
            (st == WL_CONNECT_FAILED) ||
            (st == WL_NO_SSID_AVAIL) ||
            (st == WL_NO_SHIELD);
        const bool timed_out = (millis() - s_wifi_connect_guard_start_ms) >= k_wifi_connect_guard_timeout_ms;
        if (done || timed_out) {
            s_wifi_connect_pending = false;
            _wifi_display_guard_refresh();
        }
    }
    _wifi_update_status_labels();
}

static void _clear_network_state() {
    if (s_wifi_status_timer) {
        lv_timer_del(s_wifi_status_timer);
        s_wifi_status_timer = NULL;
    }
    if (s_wifi_scan_timer) {
        lv_timer_del(s_wifi_scan_timer);
        s_wifi_scan_timer = NULL;
    }
    if (s_wifi_popup_ctx && s_wifi_popup_ctx->mask) {
        lv_obj_del(s_wifi_popup_ctx->mask);
    }

    WiFi.scanDelete();
    WiFi.setAutoReconnect(false);

    const bool wifi_pref_on = _wifi_pref_enabled_get();
    if (wifi_pref_on && WiFi.status() != WL_CONNECTED) {
        // Uscita senza connessione attiva: riprova con le credenziali salvate.
        String saved_ssid, saved_pass;
        _wifi_pref_load_credentials(saved_ssid, saved_pass);
        if (saved_ssid.length() > 0) {
            Serial.printf("[WIFI] Uscita impostazioni: ripristino connessione a '%s'\n",
                          saved_ssid.c_str());
            WiFi.begin(saved_ssid.c_str(), saved_pass.c_str());
        } else if (WiFi.getMode() != WIFI_OFF) {
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_OFF);
        }
    } else if (!wifi_pref_on && WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_OFF);
    }
    // Se già connesso: non fare nulla.

    s_wifi_scan_pending = false;
    s_wifi_connect_pending = false;
    s_wifi_waiting_for_selection = false;
    s_wifi_scan_error = false;
    _wifi_display_guard_refresh();
    s_wifi_scan_count = 0;
    s_wifi_api_section_visible = false;
    s_wifi_target_ssid = "";
    s_wifi_row = NULL;
    s_wifi_sw = NULL;
    s_wifi_status_lbl = NULL;
    s_wifi_ip_lbl = NULL;
    s_wifi_scan_lbl = NULL;
    s_wifi_list = NULL;
}

// Builders
static void _build_user_settings(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);

    {
        lv_obj_t* row = make_row(list, "Tema");
        make_seg2_prominent(row, k_map_tema, 0);
    }

    make_value_button_row(list, "Nome Impianto", "", &s_plant_name_btn, &s_plant_name_lbl);
    if (s_plant_name_btn) {
        lv_obj_set_size(s_plant_name_btn, 320, 38);
        lv_obj_add_event_cb(s_plant_name_btn, _plant_name_btn_cb, LV_EVENT_CLICKED, NULL);
    }
    if (s_plant_name_lbl) {
        lv_obj_set_width(s_plant_name_lbl, 292);
        lv_label_set_long_mode(s_plant_name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(s_plant_name_lbl, &lv_font_montserrat_14, 0);
    }
    _refresh_plant_name_row();

    make_dropdown_row(list, "Lingua", "Italiano\nEnglish\nEspanol\nFrancais", 0);
    make_brightness_row(list, ui_brightness_get());
    make_screensaver_row(list, ui_screensaver_minutes_get());

    {
        lv_obj_t* row = make_row(list, "Temperatura");
        const int sel = (ui_temperature_unit_get() == UI_TEMP_F) ? 1 : 0;
        lv_obj_t* bm = make_seg2_prominent(row, k_map_gradi, sel);
        lv_obj_add_event_cb(bm, _temp_unit_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    make_switch_row(list, "Buzzer", true);
}

// ─── Pannello "Filtraggio Aria" ───────────────────────────────────────────────

static void _filt_wiz_close(CalibWizCtx* ctx) {
    if (!ctx) return;
    if (ctx->sample_timer) { lv_timer_del(ctx->sample_timer); ctx->sample_timer = NULL; }
    if (ctx->auto_mode && ctx->step == WizStep::AUTO_RUNNING) {
        _filt_wiz_send_speed(0);
    }
    if (ctx->mask) { lv_obj_del(ctx->mask); ctx->mask = NULL; }
    delete ctx;
    s_wiz_ctx = NULL;
}

static void _filt_wiz_close_and_refresh(CalibWizCtx* ctx) {
    _filt_wiz_close(ctx);
    if (s_active == 5 && s_right_panel) _show_content(5);
}

static void _clear_filtering_state() {
    if (s_filt_timer) { lv_timer_del(s_filt_timer); s_filt_timer = NULL; }
    _filt_wiz_close(s_wiz_ctx);   // no-op se s_wiz_ctx == NULL
    s_filt_status_lbl = NULL;
    s_filt_dp_lbl     = NULL;
    s_filt_mon_sw     = NULL;
}

static void _filt_wiz_send_speed(uint8_t pct) {
    const int n = rs485_network_device_count();
    for (int i = 0; i < n; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.online)      continue;
        if (dev.type != Rs485DevType::SENSOR)  continue;
        if (dev.sensor_profile != Rs485SensorProfile::AIR_010) continue;
        String resp;
        rs485_network_motor_speed_command(dev.address, pct, resp);
    }
}

static void _filt_wiz_set_speed(uint8_t pct) {
    if (!s_wiz_ctx) return;
    if (pct > 100) pct = 100;
    s_wiz_ctx->current_speed = pct;
    _filt_wiz_send_speed(pct);
    if (s_wiz_ctx->speed_lbl) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d%%", (int)pct);
        lv_label_set_text(s_wiz_ctx->speed_lbl, buf);
    }
}

static void _filt_wiz_speed_dec_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!s_wiz_ctx) return;
    int spd = (int)s_wiz_ctx->current_speed - 10;
    if (spd < 0) spd = 0;
    _filt_wiz_set_speed((uint8_t)spd);
}

static void _filt_wiz_speed_inc_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (!s_wiz_ctx) return;
    int spd = (int)s_wiz_ctx->current_speed + 10;
    if (spd > 100) spd = 100;
    _filt_wiz_set_speed((uint8_t)spd);
}

static void _filt_wiz_render(CalibWizCtx* ctx) {
    if (!ctx) return;

    // Speed container visibility
    const bool show_speed = (ctx->step == WizStep::MANUAL_MOVE ||
                             ctx->step == WizStep::MANUAL_SAMPLING);
    if (ctx->speed_cont) {
        if (show_speed) lv_obj_clear_flag(ctx->speed_cont, LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(ctx->speed_cont, LV_OBJ_FLAG_HIDDEN);
    }

    // Spinner visibility
    const bool show_spinner = (ctx->step == WizStep::MANUAL_SAMPLING ||
                               ctx->step == WizStep::AUTO_RUNNING);
    if (ctx->spinner) {
        if (show_spinner) lv_obj_clear_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
        else              lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* lbl_l = ctx->btn_left  ? lv_obj_get_child(ctx->btn_left,  0) : NULL;
    lv_obj_t* lbl_r = ctx->btn_right ? lv_obj_get_child(ctx->btn_right, 0) : NULL;

    switch (ctx->step) {
        case WizStep::MODE_SELECT:
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, "");
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl,
                "AUTOMATICA: il sistema imposta le velocita e registra\n"
                "automaticamente quando il DeltaP e stabile.\n"
                "Assicurarsi che il filtro sia PULITO, poi premere START.\n\n"
                "MANUALE: 5 punti di calibrazione. Posiziona il motore\n"
                "alla velocita target con +/-10%%, poi premi REGISTRA\n"
                "quando il DeltaP e stabile.");
            if (ctx->value_lbl) lv_label_set_text(ctx->value_lbl, "");
            if (lbl_l) lv_label_set_text(lbl_l, "Manuale");
            if (lbl_r) lv_label_set_text(lbl_r, "Automatica");
            if (ctx->btn_left)  lv_obj_clear_state(ctx->btn_left,  LV_STATE_DISABLED);
            if (ctx->btn_right) lv_obj_clear_state(ctx->btn_right, LV_STATE_DISABLED);
            break;

        case WizStep::MANUAL_MOVE: {
            char sub[56];
            lv_snprintf(sub, sizeof(sub), "Manuale - Punto %d di %d  -  Target %d%%",
                        ctx->point_idx + 1, ctx->total_points,
                        (int)ctx->speeds[ctx->point_idx]);
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, sub);

            if (ctx->current_speed != ctx->speeds[ctx->point_idx]) {
                ctx->current_speed = ctx->speeds[ctx->point_idx];
                _filt_wiz_send_speed(ctx->current_speed);
                if (ctx->speed_lbl) {
                    char buf[8];
                    lv_snprintf(buf, sizeof(buf), "%d%%", (int)ctx->current_speed);
                    lv_label_set_text(ctx->speed_lbl, buf);
                }
            }
            char body[160];
            lv_snprintf(body, sizeof(body),
                "Velocita target: %d%%\n"
                "Usa +/-10%% per aggiustare la velocita del motore.\n"
                "Attendi la stabilizzazione del DeltaP,\n"
                "poi premi REGISTRA.",
                (int)ctx->speeds[ctx->point_idx]);
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
            if (ctx->value_lbl) lv_label_set_text(ctx->value_lbl, "-- Pa");
            if (lbl_l) lv_label_set_text(lbl_l, "Annulla");
            if (lbl_r) lv_label_set_text(lbl_r, "Registra");
            if (ctx->btn_right) lv_obj_clear_state(ctx->btn_right, LV_STATE_DISABLED);
            break;
        }

        case WizStep::MANUAL_SAMPLING: {
            char sub[56];
            lv_snprintf(sub, sizeof(sub), "Manuale - Punto %d di %d  -  Target %d%%",
                        ctx->point_idx + 1, ctx->total_points,
                        (int)ctx->speeds[ctx->point_idx]);
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, sub);
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, "Campionamento in corso...");
            if (ctx->value_lbl) lv_label_set_text(ctx->value_lbl, "-- Pa");
            if (lbl_l) lv_label_set_text(lbl_l, "Annulla");
            if (lbl_r) lv_label_set_text(lbl_r, "Registra");
            if (ctx->btn_right) lv_obj_add_state(ctx->btn_right, LV_STATE_DISABLED);
            break;
        }

        case WizStep::MANUAL_CONFIRM: {
            char sub[56];
            lv_snprintf(sub, sizeof(sub), "Manuale - Punto %d di %d  -  Rilevato %d%%",
                        ctx->point_idx + 1, ctx->total_points,
                        (int)ctx->speeds[ctx->point_idx]);
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, sub);
            char dp_str[12];
            snprintf(dp_str, sizeof(dp_str), "%.1f", ctx->sampled_dp[ctx->point_idx]);
            char body[96];
            lv_snprintf(body, sizeof(body),
                "Delta P rilevato: %s Pa\n\n"
                "Confermare questo punto?\n"
                "Premi Ripeti per rifare la misura.",
                dp_str);
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
            if (ctx->value_lbl) {
                char vbuf[20];
                snprintf(vbuf, sizeof(vbuf), "%.1f Pa", ctx->sampled_dp[ctx->point_idx]);
                lv_label_set_text(ctx->value_lbl, vbuf);
            }
            if (lbl_l) lv_label_set_text(lbl_l, "Ripeti");
            if (lbl_r) lv_label_set_text(lbl_r,
                (ctx->point_idx + 1 >= ctx->total_points) ? "Fine" : "Avanti");
            if (ctx->btn_right) lv_obj_clear_state(ctx->btn_right, LV_STATE_DISABLED);
            break;
        }

        case WizStep::AUTO_RUNNING: {
            char sub[48];
            lv_snprintf(sub, sizeof(sub), "Automatico - Punto %d di %d",
                        ctx->point_idx + 1, ctx->total_points);
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, sub);
            char body[96];
            lv_snprintf(body, sizeof(body),
                "Velocita impostata: %d%%\nAttendi la stabilizzazione...",
                (int)ctx->speeds[ctx->point_idx]);
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
            if (ctx->value_lbl) lv_label_set_text(ctx->value_lbl, "-- Pa");
            if (lbl_l) lv_label_set_text(lbl_l, "Annulla");
            if (lbl_r) lv_label_set_text(lbl_r, "");
            if (ctx->btn_right) lv_obj_add_state(ctx->btn_right, LV_STATE_DISABLED);
            break;
        }

        case WizStep::DONE: {
            if (ctx->subtitle_lbl) lv_label_set_text(ctx->subtitle_lbl, "");
            char body[256];
            int pos = lv_snprintf(body, sizeof(body), "Riepilogo calibrazione:\n");
            for (int i = 0; i < ctx->total_points && pos < (int)sizeof(body) - 32; i++) {
                char dp_str[10];
                snprintf(dp_str, sizeof(dp_str), "%.1f", ctx->sampled_dp[i]);
                pos += lv_snprintf(body + pos, (int)sizeof(body) - pos,
                                   "  %d%%  ->  %s Pa\n",
                                   (int)ctx->speeds[i], dp_str);
            }
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
            if (ctx->value_lbl) lv_label_set_text(ctx->value_lbl, "");
            if (lbl_l) lv_label_set_text(lbl_l, "Annulla");
            if (lbl_r) lv_label_set_text(lbl_r, "Salva");
            if (ctx->btn_right) lv_obj_clear_state(ctx->btn_right, LV_STATE_DISABLED);
            break;
        }
    }
}

// Timer unificato 500 ms — gestisce MANUAL_MOVE (live DP), MANUAL_SAMPLING, AUTO_RUNNING
static void _filt_wiz_tick_cb(lv_timer_t* /*t*/) {
    CalibWizCtx* ctx = s_wiz_ctx;
    if (!ctx) return;

    float dp = 0.0f;
    bool  valid = false;
    ui_filter_rs485_read_deltap(dp, valid);

    // ── MANUAL_MOVE: aggiornamento live DeltaP ───────────────────────────────
    if (ctx->step == WizStep::MANUAL_MOVE) {
        if (valid && ctx->value_lbl) {
            char buf[20]; snprintf(buf, sizeof(buf), "%.1f Pa", dp);
            lv_label_set_text(ctx->value_lbl, buf);
        }
        return;
    }

    // ── MANUAL_SAMPLING: conto alla rovescia 10 s ────────────────────────────
    if (ctx->step == WizStep::MANUAL_SAMPLING) {
        if (valid) { ctx->sample_sum += dp; ctx->sample_count++; }
        ctx->sample_tick--;
        const int sec_rem = (ctx->sample_tick + 1) / 2;
        float avg = ctx->sample_count > 0
                        ? ctx->sample_sum / (float)ctx->sample_count : 0.0f;
        char avg_str[12]; snprintf(avg_str, sizeof(avg_str), "%.1f", avg);
        char body[80];
        lv_snprintf(body, sizeof(body),
                    "Campionamento... %d sec\nMedia: %s Pa", sec_rem, avg_str);
        if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
        if (valid && ctx->value_lbl) {
            char vbuf[20]; snprintf(vbuf, sizeof(vbuf), "%.1f Pa", dp);
            lv_label_set_text(ctx->value_lbl, vbuf);
        }
        if (ctx->sample_tick <= 0) {
            ctx->sampled_dp[ctx->point_idx] =
                ctx->sample_count > 0
                    ? ctx->sample_sum / (float)ctx->sample_count : 0.0f;
            ctx->step = WizStep::MANUAL_CONFIRM;
            _filt_wiz_render(ctx);
        }
        return;
    }

    // ── AUTO_RUNNING ─────────────────────────────────────────────────────────
    if (ctx->step == WizStep::AUTO_RUNNING) {
        ctx->auto_phase_tick++;
        if (valid && ctx->value_lbl) {
            char buf[20]; snprintf(buf, sizeof(buf), "%.1f Pa", dp);
            lv_label_set_text(ctx->value_lbl, buf);
        }

        if (ctx->auto_phase == AutoPhase::WARMUP) {
            const int rem = (k_auto_warmup_ticks - ctx->auto_phase_tick + 1) / 2;
            char body[96];
            lv_snprintf(body, sizeof(body),
                "Punto %d di %d - Velocita %d%%\nRiscaldamento... %d sec",
                ctx->point_idx + 1, ctx->total_points,
                (int)ctx->speeds[ctx->point_idx], rem > 0 ? rem : 1);
            if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
            if (ctx->auto_phase_tick >= k_auto_warmup_ticks) {
                ctx->auto_phase      = AutoPhase::STABILIZING;
                ctx->auto_phase_tick = 0;
                ctx->auto_stab_idx   = 0;
                ctx->auto_stab_consec = 0;
                ctx->auto_cur_sum    = 0.0f;
                ctx->auto_cur_count  = 0;
                memset(ctx->auto_stab_buf, 0, sizeof(ctx->auto_stab_buf));
            }
            return;
        }

        // AutoPhase::STABILIZING
        if (ctx->auto_phase_tick >= k_auto_timeout_ticks) {
            ctx->sampled_dp[ctx->point_idx] =
                ctx->auto_cur_count > 0
                    ? ctx->auto_cur_sum / (float)ctx->auto_cur_count : 0.0f;
            ctx->point_idx++;
            if (ctx->point_idx >= ctx->total_points) {
                ctx->step = WizStep::DONE;
                _filt_wiz_render(ctx);
            } else {
                _filt_wiz_auto_advance(ctx);
            }
            return;
        }

        if (valid) {
            ctx->auto_stab_buf[ctx->auto_stab_idx % k_auto_stab_n] = dp;
            ctx->auto_stab_idx++;
            ctx->auto_cur_sum += dp;
            ctx->auto_cur_count++;

            if (ctx->auto_stab_idx >= k_auto_stab_n) {
                float mn = ctx->auto_stab_buf[0], mx = ctx->auto_stab_buf[0];
                for (int j = 1; j < k_auto_stab_n; j++) {
                    if (ctx->auto_stab_buf[j] < mn) mn = ctx->auto_stab_buf[j];
                    if (ctx->auto_stab_buf[j] > mx) mx = ctx->auto_stab_buf[j];
                }
                if ((mx - mn) < k_auto_stab_thr) {
                    ctx->auto_stab_consec++;
                } else {
                    ctx->auto_stab_consec = 0;
                }
                if (ctx->auto_stab_consec >= k_auto_stab_hold) {
                    float sum = 0.0f;
                    for (int j = 0; j < k_auto_stab_n; j++) sum += ctx->auto_stab_buf[j];
                    ctx->sampled_dp[ctx->point_idx] = sum / (float)k_auto_stab_n;
                    ctx->point_idx++;
                    if (ctx->point_idx >= ctx->total_points) {
                        ctx->step = WizStep::DONE;
                        _filt_wiz_render(ctx);
                    } else {
                        _filt_wiz_auto_advance(ctx);
                    }
                    return;
                }
            }
        }
        char body[96];
        lv_snprintf(body, sizeof(body),
            "Punto %d di %d - Velocita %d%%\nAnalisi stabilita...",
            ctx->point_idx + 1, ctx->total_points,
            (int)ctx->speeds[ctx->point_idx]);
        if (ctx->body_lbl) lv_label_set_text(ctx->body_lbl, body);
    }
}

static void _filt_wiz_auto_advance(CalibWizCtx* ctx) {
    _filt_wiz_send_speed(ctx->speeds[ctx->point_idx]);
    ctx->current_speed    = ctx->speeds[ctx->point_idx];
    ctx->auto_phase       = AutoPhase::WARMUP;
    ctx->auto_phase_tick  = 0;
    ctx->auto_stab_idx    = 0;
    ctx->auto_stab_consec = 0;
    ctx->auto_cur_sum     = 0.0f;
    ctx->auto_cur_count   = 0;
    memset(ctx->auto_stab_buf, 0, sizeof(ctx->auto_stab_buf));
    _filt_wiz_render(ctx);
}

static void _filt_wiz_start_manual_sampling(CalibWizCtx* ctx) {
    // Registra la velocita effettiva comandata come punto di calibrazione
    ctx->speeds[ctx->point_idx] = ctx->current_speed;
    ctx->step         = WizStep::MANUAL_SAMPLING;
    ctx->sample_tick  = k_manual_sample_ticks;
    ctx->sample_sum   = 0.0f;
    ctx->sample_count = 0;
    _filt_wiz_render(ctx);
}

static void _filt_wiz_left_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    CalibWizCtx* ctx = s_wiz_ctx;
    if (!ctx) return;
    switch (ctx->step) {
        case WizStep::MODE_SELECT:
            // Sinistra = Manuale
            ctx->auto_mode = false;
            ctx->point_idx = 0;
            ctx->step      = WizStep::MANUAL_MOVE;
            _filt_wiz_render(ctx);
            break;
        case WizStep::MANUAL_CONFIRM:
            ctx->step = WizStep::MANUAL_MOVE;
            _filt_wiz_render(ctx);
            break;
        default:
            _filt_wiz_close_and_refresh(ctx);
            break;
    }
}

static void _filt_wiz_right_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    CalibWizCtx* ctx = s_wiz_ctx;
    if (!ctx) return;
    switch (ctx->step) {
        case WizStep::MODE_SELECT:
            // Destra = Automatica
            ctx->auto_mode = true;
            ctx->point_idx = 0;
            ctx->step      = WizStep::AUTO_RUNNING;
            _filt_wiz_auto_advance(ctx);
            break;
        case WizStep::MANUAL_MOVE:
            _filt_wiz_start_manual_sampling(ctx);
            break;
        case WizStep::MANUAL_SAMPLING:
            break;  // disabilitato durante campionamento
        case WizStep::MANUAL_CONFIRM:
            ctx->point_idx++;
            if (ctx->point_idx >= ctx->total_points) {
                ctx->step = WizStep::DONE;
            } else {
                ctx->step = WizStep::MANUAL_MOVE;
            }
            _filt_wiz_render(ctx);
            break;
        case WizStep::AUTO_RUNNING:
            break;  // disabilitato durante auto
        case WizStep::DONE: {
            UiFilterCalibData nd = ui_filter_calib_get();
            nd.n = ctx->total_points;
            for (int i = 0; i < ctx->total_points; i++) {
                nd.pts[i].speed_pct = ctx->speeds[i];
                nd.pts[i].delta_p   = ctx->sampled_dp[i];
            }
            ui_filter_calib_apply(nd);
            _filt_wiz_close_and_refresh(ctx);
            break;
        }
    }
}

static void _filt_wiz_open() {
    if (s_wiz_ctx) return;

    int min_spd = ui_ventilation_min_speed_get();
    int max_spd = ui_ventilation_max_speed_get();
    if (min_spd >= max_spd) { min_spd = 20; max_spd = 100; }

    CalibWizCtx* ctx = new CalibWizCtx();
    if (!ctx) return;
    memset(ctx, 0, sizeof(CalibWizCtx));

    // Sempre 5 punti equidistanti tra min e max velocita
    ctx->total_points = 5;
    for (int i = 0; i < 5; i++) {
        ctx->speeds[i] = (uint8_t)(min_spd + (max_spd - min_spd) * i / 4);
    }

    ctx->step     = WizStep::MODE_SELECT;
    ctx->auto_mode = false;
    s_wiz_ctx = ctx;

    lv_obj_t* parent = lv_scr_act();
    if (!parent) { delete ctx; s_wiz_ctx = NULL; return; }

    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(mask);
    lv_obj_set_size(card, 720, 500);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, ST_WHITE, 0);
    lv_obj_set_style_border_color(card, ST_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_hor(card, 28, 0);
    lv_obj_set_style_pad_ver(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "Calibrazione Filtro Aria");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, ST_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // Sottotitolo (aggiornato per ogni punto)
    ctx->subtitle_lbl = lv_label_create(card);
    lv_label_set_text(ctx->subtitle_lbl, "");
    lv_obj_set_style_text_font(ctx->subtitle_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ctx->subtitle_lbl, ST_DIM, 0);
    lv_obj_align(ctx->subtitle_lbl, LV_ALIGN_TOP_LEFT, 0, 34);

    // Corpo istruzioni
    ctx->body_lbl = lv_label_create(card);
    lv_label_set_text(ctx->body_lbl, "");
    lv_obj_set_style_text_font(ctx->body_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ctx->body_lbl, ST_TEXT, 0);
    lv_obj_set_width(ctx->body_lbl, 664);
    lv_label_set_long_mode(ctx->body_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(ctx->body_lbl, LV_ALIGN_TOP_LEFT, 0, 72);

    // Controllo velocita motore (visibile solo in MOVE e SAMPLING)
    lv_obj_t* spd_cont = lv_obj_create(card);
    ctx->speed_cont = spd_cont;
    lv_obj_set_size(spd_cont, 664, 58);
    lv_obj_align(spd_cont, LV_ALIGN_TOP_LEFT, 0, 200);
    lv_obj_set_style_bg_color(spd_cont, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(spd_cont, ST_BORDER, 0);
    lv_obj_set_style_border_width(spd_cont, 1, 0);
    lv_obj_set_style_radius(spd_cont, 8, 0);
    lv_obj_set_style_pad_all(spd_cont, 0, 0);
    lv_obj_clear_flag(spd_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(spd_cont, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t* lbl = lv_label_create(spd_cont);
        lv_label_set_text(lbl, "Velocita motore");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 14, 0);
    }

    // Pulsante -10%
    {
        lv_obj_t* btn = lv_btn_create(spd_cont);
        lv_obj_set_size(btn, 72, 40);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -148, 0);
        lv_obj_set_style_bg_color(btn, ST_LEFT_BG, 0);
        lv_obj_set_style_bg_color(btn, ST_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(btn, ST_BORDER, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_t* l = lv_label_create(btn);
        lv_label_set_text(l, "-10%");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, ST_TEXT, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, _filt_wiz_speed_dec_cb, LV_EVENT_CLICKED, NULL);
    }

    // Label velocita corrente (tra i due pulsanti)
    ctx->speed_lbl = lv_label_create(spd_cont);
    lv_label_set_text(ctx->speed_lbl, "0%");
    lv_obj_set_style_text_font(ctx->speed_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->speed_lbl, ST_ORANGE, 0);
    lv_obj_align(ctx->speed_lbl, LV_ALIGN_RIGHT_MID, -84, 0);

    // Pulsante +10%
    {
        lv_obj_t* btn = lv_btn_create(spd_cont);
        lv_obj_set_size(btn, 72, 40);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(btn, ST_ORANGE, 0);
        lv_obj_set_style_bg_color(btn, ST_ORANGE2, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_t* l = lv_label_create(btn);
        lv_label_set_text(l, "+10%");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(btn, _filt_wiz_speed_inc_cb, LV_EVENT_CLICKED, NULL);
    }

    // Valore deltaP (grande, arancione)
    ctx->value_lbl = lv_label_create(card);
    lv_label_set_text(ctx->value_lbl, "");
    lv_obj_set_style_text_font(ctx->value_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(ctx->value_lbl, ST_ORANGE, 0);
    lv_obj_align(ctx->value_lbl, LV_ALIGN_CENTER, 0, 80);

    // Spinner — visibile durante MANUAL_SAMPLING e AUTO_RUNNING
    ctx->spinner = lv_spinner_create(card, 1000, 60);
    lv_obj_set_size(ctx->spinner, 56, 56);
    lv_obj_align(ctx->spinner, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_arc_color(ctx->spinner, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(ctx->spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ctx->spinner, ST_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ctx->spinner, 4, LV_PART_MAIN);
    lv_obj_add_flag(ctx->spinner, LV_OBJ_FLAG_HIDDEN);

    // Pulsante sinistra (Annulla / Ripeti)
    ctx->btn_left = lv_btn_create(card);
    lv_obj_set_size(ctx->btn_left, 170, 44);
    lv_obj_align(ctx->btn_left, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ctx->btn_left, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(ctx->btn_left, ST_BORDER, 0);
    lv_obj_set_style_border_width(ctx->btn_left, 1, 0);
    lv_obj_set_style_shadow_width(ctx->btn_left, 0, 0);
    lv_obj_set_style_radius(ctx->btn_left, 8, 0);
    {
        lv_obj_t* lbl = lv_label_create(ctx->btn_left);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, ST_TEXT, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(ctx->btn_left, _filt_wiz_left_btn_cb, LV_EVENT_CLICKED, NULL);

    // Pulsante destra (Avanti / Campiona / Salva)
    ctx->btn_right = lv_btn_create(card);
    lv_obj_set_size(ctx->btn_right, 170, 44);
    lv_obj_align(ctx->btn_right, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ctx->btn_right, ST_ORANGE, 0);
    lv_obj_set_style_bg_color(ctx->btn_right, ST_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(ctx->btn_right, ST_LEFT_BG, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(ctx->btn_right, 0, 0);
    lv_obj_set_style_shadow_width(ctx->btn_right, 0, 0);
    lv_obj_set_style_radius(ctx->btn_right, 8, 0);
    {
        lv_obj_t* lbl = lv_label_create(ctx->btn_right);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_color(lbl, ST_DIM, LV_STATE_DISABLED);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(ctx->btn_right, _filt_wiz_right_btn_cb, LV_EVENT_CLICKED, NULL);

    // Timer sempre attivo (500 ms): live DP, campionamento manuale, auto
    ctx->sample_timer = lv_timer_create(_filt_wiz_tick_cb, 500, NULL);

    _filt_wiz_render(ctx);
}

// ─── callbacks pannello Filtraggio ───────────────────────────────────────────

static void _filt_mon_switch_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    UiFilterCalibData d = ui_filter_calib_get();
    d.monitoring_en = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    ui_filter_calib_apply(d);
}

static void _filt_threshold_cb(lv_event_t* e) {
    lv_obj_t* sl  = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    const int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    UiFilterCalibData d = ui_filter_calib_get();
    d.threshold_pct = val;
    ui_filter_calib_apply(d);
}

static void _filt_calib_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _filt_wiz_open();
}

static void _filt_reset_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    UiFilterCalibData d = ui_filter_calib_get();
    d.n              = 0;
    d.monitoring_en  = false;
    ui_filter_calib_apply(d);
    if (s_active == 5 && s_right_panel) _show_content(5);
}

static void _filt_timer_cb(lv_timer_t* /*t*/) {
    float dp = 0.0f;
    bool  valid = false;
    ui_filter_rs485_read_deltap(dp, valid);
    if (s_filt_dp_lbl) {
        char buf[24];
        if (valid) snprintf(buf, sizeof(buf), "%.1f Pa", dp);
        else       lv_snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(s_filt_dp_lbl, buf);
    }
}

static void make_filt_threshold_row(lv_obj_t* parent, int val) {
    lv_obj_t* row = make_row(parent, "Soglia allarme");
    lv_obj_t* pct = lv_label_create(row);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(pct, buf);
    lv_obj_set_style_text_font(pct, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pct, ST_ORANGE, 0);
    lv_obj_align(pct, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* sl = lv_slider_create(row);
    lv_obj_set_size(sl, 210, 8);
    lv_slider_set_range(sl, 10, 90);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, ST_ORANGE, LV_PART_KNOB);
    lv_obj_align(sl, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_add_event_cb(sl, _filt_threshold_cb, LV_EVENT_VALUE_CHANGED, pct);
}

static void _build_filtering_settings(lv_obj_t* parent) {
    const UiFilterCalibData& calib = ui_filter_calib_get();
    lv_obj_t* list = make_scroll_list(parent);

    // Stato calibrazione
    lv_obj_t* st_row = make_row(list, "Calibrazione filtro");
    s_filt_status_lbl = lv_label_create(st_row);
    if (calib.n > 0) {
        char buf[40];
        lv_snprintf(buf, sizeof(buf), "%d punti, soglia %d%%", calib.n, calib.threshold_pct);
        lv_label_set_text(s_filt_status_lbl, buf);
        lv_obj_set_style_text_color(s_filt_status_lbl, ST_ORANGE, 0);
    } else {
        lv_label_set_text(s_filt_status_lbl, "Non calibrato");
        lv_obj_set_style_text_color(s_filt_status_lbl, ST_DIM, 0);
    }
    lv_obj_set_style_text_font(s_filt_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_filt_status_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    // Switch monitoraggio (abilitato solo se calibrato)
    s_filt_mon_sw = make_switch_row(list, "Monitoraggio attivo",
                                    calib.monitoring_en && calib.n > 0);
    if (calib.n == 0) lv_obj_add_state(s_filt_mon_sw, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_filt_mon_sw, _filt_mon_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* sg_sw = make_switch_row(list, "Salvaguardia T/RH",
                                      ui_air_safeguard_enabled_get());
    lv_obj_add_event_cb(sg_sw, _air_safeguard_enabled_cb, LV_EVENT_VALUE_CHANGED, NULL);
    make_air_safeguard_temp_row(list, ui_air_safeguard_temp_max_get());
    make_air_safeguard_hum_row(list, ui_air_safeguard_humidity_max_get());
    make_info_row(list, "Azione salvaguardia", "G1 +20%, media e isteresi");

    // Slider soglia allarme
    make_filt_threshold_row(list, calib.threshold_pct);

    // ΔP live dai sensori RS485
    lv_obj_t* dp_row = make_row(list, "Delta P sensori");
    s_filt_dp_lbl = lv_label_create(dp_row);
    lv_label_set_text(s_filt_dp_lbl, "--");
    lv_obj_set_style_text_font(s_filt_dp_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_filt_dp_lbl, ST_DIM, 0);
    lv_obj_align(s_filt_dp_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    // Pulsante avvia calibrazione
    lv_obj_t* calib_btn = make_action_row_button(list, "Avvia calibrazione",
                                                  LV_SYMBOL_REFRESH " Calibra");
    lv_obj_add_event_cb(calib_btn, _filt_calib_btn_cb, LV_EVENT_CLICKED, NULL);

    // Pulsante azzera (solo se già calibrato)
    if (calib.n > 0) {
        lv_obj_t* rst_row = make_row(list, "Azzera calibrazione");
        lv_obj_t* rst_btn = lv_btn_create(rst_row);
        lv_obj_set_size(rst_btn, 170, 38);
        lv_obj_set_style_bg_color(rst_btn, ST_LEFT_BG, 0);
        lv_obj_set_style_bg_color(rst_btn, ST_BORDER, LV_STATE_PRESSED);
        lv_obj_set_style_border_color(rst_btn, ST_BORDER, 0);
        lv_obj_set_style_border_width(rst_btn, 1, 0);
        lv_obj_set_style_radius(rst_btn, 8, 0);
        lv_obj_set_style_shadow_width(rst_btn, 0, 0);
        lv_obj_align(rst_btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t* rst_lbl = lv_label_create(rst_btn);
        lv_label_set_text(rst_lbl, LV_SYMBOL_TRASH " Azzera");
        lv_obj_set_style_text_font(rst_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rst_lbl, ST_TEXT, 0);
        lv_obj_center(rst_lbl);
        lv_obj_add_event_cb(rst_btn, _filt_reset_btn_cb, LV_EVENT_CLICKED, NULL);
    }

    // Info modalità ventilazione
    const int steps = ui_ventilation_step_count_get();
    if (steps >= 2) {
        char info[40];
        lv_snprintf(info, sizeof(info), "%d posizioni (%d punti)", steps, steps);
        make_info_row(list, "Modalita calibrazione", info);
    } else {
        make_info_row(list, "Modalita calibrazione", "Continua (5 ancoraggi, interpolazione)");
    }

    // Timer refresh ΔP live
    s_filt_timer = lv_timer_create(_filt_timer_cb, 1000, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────

static void _build_ventilation_settings(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);
    const Air010Presence air = _air010_presence();

    make_vent_min_speed_row(list, ui_ventilation_min_speed_get());
    make_vent_max_speed_row(list, ui_ventilation_max_speed_get());
    lv_obj_t* step_dd = make_dropdown_row(list, "Regolazione velocita",
                                          "Continua\n2 posizioni\n3 posizioni\n5 posizioni\n7 posizioni\n10 posizioni",
                                          _vent_step_count_to_index(ui_ventilation_step_count_get()));
    lv_obj_add_event_cb(step_dd, _vent_step_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (air.intake_count > 0 && air.extraction_count > 0) {
        lv_obj_t* intake_bar_sw = make_switch_row(list, "Barra Immissione",
                                                  ui_ventilation_intake_bar_enabled_get());
        lv_obj_add_event_cb(intake_bar_sw, _vent_intake_bar_cb, LV_EVENT_VALUE_CHANGED, NULL);
        if (!ui_ventilation_intake_bar_enabled_get()) {
            make_vent_intake_percentage_row(list, ui_ventilation_intake_percentage_get());
            make_info_row(list, "Barra Home", "Aspirazione + immissione proporzionale");
        } else {
            make_info_row(list, "Barra Home", "Aspirazione e Immissione separate");
        }
    } else if (air.intake_count > 0) {
        make_info_row(list, "Modalita 0/10V", "ImmissionAlone");
        make_info_row(list, "Barra Home", "Immissione");
    } else if (air.extraction_count > 0) {
        make_info_row(list, "Barra Home", "Aspirazione");
    } else {
        make_info_row(list, "Barra Home", "minimo - massimo configurati");
    }
}

static void _build_datetime_settings(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);

    s_dt_row_auto = make_row(list, "Data e Ora automatica");
    s_dt_auto_sw = lv_switch_create(s_dt_row_auto);
    lv_obj_set_size(s_dt_auto_sw, 56, 28);
    lv_obj_set_style_bg_color(s_dt_auto_sw, ST_ORANGE,
                              (uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED);
    lv_obj_align(s_dt_auto_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(s_dt_auto_sw, _dt_auto_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_dt_tz_dd = make_dropdown_row(list, "GMT", ui_dc_clock_timezone_options(),
                                   ui_dc_clock_timezone_index_get());
    s_dt_row_tz = lv_obj_get_parent(s_dt_tz_dd);
    lv_obj_set_style_bg_color(s_dt_tz_dd, ST_LEFT_BG, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(s_dt_tz_dd, ST_DIM, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_dt_tz_dd, _dt_timezone_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_dt_row_time = make_value_button_row(list, "Imposta Ora", "00:00:00",
                                          &s_dt_time_btn, &s_dt_time_lbl);
    lv_obj_add_event_cb(s_dt_time_btn, _dt_time_btn_cb, LV_EVENT_CLICKED, NULL);

    s_dt_row_date = make_value_button_row(list, "Imposta Data", "--/--/----",
                                          &s_dt_date_btn, &s_dt_date_lbl);
    lv_obj_add_event_cb(s_dt_date_btn, _dt_date_btn_cb, LV_EVENT_CLICKED, NULL);

    _refresh_datetime_value_labels();
    _update_datetime_controls_state();
    s_dt_timer = lv_timer_create(_dt_timer_cb, 1000, NULL);
}

static void _build_network_settings(lv_obj_t* parent) {
    const bool wifi_enabled = _wifi_pref_enabled_get();
    Serial.printf("[WIFI-DIAG] network_page_open heap_int=%u heap_dma=%u dma_big=%u pref_enabled=%d mode=%d status=%d\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                  wifi_enabled ? 1 : 0,
                  (int)WiFi.getMode(),
                  (int)WiFi.status());

    lv_obj_t* list = make_scroll_list(parent);

    s_wifi_row = make_row(list, "WiFi");
    s_wifi_sw = lv_switch_create(s_wifi_row);
    lv_obj_set_size(s_wifi_sw, 56, 28);
    lv_obj_set_style_bg_color(s_wifi_sw, ST_ORANGE,
                              (uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED);
    lv_obj_align(s_wifi_sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(s_wifi_sw, _wifi_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (wifi_enabled) {
        lv_obj_add_state(s_wifi_sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_wifi_sw, LV_STATE_CHECKED);
    }

    lv_obj_t* status_row = make_row(list, "Stato Connessione");
    s_wifi_status_lbl = lv_label_create(status_row);
    lv_label_set_text(s_wifi_status_lbl, "Disattivato");
    lv_obj_set_style_text_font(s_wifi_status_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wifi_status_lbl, ST_DIM, 0);
    lv_obj_align(s_wifi_status_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* ip_row = make_row(list, "Indirizzo IP");
    s_wifi_ip_lbl = lv_label_create(ip_row);
    lv_label_set_text(s_wifi_ip_lbl, "--");
    lv_obj_set_style_text_font(s_wifi_ip_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_wifi_ip_lbl, ST_DIM, 0);
    lv_obj_align(s_wifi_ip_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    s_wifi_api_section_visible = (wifi_enabled && WiFi.status() == WL_CONNECTED);
    if (s_wifi_api_section_visible) {
        lv_obj_t* api_url_btn = NULL;
        lv_obj_t* api_url_lbl = NULL;
        const String api_url_label = _api_user_url_label();
        make_value_button_row(list, "URL API Utente", api_url_label.c_str(),
                              &api_url_btn, &api_url_lbl);
        if (api_url_btn) {
            lv_obj_set_size(api_url_btn, 420, 38);
            lv_obj_add_event_cb(api_url_btn, _api_customer_url_btn_cb,
                                LV_EVENT_CLICKED, api_url_lbl);
        }
        if (api_url_lbl) {
            lv_obj_set_width(api_url_lbl, 392);
            lv_label_set_long_mode(api_url_lbl, LV_LABEL_LONG_DOT);
        }

        lv_obj_t* api_key_btn = NULL;
        lv_obj_t* api_key_lbl = NULL;
        const String api_key_label = _api_user_key_label();
        make_value_button_row(list, "API Key Utente", api_key_label.c_str(),
                              &api_key_btn, &api_key_lbl);
        if (api_key_btn) {
            lv_obj_set_size(api_key_btn, 260, 38);
            lv_obj_add_event_cb(api_key_btn, _api_customer_key_btn_cb,
                                LV_EVENT_CLICKED, api_key_lbl);
        }
    }

    lv_obj_t* rescan_btn = make_action_row_button(list, "Reti WiFi", LV_SYMBOL_REFRESH " Aggiorna");
    lv_obj_add_event_cb(rescan_btn, _wifi_rescan_cb, LV_EVENT_CLICKED, NULL);

    s_wifi_list = lv_obj_create(list);
    lv_obj_set_size(s_wifi_list, LV_PCT(100), 220);
    lv_obj_set_style_bg_color(s_wifi_list, ST_LEFT_BG, 0);
    lv_obj_set_style_border_color(s_wifi_list, ST_BORDER, 0);
    lv_obj_set_style_border_width(s_wifi_list, 1, 0);
    lv_obj_set_style_radius(s_wifi_list, 10, 0);
    lv_obj_set_style_pad_all(s_wifi_list, 10, 0);
    lv_obj_set_style_pad_row(s_wifi_list, 8, 0);
    lv_obj_set_layout(s_wifi_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    if (!s_wifi_status_timer) {
        s_wifi_status_timer = lv_timer_create(_wifi_status_timer_cb, 1000, NULL);
    }

    if (wifi_enabled) {
        String saved_ssid;
        String saved_pass;
        _wifi_pref_load_credentials(saved_ssid, saved_pass);
        (void)saved_pass;
        if (saved_ssid.length() > 0) {
            s_wifi_target_ssid = saved_ssid;
        }
        if (WiFi.status() == WL_CONNECTED) {
            // Già connessi: non disconnettere, non scansionare.
            // La scansione parte solo se l'utente preme "Aggiorna".
            _wifi_render_network_list();
        } else {
            // Non connessi: scansione automatica immediata.
            _wifi_start_scan();
        }
    } else {
        _wifi_render_network_list();
    }
    _wifi_update_status_labels();
}

// ─── Pannello "Rete e Sistema" — gestione stato ───────────────────────────────

static void _clear_system_state() {
    if (s_sys_timer) { lv_timer_del(s_sys_timer); s_sys_timer = NULL; }
    s_sys_count_lbl = NULL;
    s_sys_plant_lbl = NULL;
    s_sys_scan_btn  = NULL;
    s_sys_save_btn  = NULL;
}

static void _sys_timer_cb(lv_timer_t* /*t*/) {
    const Rs485ScanState state = rs485_network_scan_state();
    const int count            = rs485_network_device_count();
    const int plant_count      = rs485_network_plant_device_count();

    // Aggiorna label contatore
    if (s_sys_count_lbl) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", count);
        lv_label_set_text(s_sys_count_lbl, buf);
    }
    if (s_sys_plant_lbl) {
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", plant_count);
        lv_label_set_text(s_sys_plant_lbl, buf);
    }

    // Aggiorna pulsante Scansiona
    if (s_sys_scan_btn) {
        lv_obj_t* lbl = lv_obj_get_child(s_sys_scan_btn, 0);
        if (state == Rs485ScanState::RUNNING) {
            if (lbl) {
                char buf[28];
                lv_snprintf(buf, sizeof(buf), "Scan %d/200",
                            rs485_network_scan_progress());
                lv_label_set_text(lbl, buf);
            }
            lv_obj_add_state(s_sys_scan_btn, LV_STATE_DISABLED);
        } else {
            if (lbl) lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Scansiona");
            lv_obj_clear_state(s_sys_scan_btn, LV_STATE_DISABLED);
        }
    }
    if (s_sys_save_btn) {
        if (state == Rs485ScanState::RUNNING) lv_obj_add_state(s_sys_save_btn, LV_STATE_DISABLED);
        else lv_obj_clear_state(s_sys_save_btn, LV_STATE_DISABLED);
    }
}

static void _sys_scan_confirm_cb(void* /*user_data*/) {
    rs485_network_scan_start();
    if (s_sys_timer) lv_timer_reset(s_sys_timer);
}

static void _sys_scan_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Scansione periferiche", "Avvia scansione",
                                  _sys_scan_confirm_cb, NULL);
}

static void _sys_save_confirm_cb(void* /*user_data*/) {
    (void)rs485_network_save_current_as_plant();
    if (s_sys_timer) lv_timer_reset(s_sys_timer);
}

static void _sys_save_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Salvataggio impianto", "Salva impianto",
                                  _sys_save_confirm_cb, NULL);
}

static void _build_system_setup(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);

    // Row: Periferiche fotografia impianto
    {
        lv_obj_t* row = make_row(list, "Periferiche impianto");
        s_sys_plant_lbl = lv_label_create(row);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", rs485_network_plant_device_count());
        lv_label_set_text(s_sys_plant_lbl, buf);
        lv_obj_set_style_text_font(s_sys_plant_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_sys_plant_lbl, ST_DIM, 0);
        lv_obj_align(s_sys_plant_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    // Row: Periferiche presenti a runtime
    {
        lv_obj_t* row = make_row(list, "Periferiche runtime");
        s_sys_count_lbl = lv_label_create(row);
        char buf[8];
        lv_snprintf(buf, sizeof(buf), "%d", rs485_network_device_count());
        lv_label_set_text(s_sys_count_lbl, buf);
        lv_obj_set_style_text_font(s_sys_count_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s_sys_count_lbl, ST_DIM, 0);
        lv_obj_align(s_sys_count_lbl, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    // Row: Scansione Periferiche (pulsante con feedback progresso)
    s_sys_scan_btn = make_action_row_button(list, "Scansione Periferiche",
                                            LV_SYMBOL_REFRESH " Scansiona");
    lv_obj_add_event_cb(s_sys_scan_btn, _sys_scan_click_cb, LV_EVENT_CLICKED, NULL);
    if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
        lv_obj_add_state(s_sys_scan_btn, LV_STATE_DISABLED);
    }

    s_sys_save_btn = make_action_row_button(list, "Salvataggio Impianto",
                                            LV_SYMBOL_SAVE " Salva");
    lv_obj_add_event_cb(s_sys_save_btn, _sys_save_click_cb, LV_EVENT_CLICKED, NULL);
    if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
        lv_obj_add_state(s_sys_save_btn, LV_STATE_DISABLED);
    }

    make_info_row(list, "Versione Firmware", "v1.0.0");
    make_info_row(list, "MCU", "ESP32-S3");

    // Timer polling stato scan (ogni 500 ms)
    s_sys_timer = lv_timer_create(_sys_timer_cb, 500, NULL);
}

static void _show_content(int idx) {
    _clear_datetime_state();
    _clear_network_state();
    _clear_system_state();
    _clear_filtering_state();
    s_plant_name_btn = NULL;
    s_plant_name_lbl = NULL;
    lv_obj_clean(s_right_panel);

    switch (idx) {
        case 0: _build_user_settings(s_right_panel); break;
        case 1: _build_datetime_settings(s_right_panel); break;
        case 2: _build_network_settings(s_right_panel); break;
        case 3: _build_system_setup(s_right_panel); break;
        case 4: _build_ventilation_settings(s_right_panel); break;
        case 5: _build_filtering_settings(s_right_panel); break;
        case 6: make_placeholder(s_right_panel, "Sensori\n\nDa configurare"); break;
        default: break;
    }
}

static void _on_settings_delete(lv_event_t* /*e*/) {
    _clear_datetime_state();
    _clear_network_state();
    _clear_system_state();
    _clear_filtering_state();
    if (s_plant_popup_ctx && s_plant_popup_ctx->mask) {
        lv_obj_del(s_plant_popup_ctx->mask);
    }
    s_plant_name_btn = NULL;
    s_plant_name_lbl = NULL;
}

// Public builder
lv_obj_t* ui_dc_settings_create(void) {
    s_right_panel = NULL;
    s_active = 0;
    s_plant_name_btn = NULL;
    s_plant_name_lbl = NULL;
    _clear_datetime_state();
    _clear_network_state();
    _clear_filtering_state();

    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ST_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, _on_settings_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, ST_WHITE, 0);
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

    lv_obj_t* back_btn = lv_btn_create(hdr);
    lv_obj_set_size(back_btn, 48, 40);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xE0E8F0), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xC8D8E8), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, _back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(back_lbl, ST_TEXT, 0);
    lv_obj_center(back_lbl);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "Impostazioni");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, ST_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 70, 0);

    lv_obj_t* left = lv_obj_create(scr);
    lv_obj_set_size(left, LEFT_W, CONTENT_H);
    lv_obj_set_pos(left, 0, HEADER_H);
    lv_obj_set_style_bg_color(left, ST_LEFT_BG, 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(left, 1, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_color(left, ST_BORDER, 0);
    lv_obj_set_style_radius(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < k_menu_n; i++) {
        const bool selected = (i == 0);

        lv_obj_t* btn = lv_obj_create(left);
        lv_obj_set_size(btn, LEFT_W, ITEM_H);
        lv_obj_set_pos(btn, 0, i * ITEM_H);
        lv_obj_set_style_bg_color(btn, selected ? ST_ORANGE : ST_LEFT_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(btn, ST_BORDER, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xDDE5EE), LV_STATE_PRESSED);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, _menu_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        s_btn[i] = btn;

        lv_obj_t* accent = lv_obj_create(btn);
        lv_obj_set_size(accent, 5, ITEM_H);
        lv_obj_set_pos(accent, 0, 0);
        lv_obj_set_style_bg_color(accent, selected ? ST_ORANGE2 : ST_ACCENT_IDLE, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_accent[i] = accent;

        lv_obj_t* ico = lv_label_create(btn);
        lv_label_set_text(ico, k_menu_icons[i]);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(ico, selected ? lv_color_white() : lv_color_hex(k_icon_clr[i]), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 18, -12);
        s_ico[i] = ico;

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_menu_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : ST_TEXT, 0);
        lv_obj_set_width(lbl, LEFT_W - 22);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 18, 18);
        s_lbl[i] = lbl;
    }

    s_right_panel = lv_obj_create(scr);
    lv_obj_set_size(s_right_panel, RIGHT_W, CONTENT_H);
    lv_obj_set_pos(s_right_panel, LEFT_W, HEADER_H);
    lv_obj_set_style_bg_color(s_right_panel, ST_WHITE, 0);
    lv_obj_set_style_bg_opa(s_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_right_panel, 0, 0);
    lv_obj_set_style_radius(s_right_panel, 0, 0);
    lv_obj_set_style_pad_all(s_right_panel, 0, 0);
    lv_obj_clear_flag(s_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    _show_content(0);
    return scr;
}
