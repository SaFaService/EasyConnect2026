/**
 * @file ui_antralux_home.cpp
 * @brief Home screen demo — 4 pulsanti + tendina pull-down (swipe dall'alto)
 *
 * Layout (1024×600):
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │  EasyConnect                              ↓ Scorri dall'alto           │  ← header 70px
 *   ├─────────────────────────────────────────────────────────────────────────┤
 *   │                                                                         │
 *   │          [ WiFi ]        [ Impostazioni ]                               │
 *   │                                                                         │
 *   │          [ Notifiche ]   [ Stato ]                                      │
 *   │                                                                         │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * Tendina (drawer, 220px, scorre dall'alto):
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                         Hello                                     [X]  │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * Gesture: swipe verso il basso su qualsiasi punto dello schermo → apre drawer
 * Chiusura: pulsante [X] nel drawer oppure tap sull'overlay scuro
 */

#include "ui_antralux_home.h"
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
#define HM_BG       lv_color_hex(0xEEF3F8)
#define HM_WHITE    lv_color_hex(0xFFFFFF)
#define HM_ORANGE   lv_color_hex(0xE84820)
#define HM_TEXT     lv_color_hex(0x243447)
#define HM_SHADOW   lv_color_hex(0xBBCCDD)
#define HM_DIM      lv_color_hex(0x7A92B0)

// ─── Dimensioni drawer ────────────────────────────────────────────────────────
#define DRAWER_H    220

// ─── Stato globale (un'istanza alla volta) ────────────────────────────────────
static lv_obj_t* s_drawer  = NULL;
static lv_obj_t* s_overlay = NULL;
static bool      s_open    = false;

// ─── Callback animazioni ──────────────────────────────────────────────────────
static void _cb_translate_y(void* obj, int32_t v) {
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(obj), (lv_coord_t)v, 0);
}

static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
}

// ─── Chiusura drawer — called dalla ready-callback dell'animazione ─────────────
static void _on_close_done(lv_anim_t* /*a*/) {
    if (s_drawer)  lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
}

static void _close_drawer(void) {
    if (!s_open) return;

    lv_anim_t a;

    // Overlay fade-out
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 280);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    // Drawer slide-up
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, _cb_translate_y);
    lv_anim_set_values(&a, 0, -DRAWER_H);
    lv_anim_set_time(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, _on_close_done);
    lv_anim_start(&a);
}

static void _open_drawer(void) {
    if (s_open) return;
    s_open = true;

    // Prepara drawer: metti sopra lo schermo, poi mostralo
    lv_obj_set_style_translate_y(s_drawer, -DRAWER_H, 0);
    lv_obj_set_style_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_drawer,  LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;

    // Overlay fade-in
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // Drawer slide-down
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, _cb_translate_y);
    lv_anim_set_values(&a, -DRAWER_H, 0);
    lv_anim_set_time(&a, 360);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

// ─── Event callback: gesture su schermo ──────────────────────────────────────
static void _gesture_cb(lv_event_t* e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM) {
        _open_drawer();
    }
}

// ─── Event callback: tap sull'overlay ────────────────────────────────────────
static void _overlay_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _close_drawer();
    }
}

// ─── Event callback: pulsante X ──────────────────────────────────────────────
static void _close_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _close_drawer();
    }
}

// ─── Helper: crea un pulsante home ───────────────────────────────────────────
static lv_obj_t* make_home_btn(lv_obj_t* parent,
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
    // Pressed feedback
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 8, LV_STATE_PRESSED);

    // Icona (simbolo LVGL grande)
    lv_obj_t* ico = lv_label_create(card);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -22);

    // Etichetta
    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, HM_TEXT, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 52);

    return card;
}

