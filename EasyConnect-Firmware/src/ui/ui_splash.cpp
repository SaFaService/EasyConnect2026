/**
 * @file ui_splash.cpp
 * @brief Splash screen animato EasyConnect / Antralux — versione SANDBOX (tema scuro)
 *        Animated splash screen for EasyConnect / Antralux — SANDBOX version (dark theme)
 *
 * Questo file implementa la splash screen del progetto sandbox (tema scuro), distinta dalla
 * splash screen di produzione (`ui_dc_splash.cpp`, tema chiaro, prefisso `ui_dc_`).
 * This file implements the splash screen of the sandbox project (dark theme), distinct from
 * the production splash screen (`ui_dc_splash.cpp`, light theme, `ui_dc_` prefix).
 *
 * Sequenza animazioni / Animation sequence:
 *   t=   0ms  Sfondo scuro, tutto invisibile        / Dark background, everything invisible
 *   t=   0ms  Arco esterno inizia a comparire        / Outer arc starts appearing (fade-in 300ms)
 *   t=   0ms  Arco esterno inizia a disegnarsi      / Outer arc starts drawing (1500ms, ease-out)
 *   t= 200ms  Arco interno inizia a comparire        / Inner arc starts appearing (fade-in 300ms)
 *   t= 200ms  Arco interno inizia a disegnarsi      / Inner arc starts drawing (1300ms, ease-out)
 *   t= 700ms  Testo "ANTRALUX" fade-in (900ms)       / "ANTRALUX" text fades in (900ms)
 *   t=1500ms  Sottotitolo "EasyConnect" slide-up     / "EasyConnect" subtitle slides up + fades in
 *   t=2000ms  Label versione fade-in (500ms)          / Version label fades in (500ms)
 *   t= 200ms  Barra di progresso inizia a riempirsi  / Progress bar starts filling (5800ms → 100%)
 *   t=6000ms  Transizione fade → Home screen          / Fade transition to Home screen
 *
 * Dipendenze / Dependencies:
 *   - ui_splash.h    → dichiarazione di ui_splash_create()
 *   - ui_styles.h    → palette, font alias, costanti dimensionali (sandbox)
 *   - ui_home.h      → ui_home_create() per la transizione verso la schermata principale
 */

#include "ui_splash.h"
#include "ui_styles.h"
#include "ui_home.h"

// ─── Callback animazioni ──────────────────────────────────────────────────────
// Animation callbacks — queste funzioni vengono chiamate dal motore LVGL
// a ogni frame di animazione con il valore interpolato corrente.
// These functions are called by the LVGL animation engine on each frame with
// the current interpolated value.

/**
 * @brief Callback opacità generica / Generic opacity callback
 *
 * Imposta l'opacità di qualsiasi oggetto LVGL durante un'animazione.
 * Sets the opacity of any LVGL object during an animation.
 *
 * @param var  Puntatore all'oggetto LVGL (`lv_obj_t*` castato a `void*`)
 *             Pointer to the LVGL object (`lv_obj_t*` cast to `void*`)
 * @param val  Valore di opacità interpolato [0=trasparente … 255=opaco]
 *             Interpolated opacity value [0=transparent … 255=opaque]
 */
static void anim_opa_cb(void* var, int32_t val) {
    // Applica l'opacità all'oggetto — parte style 0 = stile normale (non selezionato)
    // Apply opacity to the object — style part 0 = normal (unselected) state
    lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)val, 0);
}

/**
 * @brief Callback angolo finale arco / Arc end-angle callback
 *
 * Anima l'angolo finale di un `lv_arc` da 0° a 360°, producendo l'effetto di "disegno
 * progressivo" del cerchio. L'angolo di partenza è 270° (ore 12), quindi l'angolo finale
 * viene calcolato come 270° + valore animato.
 * Animates the end angle of an `lv_arc` from 0° to 360°, producing the "progressive draw"
 * effect of a circle. The start angle is 270° (12 o'clock), so the end angle is calculated
 * as 270° + animated value.
 *
 * @param var  Puntatore all'oggetto arco / Pointer to the arc object
 * @param val  Valore angolo animato [0 … 360]  / Animated angle value [0 … 360]
 */
