/**
 * @file ui_antralux_splash.cpp
 * @brief Splash screen — logo Antralux con fade-in, transizione Home dopo 5 s
 *
 * Layout (1024×600):
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                                                         │
 *   │            [LOGO ANTRALUX 460×184 — fade-in]           │
 *   │                                                         │
 *   │              "Antralux Cloud System"  (tagline)         │
 *   │                                                         │
 *   └─────────────────────────────────────────────────────────┘
 *
 * Animazioni:
 *   t=200ms   Logo fade-in (opa 0→255, 1 200 ms, ease-out)
 *   t=200ms   Logo zoom-in (40%→100%, 1 800 ms, ease-out)
 *   t=1600ms  Tagline fade-in (0→255, 600 ms, ease-out)
 *   t=5000ms  lv_timer → carica Home con fade (600 ms)
 */

#include "ui_antralux_splash.h"
#include "ui_antralux_home.h"
#include "DisplayLogoAsset.h"
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
#define SP_BG       lv_color_hex(0xFFFFFF)
#define SP_ORANGE   lv_color_hex(0xE84820)
#define SP_TEXT_DIM lv_color_hex(0x7A92B0)

// ─── Descrittore immagine LVGL ────────────────────────────────────────────────
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

// ─── Timer callback: transizione verso Home ───────────────────────────────────
static void _go_to_home(lv_timer_t* /*t*/) {
    lv_obj_t* home = ui_antralux_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_FADE_ON, 600, 0, true);
}

// ─── Costruzione splash ───────────────────────────────────────────────────────
void ui_antralux_splash_create(void) {

    // ── Schermata base bianca ─────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, SP_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Container logo (clipping dello shimmer) ───────────────────────────────
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont,
                    (lv_coord_t)kAntraluxLogoWidth,
                    (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);
    lv_obj_set_style_radius(logo_cont, 0, 0);
    lv_obj_clear_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Immagine logo (figlio del container) ──────────────────────────────────
    lv_obj_t* img = lv_img_create(logo_cont);
    lv_img_set_src(img, &s_logo_dsc);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);          // parte invisibile
    lv_img_set_pivot(img,
                     kAntraluxLogoWidth / 2,
                     kAntraluxLogoHeight / 2);

    // ── Tagline ───────────────────────────────────────────────────────────────
    lv_obj_t* tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Antralux Cloud System");
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tagline, SP_TEXT_DIM, 0);
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 80);

    // ── Versione ──────────────────────────────────────────────────────────────
    lv_obj_t* ver = lv_label_create(scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xBBCCDD), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -20);

    // ─── Animazioni ───────────────────────────────────────────────────────────
    lv_anim_t a;

    // 1. Fade-in logo (t=200ms, 1200ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 2. Zoom-in logo 40%→100% (t=200ms, 1800ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_zoom);
    lv_anim_set_values(&a, 102, 256);   // 102/256 ≈ 40%
    lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 3. Fade-in tagline (t=1600ms, 600ms, ease-out)
    lv_anim_init(&a);
    lv_anim_set_var(&a, tagline);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── Timer one-shot: vai alla Home dopo 5 s ────────────────────────────────
    lv_timer_t* t = lv_timer_create(_go_to_home, 5000, NULL);
    lv_timer_set_repeat_count(t, 1);

    // ── Carica splash ─────────────────────────────────────────────────────────
    lv_scr_load(scr);
}
