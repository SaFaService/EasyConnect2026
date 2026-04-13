#include "ui_dc_network.h"
#include "ui_dc_home.h"
#include "ui_dc_maintenance.h"
#include "rs485_network.h"
#include "icons/icons_index.h"
#include "lvgl.h"
#include <math.h>

// ─── Palette (stessa della Home) ─────────────────────────────────────────────
#define NT_BG     lv_color_hex(0xEEF3F8)
#define NT_WHITE  lv_color_hex(0xFFFFFF)
#define NT_ORANGE lv_color_hex(0xE84820)
#define NT_ORANGE2 lv_color_hex(0xB02810)
#define NT_TEXT   lv_color_hex(0x243447)
#define NT_DIM    lv_color_hex(0x7A92B0)
#define NT_BORDER lv_color_hex(0xDDE5EE)
#define NT_SHADOW lv_color_hex(0xBBCCDD)

#define HEADER_H  60

// ─── Stato locale alla schermata ─────────────────────────────────────────────
static lv_obj_t*   s_list_cont      = NULL;  // contenitore righe
static lv_obj_t*   s_status_lbl     = NULL;  // label stato/contatore in header
static lv_obj_t*   s_scan_btn_lbl   = NULL;  // label del pulsante Scansiona
static lv_obj_t*   s_save_btn_lbl   = NULL;  // label del pulsante Salva impianto
static lv_timer_t* s_refresh_timer  = NULL;  // timer aggiornamento lista
static int         s_last_count     = -1;    // ultimo contatore noto

static const char* _relay_mode_to_text(uint8_t mode) {
    switch (mode) {
        case 1: return "Lampada";
        case 2: return "UVC";
        case 3: return "Elettrostatico";
        case 4: return "Gas";
        case 5: return "Comando";
        default: return "Relay";
    }
}

static const char* _sensor_mode_to_text(uint8_t mode) {
    switch (mode) {
        case 1: return "Temperatura e Umidita'";
        case 2: return "Pressione";
        case 3: return "Temperatura, Umidita' e Pressione";
        default: return "N/D";
    }
}

static const char* _air_role_text(uint8_t group) {
    switch (group) {
        case 1: return "Aspirazione";
        case 2: return "Immissione";
        default: return "Gruppo non configurato";
    }
}

static void _format_signed_tenths(float value, const char* unit, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!isfinite(value)) {
        lv_snprintf(out, out_sz, "N/D");
        return;
    }

    const float scaled_f = value * 10.0f;
    if (!isfinite(scaled_f) || scaled_f > 214748000.0f || scaled_f < -214748000.0f) {
        lv_snprintf(out, out_sz, "N/D");
        return;
    }

    long scaled = lroundf(scaled_f);
    const bool negative = scaled < 0;
    unsigned long abs_scaled = (scaled < 0) ? (unsigned long)(-scaled) : (unsigned long)scaled;

    if (unit && unit[0]) {
        lv_snprintf(out, out_sz, "%s%lu.%lu %s",
                    negative ? "-" : "",
                    abs_scaled / 10UL,
                    abs_scaled % 10UL,
                    unit);
    } else {
        lv_snprintf(out, out_sz, "%s%lu.%lu",
                    negative ? "-" : "",
                    abs_scaled / 10UL,
                    abs_scaled % 10UL);
    }
}

static const char* _device_type_text(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) return "Relay";
    if (dev.type == Rs485DevType::SENSOR) {
        if (dev.sensor_profile == Rs485SensorProfile::AIR_010) return "0/10V";
        return "Sensore I2C";
    }
    return "Sconosciuto";
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

static bool _air010_has_feedback_fault(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::SENSOR) return false;
    if (dev.sensor_profile != Rs485SensorProfile::AIR_010) return false;
    if (!dev.online || !dev.sensor_active) return false;
    if (dev.sensor_feedback_fault_latched) return true;

    String state = dev.sensor_state;
    state.toUpperCase();
    return !dev.sensor_feedback_ok && state.indexOf("FAULT") >= 0;
}

static bool _device_has_any_error(const Rs485Device& dev) {
    if (!dev.data_valid) return true;
    return _device_has_comm_issue(dev) ||
           _relay_has_safety_issue(dev) ||
           _relay_has_feedback_fault(dev) ||
           _air010_has_feedback_fault(dev);
}

static bool _device_is_frozen(const Rs485Device& dev) {
    return !dev.data_valid || (dev.in_plant && !dev.online);
}