// ─── Costruzione Home ─────────────────────────────────────────────────────────
lv_obj_t* ui_antralux_home_create(void) {

    // Reset stato drawer (potrebbe essere una seconda entrata nella home)
    s_drawer  = NULL;
    s_overlay = NULL;
    s_open    = false;

    // ── Schermata ─────────────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, HM_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header ────────────────────────────────────────────────────────────────
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, 70);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_shadow_color(hdr, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(hdr, 18, 0);
    lv_obj_set_style_shadow_ofs_y(hdr, 4, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "EasyConnect");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, HM_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 28, 0);

    lv_obj_t* hint = lv_label_create(hdr);
    lv_label_set_text(hint, LV_SYMBOL_DOWN "  Scorri dall'alto");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, HM_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -28, 0);

    // ── Griglia 2×2 pulsanti ──────────────────────────────────────────────────
    // Container: 480×420 centrato nell'area contenuto (y>70)
    // Offset verso il basso: (600-70)/2 = 265 dal top → y_center = 70+265 = 335
    // screen center y = 300 → offset = +35
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 500, 420);
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 35);
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

    make_home_btn(grid, LV_SYMBOL_WIFI,     "WiFi",          HM_ORANGE);
    make_home_btn(grid, LV_SYMBOL_SETTINGS, "Impostazioni",  lv_color_hex(0x3A6BC8));
    make_home_btn(grid, LV_SYMBOL_BELL,     "Notifiche",     lv_color_hex(0x28A745));
    make_home_btn(grid, LV_SYMBOL_LIST,     "Stato",         lv_color_hex(0x8C44B8));

    // ── Gesture listener su schermo (le gesture fanno bubble dai figli) ───────
    lv_obj_add_event_cb(scr, _gesture_cb, LV_EVENT_GESTURE, NULL);

    // ── Overlay scuro (tap per chiudere drawer) ───────────────────────────────
    // Aggiunto DOPO la grid → z-order superiore
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 1024, 600);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);
    lv_obj_set_style_opa(s_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_overlay, _overlay_click_cb, LV_EVENT_CLICKED, NULL);

    // ── Drawer panel (inizialmente nascosto sopra lo schermo) ─────────────────
    // Aggiunto per ultimo → z-order massimo (sopra overlay)
    s_drawer = lv_obj_create(scr);
    lv_obj_set_size(s_drawer, 1024, DRAWER_H);
    lv_obj_set_pos(s_drawer, 0, 0);
    lv_obj_set_style_translate_y(s_drawer, -DRAWER_H, 0);
    lv_obj_set_style_bg_color(s_drawer, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    lv_obj_set_style_shadow_color(s_drawer, lv_color_hex(0x7090B0), 0);
    lv_obj_set_style_shadow_width(s_drawer, 30, 0);
    lv_obj_set_style_shadow_ofs_y(s_drawer, 12, 0);
    lv_obj_set_style_pad_left(s_drawer, 40, 0);
    lv_obj_set_style_pad_right(s_drawer, 40, 0);
    lv_obj_set_style_pad_top(s_drawer, 0, 0);
    lv_obj_set_style_pad_bottom(s_drawer, 0, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);

    // "Hello" centrato nel drawer
    lv_obj_t* hello = lv_label_create(s_drawer);
    lv_label_set_text(hello, "Hello");
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hello, HM_ORANGE, 0);
    lv_obj_align(hello, LV_ALIGN_CENTER, 0, 0);

    // Indicatore "scorri su" sotto il testo
    lv_obj_t* swipe_hint = lv_label_create(s_drawer);
    lv_label_set_text(swipe_hint, LV_SYMBOL_UP "  scorri su per chiudere");
    lv_obj_set_style_text_font(swipe_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(swipe_hint, HM_DIM, 0);
    lv_obj_align(swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    // Pulsante X in alto a destra
    lv_obj_t* close_btn = lv_btn_create(s_drawer);
    lv_obj_set_size(close_btn, 48, 48);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xE0E0E0), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 24, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, _close_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, HM_TEXT, 0);
    lv_obj_center(close_lbl);

    return scr;
}
