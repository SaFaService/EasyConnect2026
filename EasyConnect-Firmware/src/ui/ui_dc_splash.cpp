/**
 * @file ui_dc_splash.cpp
 * @brief Splash screen Display Controller — logo, shimmer, progress bar.
 *        Display Controller splash screen — logo, shimmer, progress bar.
 *
 * Questo è il punto di ingresso visivo del firmware di produzione.
 * This is the visual entry point of the production firmware.
 *
 * Timing totale: ~5.4 secondi prima di caricare la Home.
 * Total timing: ~5.4 seconds before loading the Home.
 *
 * Layout 1024×600 (sfondo bianco / white background):
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │                                                              │
 *   │              [LOGO ANTRALUX 460×184]                        │
 *   │          fade-in + zoom 40%→100% + shimmer                  │
 *   │                                                              │
 *   │           "Easy Connect Cloud System"   (tagline)           │
 *   │                                                              │
 *   │       ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░  (progress bar 640×18)  │
 *   │                                                              │
 *   │                         v1.0.0                              │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * Sequenza animazioni / Animation sequence:
 *   t=200ms   [1] Logo fade-in       (0→255 opa, 1200ms, ease-out)
 *   t=200ms   [2] Logo zoom-in       (102→256 zoom, 1800ms, ease-out)
 *   t=2000ms  [3] Shimmer sweep L→R  (-shimmer_w → logo_w, 1200ms, ease-in-out)
 *   t=1600ms  [4] Tagline fade-in    (0→255 opa, 600ms, ease-out)
 *   t=400ms   [5] Progress bar       (0→100%, 5000ms, ease-in-out)
 *             Al completamento [5] → poll rs485 boot probe → carica Home
 *             On [5] completion → poll rs485 boot probe → load Home
 *
 * Dipendenza boot probe RS485:
 * RS485 boot probe dependency:
 *   La splash avvia rs485_network_boot_probe_start() che esegue una prima
 *   scansione della rete RS485 in background. Al termine della progress bar,
 *   viene creato un timer LVGL (120ms) che aspetta il completamento del probe
 *   prima di caricare la Home. Questo garantisce che la Home abbia già i dati
 *   dei dispositivi quando viene mostrata.
 *
 *   The splash starts rs485_network_boot_probe_start() which performs a first
 *   RS485 network scan in background. When the progress bar ends, an LVGL timer
 *   (120ms) is created that waits for the probe to complete before loading
 *   the Home. This ensures Home already has device data when shown.
 */

#include "ui_dc_splash.h"     // dichiarazione di ui_dc_splash_create()
#include "ui_dc_home.h"       // ui_dc_home_create() per la transizione
#include "DisplayLogoAsset.h" // kAntraluxLogo565, kAntraluxLogoWidth/Height
#include "rs485_network.h"    // rs485_network_boot_probe_start/state
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
#define SP_BG        lv_color_hex(0xFFFFFF)   ///< Sfondo bianco / White background
#define SP_ORANGE    lv_color_hex(0xE84820)   ///< Arancione Antralux (non usato direttamente)
#define SP_ORANGE_HI lv_color_hex(0xFF7035)   ///< Arancione chiaro — top del gradiente barra
#define SP_ORANGE_LO lv_color_hex(0xB02810)   ///< Arancione scuro — bottom del gradiente barra
#define SP_TRACK_BG  lv_color_hex(0xDDDDDD)   ///< Grigio chiaro — sfondo track barra
#define SP_TRACK_SH  lv_color_hex(0xBBBBBB)   ///< Grigio più scuro — bottom del gradiente track
#define SP_TEXT_DIM  lv_color_hex(0x7A92B0)   ///< Grigio-blu — colore tagline

// ─── Dimensioni barra di progresso ───────────────────────────────────────────
#define BAR_W 640   ///< Larghezza barra in pixel / Progress bar width in pixels
#define BAR_H  18   ///< Altezza barra in pixel / Progress bar height in pixels