static void anim_arc_end_angle_cb(void* var, int32_t val) {
    // Animiamo l'angolo finale dell'arco da 0 a 360
    // We animate the arc's end angle from 0 to 360
    // L'angolo 0 in LVGL corrisponde alle ore 3; aggiungiamo 270 per partire dalle ore 12
    // LVGL's angle 0 corresponds to 3 o'clock; we add 270 to start from 12 o'clock
    lv_arc_set_end_angle((lv_obj_t*)var, (int16_t)(270 + val));
}

/**
 * @brief Callback barra di progresso / Progress bar callback
 *
 * Aggiorna il valore della barra di progresso E, tramite `user_data`, aggiorna
 * anche la label testuale percentuale associata ("Avvio in corso... X%").
 * Updates the progress bar value AND, via `user_data`, also updates the associated
 * percentage text label ("Avvio in corso... X%").
 *
 * Il collegamento bar→label è realizzato con `lv_obj_set_user_data(bar, lbl_pct)`,
 * evitando variabili statiche globali.
 * The bar→label link is implemented via `lv_obj_set_user_data(bar, lbl_pct)`,
 * avoiding static global variables.
 *
 * @param var  Puntatore alla barra (`lv_bar_t*` castato a `void*`) / Pointer to bar
 * @param val  Valore animato [0 … 100] / Animated value [0 … 100]
 */
static void anim_bar_cb(void* var, int32_t val) {
    // Imposta il valore della barra SENZA animazione LVGL interna (LV_ANIM_OFF)
    // Set the bar value WITHOUT internal LVGL animation (LV_ANIM_OFF)
    // — l'animazione è già gestita dal chiamante tramite lv_anim_t
    // — animation is already managed by the caller via lv_anim_t
    lv_bar_set_value((lv_obj_t*)var, (int16_t)val, LV_ANIM_OFF);

    // Aggiorna la label percentuale se esiste (userData = label)
    // Update the percentage label if it exists (userData = label)
    lv_obj_t* lbl = (lv_obj_t*)lv_obj_get_user_data((lv_obj_t*)var);
    if (lbl) {
        // Buffer temporaneo per la stringa formattata / Temporary buffer for formatted string
        char buf[24];
        lv_snprintf(buf, sizeof(buf), "Avvio in corso... %d%%", (int)val);
        lv_label_set_text(lbl, buf);
    }
}

/**
 * @brief Callback posizione Y / Y-position callback
 *
 * Muove un oggetto verticalmente durante un'animazione.
 * Moves an object vertically during an animation.
 * Usato per l'effetto "slide-up" del sottotitolo.
 * Used for the subtitle "slide-up" effect.
 *
 * @param var  Puntatore all'oggetto LVGL / Pointer to LVGL object
 * @param val  Coordinata Y in pixel / Y coordinate in pixels
 */
static void anim_y_cb(void* var, int32_t val) {
    lv_obj_set_y((lv_obj_t*)var, val);
}

// ─── Callback transizione verso Home ─────────────────────────────────────────
// Transition callback to Home screen — chiamata dal motore animazioni al
// completamento della barra di progresso (t ≈ 6000ms)
// Called by the animation engine when the progress bar completes (t ≈ 6000ms)

/**
 * @brief Callback di fine splash / Splash completion callback
 *
 * Viene invocata automaticamente da LVGL al termine dell'animazione della barra
 * di progresso (ready_cb). Crea la schermata Home e la carica con una transizione
 * fade da 600ms. Il parametro `true` in `lv_scr_load_anim` indica che la vecchia
 * schermata splash viene eliminata automaticamente dopo la transizione.
 * Automatically invoked by LVGL when the progress bar animation completes (ready_cb).
 * Creates the Home screen and loads it with a 600ms fade transition. The `true`
 * parameter in `lv_scr_load_anim` means the old splash screen is auto-deleted.
 *
 * @param a  Puntatore alla struttura animazione (non usato) / Animation struct pointer (unused)
 */
