/**
 * @file ui_splash.cpp
 * @brief Splash screen animato EasyConnect / Antralux
 *
 * Sequenza animazioni:
 *   t=   0ms  Sfondo scuro, tutto invisibile
 *   t= 200ms  Arco esterno inizia a disegnarsi (1400ms, ease-out)
 *   t= 400ms  Arco interno inizia a disegnarsi (1200ms, ease-out)
 *   t= 800ms  Testo "ANTRALUX" fade-in (900ms)
 *   t=1600ms  Sottotitolo "EasyConnect" slide-up + fade-in (700ms)
 *   t=2000ms  Label versione fade-in (500ms)
 *   t= 200ms  Barra di progresso inizia a riempirsi (5800ms → 100%)
 *   t=6000ms  Transizione LV_SCR_LOAD_ANIM_FADE_ON → Home screen
 */

#include "ui_splash.h"
#include "ui_styles.h"
#include "ui_home.h"

// ─── Callback animazioni ──────────────────────────────────────────────────────

static void anim_opa_cb(void* var, int32_t val) {
    lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)val, 0);
}

static void anim_arc_end_angle_cb(void* var, int32_t val) {
    // Animiamo l'angolo finale dell'arco da 0 a 360
    lv_arc_set_end_angle((lv_obj_t*)var, (int16_t)(270 + val));
}

static void anim_bar_cb(void* var, int32_t val) {
    lv_bar_set_value((lv_obj_t*)var, (int16_t)val, LV_ANIM_OFF);
    // Aggiorna la label percentuale se esiste (userData = label)
    lv_obj_t* lbl = (lv_obj_t*)lv_obj_get_user_data((lv_obj_t*)var);
    if (lbl) {
        char buf[24];
        lv_snprintf(buf, sizeof(buf), "Avvio in corso... %d%%", (int)val);
        lv_label_set_text(lbl, buf);
    }
}

static void anim_y_cb(void* var, int32_t val) {
    lv_obj_set_y((lv_obj_t*)var, val);
}

// ─── Callback transizione verso Home ─────────────────────────────────────────

static void on_splash_complete(lv_anim_t* a) {
    (void)a;
    // Crea la home screen e la carica con una transizione fade
    lv_obj_t* home = ui_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_FADE_ON, 600, 0, true);
}

// ─── Costruzione splash screen ────────────────────────────────────────────────

