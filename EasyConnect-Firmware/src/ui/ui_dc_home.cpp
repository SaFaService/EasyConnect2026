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
 * Idle dim:   dopo 5 min senza touch porta la luminosita al 10 %.
 *             Al primo tocco successivo ripristina il valore precedente.
 */

#include "ui_dc_home.h"
#include "ui_dc_settings.h"
#include "display_port/io_extension.h"
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
#define IDLE_MS      (5UL * 60UL * 1000UL)   // 5 minuti
#define IDLE_DIM_PCT 10                        // luminosita al dim (%)

// ─── Stato globale luminosita (persiste tra le schermate) ────────────────────
static int         g_brightness       = 80;   // 5-100 %
static bool        g_dimmed           = false;
static int         g_saved_brightness = 80;
static lv_timer_t* g_idle_timer       = NULL;

// ─── Stato orologio (locale alla schermata home) ──────────────────────────────
static lv_obj_t*   s_time_lbl    = NULL;
static lv_timer_t* s_clock_timer = NULL;

// ─── Controllo luminosita ─────────────────────────────────────────────────────
void ui_brightness_set(int pct) {
    if (pct < 5)   pct = 5;
    if (pct > 100) pct = 100;
    g_brightness = pct;
    IO_EXTENSION_Pwm_Output((uint8_t)pct);
}

// ─── Timer idle dim (ogni 2 s, non viene mai eliminato) ──────────────────────
static void _idle_cb(lv_timer_t* /*t*/) {
    uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);

    if (!g_dimmed && inactive_ms > IDLE_MS) {
        // Scende al 10 %
        g_saved_brightness = g_brightness;
        g_dimmed = true;
        IO_EXTENSION_Pwm_Output((uint8_t)(IDLE_DIM_PCT / 100.0f * 255.0f + 0.5f));
    } else if (g_dimmed && inactive_ms < 2000UL) {
        // Touchscreen usato di nuovo: ripristina
        g_dimmed = false;
        ui_brightness_set(g_saved_brightness);
    }
}

// ─── Orologio (ogni secondo) ──────────────────────────────────────────────────
static void _clock_cb(lv_timer_t* /*t*/) {
    if (!s_time_lbl) return;
    uint32_t up = millis() / 1000;
    char buf[12];
    lv_snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                (unsigned)(up / 3600),
                (unsigned)((up % 3600) / 60),
                (unsigned)(up % 60));
    lv_label_set_text(s_time_lbl, buf);
}

// ─── Pulizia quando la home viene distrutta ───────────────────────────────────
static void _on_home_delete(lv_event_t* /*e*/) {
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    s_time_lbl = NULL;
    // g_idle_timer NON viene eliminato: deve continuare su tutte le schermate
}

// ─── Navigazione verso Impostazioni ──────────────────────────────────────────
static void _open_settings_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* settings = ui_dc_settings_create();
    lv_scr_load_anim(settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 280, 0, true);
}

// ─── Helper: card pulsante home ───────────────────────────────────────────────
static lv_obj_t* make_card(lv_obj_t*   parent,
                            const char* symbol,
                            const char* label_text,
                            lv_color_t  icon_color) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 210, 190);
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 22, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 8, LV_STATE_PRESSED);

    lv_obj_t* ico = lv_label_create(card);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, HM_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 52);

    return card;
}

// ─── Costruzione Home ─────────────────────────────────────────────────────────
lv_obj_t* ui_dc_home_create(void) {

    lv_obj_t* scr = lv_obj_create(NULL);
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

    s_time_lbl = lv_label_create(hdr);
    lv_label_set_text(s_time_lbl, "00:00:00");
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_time_lbl, HM_TEXT, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_LEFT_MID, 24, -7);

    lv_obj_t* date_lbl = lv_label_create(hdr);
    lv_label_set_text(date_lbl, "03 Apr 2026");
    lv_obj_set_style_text_font(date_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(date_lbl, HM_DIM, 0);
    lv_obj_align(date_lbl, LV_ALIGN_LEFT_MID, 24, 13);

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
    lv_obj_t* temp_lbl = lv_label_create(hdr);
    lv_label_set_text(temp_lbl, "-- C");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(temp_lbl, HM_TEXT, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_RIGHT_MID, -24, -7);

    // Umidita
    lv_obj_t* hum_lbl = lv_label_create(hdr);
    lv_label_set_text(hum_lbl, "-- %RH");
    lv_obj_set_style_text_font(hum_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hum_lbl, HM_DIM, 0);
    lv_obj_align(hum_lbl, LV_ALIGN_RIGHT_MID, -24, 13);

    // ── Timer orologio ────────────────────────────────────────────────────────
    s_clock_timer = lv_timer_create(_clock_cb, 1000, NULL);

    // ── Griglia 2x2 pulsanti ──────────────────────────────────────────────────
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 500, 420);
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, 40, 0);
    lv_obj_set_style_pad_row(grid, 40, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    make_card(grid, LV_SYMBOL_WIFI,  "WiFi",      HM_ORANGE);

    lv_obj_t* settings_card = make_card(grid, LV_SYMBOL_SETTINGS,
                                        "Impostazioni", lv_color_hex(0x3A6BC8));
    lv_obj_add_event_cb(settings_card, _open_settings_cb, LV_EVENT_CLICKED, NULL);

    make_card(grid, LV_SYMBOL_BELL, "Notifiche", lv_color_hex(0x28A745));
    make_card(grid, LV_SYMBOL_LIST, "Stato",     lv_color_hex(0x8C44B8));

    return scr;
}
