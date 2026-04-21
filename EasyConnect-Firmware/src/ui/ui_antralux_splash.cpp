/**
 * @file ui_antralux_splash.cpp
 * @brief Splash screen demo Antralux — logo fade-in + zoom, poi Home dopo 5s.
 *        Antralux demo splash screen — logo fade-in + zoom, then Home after 5s.
 *
 * Questo file è parte della UI "sandbox/demo" Antralux, NON del target di produzione.
 * This file is part of the Antralux "sandbox/demo" UI, NOT the production target.
 *
 * Layout 1024×600 (sfondo bianco / white background):
 *   ┌─────────────────────────────────────────────────────────┐
 *   │                                                         │
 *   │         [LOGO ANTRALUX 460×184 — fade-in+zoom]         │
 *   │                                                         │
 *   │           "Antralux Cloud System"  (tagline)            │
 *   │                                                         │
 *   │                        v1.0.0                           │
 *   └─────────────────────────────────────────────────────────┘
 *
 * Sequenza animazioni / Animation sequence:
 *   t=200ms   Logo fade-in (opacità 0→255, 1200ms, ease-out)
 *   t=200ms   Logo zoom-in (40%→100%, 1800ms, ease-out)
 *             In LVGL lo zoom 100%=256, 40% ≈ 256×0.4 = 102
 *             In LVGL zoom 100%=256, 40% ≈ 256×0.4 = 102
 *   t=1600ms  Tagline fade-in (0→255, 600ms, ease-out)
 *   t=5000ms  Timer one-shot → ui_antralux_home_create() con fade 600ms
 */

#include "ui_antralux_splash.h"   // dichiarazione di ui_antralux_splash_create()
#include "ui_antralux_home.h"     // ui_antralux_home_create() per la transizione
#include "DisplayLogoAsset.h"     // kAntraluxLogo565, kAntraluxLogoWidth/Height
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
// Sfondo bianco per la splash demo (diverso dal dark-mode della sandbox)
// White background for the demo splash (different from sandbox dark-mode)
#define SP_BG       lv_color_hex(0xFFFFFF)   ///< Sfondo bianco / White background
#define SP_ORANGE   lv_color_hex(0xE84820)   ///< Arancione Antralux (non usato qui, riservato)
#define SP_TEXT_DIM lv_color_hex(0x7A92B0)   ///< Grigio-blu per testi secondari / Grey-blue for secondary text

// ─── Descrittore immagine LVGL per il logo ────────────────────────────────────
// LVGL lavora con un descrittore (lv_img_dsc_t) che contiene:
// LVGL works with a descriptor (lv_img_dsc_t) containing:
//   - header: formato colore, larghezza, altezza
//   - header: color format, width, height
//   - data_size: dimensione in byte dell'array pixel
//   - data_size: pixel array size in bytes
//   - data: puntatore all'array RGB565 in flash
//   - data: pointer to the RGB565 array in flash
//
// LV_IMG_CF_TRUE_COLOR = RGB565 nativo del display.
// LV_IMG_CF_TRUE_COLOR = display-native RGB565.
static const lv_img_dsc_t s_logo_dsc = {
    .header = {
        .cf          = LV_IMG_CF_TRUE_COLOR,   // formato RGB565 / RGB565 format
        .always_zero = 0,                       // campo riservato LVGL / LVGL reserved field
        .reserved    = 0,                       // campo riservato LVGL / LVGL reserved field
        .w           = kAntraluxLogoWidth,      // larghezza in pixel (460) / width in pixels (460)
        .h           = kAntraluxLogoHeight,     // altezza in pixel (184) / height in pixels (184)
    },
    // Dimensione totale = w × h × 2 byte per pixel (RGB565 = 16 bit = 2 byte)
    // Total size = w × h × 2 bytes per pixel (RGB565 = 16 bit = 2 bytes)
    .data_size = (uint32_t)(kAntraluxLogoWidth * kAntraluxLogoHeight * sizeof(uint16_t)),
    // Array RGB565 in flash, reinterpretato come byte per LVGL
    // RGB565 array in flash, reinterpreted as bytes for LVGL
    .data      = reinterpret_cast<const uint8_t*>(kAntraluxLogo565),
};

// ─── Callback animazioni ──────────────────────────────────────────────────────
// Le callback di animazione LVGL ricevono (void* obj, int32_t v) e applicano
// il valore interpolato v all'oggetto obj.
// LVGL animation callbacks receive (void* obj, int32_t v) and apply
// the interpolated value v to the object obj.

/**
 * @brief Callback opacità — imposta lv_obj_t::opa al valore interpolato v.
 *        Opacity callback — sets lv_obj_t::opa to the interpolated value v.
 *
 * Usata per fade-in/fade-out di oggetti LVGL.
 * Used for fade-in/fade-out of LVGL objects.
 *
 * @param obj  Puntatore all'oggetto LVGL (castato da void*).
 * @param v    Valore opacità interpolato [LV_OPA_TRANSP (0) … LV_OPA_COVER (255)].
 */