void ui_splash_create(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ── Container logo (centrato, leggermente in alto) ──────────────────────
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont, 280, 280);
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);

    // ── Arco esterno (Ø 260px) ─────────────────────────────────────────────
    lv_obj_t* arc_outer = lv_arc_create(logo_cont);
    lv_obj_set_size(arc_outer, 260, 260);
    lv_obj_align(arc_outer, LV_ALIGN_CENTER, 0, 0);
    // Background arc: anello sottile scuro
    lv_obj_set_style_arc_color(arc_outer, UI_COLOR_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_outer, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_outer, LV_OPA_40, LV_PART_MAIN);
    // Indicator arc: teal brillante
    lv_obj_set_style_arc_color(arc_outer, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_outer, 7, LV_PART_INDICATOR);
    // Rimuovi knob
    lv_obj_remove_style(arc_outer, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc_outer, LV_OPA_TRANSP, 0);
    // Imposta angoli iniziali: start=270 (12 o'clock), end=270 (arco vuoto)
    lv_arc_set_bg_angles(arc_outer, 0, 360);
    lv_arc_set_start_angle(arc_outer, 270);
    lv_arc_set_end_angle(arc_outer, 270);
    lv_obj_set_style_opa(arc_outer, LV_OPA_TRANSP, 0);  // inizia invisibile

    // ── Arco interno (Ø 200px) ─────────────────────────────────────────────
    lv_obj_t* arc_inner = lv_arc_create(logo_cont);
    lv_obj_set_size(arc_inner, 200, 200);
    lv_obj_align(arc_inner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_arc_color(arc_inner, UI_COLOR_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_inner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc_inner, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_inner, UI_COLOR_ACCENT2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_inner, 5, LV_PART_INDICATOR);
    lv_obj_remove_style(arc_inner, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc_inner, LV_OPA_TRANSP, 0);
    lv_arc_set_bg_angles(arc_inner, 0, 360);
    lv_arc_set_start_angle(arc_inner, 270);
    lv_arc_set_end_angle(arc_inner, 270);
    lv_obj_set_style_opa(arc_inner, LV_OPA_TRANSP, 0);

    // ── Testo "ANTRALUX" (dentro logo_cont, centrato) ──────────────────────
    lv_obj_t* lbl_brand = lv_label_create(logo_cont);
    lv_label_set_text(lbl_brand, "ANTRALUX");
    lv_obj_set_style_text_font(lbl_brand, UI_FONT_LARGE, 0);
    lv_obj_set_style_text_color(lbl_brand, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_letter_space(lbl_brand, 6, 0);
    lv_obj_align(lbl_brand, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(lbl_brand, LV_OPA_TRANSP, 0);

    // ── Sottotitolo "EasyConnect" (sotto logo_cont) ─────────────────────────
    lv_obj_t* lbl_sub = lv_label_create(scr);
    lv_label_set_text(lbl_sub, "EasyConnect  Display");
    lv_obj_set_style_text_font(lbl_sub, UI_FONT_SUBTITLE, 0);
    lv_obj_set_style_text_color(lbl_sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_letter_space(lbl_sub, 3, 0);
    lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_style_opa(lbl_sub, LV_OPA_TRANSP, 0);

    // ── Label versione ──────────────────────────────────────────────────────
    lv_obj_t* lbl_ver = lv_label_create(scr);
    lv_label_set_text(lbl_ver, "v" UI_SANDBOX_VERSION);
    lv_obj_set_style_text_font(lbl_ver, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_ver, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_ver, LV_ALIGN_CENTER, 0, 148);
    lv_obj_set_style_opa(lbl_ver, LV_OPA_TRANSP, 0);

    // ── Barra di progresso ──────────────────────────────────────────────────
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 580, 14);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -56);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    // Stile sfondo barra
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, LV_PART_MAIN);
    // Stile indicatore barra
    lv_obj_set_style_bg_color(bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);

    // ── Label percentuale ──────────────────────────────────────────────────
    lv_obj_t* lbl_pct = lv_label_create(scr);
    lv_label_set_text(lbl_pct, "Avvio in corso... 0%");
    lv_obj_set_style_text_font(lbl_pct, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_pct, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_pct, LV_ALIGN_BOTTOM_MID, 0, -78);
    // Link bar -> label via user_data per aggiornamento nel callback
    lv_obj_set_user_data(bar, lbl_pct);

    // ────────────────────────────────────────────────────────────────────────
    // ANIMAZIONI
    // ────────────────────────────────────────────────────────────────────────
    lv_anim_t a;

    // 1. Fade-in arco esterno (inizia t=0, dura 300ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_outer);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 300);
    lv_anim_set_delay(&a, 0);
    lv_anim_start(&a);

    // 2. Disegno arco esterno 0→360° (t=0, 1500ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_outer);
    lv_anim_set_exec_cb(&a, anim_arc_end_angle_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 1500);
    lv_anim_set_delay(&a, 0);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 3. Fade-in arco interno (t=200ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_inner);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 300);
    lv_anim_set_delay(&a, 200);
    lv_anim_start(&a);

    // 4. Disegno arco interno 0→360° (t=200ms, 1300ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_inner);
    lv_anim_set_exec_cb(&a, anim_arc_end_angle_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 1300);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 5. Fade-in testo ANTRALUX (t=700ms, 900ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_brand);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 900);
    lv_anim_set_delay(&a, 700);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 6. Slide-up + fade-in sottotitolo (t=1500ms, 700ms)
    //    Parte da y+30 e va a y normale
    int sub_y_final = lv_obj_get_y(lbl_sub);
    lv_obj_set_y(lbl_sub, sub_y_final + 30);  // posizione iniziale (piu' in basso)

    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_sub);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, sub_y_final + 30, sub_y_final);
    lv_anim_set_time(&a, 700);
    lv_anim_set_delay(&a, 1500);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_sub);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 700);
    lv_anim_set_delay(&a, 1500);
    lv_anim_start(&a);

    // 7. Fade-in label versione (t=2000ms, 500ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_ver);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 500);
    lv_anim_set_delay(&a, 2000);
    lv_anim_start(&a);

    // 8. Barra di progresso 0→100 in 5800ms (con delay 200ms)
    //    Al completamento chiama on_splash_complete → carica Home
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, anim_bar_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 5800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_ready_cb(&a, on_splash_complete);
    lv_anim_start(&a);

    // ── Carica lo splash come schermata attiva ──────────────────────────────
    lv_scr_load(scr);
}