static void _device_error_summary(const Rs485Device& dev, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;

    if (_device_is_frozen(dev)) {
        if (!dev.data_valid) {
            lv_snprintf(out, out_sz, "Configurazione incompleta");
        } else {
            lv_snprintf(out, out_sz, "Nessuna comunicazione 485");
        }
        return;
    }
    if (!dev.in_plant) {
        lv_snprintf(out, out_sz, "Rilevata ma non salvata");
        return;
    }
    if (_relay_has_safety_issue(dev)) {
        lv_snprintf(out, out_sz, "Errore: sicurezza aperta");
        return;
    }
    if (_relay_has_feedback_fault(dev)) {
        lv_snprintf(out, out_sz, "Errore: feedback mancato");
        return;
    }
    if (_air010_has_feedback_fault(dev)) {
        lv_snprintf(out, out_sz, "Errore: feedback inverter");
        return;
    }

    out[0] = '\0';
}

static bool _device_is_on(const Rs485Device& dev) {
    if (_device_is_frozen(dev)) return false;
    if (dev.type == Rs485DevType::RELAY) return dev.relay_on;
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        return dev.sensor_active;
    }
    return false;
}

static const lv_img_dsc_t* _device_icon(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) {
        switch (dev.relay_mode) {
            case 1: return dev.relay_on ? &light_on : &light_off;
            case 2: return dev.relay_on ? &uvc_on : &uvc_off;
            case 3: return dev.relay_on ? &electrostatic_on : &electrostatic_off;
            case 4: return dev.relay_on ? &gas_on : &gas_off;
            case 5: return &settings; // Placeholder per modalita' COMANDO
            default: return dev.relay_on ? &light_on : &light_off;
        }
    }

    if (dev.type == Rs485DevType::SENSOR) {
        if (dev.sensor_profile == Rs485SensorProfile::AIR_010) {
            if (dev.group != 1 && dev.group != 2) return &airintake_off;
            const bool extraction = (dev.group == 1);
            if (extraction) return dev.sensor_active ? &airextraction_on : &airextraction_off;
            return dev.sensor_active ? &airintake_on : &airintake_off;
        }
        return &pressure;
    }

    return &settings;
}

static void _device_subtitle(const Rs485Device& dev, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    _device_error_summary(dev, out, out_sz);
    if (out[0]) return;
    if (dev.type == Rs485DevType::RELAY) {
        lv_snprintf(out, out_sz, "%s • %s", _relay_mode_to_text(dev.relay_mode), dev.relay_on ? "ON" : "OFF");
        return;
    }
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        const char* role = _air_role_text(dev.group);
        lv_snprintf(out, out_sz, "%s • %s", role, dev.sensor_active ? "ON" : "OFF");
        return;
    }
    if (dev.type == Rs485DevType::SENSOR) {
        lv_snprintf(out, out_sz, "Sensore I2C • Gruppo %u", (unsigned)dev.group);
        return;
    }
    lv_snprintf(out, out_sz, "%s", _device_type_text(dev));
}

// ─── Navigazione ─────────────────────────────────────────────────────────────
static void _back_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* home = ui_dc_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

// ─── Popup dettaglio dispositivo ─────────────────────────────────────────────
struct NetworkDeleteCtx {
    lv_obj_t* detail_mask;
    uint8_t address;
    char sn[32];
};

static void _detail_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* mask = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (mask) lv_obj_del(mask);
}

static void _detail_delete_success_cb(void* user_data) {
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(user_data);
    if (!ctx) return;
    if (rs485_network_remove_device_from_plant(ctx->address, ctx->sn)) {
        if (ctx->detail_mask) lv_obj_del(ctx->detail_mask);
        if (s_refresh_timer) lv_timer_reset(s_refresh_timer);
    }
    delete ctx;
}

static void _detail_delete_pin_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;
    NetworkDeleteCtx* action_ctx = new NetworkDeleteCtx();
    if (!action_ctx) return;
    *action_ctx = *ctx;
    ui_dc_maintenance_request_pin("Eliminazione periferica", "Elimina periferica",
                                  _detail_delete_success_cb, action_ctx);
}

static void _detail_delete_ctx_free_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(lv_event_get_user_data(e));
    delete ctx;
}