static void on_splash_complete(lv_anim_t* a) {
    (void)a;  // Parametro non utilizzato / Unused parameter
    // Crea la home screen e la carica con una transizione fade
    // Create the home screen and load it with a fade transition
    lv_obj_t* home = ui_home_create();
    // LV_SCR_LOAD_ANIM_FADE_ON: dissolvenza in entrata (la nuova schermata appare gradualmente)
    // LV_SCR_LOAD_ANIM_FADE_ON: fade-in (the new screen gradually appears)
    // 600 = durata transizione in ms / transition duration in ms
    // 0   = delay prima della transizione / delay before transition
    // true = elimina la schermata corrente (splash) dopo la transizione / delete current screen after
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_FADE_ON, 600, 0, true);
}

// ─── Costruzione splash screen ────────────────────────────────────────────────
// Splash screen construction — crea tutti gli oggetti LVGL e avvia le animazioni
// Creates all LVGL objects and starts the animations

/**
 * @brief Crea e attiva la splash screen sandbox (tema scuro)
 *        Creates and activates the sandbox splash screen (dark theme)
 *
 * Questa funzione è l'entry point della splash screen sandbox. Va chiamata
 * dopo l'inizializzazione di LVGL (dentro `lvgl_port_lock`).
 * This function is the entry point for the sandbox splash screen. It should be
 * called after LVGL initialization (inside `lvgl_port_lock`).
 *
 * Struttura oggetti LVGL / LVGL object structure:
 * ```
 * scr (schermata root, sfondo scuro)
 *   ├── logo_cont (contenitore 280×280, centrato -40px Y)
 *   │     ├── arc_outer (Ø 260px, teal, 7px wide)
 *   │     ├── arc_inner (Ø 200px, teal2, 5px wide)
 *   │     └── lbl_brand ("ANTRALUX", font LARGE, centrato)
 *   ├── lbl_sub ("EasyConnect Display", subtitle, centrato +115px Y)
 *   ├── lbl_ver ("vX.Y.Z", font SMALL, centrato +148px Y)
 *   ├── bar (barra progresso 580×14px, in basso -56px)
 *   └── lbl_pct ("Avvio in corso... 0%", sopra la barra)
 * ```
 */
