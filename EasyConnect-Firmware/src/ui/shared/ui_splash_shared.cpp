#include "ui_splash_shared.h"
#include "ui/ui_dc_home.h"
#include "DisplayLogoAsset.h"
#include "dc_data_model.h"
#include "lvgl.h"

// ─── Palette (identica a ui_dc_splash.cpp) ───────────────────────────────────
#define SP_BG        lv_color_hex(0xFFFFFF)
#define SP_ORANGE_HI lv_color_hex(0xFF7035)
#define SP_ORANGE_LO lv_color_hex(0xB02810)
#define SP_TRACK_BG  lv_color_hex(0xDDDDDD)
#define SP_TRACK_SH  lv_color_hex(0xBBBBBB)
#define SP_TEXT_DIM  lv_color_hex(0x7A92B0)

#define BAR_W 640
#define BAR_H  18

// Mapping step (0–10) → percentuale barra (arch. §10.2)
static const int k_step_pct[11] = {0, 15, 25, 35, 45, 55, 65, 75, 85, 95, 100};

// Tempo minimo visualizzazione splash — attende almeno il completamento dello shimmer
static constexpr uint32_t k_min_splash_ms = 3500;

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

// ─── Stato splash (singleton — una sola splash alla volta) ───────────────────
struct SplashCtx {
    lv_obj_t* bar;
    lv_obj_t* info_label;
    uint32_t  created_tick;
    int       prev_step;
    bool      home_loaded;
};
static SplashCtx s_ctx;

// ─── Callback animazioni ──────────────────────────────────────────────────────
static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)v, 0);
}
static void _cb_zoom(void* obj, int32_t v) {
    lv_img_set_zoom(static_cast<lv_obj_t*>(obj), (uint16_t)v);
}
static void _cb_translate_x(void* obj, int32_t v) {
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), (lv_coord_t)v, 0);
}

// ─── Timer di sincronizzazione boot ──────────────────────────────────────────
// Chiamato ogni 150 ms dal task LVGL.
// Legge g_dc_model.boot e:
//   - aggiorna la barra al nuovo step (animazione 300 ms)
//   - aggiorna la label informativa
//   - quando boot.complete == true E il tempo minimo è trascorso → carica Home
static void _boot_sync_timer(lv_timer_t* t) {
    if (s_ctx.home_loaded) return;

    const int step = g_dc_model.boot.step;

    if (step != s_ctx.prev_step) {
        s_ctx.prev_step = step;
        const int pct = (step >= 0 && step <= 10) ? k_step_pct[step] : 0;
        lv_bar_set_value(s_ctx.bar, pct, LV_ANIM_ON);
        if (g_dc_model.boot.label[0] != '\0') {
            lv_label_set_text(s_ctx.info_label, g_dc_model.boot.label);
        }
    }

    if (!g_dc_model.boot.complete) return;
    if ((lv_tick_get() - s_ctx.created_tick) < k_min_splash_ms) return;

    s_ctx.home_loaded = true;
    lv_timer_del(t);

    lv_obj_t* home = ui_dc_home_create();
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

// ─── Costruzione splash ───────────────────────────────────────────────────────
lv_obj_t* ui_splash_shared_create(void) {
    s_ctx = {};
    s_ctx.created_tick = lv_tick_get();
    s_ctx.prev_step    = -1;
    s_ctx.home_loaded  = false;

    // Schermata
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, SP_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Container logo con clipping
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont, (lv_coord_t)kAntraluxLogoWidth, (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);
    lv_obj_set_style_radius(logo_cont, 0, 0);
    lv_obj_clear_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Logo
    lv_obj_t* img = lv_img_create(logo_cont);
    lv_img_set_src(img, &s_logo_dsc);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);
    lv_img_set_pivot(img, kAntraluxLogoWidth / 2, kAntraluxLogoHeight / 2);

    // Shimmer
    const lv_coord_t shimmer_w = (lv_coord_t)(kAntraluxLogoWidth / 4);
    lv_obj_t* shimmer = lv_obj_create(logo_cont);
    lv_obj_set_size(shimmer, shimmer_w, (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_set_pos(shimmer, 0, 0);
    lv_obj_set_style_translate_x(shimmer, -shimmer_w, 0);
    lv_obj_set_style_bg_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(shimmer, LV_OPA_40, 0);
    lv_obj_set_style_border_width(shimmer, 0, 0);
    lv_obj_set_style_radius(shimmer, 0, 0);
    lv_obj_clear_flag(shimmer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Tagline
    lv_obj_t* tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Easy Connect Cloud System");
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tagline, SP_TEXT_DIM, 0);
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 60);

    // Barra di progresso
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, SP_TRACK_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(bar, SP_TRACK_SH, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, SP_ORANGE_HI, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(bar, SP_ORANGE_LO, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_INDICATOR);
    lv_obj_set_style_anim_time(bar, 300, 0);
    s_ctx.bar = bar;

    // Label step corrente (testo dinamico aggiornato dal timer)
    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info, "Avvio...");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0xBBCCDD), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -40);
    s_ctx.info_label = info;

    // Versione firmware
    lv_obj_t* ver = lv_label_create(scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xBBCCDD), 0);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -24);

    // ─── Animazioni (identiche alla splash originale) ─────────────────────────
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_zoom);
    lv_anim_set_values(&a, 102, 256);
    lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, shimmer);
    lv_anim_set_exec_cb(&a, _cb_translate_x);
    lv_anim_set_values(&a, -shimmer_w, (lv_coord_t)kAntraluxLogoWidth);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    lv_anim_init(&a);
    lv_anim_set_var(&a, tagline);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // Timer che aggiorna barra + label e carica Home al completamento
    lv_timer_create(_boot_sync_timer, 150, NULL);

    lv_scr_load(scr);
    return scr;
}