static void _open_device_detail(const Rs485Device& dev) {
    lv_obj_t* parent = lv_scr_act();
    if (!parent) return;

    // Maschera semitrasparente
    lv_obj_t* mask = lv_obj_create(parent);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    // Card centrale
    lv_obj_t* card = lv_obj_create(mask);
    lv_obj_set_size(card, 620, 460);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, NT_WHITE, 0);
    lv_obj_set_style_border_color(card, NT_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_pad_all(card, 24, 0);
    lv_obj_set_style_shadow_color(card, NT_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo
    char title[48];
    lv_snprintf(title, sizeof(title), "Periferica IP: %d", (int)dev.address);
    lv_obj_t* ttl = lv_label_create(card);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ttl, NT_TEXT, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, 0, 0);

    // Separatore
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_size(sep, 572, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_style_bg_color(sep, NT_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Helper per riga info
    auto add_row = [&](const char* label, const char* value, int y_offset) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, NT_DIM, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 50 + y_offset);

        lv_obj_t* val = lv_label_create(card);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, NT_TEXT, 0);
        lv_obj_align(val, LV_ALIGN_TOP_LEFT, 180, 50 + y_offset);
    };

    const char* type_str = _device_type_text(dev);
    const char* sensor_type_str = _sensor_mode_to_text(dev.sensor_mode);
    const char* detail_type_value =
        (dev.type == Rs485DevType::SENSOR && dev.sensor_profile != Rs485SensorProfile::AIR_010)
            ? sensor_type_str
            : type_str;
    char sn_buf[34];
    lv_snprintf(sn_buf, sizeof(sn_buf), "%s", dev.sn[0] ? dev.sn : "N/D");

    lv_obj_t* icon = lv_img_create(card);
    lv_img_set_src(icon, _device_icon(dev));
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, 0, 4);

    add_row("Seriale:",       sn_buf,        0);
    add_row("Versione FW:",   dev.version[0] ? dev.version : "N/D", 30);
    add_row("Impianto:",      dev.in_plant ? "Salvata nella fotografia" : "Rilevata ma ignorata", 60);
    add_row("Comunicazione:", dev.online ? "OK" : "Nessuna comunicazione 485", 90);
    add_row("Tipo:",          detail_type_value, 120);

    if (dev.type == Rs485DevType::RELAY) {
        add_row("Modalita':", _relay_mode_to_text(dev.relay_mode), 160);
        add_row("Relay:", dev.relay_on ? "ON" : "OFF", 190);
        add_row("Safety:", dev.relay_safety_closed ? "CHIUSA" : "APERTA", 220);
        add_row("Feedback:", dev.relay_feedback_ok ? "OK" : "FAIL", 250);
        add_row("Stato:", dev.relay_state[0] ? dev.relay_state : "N/D", 280);
    } else if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        add_row("Ruolo:", _air_role_text(dev.group), 160);
        add_row("Stato:", dev.sensor_active ? "ON" : "OFF", 190);
        char spd_buf[16];
        lv_snprintf(spd_buf, sizeof(spd_buf), "%d %%", (int)(dev.h + 0.5f));
        add_row("Velocita':", spd_buf, 220);
        add_row("Feedback:", dev.sensor_feedback_ok ? "OK" : "FAIL", 250);
        add_row("Run state:", dev.sensor_state[0] ? dev.sensor_state : "N/D", 280);
        char grp_buf[8];
        lv_snprintf(grp_buf, sizeof(grp_buf), "%u", dev.group);
        add_row("Gruppo:", grp_buf, 310);
    } else if (dev.type == Rs485DevType::SENSOR) {
        char t_buf[24], h_buf[24], p_buf[24];
        _format_signed_tenths(dev.t, "C", t_buf, sizeof(t_buf));
        _format_signed_tenths(dev.h, "%RH", h_buf, sizeof(h_buf));
        _format_signed_tenths(dev.p, "Pa", p_buf, sizeof(p_buf));
        add_row("Temperatura:", t_buf, 160);
        add_row("Umidità:",     h_buf, 190);
        add_row("Pressione:",   p_buf, 220);
        char grp_buf[8];
        lv_snprintf(grp_buf, sizeof(grp_buf), "%u", dev.group);
        add_row("Gruppo:",      grp_buf, 250);
    }

    String error_text = "Nessun errore attivo";
    if (!dev.data_valid) {
        error_text = "Periferica congelata.\nSeriale, gruppo o payload non coerenti.";
    } else if (_device_is_frozen(dev)) {
        error_text = "Periferica freezata.\nUltimi dati salvati mostrati a video.";
    } else if (!dev.in_plant) {
        error_text = "Periferica non inclusa nella fotografia impianto.";
    } else if (_device_has_comm_issue(dev)) {
        error_text = "Mancata comunicazione";
    } else if (_relay_has_safety_issue(dev) && _relay_has_feedback_fault(dev)) {
        error_text = "Sicurezza aperta\nFeedback mancato";
    } else if (_relay_has_safety_issue(dev)) {
        error_text = "Sicurezza aperta";
    } else if (_relay_has_feedback_fault(dev)) {
        error_text = "Feedback mancato";
    } else if (_air010_has_feedback_fault(dev)) {
        error_text = "Feedback inverter mancato";
    }

    lv_obj_t* err_lbl = lv_label_create(card);
    lv_label_set_text(err_lbl, "Errore:");
    lv_obj_set_style_text_font(err_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(err_lbl, NT_DIM, 0);
    lv_obj_align(err_lbl, LV_ALIGN_TOP_LEFT, 0, 340);

    lv_obj_t* err_val = lv_label_create(card);
    lv_label_set_text(err_val, error_text.c_str());
    lv_obj_set_width(err_val, 360);
    lv_obj_set_style_text_font(err_val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(err_val, (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE : NT_TEXT, 0);
    lv_obj_align(err_val, LV_ALIGN_TOP_LEFT, 180, 370);

    if (_device_has_any_error(dev)) {
        lv_obj_t* warn = lv_img_create(card);
        lv_img_set_src(warn, &warning);
        lv_img_set_zoom(warn, 180);
        lv_obj_align(warn, LV_ALIGN_TOP_RIGHT, -4, 44);
    }

    if (dev.in_plant) {
        NetworkDeleteCtx* del_ctx = new NetworkDeleteCtx();
        if (del_ctx) {
            memset(del_ctx, 0, sizeof(*del_ctx));
            del_ctx->detail_mask = mask;
            del_ctx->address = dev.address;
            strncpy(del_ctx->sn, dev.sn, sizeof(del_ctx->sn) - 1);

            lv_obj_t* delete_btn = lv_btn_create(card);
            lv_obj_set_size(delete_btn, 220, 40);
            lv_obj_align(delete_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0xB93A32), 0);
            lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x8F2B24), LV_STATE_PRESSED);
            lv_obj_set_style_radius(delete_btn, 8, 0);
            lv_obj_set_style_shadow_width(delete_btn, 0, 0);
            lv_obj_set_style_border_width(delete_btn, 0, 0);
            lv_obj_add_event_cb(delete_btn, _detail_delete_pin_cb, LV_EVENT_CLICKED, del_ctx);
            lv_obj_add_event_cb(delete_btn, _detail_delete_ctx_free_cb, LV_EVENT_DELETE, del_ctx);

            lv_obj_t* delete_lbl = lv_label_create(delete_btn);
            lv_label_set_text(delete_lbl, "Elimina periferica");
            lv_obj_set_style_text_font(delete_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(delete_lbl, lv_color_white(), 0);
            lv_obj_center(delete_lbl);
        }
    }

    // Pulsante Chiudi
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, NT_ORANGE, 0);
    lv_obj_set_style_bg_color(close_btn, NT_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Chiudi");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);
    lv_obj_add_event_cb(close_btn, _detail_close_cb, LV_EVENT_CLICKED, mask);
}

