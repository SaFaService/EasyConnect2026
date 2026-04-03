/**
 * @file ui_dc_splash.cpp
 * @brief Splash screen Display Controller — logo fade-in, shimmer, progress bar
 *
 * Layout (1024×600):
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │                                                              │
 *   │              [LOGO ANTRALUX 460×184]                        │
 *   │          fade-in + zoom + shimmer dorato                    │
 *   │                                                              │
 *   │           "Antralux Cloud System"   (tagline)               │
 *   │                                                              │
 *   │       ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░  (progress bar)         │
 *   │                                                              │
 *   │                         v1.0.0                              │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * Timing animazioni:
 *   t=200ms   Logo fade-in (1200ms, ease-out)
 *   t=200ms   Logo zoom 40%→100% (1800ms, ease-out)
 *   t=2000ms  Shimmer sweep L→R (1200ms, ease-in-out)
 *   t=1600ms  Tagline fade-in (600ms, ease-out)
 *   t=400ms   Progress bar 0→100% (5000ms, ease-in-out)
 *             Al completamento (t≈5400ms): carica Home con fade 600ms
 */

#include "ui_dc_splash.h"
#include "ui_dc_home.h"
#include "DisplayLogoAsset.h"
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
#define SP_BG        lv_color_hex(0xFFFFFF)
#define SP_ORANGE    lv_color_hex(0xE84820)
#define SP_ORANGE_HI lv_color_hex(0xFF7035)
#define SP_ORANGE_LO lv_color_hex(0xB02810)
#define SP_TRACK_BG  lv_color_hex(0xDDDDDD)
#define SP_TRACK_SH  lv_color_hex(0xBBBBBB)
#define SP_TEXT_DIM  lv_color_hex(0x7A92B0)

// ─── Dimensioni barra ─────────────────────────────────────────────────────────
#define BAR_W 640
#define BAR_H  18

// ─── Descrittore immagine LVGL (logo RGB565 in flash) ────────────────────────
static const lv_img_dsc_t s_logo_dsc = {
    .header = {
        .cf          = LV_IMG_CF_TRUE_COLOR,
        .always_zero = 0,
        .reserved    = 0,
        .w           = kAntraluxLogoWidth,
        .h           = kAntraluxLogoHeight,
    },
    .data_size = (uint32_t)(kAntraluxLogoWidth * kAntraluxLogoHeight * sizeof(uint16_t)),
    .data      = reinterpret_cast<const uint8_t*>(kAntraluxLogo565),
};

// ─── Callback animazioni ──────────────────────────────────────────────────────

static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
}

static void _cb_zoom(void* obj, int32_t v) {
    lv_img_set_zoom(static_cast<lv_obj_t*>(obj), static_cast<uint16_t>(v));
}

static void _cb_translate_x(void* obj, int32_t v) {
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), (lv_coord_t)v, 0);
}

static void _cb_bar(void* obj, int32_t v) {
    lv_bar_set_value(static_cast<lv_obj_t*>(obj), (int16_t)v, LV_ANIM_OFF);
}

// ─── Ready callback barra → carica Home (via timer differito) ────────────────
static void _load_home_timer(lv_timer_t* t) {
    lv_timer_del(t);
    lv_obj_t* home = ui_dc_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_FADE_ON, 600, 0, true);
}

static void _on_bar_done(lv_anim_t* /*a*/) {
    lv_timer_t* t = lv_timer_create(_load_home_timer, 50, NULL);
    lv_timer_set_repeat_count(t, 1);
}

// ─── Costruzione splash ───────────────────────────────────────────────────────
void ui_dc_splash_create(void) {

    // ── Schermata ─────────────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, SP_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Container logo — clipping attivo per lo shimmer ───────────────────────
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont,
                    (lv_coord_t)kAntraluxLogoWidth,
                    (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);
    lv_obj_set_style_radius(logo_cont, 0, 0);
    lv_obj_clear_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    // LV_OBJ_FLAG_OVERFLOW_VISIBLE NON impostato → figli clippati ai bordi

    // ── Logo ──────────────────────────────────────────────────────────────────
    lv_obj_t* img = lv_img_create(logo_cont);
    lv_img_set_src(img, &s_logo_dsc);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);   // invisibile inizialmente
    lv_img_set_pivot(img, kAntraluxLogoWidth / 2, kAntraluxLogoHeight / 2);

    // ── Shimmer — striscia bianca semitrasparente, scorre L→R ─────────────────
    const lv_coord_t shimmer_w = (lv_coord_t)(kAntraluxLogoWidth / 4);  // ~115px
    lv_obj_t* shimmer = lv_obj_create(logo_cont);
    lv_obj_set_size(shimmer, shimmer_w, (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_set_pos(shimmer, 0, 0);
    lv_obj_set_style_translate_x(shimmer, -shimmer_w, 0);  // fuori bordo sinistro
    lv_obj_set_style_bg_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(shimmer, LV_OPA_40, 0);
    lv_obj_set_style_border_width(shimmer, 0, 0);
    lv_obj_set_style_radius(shimmer, 0, 0);
    lv_obj_clear_flag(shimmer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Tagline ───────────────────────────────────────────────────────────────
    lv_obj_t* tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Antralux Cloud System");
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tagline, SP_TEXT_DIM, 0);
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 60);

    // ── Progress bar ──────────────────────────────────────────────────────────
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Track (scanalatura grigia)
    lv_obj_set_style_bg_color(bar, SP_TRACK_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(bar, SP_TRACK_SH, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);

    // Indicator (riempimento arancione con gradiente)
    lv_obj_set_style_bg_color(bar, SP_ORANGE_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, SP_ORANGE_LO, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_INDICATOR);

    // ── Versione ──────────────────────────────────────────────────────────────
    lv_obj_t* ver = lv_label_create(scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xBBCCDD), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -24);

    // ─── Animazioni ───────────────────────────────────────────────────────────
    lv_anim_t a;

    // 1. Logo fade-in (t=200ms, 1200ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 2. Logo zoom 40%→100% (t=200ms, 1800ms, ease-out)
    //    256 = 100% | 102 ≈ 40%
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_zoom);
    lv_anim_set_values(&a, 102, 256);
    lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 3. Shimmer sweep L→R (t=2000ms, 1200ms, ease-in-out)
    //    translate_x: -shimmer_w → logo_width (logo completamente visibile)
    lv_anim_init(&a);
    lv_anim_set_var(&a, shimmer);
    lv_anim_set_exec_cb(&a, _cb_translate_x);
    lv_anim_set_values(&a, -shimmer_w, (lv_coord_t)kAntraluxLogoWidth);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    // 4. Tagline fade-in (t=1600ms, 600ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, tagline);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 5. Progress bar 0→100% (t=400ms, 5000ms, ease-in-out) → Home al termine
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, _cb_bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 5000);
    lv_anim_set_delay(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_ready_cb(&a, _on_bar_done);
    lv_anim_start(&a);

    // ── Attiva schermata ──────────────────────────────────────────────────────
    lv_scr_load(scr);
}
