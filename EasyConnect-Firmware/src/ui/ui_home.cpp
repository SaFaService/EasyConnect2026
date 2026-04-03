/**
 * @file ui_home.cpp
 * @brief Home screen EasyConnect UI Sandbox - 4 tab LVGL
 *
 * Tab 1 - Controlli : pulsanti ON/OFF/TOGGLE, slider, switch x3, dropdown
 * Tab 2 - Misure    : arc gauge DeltaP 0-150Pa + line chart storico
 * Tab 3 - Touch     : multi-touch tester fino a 5 dita (GT911)
 * Tab 4 - Info      : info hardware, display, firmware
 */

#include <initializer_list>
#include "ui_home.h"
#include "ui_styles.h"
#include "ui_notifications.h"
#include "touch.h"   // per esp_lcd_touch_read_data / get_coordinates

// Handle touch globale (definito nell'entrypoint firmware display)
extern esp_lcd_touch_handle_t g_tp_handle;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: crea una card con titolo
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t* make_card(lv_obj_t* parent, const char* title,
                            int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(card, UI_PADDING, 0);
    lv_obj_set_style_pad_top(card, 36, 0);  // spazio per il titolo
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title && title[0]) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, -20);
    }
    return card;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 – CONTROLLI
// ─────────────────────────────────────────────────────────────────────────────

// Callback pulsante ON/OFF/TOGGLE
static lv_obj_t* g_btn_status_label = NULL;
static bool g_relay_state = false;

static void btn_on_cb(lv_event_t* e) {
    (void)e;
    g_relay_state = true;
    if (g_btn_status_label)
        lv_label_set_text(g_btn_status_label, LV_SYMBOL_OK "  Relay: ON");
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_SUCCESS, 0);
}
static void btn_off_cb(lv_event_t* e) {
    (void)e;
    g_relay_state = false;
    if (g_btn_status_label)
        lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
}
static void btn_toggle_cb(lv_event_t* e) {
    (void)e;
    g_relay_state = !g_relay_state;
    if (g_btn_status_label) {
        if (g_relay_state) {
            lv_label_set_text(g_btn_status_label, LV_SYMBOL_OK "  Relay: ON");
            lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
            lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
        }
    }
}

// Callback slider
static lv_obj_t* g_slider_label = NULL;
static void slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    if (g_slider_label) {
        char buf[32];
        lv_snprintf(buf, sizeof(buf), "Valore: %d%%", (int)lv_slider_get_value(slider));
        lv_label_set_text(g_slider_label, buf);
    }
}

static lv_obj_t* make_styled_btn(lv_obj_t* parent, const char* text,
                                  lv_color_t color, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 120, 44);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(lbl);
    return btn;
}