static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
}

/**
 * @brief Callback zoom — imposta il fattore di scala dell'immagine.
 *        Zoom callback — sets the image scale factor.
 *
 * In LVGL: zoom 256 = 100%, zoom 128 = 50%, zoom 512 = 200%.
 * In LVGL: zoom 256 = 100%, zoom 128 = 50%, zoom 512 = 200%.
 *
 * @param obj  Puntatore all'immagine LVGL (lv_obj_t* con tipo LV_OBJ_CLASS img).
 * @param v    Fattore di zoom [102 (≈40%) … 256 (100%)].
 */
static void _cb_zoom(void* obj, int32_t v) {
    lv_img_set_zoom(static_cast<lv_obj_t*>(obj), static_cast<uint16_t>(v));
}

// ─── Timer callback: transizione verso Home ───────────────────────────────────

/**
 * @brief Callback del timer one-shot (5000ms): carica la Home screen Antralux demo.
 *        One-shot timer callback (5000ms): loads the Antralux demo Home screen.
 *
 * Viene chiamata una sola volta da LVGL dopo 5 secondi dall'avvio della splash.
 * Called once by LVGL after 5 seconds from splash start.
 *
 * Crea la home con ui_antralux_home_create() e la carica con una transizione
 * fade da 600ms. Il flag auto_del=true di lv_scr_load_anim elimina la splash
 * dopo la transizione.
 *
 * Creates the home with ui_antralux_home_create() and loads it with a 600ms
 * fade transition. The auto_del=true flag of lv_scr_load_anim deletes the
 * splash after the transition.
 *
 * @param t  Puntatore al timer LVGL (non usato direttamente).
 */
static void _go_to_home(lv_timer_t* /*t*/) {
    lv_obj_t* home = ui_antralux_home_create();
    // LV_SCR_LOAD_ANIM_FADE_ON: dissolvenza da nero (in realtà fade-in della home)
    // LV_SCR_LOAD_ANIM_FADE_ON: fade from black (actually fade-in of home)
    // 600ms durata / 0ms delay / auto_del=true: elimina la schermata precedente
    // 600ms duration / 0ms delay / auto_del=true: deletes the previous screen
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_FADE_ON, 600, 0, true);
}

// ─── Costruzione splash ───────────────────────────────────────────────────────

/**
 * @brief Crea, anima e attiva la splash screen Antralux demo.
 *        Creates, animates and activates the Antralux demo splash screen.
 *
 * Struttura degli oggetti LVGL creati / Structure of created LVGL objects:
 *
 *   scr (schermata radice, sfondo bianco)
 *   ├── logo_cont (container 460×184, centrato -50px in Y, clipping attivo)
 *   │   └── img (immagine logo RGB565, inizialmente invisibile)
 *   ├── tagline (label "Antralux Cloud System", inizialmente invisibile)
 *   └── ver (label "v1.0.0", visibile sempre, colore grigio chiaro)
 *
 * Il container logo_cont ha overflow clipping attivo (default in LVGL):
 * eventuali trasformazioni zoom che escono dai bordi vengono tagliate.
 *
 * The logo_cont container has active overflow clipping (LVGL default):
 * any zoom transformations that exceed its bounds are clipped.
 */
