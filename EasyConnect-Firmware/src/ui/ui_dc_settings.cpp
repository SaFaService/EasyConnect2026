/**
 * @file ui_dc_settings.cpp
 * @brief Pagina Impostazioni Display Controller
 *
 * Struttura:
 *   - Header 3D (60px): tasto "<" + titolo "Impostazioni"
 *   - Colonna sinistra (300px): 5 voci di menu con icone e accent bar
 *   - Colonna destra (724px): contenuto scrollabile della voce selezionata
 *
 * Voci:
 *   0 - Impostazioni Utente  (tema, lingua, luminosita, data/ora, gradi, buzzer, WiFi)
 *   1 - Setup Sistema         (periferiche, reset IP, versione)
 *   2 - Ventilazione          (placeholder)
 *   3 - Filtraggio            (placeholder)
 *   4 - Sensori               (placeholder)
 *
 * Luminosita: slider 5-100 %, chiama ui_brightness_set() definita in ui_dc_home.cpp
 */

#include "ui_dc_settings.h"
#include "ui_dc_home.h"
#include "lvgl.h"

// ─── Layout ───────────────────────────────────────────────────────────────────
#define HEADER_H  60
#define LEFT_W   300
#define RIGHT_W  724    // 1024 - 300
#define CONTENT_H 540   // 600  - 60
#define ITEM_H   (CONTENT_H / 5)   // 108 px per voce menu

// ─── Palette ──────────────────────────────────────────────────────────────────
#define ST_BG      lv_color_hex(0xEEF3F8)
#define ST_WHITE   lv_color_hex(0xFFFFFF)
#define ST_ORANGE  lv_color_hex(0xE84820)
#define ST_ORANGE2 lv_color_hex(0xB02810)   // arancione scuro (accent bar selezionato)
#define ST_TEXT    lv_color_hex(0x243447)
#define ST_DIM     lv_color_hex(0x7A92B0)
#define ST_BORDER  lv_color_hex(0xDDE5EE)
#define ST_LEFT_BG lv_color_hex(0xF5F8FB)
#define ST_ACCENT_IDLE lv_color_hex(0xDDE5EE)   // accent bar non selezionato

// ─── Dati menu ────────────────────────────────────────────────────────────────
static const char* const k_menu_labels[] = {
    "Impostazioni\nUtente",
    "Setup\nSistema",
    "Ventilazione",
    "Filtraggio",
    "Sensori",
};

static const char* const k_menu_icons[] = {
    LV_SYMBOL_EDIT,      // Impostazioni Utente
    LV_SYMBOL_SETTINGS,  // Setup Sistema
    LV_SYMBOL_REFRESH,   // Ventilazione
    LV_SYMBOL_SHUFFLE,   // Filtraggio
    LV_SYMBOL_EYE_OPEN,  // Sensori
};

// Colori icone (come uint32_t per evitare init statica di lv_color_t)
static const uint32_t k_icon_clr[] = {
    0x3A6BC8,   // blu
    0xE84820,   // arancione
    0x0FA8A8,   // teal
    0x28A745,   // verde
    0x8C44B8,   // viola
};

static constexpr int k_menu_n = 5;

// ─── Stato ────────────────────────────────────────────────────────────────────
static lv_obj_t* s_right_panel = NULL;
static int        s_active     = 0;

// Per ogni voce menu: [0]=container, [1]=accent bar, [2]=icon label, [3]=text label
static lv_obj_t* s_btn[k_menu_n]    = {};
static lv_obj_t* s_accent[k_menu_n] = {};
static lv_obj_t* s_ico[k_menu_n]    = {};
static lv_obj_t* s_lbl[k_menu_n]    = {};

// ─── Mappe btnmatrix (devono restare valide per tutta la vita del widget) ─────
static const char* k_map_tema[]  = {"Chiaro", "Scuro", ""};
static const char* k_map_gradi[] = {"C", "F", ""};

// ─── Forward declaration ──────────────────────────────────────────────────────
static void _show_content(int idx);

// ─── Navigazione: torna alla Home ─────────────────────────────────────────────
static void _back_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* home = ui_dc_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 280, 0, true);
}