static void tab_controlli_create(lv_obj_t* parent) {
    // Layout: 2 righe x 2 colonne di card (ciascuna ~490x220)
    const int32_t CW = 478, CH = 218, GAP = 10;
    const int32_t X1 = 0, X2 = CW + GAP;
    const int32_t Y1 = 0, Y2 = CH + GAP;

    // ── Card 1: Pulsanti relay ──────────────────────────────────────────────
    lv_obj_t* c1 = make_card(parent, "PULSANTI  RELAY", X1, Y1, CW, CH);

    lv_obj_t* btn_on = make_styled_btn(c1, LV_SYMBOL_OK " ACCENDI",
                                        UI_COLOR_SUCCESS, btn_on_cb);
    lv_obj_set_pos(btn_on, 0, 10);

    lv_obj_t* btn_off = make_styled_btn(c1, LV_SYMBOL_CLOSE " SPEGNI",
                                         UI_COLOR_ERROR, btn_off_cb);
    lv_obj_set_pos(btn_off, 130, 10);

    lv_obj_t* btn_tog = make_styled_btn(c1, LV_SYMBOL_REFRESH " TOGGLE",
                                         UI_COLOR_ACCENT2, btn_toggle_cb);
    lv_obj_set_pos(btn_tog, 260, 10);

    g_btn_status_label = lv_label_create(c1);
    lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
    lv_obj_set_style_text_font(g_btn_status_label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
    lv_obj_set_pos(g_btn_status_label, 0, 70);

    // ── Card 2: Slider ─────────────────────────────────────────────────────
    lv_obj_t* c2 = make_card(parent, "SLIDER", X2, Y1, CW, CH);

    g_slider_label = lv_label_create(c2);
    lv_label_set_text(g_slider_label, "Valore: 50%");
    lv_obj_set_style_text_font(g_slider_label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_slider_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(g_slider_label, 0, 10);

    lv_obj_t* slider = lv_slider_create(c2);
    lv_obj_set_width(slider, CW - 2 * UI_PADDING);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_set_pos(slider, 0, 56);
    lv_obj_set_style_bg_color(slider, UI_COLOR_BG_CARD2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT_PRIMARY, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Label min/max
    lv_obj_t* lbl_min = lv_label_create(c2);
    lv_label_set_text(lbl_min, "0%");
    lv_obj_set_style_text_font(lbl_min, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_min, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_min, 0, 82);

    lv_obj_t* lbl_max = lv_label_create(c2);
    lv_label_set_text(lbl_max, "100%");
    lv_obj_set_style_text_font(lbl_max, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_max, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align_to(lbl_max, slider, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

    // ── Card 3: Switch ─────────────────────────────────────────────────────
    lv_obj_t* c3 = make_card(parent, "SWITCH", X1, Y2, CW, CH);

    const char* sw_labels[] = { "Luci", "Ventilazione", "Relay Aux" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* sw = lv_switch_create(c3);
        lv_obj_set_pos(sw, 0, i * 50 + 10);
        lv_obj_set_style_bg_color(sw, UI_COLOR_BG_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, UI_COLOR_ACCENT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sw, UI_COLOR_TEXT_PRIMARY, LV_PART_KNOB);

        lv_obj_t* sw_lbl = lv_label_create(c3);
        lv_label_set_text(sw_lbl, sw_labels[i]);
        lv_obj_set_style_text_font(sw_lbl, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(sw_lbl, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_align_to(sw_lbl, sw, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    }

    // ── Card 4: Dropdown ───────────────────────────────────────────────────
    lv_obj_t* c4 = make_card(parent, "DROPDOWN", X2, Y2, CW, CH);

    lv_obj_t* lbl_dd = lv_label_create(c4);
    lv_label_set_text(lbl_dd, "Modalita' scheda:");
    lv_obj_set_style_text_font(lbl_dd, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_dd, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(lbl_dd, 0, 10);

    lv_obj_t* dd = lv_dropdown_create(c4);
    lv_dropdown_set_options(dd, "Standalone\nRewamping\nDisplay\nRelay UVC\nSensore\nMotore");
    lv_obj_set_width(dd, CW - 2 * UI_PADDING);
    lv_obj_set_pos(dd, 0, 42);
    lv_obj_set_style_bg_color(dd, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_text_color(dd, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(dd, UI_COLOR_BORDER_ACTIVE, 0);
    lv_obj_set_style_text_font(dd, UI_FONT_LABEL, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 – MISURE (Arc gauge + line chart)
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t* g_gauge_arc      = NULL;
static lv_obj_t* g_gauge_val_lbl  = NULL;
static lv_obj_t* g_gauge_unit_lbl = NULL;
static lv_obj_t* g_chart          = NULL;
static lv_chart_series_t* g_chart_ser = NULL;
static float    g_deltap_sim      = 0.0f;
static float    g_sim_dir         = 1.0f;

static void update_gauge_color(lv_obj_t* arc, float val) {
    lv_color_t col;
    if (val < 50.0f)       col = UI_COLOR_SUCCESS;
    else if (val < 100.0f) col = UI_COLOR_WARNING;
    else                   col = UI_COLOR_ERROR;
    lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
}

static void misure_timer_cb(lv_timer_t* t) {
    (void)t;
    if (!g_gauge_arc || !g_chart || !g_chart_ser) return;

    // Simulazione DeltaP: onde sinusoidali lente 0-140 Pa
    g_deltap_sim += g_sim_dir * 0.8f;
    if (g_deltap_sim >= 140.0f) { g_deltap_sim = 140.0f; g_sim_dir = -1.0f; }
    if (g_deltap_sim <= 0.0f)   { g_deltap_sim = 0.0f;   g_sim_dir =  1.0f; }

    // Aggiorna arco: range 0-150 → angolo 140..400 (260° totali)
    int32_t arc_val = (int32_t)((g_deltap_sim / 150.0f) * 260.0f);
    lv_arc_set_end_angle(g_gauge_arc, (int16_t)(140 + arc_val));
    update_gauge_color(g_gauge_arc, g_deltap_sim);

    // Aggiorna label valore
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%.1f", (double)g_deltap_sim);
    lv_label_set_text(g_gauge_val_lbl, buf);

    // Aggiorna chart (aggiungi punto e shifta)
    lv_chart_set_next_value(g_chart, g_chart_ser, (lv_coord_t)g_deltap_sim);
}

static void tab_misure_create(lv_obj_t* parent) {
    // ── Gauge arc a sinistra ────────────────────────────────────────────────
    lv_obj_t* gauge_card = lv_obj_create(parent);
    lv_obj_set_pos(gauge_card, 0, 0);
    lv_obj_set_size(gauge_card, 440, 448);
    lv_obj_set_style_bg_color(gauge_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(gauge_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(gauge_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(gauge_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(gauge_card, UI_PADDING, 0);
    lv_obj_clear_flag(gauge_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(gauge_card);
    lv_label_set_text(lbl_title, "DELTA P  SIMULATO");
    lv_obj_set_style_text_font(lbl_title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Arco gauge: bg 140°→400° (260° sweep), indicator parte da 140°
    g_gauge_arc = lv_arc_create(gauge_card);
    lv_obj_set_size(g_gauge_arc, 300, 300);
    lv_obj_align(g_gauge_arc, LV_ALIGN_CENTER, 0, 10);
    lv_arc_set_bg_angles(g_gauge_arc, 140, 400);
    lv_arc_set_start_angle(g_gauge_arc, 140);
    lv_arc_set_end_angle(g_gauge_arc, 140);
    lv_obj_set_style_arc_color(g_gauge_arc, UI_COLOR_BG_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_gauge_arc, 22, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g_gauge_arc, UI_COLOR_SUCCESS, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(g_gauge_arc, 22, LV_PART_INDICATOR);
    lv_obj_remove_style(g_gauge_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(g_gauge_arc, LV_OPA_TRANSP, 0);

    // Valore numerico al centro dell'arco
    g_gauge_val_lbl = lv_label_create(gauge_card);
    lv_label_set_text(g_gauge_val_lbl, "0.0");
    lv_obj_set_style_text_font(g_gauge_val_lbl, UI_FONT_TITLE, 0);
    lv_obj_set_style_text_color(g_gauge_val_lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(g_gauge_val_lbl, LV_ALIGN_CENTER, 0, 10);

    g_gauge_unit_lbl = lv_label_create(gauge_card);
    lv_label_set_text(g_gauge_unit_lbl, "Pa");
    lv_obj_set_style_text_font(g_gauge_unit_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_gauge_unit_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(g_gauge_unit_lbl, LV_ALIGN_CENTER, 0, 46);

    // Label soglie (0 / 75 / 150)
    const char* thresholds[] = { "0", "75", "150 Pa" };
    int32_t th_x[] = { -130, 0, 106 };
    int32_t th_y[] = { 120, 138, 120 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t* tl = lv_label_create(gauge_card);
        lv_label_set_text(tl, thresholds[i]);
        lv_obj_set_style_text_font(tl, UI_FONT_TINY, 0);
        lv_obj_set_style_text_color(tl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_align(tl, LV_ALIGN_CENTER, th_x[i], th_y[i]);
    }

    // ── Chart a destra ──────────────────────────────────────────────────────
    lv_obj_t* chart_card = lv_obj_create(parent);
    lv_obj_set_pos(chart_card, 450, 0);
    lv_obj_set_size(chart_card, 536, 448);
    lv_obj_set_style_bg_color(chart_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(chart_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(chart_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(chart_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(chart_card, UI_PADDING, 0);
    lv_obj_set_style_pad_top(chart_card, 36, 0);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_chart = lv_label_create(chart_card);
    lv_label_set_text(lbl_chart, "STORICO  DELTA P");
    lv_obj_set_style_text_font(lbl_chart, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_chart, UI_COLOR_ACCENT, 0);
    lv_obj_align(lbl_chart, LV_ALIGN_TOP_LEFT, 0, -20);

    g_chart = lv_chart_create(chart_card);
    lv_obj_set_size(g_chart, 504, 360);
    lv_obj_align(g_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 150);
    lv_chart_set_point_count(g_chart, 60);
    lv_chart_set_div_line_count(g_chart, 5, 6);
    // Stile chart
    lv_obj_set_style_bg_color(g_chart, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_bg_opa(g_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_chart, 0, 0);
    lv_obj_set_style_line_color(g_chart, UI_COLOR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_line_opa(g_chart, LV_OPA_30, LV_PART_MAIN);
    // Serie dati
    g_chart_ser = lv_chart_add_series(g_chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(g_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(g_chart, 0, LV_PART_INDICATOR);  // niente punti, solo linea

    // Pre-popola con zeri
    for (int i = 0; i < 60; i++)
        lv_chart_set_next_value(g_chart, g_chart_ser, 0);

    // Timer aggiornamento misure (ogni 400ms)
    lv_timer_create(misure_timer_cb, 400, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 – TOUCH (multi-touch visualizer fino a 5 dita)
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t* g_touch_circles[5]     = { NULL };
static lv_obj_t* g_touch_coord_lbl      = NULL;
static lv_obj_t* g_touch_count_lbl      = NULL;
static lv_color_t g_touch_colors[5];

static void touch_timer_cb(lv_timer_t* t) {
    (void)t;
    if (!g_tp_handle) return;

    uint16_t x[5] = {0}, y[5] = {0};
    uint8_t  cnt = 0;

    // Leggi dati touch direttamente dal GT911 (fino a 5 punti)
    esp_lcd_touch_read_data(g_tp_handle);
    esp_lcd_touch_get_coordinates(g_tp_handle, x, y, NULL, &cnt, 5);

    // Aggiorna label contatore
    char buf[40];
    lv_snprintf(buf, sizeof(buf), LV_SYMBOL_EYE_OPEN "  Punti attivi: %d / 5", (int)cnt);
    lv_label_set_text(g_touch_count_lbl, buf);

    // Aggiorna cerchi
    char coords[128] = "";
    for (int i = 0; i < 5; i++) {
        if (i < cnt) {
            // Cerchio visibile nella posizione del dito
            // Correggi l'offset dei tab (header 52 + tabbar 50 = 102px)
            int32_t cx = (int32_t)x[i] - 40;   // raggio 40px
            int32_t cy = (int32_t)y[i] - 102 - 40;
            if (cx < 0) cx = 0;
            if (cy < 0) cy = 0;
            lv_obj_set_pos(g_touch_circles[i], cx, cy);
            lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_COVER, 0);

            char tmp[24];
            lv_snprintf(tmp, sizeof(tmp), " P%d(%d,%d)", i + 1, (int)x[i], (int)y[i]);
            strncat(coords, tmp, sizeof(coords) - strlen(coords) - 1);
        } else {
            lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_TRANSP, 0);
        }
    }
    if (cnt == 0) lv_snprintf(coords, sizeof(coords), "Tocca il display...");
    lv_label_set_text(g_touch_coord_lbl, coords);
}

static void tab_touch_create(lv_obj_t* parent) {
    // Colori per i 5 diti
    g_touch_colors[0] = UI_COLOR_TOUCH_0;
    g_touch_colors[1] = UI_COLOR_TOUCH_1;
    g_touch_colors[2] = UI_COLOR_TOUCH_2;
    g_touch_colors[3] = UI_COLOR_TOUCH_3;
    g_touch_colors[4] = UI_COLOR_TOUCH_4;

    // Sfondo scuro per l'area touch
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Label istruzioni (al centro, inizialmente visibile)
    lv_obj_t* hint = lv_label_create(parent);
    lv_label_set_text(hint,
        LV_SYMBOL_PREV " TOCCA CON PIU' DITA " LV_SYMBOL_NEXT "\n"
        "Supporta fino a 5 tocchi simultanei (GT911)");
    lv_obj_set_style_text_font(hint, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    // Panel info in alto a sinistra
    lv_obj_t* info_card = lv_obj_create(parent);
    lv_obj_set_size(info_card, 360, 80);
    lv_obj_set_pos(info_card, 0, 0);
    lv_obj_set_style_bg_color(info_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(info_card, LV_OPA_80, 0);
    lv_obj_set_style_radius(info_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(info_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(info_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(info_card, 10, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    g_touch_count_lbl = lv_label_create(info_card);
    lv_label_set_text(g_touch_count_lbl, LV_SYMBOL_EYE_OPEN "  Punti attivi: 0 / 5");
    lv_obj_set_style_text_font(g_touch_count_lbl, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(g_touch_count_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_align(g_touch_count_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    g_touch_coord_lbl = lv_label_create(info_card);
    lv_label_set_text(g_touch_coord_lbl, "Tocca il display...");
    lv_obj_set_style_text_font(g_touch_coord_lbl, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g_touch_coord_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(g_touch_coord_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Crea 5 cerchi (uno per dito), inizialmente nascosti
    for (int i = 0; i < 5; i++) {
        g_touch_circles[i] = lv_obj_create(parent);
        lv_obj_set_size(g_touch_circles[i], 80, 80);
        lv_obj_set_style_radius(g_touch_circles[i], UI_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(g_touch_circles[i], g_touch_colors[i], 0);
        lv_obj_set_style_bg_opa(g_touch_circles[i], LV_OPA_60, 0);
        lv_obj_set_style_border_color(g_touch_circles[i], UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_width(g_touch_circles[i], 3, 0);
        lv_obj_set_style_border_opa(g_touch_circles[i], LV_OPA_80, 0);
        lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(g_touch_circles[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(g_touch_circles[i], LV_OBJ_FLAG_SCROLLABLE);

        // Numero del dito al centro del cerchio
        char num[4];
        lv_snprintf(num, sizeof(num), "%d", i + 1);
        lv_obj_t* nl = lv_label_create(g_touch_circles[i]);
        lv_label_set_text(nl, num);
        lv_obj_set_style_text_font(nl, UI_FONT_SUBTITLE, 0);
        lv_obj_set_style_text_color(nl, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(nl);
    }

    // Timer lettura touch (30ms = ~33Hz)
    lv_timer_create(touch_timer_cb, 30, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 – INFO
// ─────────────────────────────────────────────────────────────────────────────

static void tab_info_create(lv_obj_t* parent) {
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // Card info hardware
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_size(card, 980, 430);
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(card, UI_PADDING * 2, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "INFORMAZIONI  SISTEMA");
    lv_obj_set_style_text_font(title, UI_FONT_SUBTITLE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Separatore
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_size(sep, 940, 2);
    lv_obj_set_style_bg_color(sep, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 36);

    // Righe info: label_key + label_val in 2 colonne
    struct InfoRow { const char* key; const char* val; };
    InfoRow rows[] = {
        { "Display",      "Waveshare ESP32-S3-Touch-LCD-7B"       },
        { "Risoluzione",  "1024 x 600 pixel  (WSVGA)"             },
        { "Interfaccia",  "RGB565 parallelo 16-bit, 30 MHz"        },
        { "Touch",        "GT911 capacitivo, max 5 punti"          },
        { "MCU",          "ESP32-S3, dual-core 240 MHz"            },
        { "Memoria",      "16 MB Flash QIO  +  OPI PSRAM"          },
        { "LVGL",         "v8.4.0  (doppio buffer, anti-tearing)"  },
        { "Firmware",     "UI Sandbox v" UI_SANDBOX_VERSION        },
        { "Build",        UI_SANDBOX_BUILD                         },
        { "Progetto",     "EasyConnect 2026  -  Antralux"          },
    };
    const int n = sizeof(rows) / sizeof(rows[0]);

    for (int i = 0; i < n; i++) {
        int32_t y_pos = 50 + i * 36;

        lv_obj_t* lk = lv_label_create(card);
        lv_label_set_text(lk, rows[i].key);
        lv_obj_set_style_text_font(lk, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lk, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_pos(lk, 0, y_pos);

        lv_obj_t* lv_ = lv_label_create(card);
        lv_label_set_text(lv_, rows[i].val);
        lv_obj_set_style_text_font(lv_, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lv_, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_pos(lv_, 220, y_pos);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HOME SCREEN PRINCIPALE
// ─────────────────────────────────────────────────────────────────────────────

lv_obj_t* ui_home_create(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ── HEADER ──────────────────────────────────────────────────────────────
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, UI_SCREEN_W, UI_HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_HEADER, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_hor(header, UI_PADDING, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Logo testo header (sinistra)
    lv_obj_t* h_brand = lv_label_create(header);
    lv_label_set_text(h_brand, "ANTRALUX");
    lv_obj_set_style_text_font(h_brand, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(h_brand, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_letter_space(h_brand, 3, 0);
    lv_obj_align(h_brand, LV_ALIGN_LEFT_MID, 0, 0);

    // Sottotitolo header
    lv_obj_t* h_sub = lv_label_create(header);
    lv_label_set_text(h_sub, "EasyConnect Display  |  UI Sandbox");
    lv_obj_set_style_text_font(h_sub, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(h_sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(h_sub, LV_ALIGN_CENTER, 0, 0);

    // Versione firmware (destra)
    lv_obj_t* h_ver = lv_label_create(header);
    lv_label_set_text(h_ver, "FW " UI_SANDBOX_VERSION);
    lv_obj_set_style_text_font(h_ver, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(h_ver, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(h_ver, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── TABVIEW ─────────────────────────────────────────────────────────────
    // LV_DIR_TOP = tab bar in alto (sotto l'header)
    lv_obj_t* tabview = lv_tabview_create(scr, LV_DIR_TOP, UI_TAB_BAR_H);
    lv_obj_set_pos(tabview, 0, UI_HEADER_H);
    lv_obj_set_size(tabview, UI_SCREEN_W, UI_SCREEN_H - UI_HEADER_H);
    lv_obj_set_style_bg_color(tabview, UI_COLOR_BG_MAIN, 0);

    // Stile tab bar (il contenitore dei bottoni)
    lv_obj_t* tab_bar = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_bar, UI_COLOR_HEADER, 0);
    lv_obj_set_style_text_color(tab_bar, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(tab_bar, UI_FONT_LABEL, 0);
    // Tab attivo
    lv_obj_set_style_bg_color(tab_bar, UI_COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_bar, UI_COLOR_BG_DEEP, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_bar, UI_COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_bar, 3, LV_PART_ITEMS | LV_STATE_CHECKED);

    // ── Crea i 4 tab ────────────────────────────────────────────────────────
    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "  Controlli");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, LV_SYMBOL_CHARGE   "  Misure");
    lv_obj_t* tab3 = lv_tabview_add_tab(tabview, LV_SYMBOL_EDIT     "  Touch");
    lv_obj_t* tab4 = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST     "  Info");

    // Stile contenuto tab
    lv_obj_t* tab_content = lv_tabview_get_content(tabview);
    lv_obj_set_style_bg_color(tab_content, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_pad_all(tab_content, 0, 0);

    for (lv_obj_t* t : { tab1, tab2, tab3, tab4 }) {
        lv_obj_set_style_bg_color(t, UI_COLOR_BG_MAIN, 0);
        lv_obj_set_style_pad_all(t, UI_PADDING, 0);
        lv_obj_set_style_pad_top(t, UI_PADDING, 0);
    }

    // Popola i tab
    tab_controlli_create(tab1);
    tab_misure_create(tab2);
    tab_touch_create(tab3);
    tab_info_create(tab4);

    ui_notif_panel_init(scr, header);

    return scr;
}