// ─── Click su una riga dispositivo ───────────────────────────────────────────
static void _row_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    Rs485Device dev;
    if (rs485_network_get_device(idx, dev)) {
        _open_device_detail(dev);
    }
}

// ─── Costruzione delle righe della lista ─────────────────────────────────────
static lv_obj_t* _make_device_row(lv_obj_t* parent, const Rs485Device& dev, int idx) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 72);
    lv_obj_set_style_bg_color(row, NT_WHITE, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, NT_BORDER, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, _row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    // Icona dispositivo (con sfondo circolare)
    lv_obj_t* badge = lv_obj_create(row);
    lv_obj_set_size(badge, 50, 50);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF4F8FC), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, NT_BORDER, 0);
    lv_obj_set_style_radius(badge, 25, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* icon = lv_img_create(badge);
    lv_img_set_src(icon, _device_icon(dev));
    lv_obj_center(icon);

    if (_device_has_any_error(dev)) {
        lv_obj_t* warn = lv_img_create(badge);
        lv_img_set_src(warn, &warning);
        lv_img_set_zoom(warn, 170);
        lv_obj_align(warn, LV_ALIGN_BOTTOM_RIGHT, 1, 1);
    }

    // Seriale + sottotitolo
    lv_obj_t* sn_lbl = lv_label_create(row);
    lv_label_set_text(sn_lbl, dev.sn[0] ? dev.sn : "N/D");
    lv_obj_set_style_text_font(sn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sn_lbl, NT_TEXT, 0);
    lv_obj_align(sn_lbl, LV_ALIGN_LEFT_MID, 68, -10);

    lv_obj_t* type_lbl = lv_label_create(row);
    char subtitle[64];
    _device_subtitle(dev, subtitle, sizeof(subtitle));
    lv_label_set_text(type_lbl, subtitle);
    lv_obj_set_style_text_font(type_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(type_lbl, (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE : NT_DIM, 0);
    lv_obj_align(type_lbl, LV_ALIGN_LEFT_MID, 68, 12);

    // IP a destra
    char ip_buf[16];
    lv_snprintf(ip_buf, sizeof(ip_buf), "IP %d", (int)dev.address);
    lv_obj_t* ip_lbl = lv_label_create(row);
    lv_label_set_text(ip_lbl, ip_buf);
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ip_lbl,
        (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE :
        (_device_is_on(dev) ? NT_ORANGE : NT_DIM), 0);
    lv_obj_align(ip_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    return row;
}

// ─── Ricostruzione contenuto lista ───────────────────────────────────────────
static void _rebuild_list() {
    if (!s_list_cont) return;

    lv_obj_clean(s_list_cont);

    const int count = rs485_network_device_count();
    const int plant_count = rs485_network_plant_device_count();
    s_last_count = count;

    // Aggiorna label contatore nell'header
    if (s_status_lbl) {
        if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
            char buf[32];
            lv_snprintf(buf, sizeof(buf), "Scansione in corso... %d/200",
                        rs485_network_scan_progress());
            lv_label_set_text(s_status_lbl, buf);
        } else {
            char buf[48];
            lv_snprintf(buf, sizeof(buf), "Impianto %d  •  Runtime %d", plant_count, count);
            lv_label_set_text(s_status_lbl, buf);
        }
    }

    if (count == 0) {
        lv_obj_t* empty = lv_label_create(s_list_cont);
        const Rs485ScanState st = rs485_network_scan_state();
        lv_label_set_text(empty,
            st == Rs485ScanState::IDLE    ? "Nessuna periferica runtime.\nEseguire una scansione protetta per rilevare le schede." :
            st == Rs485ScanState::RUNNING ? "Scansione in corso..." :
                                           "Nessuna periferica rilevata.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, NT_DIM, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty);
        return;
    }

    Rs485Device dev;
    for (int i = 0; i < count; i++) {
        if (rs485_network_get_device(i, dev)) {
            _make_device_row(s_list_cont, dev, i);
        }
    }
}

// ─── Timer aggiornamento (durante scansione) ──────────────────────────────────
static void _refresh_timer_cb(lv_timer_t* /*t*/) {
    const Rs485ScanState state = rs485_network_scan_state();

    // Aggiorna label pulsante Scansiona
    if (s_scan_btn_lbl) {
        if (state == Rs485ScanState::RUNNING) {
            char buf[28];
            lv_snprintf(buf, sizeof(buf), "Scansione %d/200",
                        rs485_network_scan_progress());
            lv_label_set_text(s_scan_btn_lbl, buf);
        } else {
            lv_label_set_text(s_scan_btn_lbl, LV_SYMBOL_REFRESH " Scansiona");
        }
    }
    if (s_save_btn_lbl) {
        lv_label_set_text(s_save_btn_lbl, LV_SYMBOL_SAVE " Salva");
    }

    _rebuild_list();

    // Quando la scansione e' terminata e la lista e' stabile, il timer puo'
    // andare a frequenza ridotta (evita lavoro inutile).
    if (state != Rs485ScanState::RUNNING) {
        lv_timer_set_period(s_refresh_timer, 2000);
    } else {
        lv_timer_set_period(s_refresh_timer, 500);
    }
}

static void _scan_btn_confirm_cb(void* /*user_data*/) {
    rs485_network_scan_start();
    lv_timer_set_period(s_refresh_timer, 500);
    _rebuild_list();
}

static void _save_btn_confirm_cb(void* /*user_data*/) {
    (void)rs485_network_save_current_as_plant();
    lv_timer_set_period(s_refresh_timer, 500);
    _rebuild_list();
}

// ─── Avvio scansione dal pulsante nella schermata ────────────────────────────
static void _scan_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Scansione periferiche", "Avvia scansione",
                                  _scan_btn_confirm_cb, NULL);
}

static void _save_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Salvataggio impianto", "Salva impianto",
                                  _save_btn_confirm_cb, NULL);
}