// ─── Aggiorna stile visivo voce menu (selezionata / non selezionata) ──────────
static void _apply_menu_style(int idx, bool selected) {
    if (!s_btn[idx]) return;

    lv_color_t bg      = selected ? ST_ORANGE  : ST_LEFT_BG;
    lv_color_t accent  = selected ? ST_ORANGE2 : ST_ACCENT_IDLE;
    lv_color_t ico_clr = selected ? lv_color_white()
                                  : lv_color_hex(k_icon_clr[idx]);
    lv_color_t txt_clr = selected ? lv_color_white() : ST_TEXT;

    lv_obj_set_style_bg_color(s_btn[idx],    bg,     0);
    lv_obj_set_style_bg_color(s_accent[idx], accent, 0);
    lv_obj_set_style_text_color(s_ico[idx],  ico_clr, 0);
    lv_obj_set_style_text_color(s_lbl[idx],  txt_clr, 0);
}

// ─── Clic voce di menu ────────────────────────────────────────────────────────
static void _menu_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_active) return;

    _apply_menu_style(s_active, false);
    s_active = idx;
    _apply_menu_style(s_active, true);
    _show_content(idx);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HELPERS per le righe del pannello destro
// ─────────────────────────────────────────────────────────────────────────────

// Riga base: restituisce il container (caller posiziona il controllo a destra)
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