// ─── Descrittore immagine LVGL (logo RGB565 in flash) ────────────────────────
// Stesso pattern di ui_antralux_splash.cpp.
// Same pattern as ui_antralux_splash.cpp.
// LV_IMG_CF_TRUE_COLOR = formato nativo RGB565 del pannello display.
// LV_IMG_CF_TRUE_COLOR = display panel native RGB565 format.
static const lv_img_dsc_t s_logo_dsc = {
    .header = {
        .cf          = LV_IMG_CF_TRUE_COLOR,   // RGB565, stesso formato del display
        .always_zero = 0,
        .reserved    = 0,
        .w           = kAntraluxLogoWidth,     // 460 pixel larghezza
        .h           = kAntraluxLogoHeight,    // 184 pixel altezza
    },
    .data_size = (uint32_t)(kAntraluxLogoWidth * kAntraluxLogoHeight * sizeof(uint16_t)),
    .data      = reinterpret_cast<const uint8_t*>(kAntraluxLogo565),
};

// ─── Callback animazioni ──────────────────────────────────────────────────────

/**
 * @brief Callback opacità — imposta la trasparenza globale dell'oggetto.
 *        Opacity callback — sets the object's global transparency.
 *
 * @param obj  Oggetto LVGL. LVGL object.
 * @param v    Valore opacità [0..255]. Opacity value [0..255].
 */
static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
}

/**
 * @brief Callback zoom — imposta il fattore di scala dell'immagine.
 *        Zoom callback — sets the image scale factor.
 *
 * 256 = 100% (dimensione originale), 102 ≈ 40%, 128 = 50%.
 * 256 = 100% (original size), 102 ≈ 40%, 128 = 50%.
 *
 * @param obj  Oggetto immagine LVGL. LVGL image object.
 * @param v    Fattore di zoom (intero LVGL). Zoom factor (LVGL integer).
 */
static void _cb_zoom(void* obj, int32_t v) {
    lv_img_set_zoom(static_cast<lv_obj_t*>(obj), static_cast<uint16_t>(v));
}

/**
 * @brief Callback traslazione X — muove un oggetto sull'asse orizzontale.
 *        X-translation callback — moves an object on the horizontal axis.
 *
 * Usata per l'animazione shimmer: la striscia bianca scorre da sinistra a destra.
 * Used for the shimmer animation: the white strip scrolls from left to right.
 *
 * @param obj  Oggetto LVGL (la striscia shimmer). LVGL object (shimmer strip).
 * @param v    Offset X in pixel. X offset in pixels.
 */
static void _cb_translate_x(void* obj, int32_t v) {
    lv_obj_set_style_translate_x(static_cast<lv_obj_t*>(obj), (lv_coord_t)v, 0);
}

/**
 * @brief Callback progress bar — imposta il valore della barra.
 *        Progress bar callback — sets the bar value.
 *
 * Chiama lv_bar_set_value con LV_ANIM_OFF perché l'animazione è già gestita
 * dal sistema di animazione LVGL esternamente.
 * Calls lv_bar_set_value with LV_ANIM_OFF because the animation is already
 * managed by the LVGL animation system externally.
 *
 * @param obj  Oggetto lv_bar. lv_bar object.
 * @param v    Valore [0..100]. Value [0..100].
 */
static void _cb_bar(void* obj, int32_t v) {
    lv_bar_set_value(static_cast<lv_obj_t*>(obj), (int16_t)v, LV_ANIM_OFF);
}

// ─── Caricamento Home al termine della progress bar ───────────────────────────

/**
 * @brief Timer LVGL che attende il completamento del boot probe RS485.
 *        LVGL timer that waits for RS485 boot probe completion.
 *
 * Viene creato con period=120ms quando la progress bar completa (t≈5400ms).
 * Ogni tick controlla lo stato del probe:
 * Created with period=120ms when the progress bar completes (t≈5400ms).
 * Every tick checks the probe state:
 *
 *   - RUNNING: il probe è ancora in corso → non fare nulla, aspetta il prossimo tick
 *   - RUNNING: probe still in progress → do nothing, wait for next tick
 *   - qualsiasi altro stato: probe completato → elimina timer, carica Home
 *   - any other state: probe completed → delete timer, load Home
 *
 * La transizione usa LV_SCR_LOAD_ANIM_NONE (nessuna animazione) con delay=0
 * e auto_del=true per eliminare la splash.
 * The transition uses LV_SCR_LOAD_ANIM_NONE (no animation) with delay=0
 * and auto_del=true to delete the splash.
 *
 * @param t  Puntatore al timer LVGL.
 */