void ui_splash_create(void) {

    // ── Creazione schermata root ───────────────────────────────────────────────
    // Ogni "schermata" LVGL è un oggetto `lv_obj_t` con parent NULL.
    // Each LVGL "screen" is an `lv_obj_t` object with NULL parent.
    lv_obj_t* scr = lv_obj_create(NULL);

    // Sfondo scuro: UI_COLOR_BG_DEEP = colore di fondo profondo del tema sandbox
    // Dark background: UI_COLOR_BG_DEEP = deep background color of the sandbox theme
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);   // Opacità totale / Full opacity

    // Rimuove bordo e padding predefiniti di lv_obj
    // Removes default lv_obj border and padding
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ── Container logo (centrato, leggermente in alto) ──────────────────────
    // Contenitore trasparente per raggruppare gli archi e il testo del brand.
    // Transparent container to group the arcs and brand text.
    // Posizionato leggermente sopra il centro (-40px) per bilanciare visivamente
    // la barra di progresso in basso.
    // Positioned slightly above center (-40px) to visually balance the progress bar below.
    lv_obj_t* logo_cont = lv_obj_create(scr);
    lv_obj_set_size(logo_cont, 280, 280);                       // 280×280 pixel
    lv_obj_align(logo_cont, LV_ALIGN_CENTER, 0, -40);           // Centrato, +Y=-40 (verso l'alto)
    lv_obj_set_style_bg_opa(logo_cont, LV_OPA_TRANSP, 0);      // Sfondo trasparente
    lv_obj_set_style_border_width(logo_cont, 0, 0);             // Nessun bordo
    lv_obj_set_style_pad_all(logo_cont, 0, 0);                  // Nessun padding interno

    // ── Arco esterno (Ø 260px) ─────────────────────────────────────────────
    // L'arco `lv_arc` ha due parti: MAIN (sfondo circolare fisso) e INDICATOR (arco animato).
    // The `lv_arc` has two parts: MAIN (fixed circular background) and INDICATOR (animated arc).
    lv_obj_t* arc_outer = lv_arc_create(logo_cont);
    lv_obj_set_size(arc_outer, 260, 260);
    lv_obj_align(arc_outer, LV_ALIGN_CENTER, 0, 0);

    // MAIN: anello di sfondo sottile e semi-trasparente
    // MAIN: thin, semi-transparent background ring
    lv_obj_set_style_arc_color(arc_outer, UI_COLOR_ACCENT_DIM, LV_PART_MAIN);   // Colore dimmed
    lv_obj_set_style_arc_width(arc_outer, 4, LV_PART_MAIN);                      // 4px di spessore
    lv_obj_set_style_arc_opa(arc_outer, LV_OPA_40, LV_PART_MAIN);               // 40% opaco

    // INDICATOR: arco animato brillante (teal)
    // INDICATOR: bright animated arc (teal)
    lv_obj_set_style_arc_color(arc_outer, UI_COLOR_ACCENT, LV_PART_INDICATOR);  // Colore accent
    lv_obj_set_style_arc_width(arc_outer, 7, LV_PART_INDICATOR);                 // 7px di spessore

    // Rimuove il KNOB (pallino trascinabile) — questo arco è solo decorativo
    // Removes the KNOB (draggable dot) — this arc is purely decorative
    lv_obj_remove_style(arc_outer, NULL, LV_PART_KNOB);

    // Sfondo interno del widget arco: trasparente (non vogliamo un cerchio pieno)
    // Inner background of the arc widget: transparent (we don't want a filled circle)
    lv_obj_set_style_bg_opa(arc_outer, LV_OPA_TRANSP, 0);

    // Imposta angoli iniziali: start=270 (12 o'clock), end=270 (arco vuoto = 0°)
    // Sets initial angles: start=270 (12 o'clock), end=270 (empty arc = 0°)
    // BG_ANGLES: l'arco di sfondo copre 360° (cerchio completo)
    // BG_ANGLES: the background arc spans 360° (full circle)
    lv_arc_set_bg_angles(arc_outer, 0, 360);
    lv_arc_set_start_angle(arc_outer, 270);     // 12 o'clock = 270° in LVGL
    lv_arc_set_end_angle(arc_outer, 270);        // End = Start → arco vuoto all'inizio
    lv_obj_set_style_opa(arc_outer, LV_OPA_TRANSP, 0);  // Inizia completamente invisibile

    // ── Arco interno (Ø 200px) ─────────────────────────────────────────────
    // Arco più piccolo, stesso schema ma con UI_COLOR_ACCENT2 (variante del teal)
    // Smaller arc, same pattern but with UI_COLOR_ACCENT2 (teal variant)
    lv_obj_t* arc_inner = lv_arc_create(logo_cont);
    lv_obj_set_size(arc_inner, 200, 200);
    lv_obj_align(arc_inner, LV_ALIGN_CENTER, 0, 0);

    lv_obj_set_style_arc_color(arc_inner, UI_COLOR_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_inner, 3, LV_PART_MAIN);                  // 3px (più sottile)
    lv_obj_set_style_arc_opa(arc_inner, LV_OPA_30, LV_PART_MAIN);           // 30% opaco

    lv_obj_set_style_arc_color(arc_inner, UI_COLOR_ACCENT2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_inner, 5, LV_PART_INDICATOR);

    lv_obj_remove_style(arc_inner, NULL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(arc_inner, LV_OPA_TRANSP, 0);
    lv_arc_set_bg_angles(arc_inner, 0, 360);
    lv_arc_set_start_angle(arc_inner, 270);
    lv_arc_set_end_angle(arc_inner, 270);
    lv_obj_set_style_opa(arc_inner, LV_OPA_TRANSP, 0);  // Inizia invisibile

    // ── Testo "ANTRALUX" (dentro logo_cont, centrato) ──────────────────────
    // Testo del brand, posizionato al centro del logo_cont, sopra gli archi.
    // Nota: in LVGL il rendering degli oggetti figli avviene nell'ordine di creazione,
    // quindi lbl_brand (creato dopo gli archi) viene disegnato sopra di loro.
    // Brand text, centered inside logo_cont, above the arcs.
    // Note: LVGL renders child objects in creation order, so lbl_brand (created after arcs)
    // is drawn on top of them (higher z-order).
    lv_obj_t* lbl_brand = lv_label_create(logo_cont);
    lv_label_set_text(lbl_brand, "ANTRALUX");
    lv_obj_set_style_text_font(lbl_brand, UI_FONT_LARGE, 0);             // Font grande
    lv_obj_set_style_text_color(lbl_brand, UI_COLOR_TEXT_PRIMARY, 0);   // Colore testo primario
    lv_obj_set_style_text_letter_space(lbl_brand, 6, 0);                 // Spaziatura lettere 6px
    lv_obj_align(lbl_brand, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(lbl_brand, LV_OPA_TRANSP, 0);  // Inizia invisibile

    // ── Sottotitolo "EasyConnect" (sotto logo_cont) ─────────────────────────
    // Figlio di `scr` (non di logo_cont) per posizionamento indipendente.
    // Subtext, child of `scr` (not logo_cont) for independent positioning.
    lv_obj_t* lbl_sub = lv_label_create(scr);
    lv_label_set_text(lbl_sub, "EasyConnect  Display");
    lv_obj_set_style_text_font(lbl_sub, UI_FONT_SUBTITLE, 0);
    lv_obj_set_style_text_color(lbl_sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_letter_space(lbl_sub, 3, 0);
    // +115px dal centro = sotto il logo_cont (che è centrato a -40px)
    // +115px from center = below logo_cont (which is centered at -40px)
    lv_obj_align(lbl_sub, LV_ALIGN_CENTER, 0, 115);
    lv_obj_set_style_opa(lbl_sub, LV_OPA_TRANSP, 0);  // Inizia invisibile

    // ── Label versione ──────────────────────────────────────────────────────
    // Mostra la versione firmware sandbox (definita in ui_styles.h come UI_SANDBOX_VERSION)
    // Shows the sandbox firmware version (defined in ui_styles.h as UI_SANDBOX_VERSION)
    lv_obj_t* lbl_ver = lv_label_create(scr);
    lv_label_set_text(lbl_ver, "v" UI_SANDBOX_VERSION);  // Concatenazione macro C
    lv_obj_set_style_text_font(lbl_ver, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_ver, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_ver, LV_ALIGN_CENTER, 0, 148);      // Sotto lbl_sub
    lv_obj_set_style_opa(lbl_ver, LV_OPA_TRANSP, 0);

    // ── Barra di progresso ──────────────────────────────────────────────────
    // `lv_bar` con range [0, 100] — usata come indicatore di avanzamento boot.
    // `lv_bar` with range [0, 100] — used as boot progress indicator.
    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 580, 14);                              // 580px wide, 14px alto
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -56);            // Ancorata in basso, -56px dal fondo
    lv_bar_set_range(bar, 0, 100);                              // Range percentuale
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);                     // Valore iniziale = 0

    // Stile MAIN (sfondo della barra) / MAIN style (bar background)
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG_CARD, LV_PART_MAIN);     // Sfondo card
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 7, LV_PART_MAIN);                       // Bordi arrotondati
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, UI_COLOR_BORDER, LV_PART_MAIN);

    // Stile INDICATOR (parte piena della barra) / INDICATOR style (filled part of bar)
    lv_obj_set_style_bg_color(bar, UI_COLOR_ACCENT, LV_PART_INDICATOR);  // Teal accent
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 7, LV_PART_INDICATOR);

    // ── Label percentuale ──────────────────────────────────────────────────
    // Testo sopra la barra che mostra la percentuale in tempo reale.
    // Text above the bar that shows the percentage in real time.
    lv_obj_t* lbl_pct = lv_label_create(scr);
    lv_label_set_text(lbl_pct, "Avvio in corso... 0%");
    lv_obj_set_style_text_font(lbl_pct, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(lbl_pct, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_pct, LV_ALIGN_BOTTOM_MID, 0, -78);                 // Sopra la barra

    // Link bar → label via user_data per aggiornamento nel callback
    // Link bar → label via user_data for update in callback
    // PATTERN: anziché variabili statiche globali, usiamo user_data come "attached context"
    // PATTERN: instead of static globals, we use user_data as "attached context"
    lv_obj_set_user_data(bar, lbl_pct);

    // ────────────────────────────────────────────────────────────────────────
    // ANIMAZIONI / ANIMATIONS
    // ────────────────────────────────────────────────────────────────────────
    // Tutte le animazioni usano la struttura `lv_anim_t a` re-inizializzata a ogni uso.
    // All animations use the `lv_anim_t a` struct re-initialized at each use.
    // lv_anim_init() azzera la struttura e imposta i valori di default.
    // lv_anim_init() zeroes the struct and sets default values.
    lv_anim_t a;

    // ── 1. Fade-in arco esterno (inizia t=0, dura 300ms) ─────────────────
    // L'arco esterno appare immediatamente, poi inizia a "disegnarsi" (animazione 2).
    // The outer arc appears immediately, then starts "drawing" (animation 2).
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_outer);
    lv_anim_set_exec_cb(&a, anim_opa_cb);                        // Callback opacità
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);        // 0 → 255
    lv_anim_set_time(&a, 300);                                    // 300ms di durata
    lv_anim_set_delay(&a, 0);                                     // Nessun ritardo
    lv_anim_start(&a);

    // ── 2. Disegno arco esterno 0→360° (t=0, 1500ms, ease-out) ──────────
    // L'arco si "disegna" progressivamente partendo dalle 12 e girando in senso orario.
    // The arc "draws" progressively starting from 12 o'clock and going clockwise.
    // ease-out = veloce all'inizio, rallenta alla fine (effetto naturale di decelerazione)
    // ease-out = fast at start, slows at end (natural deceleration effect)
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_outer);
    lv_anim_set_exec_cb(&a, anim_arc_end_angle_cb);              // Callback angolo arco
    lv_anim_set_values(&a, 0, 360);                               // Da 0° a 360° (cerchio completo)
    lv_anim_set_time(&a, 1500);                                   // 1500ms di durata
    lv_anim_set_delay(&a, 0);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);              // Curva ease-out
    lv_anim_start(&a);

    // ── 3. Fade-in arco interno (t=200ms, dura 300ms) ────────────────────
    // L'arco interno appare 200ms dopo quello esterno, creando un effetto sfalsato.
    // The inner arc appears 200ms after the outer one, creating a staggered effect.
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_inner);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 300);
    lv_anim_set_delay(&a, 200);                                   // 200ms di ritardo
    lv_anim_start(&a);

    // ── 4. Disegno arco interno 0→360° (t=200ms, 1300ms, ease-out) ──────
    // Leggermente più veloce (1300ms vs 1500ms) e sfalsato di 200ms.
    // Slightly faster (1300ms vs 1500ms) and offset by 200ms.
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc_inner);
    lv_anim_set_exec_cb(&a, anim_arc_end_angle_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, 1300);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── 5. Fade-in testo ANTRALUX (t=700ms, 900ms) ───────────────────────
    // Il testo del brand appare dopo che gli archi sono già quasi completi.
    // The brand text appears after the arcs are nearly complete.
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_brand);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 900);                                    // Fade lungo (900ms)
    lv_anim_set_delay(&a, 700);                                   // Dopo 700ms
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // ── 6. Slide-up + fade-in sottotitolo (t=1500ms, 700ms) ──────────────
    // Effetto combinato: il sottotitolo sale verso l'alto (da +30px) e contemporaneamente
    // compare per dissolvenza. Questo richiede DUE animazioni parallele sullo stesso oggetto.
    // Combined effect: the subtitle rises upward (from +30px) and simultaneously fades in.
    // This requires TWO parallel animations on the same object.

    // Prima, leggiamo la posizione Y finale corrente (calcolata da lv_obj_align)
    // First, read the current final Y position (calculated by lv_obj_align)
    int sub_y_final = lv_obj_get_y(lbl_sub);

    // Spostiamo l'oggetto di 30px in basso come posizione di partenza dell'animazione
    // Move the object 30px down as the starting position for the animation
    lv_obj_set_y(lbl_sub, sub_y_final + 30);  // posizione iniziale (più in basso)

    // Animazione Y: scende da +30px alla posizione finale
    // Y animation: moves from +30px to final position
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_sub);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_values(&a, sub_y_final + 30, sub_y_final);  // Da +30px alla posizione finale
    lv_anim_set_time(&a, 700);
    lv_anim_set_delay(&a, 1500);                              // Inizia a t=1500ms
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // Animazione opacità parallela: fade-in simultaneo allo slide-up
    // Parallel opacity animation: simultaneous fade-in with the slide-up
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_sub);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 700);
    lv_anim_set_delay(&a, 1500);
    // Nota: nessuna path_cb → default lineare (il fade è lineare, lo slide ha ease-out)
    // Note: no path_cb → default linear (fade is linear, slide has ease-out)
    lv_anim_start(&a);

    // ── 7. Fade-in label versione (t=2000ms, 500ms) ───────────────────────
    // L'ultima animazione "cosmetica" — la versione appare per ultima.
    // The last "cosmetic" animation — the version label appears last.
    lv_anim_init(&a);
    lv_anim_set_var(&a, lbl_ver);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a, 500);
    lv_anim_set_delay(&a, 2000);                                  // Inizia a t=2000ms
    lv_anim_start(&a);

    // ── 8. Barra di progresso 0→100 in 5800ms (con delay 200ms) ──────────
    // Questa è l'animazione "driver" della splash: quando termina (t≈6000ms),
    // scatta on_splash_complete() che avvia la transizione verso Home.
    // This is the splash "driver" animation: when it completes (t≈6000ms),
    // on_splash_complete() fires and starts the transition to Home.
    //
    // Timing totale:  delay(200ms) + time(5800ms) = 6000ms
    // Total timing:   delay(200ms) + time(5800ms) = 6000ms
    //
    // Il percorso è lineare (lv_anim_path_linear) perché la barra di progresso
    // deve avanzare a velocità costante per sembrare un "caricamento reale".
    // The path is linear (lv_anim_path_linear) because the progress bar must advance
    // at constant speed to look like a "real loading" process.
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_exec_cb(&a, anim_bar_cb);                        // Aggiorna barra + label
    lv_anim_set_values(&a, 0, 100);                               // 0% → 100%
    lv_anim_set_time(&a, 5800);                                   // 5800ms di durata
    lv_anim_set_delay(&a, 200);                                   // Parte dopo 200ms
    lv_anim_set_path_cb(&a, lv_anim_path_linear);                // Progresso lineare
    lv_anim_set_ready_cb(&a, on_splash_complete);                 // Callback di fine → Home
    lv_anim_start(&a);

    // ── Carica lo splash come schermata attiva ──────────────────────────────
    // lv_scr_load() attiva immediatamente la schermata senza animazione.
    // Le animazioni definite sopra partiranno nel prossimo tick del task LVGL.
    // lv_scr_load() activates the screen immediately without animation.
    // The animations defined above will start on the next LVGL task tick.
    lv_scr_load(scr);
}