// Btnmatrix 2 opzioni (segmented control)
static lv_obj_t* make_seg2(lv_obj_t* row, const char** map, int checked_idx) {
    const uint32_t sel = (uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_CHECKED;

    lv_obj_t* bm = lv_btnmatrix_create(row);
    lv_obj_set_size(bm, 180, 38);
    lv_btnmatrix_set_map(bm, map);
    lv_btnmatrix_set_btn_ctrl_all(bm, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(bm, true);
    lv_btnmatrix_set_btn_ctrl(bm, (uint16_t)checked_idx, LV_BTNMATRIX_CTRL_CHECKED);

    lv_obj_set_style_bg_color(bm, ST_ORANGE, sel);
    lv_obj_set_style_text_color(bm, lv_color_white(), sel);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, sel);
    lv_obj_set_style_bg_color(bm, ST_WHITE, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, ST_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_border_color(bm, ST_BORDER, 0);
    lv_obj_set_style_border_width(bm, 1, 0);
    lv_obj_set_style_radius(bm, 8, 0);
    lv_obj_align(bm, LV_ALIGN_RIGHT_MID, 0, 0);
    return bm;
}

// Slider con label percentuale — versione per la luminosita
static void _brightness_cb(lv_event_t* e) {
    lv_obj_t* sl  = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    int val = (int)lv_slider_get_value(sl);
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", val);
    lv_label_set_text(lbl, buf);
    ui_brightness_set(val);
}

// Slider generico (solo aggiorna il label)
static void _slider_cb(lv_event_t* e) {
    lv_obj_t* sl  = lv_event_get_target(e);
    lv_obj_t* lbl = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(sl));
    lv_label_set_text(lbl, buf);
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

static void make_switch_row(lv_obj_t* parent, const char* label_text, bool on) {
    const uint32_t sel = (uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED;
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* sw  = lv_switch_create(row);
    lv_obj_set_size(sw, 56, 28);
    lv_obj_set_style_bg_color(sw, ST_ORANGE, sel);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void make_info_row(lv_obj_t* parent, const char* label_text,
                           const char* value_text) {
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text(val, value_text);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, ST_DIM, 0);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void make_action_row(lv_obj_t* parent, const char* label_text,
                             const char* btn_text) {
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
}

static void make_dropdown_row(lv_obj_t* parent, const char* label_text,
                               const char* options, int sel) {
    lv_obj_t* row = make_row(parent, label_text);
    lv_obj_t* dd  = lv_dropdown_create(row);
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, (uint16_t)sel);
    lv_obj_set_size(dd, 200, 38);
    lv_obj_set_style_bg_color(dd, ST_WHITE, 0);
    lv_obj_set_style_border_color(dd, ST_BORDER, 0);
    lv_obj_set_style_text_font(dd, &lv_font_montserrat_16, 0);
    lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
}

// Contenitore scrollabile per le righe
static lv_obj_t* make_scroll_list(lv_obj_t* parent) {
    lv_obj_t* list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
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

// ─────────────────────────────────────────────────────────────────────────────
//  BUILDER: Impostazioni Utente
// ─────────────────────────────────────────────────────────────────────────────
static void _build_user_settings(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);

    // Tema
    {
        lv_obj_t* row = make_row(list, "Tema");
        make_seg2(row, k_map_tema, 0);
    }
    // Lingua
    make_dropdown_row(list, "Lingua",
                      "Italiano\nEnglish\nEspanol\nFrancais", 0);
    // Luminosita (slider 5-100, chiama ui_brightness_set)
    make_brightness_row(list, 80);

    // Data / Ora
    make_info_row(list, "Data / Ora", "--:-- / --/--/----");

    // Temperatura
    {
        lv_obj_t* row = make_row(list, "Temperatura");
        make_seg2(row, k_map_gradi, 0);
    }
    // Buzzer
    make_switch_row(list, "Buzzer", true);

    // WiFi
    make_switch_row(list, "WiFi", false);

    // IP statico
    make_info_row(list, "Indirizzo IP", "---.---.---.---");

    // API endpoint
    make_info_row(list, "API Endpoint", "Non configurato");
}

// ─────────────────────────────────────────────────────────────────────────────
//  BUILDER: Setup Sistema
// ─────────────────────────────────────────────────────────────────────────────
static void _build_system_setup(lv_obj_t* parent) {
    lv_obj_t* list = make_scroll_list(parent);

    make_info_row(list,   "Periferiche rilevate", "0");
    make_action_row(list, "Scansione Periferiche", LV_SYMBOL_REFRESH " Scansiona");
    make_action_row(list, "Reset Indirizzi IP",    LV_SYMBOL_TRASH   " Reset");
    make_info_row(list,   "Versione Firmware",     "v1.0.0");
    make_info_row(list,   "MCU",                   "ESP32-S3");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Aggiorna il pannello destro con il contenuto selezionato
// ─────────────────────────────────────────────────────────────────────────────
static void _show_content(int idx) {
    lv_obj_clean(s_right_panel);

    switch (idx) {
        case 0: _build_user_settings(s_right_panel); break;
        case 1: _build_system_setup(s_right_panel);  break;
        case 2: make_placeholder(s_right_panel, "Ventilazione\n\nDa configurare"); break;
        case 3: make_placeholder(s_right_panel, "Filtraggio\n\nDa configurare");   break;
        case 4: make_placeholder(s_right_panel, "Sensori\n\nDa configurare");      break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  COSTRUZIONE pagina Impostazioni
// ─────────────────────────────────────────────────────────────────────────────
lv_obj_t* ui_dc_settings_create(void) {

    s_right_panel = NULL;
    s_active      = 0;

    // ── Schermata ─────────────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ST_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header 3D ─────────────────────────────────────────────────────────────
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

    // Tasto indietro
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

    // ── Colonna sinistra ──────────────────────────────────────────────────────
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

    // ── Voci di menu con icona e accent bar ───────────────────────────────────
    for (int i = 0; i < k_menu_n; i++) {
        const bool selected = (i == 0);

        // Container voce
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
        lv_obj_add_event_cb(btn, _menu_click_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);
        s_btn[i] = btn;

        // Accent bar verticale sinistra (5px)
        lv_obj_t* accent = lv_obj_create(btn);
        lv_obj_set_size(accent, 5, ITEM_H);
        lv_obj_set_pos(accent, 0, 0);
        lv_obj_set_style_bg_color(accent,
                                  selected ? ST_ORANGE2 : ST_ACCENT_IDLE, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_radius(accent, 0, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        s_accent[i] = accent;

        // Icona
        lv_obj_t* ico = lv_label_create(btn);
        lv_label_set_text(ico, k_menu_icons[i]);
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(ico,
                                    selected ? lv_color_white()
                                             : lv_color_hex(k_icon_clr[i]), 0);
        lv_obj_align(ico, LV_ALIGN_LEFT_MID, 18, -12);
        s_ico[i] = ico;

        // Testo etichetta
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, k_menu_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl,
                                    selected ? lv_color_white() : ST_TEXT, 0);
        lv_obj_set_width(lbl, LEFT_W - 22);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 18, 18);
        s_lbl[i] = lbl;
    }

    // ── Colonna destra ────────────────────────────────────────────────────────
    s_right_panel = lv_obj_create(scr);
    lv_obj_set_size(s_right_panel, RIGHT_W, CONTENT_H);
    lv_obj_set_pos(s_right_panel, LEFT_W, HEADER_H);
    lv_obj_set_style_bg_color(s_right_panel, ST_WHITE, 0);
    lv_obj_set_style_bg_opa(s_right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_right_panel, 0, 0);
    lv_obj_set_style_radius(s_right_panel, 0, 0);
    lv_obj_set_style_pad_all(s_right_panel, 0, 0);
    lv_obj_clear_flag(s_right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Carica contenuto iniziale
    _show_content(0);

    return scr;
}