static void _load_home_timer(lv_timer_t* t) {
    // Controlla se il boot probe è ancora in esecuzione
    // Check if the boot probe is still running
    const Rs485BootProbeState st = rs485_network_boot_probe_state();
    if (st == Rs485BootProbeState::RUNNING) return;  // aspetta il prossimo tick

    // Probe completato (o non avviato/fallito) → vai alla Home
    // Probe completed (or not started/failed) → go to Home
    lv_timer_del(t);  // elimina questo timer one-shot

    lv_obj_t* home = ui_dc_home_create();
    // NONE = transizione immediata senza animazione (la splash ha già "preparato" l'utente)
    // NONE = immediate transition without animation (splash already "prepared" the user)
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * @brief Ready callback della progress bar — crea il timer di attesa boot probe.
 *        Progress bar ready callback — creates the boot probe wait timer.
 *
 * Chiamata da LVGL quando l'animazione della barra (t=5400ms) è completata.
 * Called by LVGL when the bar animation (t=5400ms) is complete.
 *
 * Crea un timer LVGL ripetuto ogni 120ms che controllerà quando il probe
 * RS485 è terminato, poi caricherà la Home.
 * Creates an LVGL timer repeating every 120ms that will check when the
 * RS485 probe is done, then load the Home.
 *
 * @param a  Puntatore all'animazione completata (non usato).
 */
static void _on_bar_done(lv_anim_t* /*a*/) {
    // Crea timer (period=120ms, NULL user_data) che chiamerà _load_home_timer
    // Create timer (period=120ms, NULL user_data) that will call _load_home_timer
    lv_timer_create(_load_home_timer, 120, NULL);
}

// ─── Costruzione splash ───────────────────────────────────────────────────────

/**
 * @brief Crea, anima e attiva la splash screen del Display Controller.
 *        Creates, animates and activates the Display Controller splash screen.
 *
 * Struttura oggetti LVGL / LVGL object structure:
 *
 *   scr (schermata radice, sfondo bianco)
 *   ├── logo_cont (460×184, centrato -70px Y, overflow clipping attivo)
 *   │   ├── img (logo RGB565, inizialmente invisibile, pivot al centro)
 *   │   └── shimmer (striscia bianca 40%, inizialmente fuori bordo sinistro)
 *   ├── tagline (label, inizialmente invisibile)
 *   ├── bar (progress bar 640×18, in basso)
 *   └── ver (label versione, in basso)
 */
void ui_dc_splash_create(void) {

    // ── Avvia la scansione RS485 in background ────────────────────────────────
    // Il probe esegue in un task separato e aggiorna rs485_network internamente.
    // The probe runs in a separate task and updates rs485_network internally.
    rs485_network_boot_probe_start();

    // ── Schermata ─────────────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, SP_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Container logo (con clipping) ─────────────────────────────────────────
    // Il container ha le stesse dimensioni del logo e serve per:
    // 1. Clippare il logo durante lo zoom (che potrebbe uscire dai bordi)
    // 2. Clippare la striscia shimmer (che scorre da fuori bordo sinistro a fuori bordo destro)
    // The container has the same dimensions as the logo and serves to:
    // 1. Clip the logo during zoom (which may exceed bounds)
    // 2. Clip the shimmer strip (which scrolls from outside left to outside right)
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont,
                    (lv_coord_t)kAntraluxLogoWidth,    // 460px
                    (lv_coord_t)kAntraluxLogoHeight);  // 184px
    // -70px dal centro = più in alto rispetto ad antralux_splash (-50px)
    // per fare spazio alla progress bar e alla tagline sotto
    // -70px from center = higher than antralux_splash (-50px)
    // to make room for progress bar and tagline below
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);   // sfondo trasparente
    lv_obj_set_style_border_width(logo_cont, 0, 0);
    lv_obj_set_style_pad_all(logo_cont, 0, 0);
    lv_obj_set_style_radius(logo_cont, 0, 0);
    lv_obj_clear_flag(logo_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    // NOTA: LV_OBJ_FLAG_OVERFLOW_VISIBLE NON è impostato → i figli vengono clippati
    // NOTE: LV_OBJ_FLAG_OVERFLOW_VISIBLE is NOT set → children are clipped

    // ── Logo ──────────────────────────────────────────────────────────────────
    lv_obj_t* img = lv_img_create(logo_cont);
    lv_img_set_src(img, &s_logo_dsc);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(img, LV_OPA_TRANSP, 0);  // inizialmente invisibile / initially invisible
    // Pivot al centro per zoom centrato (default LVGL = top-left = (0,0))
    // Center pivot for centered zoom (LVGL default = top-left = (0,0))
    lv_img_set_pivot(img, kAntraluxLogoWidth / 2, kAntraluxLogoHeight / 2);

    // ── Shimmer ───────────────────────────────────────────────────────────────
    // La striscia shimmer è un rettangolo bianco semitrasparente (40% opacità)
    // largo 1/4 del logo (≈115px). Parte fuori dal bordo sinistro del container
    // (-shimmer_w) e scorre fino a fuori dal bordo destro (+logo_width).
    // Essendo figlio del container (che non ha OVERFLOW_VISIBLE), viene clippata
    // ai bordi del container → si vede solo quando è sopra il logo.
    //
    // The shimmer strip is a semi-transparent white rectangle (40% opacity)
    // 1/4 of the logo width wide (≈115px). It starts outside the container's
    // left border (-shimmer_w) and scrolls to outside the right border (+logo_width).
    // Being a child of the container (no OVERFLOW_VISIBLE), it's clipped to
    // container bounds → only visible when over the logo.
    const lv_coord_t shimmer_w = (lv_coord_t)(kAntraluxLogoWidth / 4);  // ~115px
    lv_obj_t* shimmer = lv_obj_create(logo_cont);
    lv_obj_set_size(shimmer, shimmer_w, (lv_coord_t)kAntraluxLogoHeight);
    lv_obj_set_pos(shimmer, 0, 0);
    // Posizione iniziale: fuori dal bordo sinistro del container
    // Initial position: outside the left border of the container
    lv_obj_set_style_translate_x(shimmer, -shimmer_w, 0);
    lv_obj_set_style_bg_color(shimmer, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(shimmer, LV_OPA_40, 0);         // 40% bianco → effetto "lucido"
    lv_obj_set_style_border_width(shimmer, 0, 0);
    lv_obj_set_style_radius(shimmer, 0, 0);
    lv_obj_clear_flag(shimmer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Tagline ───────────────────────────────────────────────────────────────
    // +60px dal centro = sotto il container logo (che è a -70px)
    // center_logo_bottom ≈ -70 + 184/2 = -70 + 92 = +22px → tagline a +60px
    // +60px from center = below the logo container (which is at -70px)
    lv_obj_t* tagline = lv_label_create(scr);
    lv_label_set_text(tagline, "Easy Connect Cloud System");
    lv_obj_set_style_text_font(tagline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tagline, SP_TEXT_DIM, 0);
    lv_obj_set_style_opa(tagline, LV_OPA_TRANSP, 0);         // invisibile inizialmente
    lv_obj_align(tagline, LV_ALIGN_CENTER, 0, 60);

    // ── Progress bar ──────────────────────────────────────────────────────────
    // 640×18px, centrata in basso, -60px dal bordo inferiore (sopra la versione).
    // Due parti stilizzate: track (sfondo) e indicator (riempimento).
    // 640×18px, centered bottom, -60px from bottom edge (above version label).
    // Two styled parts: track (background) and indicator (fill).
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, BAR_W, BAR_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_bar_set_range(bar, 0, 100);                           // range 0-100%
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);                  // inizia a 0

    // Track (sfondo grigio 3D con gradiente verticale luce→ombra)
    // Track (grey 3D background with vertical light→shadow gradient)
    lv_obj_set_style_bg_color(bar, SP_TRACK_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(bar, SP_TRACK_SH, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_MAIN);  // gradiente verticale
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_MAIN);  // angoli a pillola (r=metà altezza)
    lv_obj_set_style_border_color(bar, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);

    // Indicator (riempimento arancione con gradiente verticale chiaro→scuro)
    // Indicator (orange fill with vertical light→dark gradient)
    lv_obj_set_style_bg_color(bar, SP_ORANGE_HI, LV_PART_INDICATOR);   // top: arancione chiaro
    lv_obj_set_style_bg_grad_color(bar, SP_ORANGE_LO, LV_PART_INDICATOR); // bottom: arancione scuro
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_VER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, BAR_H / 2, LV_PART_INDICATOR);

    // ── Versione ──────────────────────────────────────────────────────────────
    // -24px dal bordo inferiore (sotto la progress bar a -60px)
    // -24px from bottom edge (below progress bar at -60px)
    lv_obj_t* ver = lv_label_create(scr);
    lv_label_set_text(ver, "v1.0.0");
    lv_obj_set_style_text_font(ver, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver, lv_color_hex(0xBBCCDD), 0);  // grigio azzurro chiarissimo
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -24);

    // ─── Animazioni ───────────────────────────────────────────────────────────
    // Tutte le animazioni usano lo stesso struct lv_anim_t (re-inizializzato ogni volta).
    // All animations use the same lv_anim_t struct (re-initialized each time).
    lv_anim_t a;

    // ── [1] Logo fade-in ──────────────────────────────────────────────────────
    // Delay=200ms per dare tempo al display di stabilizzarsi dopo l'accensione.
    // Delay=200ms to allow the display to stabilize after power-on.
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);  // 0 → 255
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);        // decelera → sembra "materializzarsi"
    lv_anim_start(&a);

    // ── [2] Logo zoom-in ──────────────────────────────────────────────────────
    // 102/256 ≈ 40% della dimensione originale → 256/256 = 100%.
    // Parte insieme al fade ma dura 600ms in più per un effetto "pop" più pronunciato.
    // 102/256 ≈ 40% of original size → 256/256 = 100%.
    // Starts together with fade but lasts 600ms more for a more pronounced "pop".
    lv_anim_init(&a);
    lv_anim_set_var(&a, img);
    lv_anim_set_exec_cb(&a, _cb_zoom);
    lv_anim_set_values(&a, 102, 256);   // 40% → 100%
    lv_anim_set_time(&a, 1800);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── [3] Shimmer sweep L→R ─────────────────────────────────────────────────
    // La striscia parte a -shimmer_w (sinistra del container, fuori vista) e
    // scorre fino a +logo_width (destra del container, fuori vista).
    // L'effetto è visibile solo durante l'attraversamento del logo (0..logo_width).
    //
    // The strip starts at -shimmer_w (left of container, out of view) and
    // scrolls to +logo_width (right of container, out of view).
    // The effect is visible only while crossing the logo (0..logo_width).
    //
    // Delay=2000ms = dopo che il fade e lo zoom sono quasi completati, quindi
    // il logo è pienamente visibile quando lo shimmer lo attraversa.
    // Delay=2000ms = after fade and zoom are nearly complete, so the logo
    // is fully visible when shimmer crosses it.
    lv_anim_init(&a);
    lv_anim_set_var(&a, shimmer);
    lv_anim_set_exec_cb(&a, _cb_translate_x);
    lv_anim_set_values(&a, -shimmer_w, (lv_coord_t)kAntraluxLogoWidth);
    lv_anim_set_time(&a, 1200);
    lv_anim_set_delay(&a, 2000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);     // accelera poi decelera → naturale
    lv_anim_start(&a);

    // ── [4] Tagline fade-in ───────────────────────────────────────────────────
    // Delay=1600ms = la tagline appare mentre lo zoom/fade sono ancora attivi,
    // creando un effetto "presentazione sequenziale".
    // Delay=1600ms = tagline appears while zoom/fade are still active,
    // creating a "sequential presentation" effect.
    lv_anim_init(&a);
    lv_anim_set_var(&a, tagline);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 600);
    lv_anim_set_delay(&a, 1600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── [5] Progress bar 0→100% ───────────────────────────────────────────────
    // Delay=400ms: la barra inizia quasi subito (l'utente vede subito il progresso).
    // Durata=5000ms: dura 5 secondi per dare tempo al boot probe RS485.
    // ease-in-out: parte lenta, accelera, rallenta → percepita come "carico reale".
    // Al termine (t≈5400ms): _on_bar_done crea il timer che attende il probe.
    //
    // Delay=400ms: bar starts almost immediately (user sees progress right away).
    // Duration=5000ms: lasts 5 seconds to allow RS485 boot probe.
    // ease-in-out: starts slow, accelerates, slows → perceived as "real loading".
    // On completion (t≈5400ms): _on_bar_done creates timer that waits for probe.
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, _cb_bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 5000);
    lv_anim_set_delay(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    // Registra la ready callback: viene chiamata quando il valore raggiunge 100
    // Register the ready callback: called when the value reaches 100
    lv_anim_set_ready_cb(&a, _on_bar_done);
    lv_anim_start(&a);

    // ── Attiva la schermata ───────────────────────────────────────────────────
    // lv_scr_load() rende la schermata attiva immediatamente (nessuna transizione).
    // lv_scr_load() makes the screen active immediately (no transition).
    lv_scr_load(scr);
}