void ui_antralux_splash_create(void) {

    // ── Schermata base bianca ─────────────────────────────────────────────────
    // Crea una nuova schermata LVGL (parent=NULL → schermata radice)
    // Creates a new LVGL screen (parent=NULL → root screen)
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, SP_BG, 0);              // sfondo bianco / white bg
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);         // opacità piena / full opacity
    lv_obj_set_style_border_width(scr, 0, 0);               // nessun bordo / no border
    lv_obj_set_style_pad_all(scr, 0, 0);                    // nessun padding / no padding
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);        // no scroll sulla schermata

    // ── Container logo ─────────────────────────────────────────────────────────
    // Questo container ha le stesse dimensioni del logo e serve principalmente
    // per il clipping: il logo può scalare oltre i suoi bordi durante lo zoom,
    // ma il container lo taglia in modo netto.
    // This container has the same dimensions as the logo and mainly serves
    // for clipping: the logo can scale beyond its bounds during zoom,
    // but the container clips it cleanly.
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont,
                    (lv_coord_t)kAntraluxLogoWidth,    // 460 px
                    (lv_coord_t)kAntraluxLogoHeight);  // 184 px
    // Allineamento: centro dello schermo, -50px in Y per spazio alla tagline sotto
    // Alignment: screen center, -50px in Y to leave space for tagline below
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);  // sfondo trasparente
    lv_obj_set_style_border_width(logo_cont, 0, 0);          // nessun bordo
    lv_obj_set_style_pad_all(logo_cont, 0, 0);               // nessun padding
    lv_obj_set_style_radius(logo_cont, 0, 0);                // angoli vivi (no round)
    // Rimuovi flag di scrollabilità e clic-passthrough per performance
    // Remove scrollability and click flags for performance
    lv_obj_clear_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Immagine logo ──────────────────────────────────────────────────────────
    // Figlio del container, inizialmente completamente trasparente (fade-in in arrivo)
    // Child of container, initially fully transparent (fade-in coming)
    lv_obj_t* img = lv_img_create(logo_cont);
    lv_img_set_src(img, &s_logo_dsc);                       // fonte: array RGB565 in flash
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);               // centrato nel container
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);            // parte invisibile (animato dopo)

    // Imposta il pivot dello zoom al centro dell'immagine per uno zoom centrato.
    // Sets the zoom pivot to the image center for centered zoom.
    // Senza questo, il pivot sarebbe nell'angolo (0,0) in alto a sinistra.
    // Without this, the pivot would be at (0,0) top-left.
    lv_img_set_pivot(img,
                     kAntraluxLogoWidth / 2,    // x del pivot = metà larghezza
                     kAntraluxLogoHeight / 2);  // y del pivot = metà altezza

    // ── Tagline ───────────────────────────────────────────────────────────────
    // Testo sotto il logo, inizialmente invisibile (fade-in ritardato)
    // Text below the logo, initially invisible (delayed fade-in)
    lv_obj_t* tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Antralux Cloud System");
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tagline, SP_TEXT_DIM, 0);   // grigio-blu per testo secondario
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);         // invisibile, poi animato
    // +80px in Y dalla center per stare sotto il logo (logo center - 50 + 184/2 + margin ≈ +80)
    // +80px in Y from center to be below the logo
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 80);

    // ── Versione ──────────────────────────────────────────────────────────────
    // Label versione in basso, sempre visibile (grigio chiarissimo, discreta)
    // Version label at bottom, always visible (very light grey, subtle)
    lv_obj_t* ver = lv_label_create(scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xBBCCDD), 0);  // grigio azzurrato chiaro
    // -20px dalla parte inferiore dello schermo
    // -20px from the bottom of the screen
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -20);

    // ─── Animazioni ───────────────────────────────────────────────────────────
    // Tutte le animazioni sono indipendenti e partono in parallelo con delay diversi.
    // All animations are independent and start in parallel with different delays.
    // La struttura lv_anim_t viene riutilizzata (re-inizializzata per ogni animazione).
    // The lv_anim_t struct is reused (re-initialized for each animation).
    lv_anim_t a;

    // ── Animazione 1: Logo fade-in ────────────────────────────────────────────
    // Opacità: 0 → 255 (LV_OPA_TRANSP → LV_OPA_COVER)
    // Delay: 200ms (attesa che il display si stabilizzi)
    // Durata: 1200ms
    // Curva: ease-out (veloce all'inizio, rallenta alla fine)
    // Delay: 200ms (wait for display to stabilize)
    // Duration: 1200ms
    // Curve: ease-out (fast at start, slows at end)
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── Animazione 2: Logo zoom-in ────────────────────────────────────────────
    // Zoom: 102 (≈40%) → 256 (100%)
    // 256 = 100% in LVGL (unità interna: 256 = 1.0× = dimensione originale)
    // 102 = 256 × 0.4 ≈ 40% della dimensione originale
    // Delay: 200ms (stesso del fade-in, partono insieme)
    // Durata: 1800ms (più lunga del fade per un effetto "pop" morbido)
    // 102 = 256 × 0.4 ≈ 40% of original size
    // Delay: 200ms (same as fade-in, they start together)
    // Duration: 1800ms (longer than fade for a soft "pop" effect)
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_zoom);
    lv_anim_set_values(&a, 102, 256);   // 102/256 ≈ 40% → 100%
    lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── Animazione 3: Tagline fade-in ─────────────────────────────────────────
    // La tagline compare dopo che il logo è quasi completamente visibile.
    // The tagline appears after the logo is almost fully visible.
    // Delay: 1600ms (logo fade finisce a ~200+1200=1400ms, tagline inizia a 1600ms)
    // Delay: 1600ms (logo fade ends at ~200+1200=1400ms, tagline starts at 1600ms)
    lv_anim_init(&a);
    lv_anim_set_var(&a, tagline);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── Timer one-shot: transizione alla Home dopo 5 secondi ─────────────────
    // lv_timer_create(callback, period_ms, user_data)
    // lv_timer_set_repeat_count(t, 1) → il timer si attiva una sola volta poi si ferma
    // lv_timer_set_repeat_count(t, 1) → timer fires exactly once then stops
    lv_timer_t* t = lv_timer_create(_go_to_home, 5000, NULL);
    lv_timer_set_repeat_count(t, 1);

    // ── Attiva la schermata ───────────────────────────────────────────────────
    // lv_scr_load() attiva la schermata immediatamente senza transizione.
    // lv_scr_load() activates the screen immediately without transition.
    // (La transizione è gestita dalla splash stessa tramite il timer above)
    // (The transition is handled by the splash itself via the timer above)
    lv_scr_load(scr);
}