// ─── Pulizia alla delete della schermata ─────────────────────────────────────
static void _on_delete(lv_event_t* /*e*/) {
    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    s_list_cont    = NULL;
    s_status_lbl   = NULL;
    s_scan_btn_lbl = NULL;
    s_save_btn_lbl = NULL;
    s_last_count   = -1;
}

// ─── Costruzione schermata ────────────────────────────────────────────────────
lv_obj_t* ui_dc_network_create(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, NT_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, _on_delete, LV_EVENT_DELETE, NULL);

    // ── Header ───────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, NT_WHITE, 0);
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
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Pulsante Indietro
    lv_obj_t* back_btn = lv_btn_create(hdr);
    lv_obj_set_size(back_btn, 48, 48);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xDDE5EE), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xC0D0E0), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_lbl, NT_TEXT, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, _back_cb, LV_EVENT_CLICKED, NULL);

    // Titolo
    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "Dispositivi RS485");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, NT_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 68, 0);

    // Label stato (contatore / progresso scan)
    s_status_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status_lbl, NT_DIM, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 280, 0);

    // Pulsante Salva impianto
    lv_obj_t* save_btn = lv_btn_create(hdr);
    lv_obj_set_size(save_btn, 170, 40);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -214, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x355D9B), 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x24467A), LV_STATE_PRESSED);
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    s_save_btn_lbl = lv_label_create(save_btn);
    lv_label_set_text(s_save_btn_lbl, LV_SYMBOL_SAVE " Salva");
    lv_obj_set_style_text_font(s_save_btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_save_btn_lbl, lv_color_white(), 0);
    lv_obj_center(s_save_btn_lbl);
    lv_obj_add_event_cb(save_btn, _save_btn_cb, LV_EVENT_CLICKED, NULL);

    // Pulsante Scansiona (in alto a destra)
    lv_obj_t* scan_btn = lv_btn_create(hdr);
    lv_obj_set_size(scan_btn, 190, 40);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_bg_color(scan_btn, NT_ORANGE, 0);
    lv_obj_set_style_bg_color(scan_btn, NT_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(scan_btn, 8, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);
    s_scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(s_scan_btn_lbl, LV_SYMBOL_REFRESH " Scansiona");
    lv_obj_set_style_text_font(s_scan_btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_scan_btn_lbl, lv_color_white(), 0);
    lv_obj_center(s_scan_btn_lbl);
    lv_obj_add_event_cb(scan_btn, _scan_btn_cb, LV_EVENT_CLICKED, NULL);

    // ── Area lista ───────────────────────────────────────────────────────────
    lv_obj_t* body = lv_obj_create(scr);
    lv_obj_set_size(body, 1024, 540);
    lv_obj_set_pos(body, 0, HEADER_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);

    // Contenitore scrollabile delle righe
    s_list_cont = lv_obj_create(body);
    lv_obj_set_size(s_list_cont, 984, LV_SIZE_CONTENT);
    lv_obj_align(s_list_cont, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_bg_color(s_list_cont, NT_WHITE, 0);
    lv_obj_set_style_bg_opa(s_list_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_list_cont, NT_BORDER, 0);
    lv_obj_set_style_border_width(s_list_cont, 1, 0);
    lv_obj_set_style_radius(s_list_cont, 12, 0);
    lv_obj_set_style_pad_all(s_list_cont, 0, 0);
    lv_obj_set_style_shadow_color(s_list_cont, NT_SHADOW, 0);
    lv_obj_set_style_shadow_width(s_list_cont, 16, 0);
    lv_obj_set_style_shadow_ofs_y(s_list_cont, 4, 0);
    lv_obj_set_layout(s_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);

    // Timer di aggiornamento
    s_last_count    = -1;
    s_refresh_timer = lv_timer_create(_refresh_timer_cb, 500, NULL);

    // Costruisce la lista iniziale
    _rebuild_list();

    return scr;
}
