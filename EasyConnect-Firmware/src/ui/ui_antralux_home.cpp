/**
 * @file ui_antralux_home.cpp
 * @brief Home screen demo Antralux — 4 pulsanti + tendina pull-down (drawer).
 *        Antralux demo home screen — 4 buttons + pull-down drawer.
 *
 * Questo file è parte della UI "sandbox/demo" Antralux, NON del target di produzione.
 * This file is part of the Antralux "sandbox/demo" UI, NOT the production target.
 *
 * Layout 1024×600:
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
 * Pull-down drawer (220px, slides from top):
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                         Hello                                     [X]  │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 * Gesture: swipe verso il basso su qualsiasi punto → apre drawer
 * Gesture: swipe downward anywhere → opens drawer
 * Chiusura: pulsante [X] nel drawer oppure tap sull'overlay scuro
 * Close: [X] button in drawer or tap on dark overlay
 *
 * Architettura z-order degli oggetti LVGL / LVGL z-order architecture:
 *   1. scr          → background
 *   2. hdr          → header 70px
 *   3. grid         → griglia 2×2 pulsanti
 *   4. s_overlay    → overlay nero semitrasparente (sopra grid, sotto drawer)
 *   5. s_drawer     → pannello tendina (z-order massimo)
 *
 * Il z-order in LVGL segue l'ordine di creazione dei figli: l'ultimo figlio
 * creato è il più in alto (viene disegnato sopra tutti gli altri).
 * Z-order in LVGL follows child creation order: the last child created
 * is topmost (drawn above all others).
 */

#include "ui_antralux_home.h"   // dichiarazione di ui_antralux_home_create()
#include "lvgl.h"

// ─── Palette ──────────────────────────────────────────────────────────────────
// Colori usati solo in questo file (prefisso HM_ per evitare conflitti)
// Colors used only in this file (HM_ prefix to avoid conflicts)
#define HM_BG       lv_color_hex(0xEEF3F8)   ///< Sfondo azzurro chiaro / Light blue background
#define HM_WHITE    lv_color_hex(0xFFFFFF)   ///< Bianco puro / Pure white
#define HM_ORANGE   lv_color_hex(0xE84820)   ///< Arancione Antralux / Antralux orange
#define HM_TEXT     lv_color_hex(0x243447)   ///< Blu scuro testo / Dark blue text
#define HM_SHADOW   lv_color_hex(0xBBCCDD)   ///< Grigio-blu per ombre / Grey-blue for shadows
#define HM_DIM      lv_color_hex(0x7A92B0)   ///< Grigio-blu attenuato / Dimmed grey-blue

// ─── Dimensioni drawer ────────────────────────────────────────────────────────
// Altezza della tendina pull-down in pixel / Height of the pull-down drawer in pixels
#define DRAWER_H    220

// ─── Stato globale (un'istanza alla volta) ────────────────────────────────────
// Questi puntatori sono globali al file (static) e vengono azzerati ad ogni
// chiamata a ui_antralux_home_create() per gestire il caso in cui la home
// viene ricreata (es. ritorno dalle impostazioni).
// These pointers are file-global (static) and reset on each call to
// ui_antralux_home_create() to handle the case where home is recreated
// (e.g. returning from settings).
static lv_obj_t* s_drawer  = NULL;  ///< Puntatore al pannello drawer / Drawer panel pointer
static lv_obj_t* s_overlay = NULL;  ///< Puntatore all'overlay scuro / Dark overlay pointer
static bool      s_open    = false; ///< True se il drawer è aperto / True if drawer is open

// ─── Callback animazioni ──────────────────────────────────────────────────────

/**
 * @brief Callback animazione traslazione Y — muove un oggetto sull'asse verticale.
 *        Y-translation animation callback — moves an object on the vertical axis.
 *
 * Usata per il drawer: scende (slide-down) quando si apre, sale (slide-up) quando si chiude.
 * Used for the drawer: slides down when opening, slides up when closing.
 *
 * lv_obj_set_style_translate_y applica una traslazione pura senza cambiare
 * la posizione "logica" dell'oggetto: utile per animazioni che devono essere
 * reversibili senza effetti collaterali sul layout.
 * lv_obj_set_style_translate_y applies a pure translation without changing
 * the object's "logical" position: useful for reversible animations without
 * layout side effects.
 *
 * @param obj  Oggetto LVGL da traslare (castato da void*).
 * @param v    Offset Y in pixel (negativo = sopra la posizione originale).
 *             Y offset in pixels (negative = above original position).
 */
static void _cb_translate_y(void* obj, int32_t v) {
    lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(obj), (lv_coord_t)v, 0);
}

/**
 * @brief Callback animazione opacità — imposta la trasparenza globale dell'oggetto.
 *        Opacity animation callback — sets the object's global transparency.
 *
 * Usata per il fade-in dell'overlay e il fade-out quando si chiude il drawer.
 * Used for the overlay fade-in and fade-out when closing the drawer.
 *
 * @param obj  Oggetto LVGL (castato da void*).
 * @param v    Valore opacità [LV_OPA_TRANSP(0) … LV_OPA_COVER(255)].
 */
static void _cb_opa(void* obj, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(v), 0);
}

// ─── Chiusura drawer ──────────────────────────────────────────────────────────

/**
 * @brief Callback "ready" dell'animazione di chiusura drawer.
 *        "Ready" callback of the drawer close animation.
 *
 * Viene chiamata da LVGL quando l'animazione slide-up è completata.
 * Called by LVGL when the slide-up animation is complete.
 *
 * Nasconde sia il drawer che l'overlay (LV_OBJ_FLAG_HIDDEN) e resetta il flag s_open.
 * Hides both the drawer and the overlay (LV_OBJ_FLAG_HIDDEN) and resets the s_open flag.
 *
 * Nascondere invece di eliminare permette di riutilizzare gli oggetti alla
 * prossima apertura senza riallocarli.
 * Hiding instead of deleting allows reuse on the next open without reallocation.
 *
 * @param a  Puntatore all'animazione completata (non usato).
 */
static void _on_close_done(lv_anim_t* /*a*/) {
    if (s_drawer)  lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_open = false;
}

/**
 * @brief Avvia l'animazione di chiusura del drawer.
 *        Starts the drawer close animation.
 *
 * Se il drawer è già chiuso (s_open == false), non fa nulla.
 * If the drawer is already closed (s_open == false), does nothing.
 *
 * Animazioni in parallelo / Parallel animations:
 *   1. Overlay fade-out: opa 128 (50%) → 0, 280ms, ease-in
 *   2. Drawer slide-up: translate_y 0 → -DRAWER_H (-220px), 320ms, ease-in
 *      Al termine del slide-up → _on_close_done() nasconde gli oggetti.
 *      On slide-up completion → _on_close_done() hides the objects.
 *
 * ease-in = accelera all'inizio (sembra "tirare su" il drawer).
 * ease-in = accelerates at start (feels like "pulling up" the drawer).
 */
static void _close_drawer(void) {
    if (!s_open) return;  // già chiuso, esci subito / already closed, exit immediately

    lv_anim_t a;

    // Animazione 1: Overlay fade-out (50% → 0% opacità)
    // Animation 1: Overlay fade-out (50% → 0% opacity)
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_TRANSP);  // da semitrasparente a invisibile
    lv_anim_set_time(&a, 280);                           // 280ms per il fade
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);      // accelera → sembra "scomparire velocemente"
    lv_anim_start(&a);

    // Animazione 2: Drawer slide-up (torna sopra lo schermo)
    // Animation 2: Drawer slide-up (returns above the screen)
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, _cb_translate_y);
    lv_anim_set_values(&a, 0, -DRAWER_H);               // da posizione 0 a -220px (fuori schermo)
    lv_anim_set_time(&a, 320);                           // leggermente più lungo del fade
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);      // accelera → sembra "scattare su"
    // Quando lo slide-up finisce, chiama _on_close_done per nascondere gli oggetti
    // When slide-up ends, call _on_close_done to hide the objects
    lv_anim_set_ready_cb(&a, _on_close_done);
    lv_anim_start(&a);
}

/**
 * @brief Avvia l'animazione di apertura del drawer.
 *        Starts the drawer open animation.
 *
 * Se il drawer è già aperto (s_open == true), non fa nulla.
 * If the drawer is already open (s_open == true), does nothing.
 *
 * Prima rende visibili gli oggetti (rimuove HIDDEN) e li prepara nella
 * posizione di partenza, poi avvia le animazioni.
 * First makes objects visible (removes HIDDEN) and prepares them in
 * start position, then starts animations.
 *
 * Animazioni in parallelo / Parallel animations:
 *   1. Overlay fade-in: opa 0 → 128 (50%), 300ms, ease-out
 *   2. Drawer slide-down: translate_y -DRAWER_H → 0, 360ms, ease-out
 *
 * ease-out = decelera alla fine (sembra "atterrare" dolcemente il drawer).
 * ease-out = decelerates at end (feels like the drawer "lands" softly).
 */
static void _open_drawer(void) {
    if (s_open) return;  // già aperto, esci subito / already open, exit immediately
    s_open = true;

    // Prepara il drawer fuori dallo schermo (sopra, translate_y negativo)
    // Prepare the drawer off-screen (above, negative translate_y)
    lv_obj_set_style_translate_y(s_drawer, -DRAWER_H, 0);

    // Prepara overlay inizialmente invisibile
    // Prepare overlay initially invisible
    lv_obj_set_style_opa(s_overlay, LV_OPA_TRANSP, 0);

    // Rendi visibili prima di animare (da HIDDEN a visible)
    // Make visible before animating (from HIDDEN to visible)
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_drawer,  LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;

    // Animazione 1: Overlay fade-in (0 → 50% opacità)
    // Animation 1: Overlay fade-in (0 → 50% opacity)
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_exec_cb(&a, _cb_opa);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_50);
    lv_anim_set_time(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // Animazione 2: Drawer slide-down (da -220px a posizione 0)
    // Animation 2: Drawer slide-down (from -220px to position 0)
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_drawer);
    lv_anim_set_exec_cb(&a, _cb_translate_y);
    lv_anim_set_values(&a, -DRAWER_H, 0);               // da fuori schermo a posizione finale
    lv_anim_set_time(&a, 360);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);     // decelera → atterraggio morbido
    lv_anim_start(&a);
}

// ─── Event callback: gesture su schermo ──────────────────────────────────────

/**
 * @brief Callback evento gesture sulla schermata principale.
 *        Gesture event callback on the main screen.
 *
 * Registrata su scr con LV_EVENT_GESTURE. Le gesture "fanno bubble" dai
 * figli verso il genitore, quindi un swipe su qualsiasi oggetto della home
 * (grid, header, ecc.) arriva a questa callback.
 *
 * Registered on scr with LV_EVENT_GESTURE. Gestures "bubble up" from
 * children to parent, so a swipe on any home object (grid, header, etc.)
 * reaches this callback.
 *
 * @param e  Evento LVGL (non usato direttamente; usiamo lv_indev_get_act()).
 */
static void _gesture_cb(lv_event_t* e) {
    // Legge la direzione della gesture dal dispositivo di input attivo
    // Reads the gesture direction from the active input device
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    // LV_DIR_BOTTOM = swipe verso il basso (dal alto verso il basso)
    // LV_DIR_BOTTOM = swipe downward (from top to bottom)
    if (dir == LV_DIR_BOTTOM) {
        _open_drawer();
    }
}

// ─── Event callback: tap sull'overlay ────────────────────────────────────────

/**
 * @brief Callback click sull'overlay semitrasparente.
 *        Click callback on the semi-transparent overlay.
 *
 * L'overlay copre tutta la schermata con sfondo nero al 50%.
 * Tappare l'overlay chiude il drawer (come su Android/iOS).
 *
 * The overlay covers the full screen with 50% black background.
 * Tapping the overlay closes the drawer (as on Android/iOS).
 *
 * @param e  Evento LVGL. Verifichiamo che sia LV_EVENT_CLICKED.
 */
static void _overlay_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _close_drawer();
    }
}

// ─── Event callback: pulsante X ──────────────────────────────────────────────

/**
 * @brief Callback click sul pulsante [X] nel drawer.
 *        Click callback on the [X] button inside the drawer.
 *
 * @param e  Evento LVGL.
 */
static void _close_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _close_drawer();
    }
}

// ─── Helper: crea un pulsante home ───────────────────────────────────────────

/**
 * @brief Crea un pulsante card 210×190 per la griglia home.
 *        Creates a 210×190 card button for the home grid.
 *
 * Ogni card è un oggetto LVGL con:
 * Each card is an LVGL object with:
 *   - Sfondo bianco con ombra soft / White background with soft shadow
 *   - Icona (simbolo LVGL font Montserrat 48) colorata / Colored icon (LVGL symbol font 48)
 *   - Etichetta testo sotto l'icona / Text label below the icon
 *   - Feedback visivo al press (sfondo leggermente grigio, ombra ridotta)
 *   - Visual feedback on press (slightly grey bg, reduced shadow)
 *
 * Le card NON hanno callback clic: in questo demo sono solo decorative.
 * Cards have NO click callbacks: in this demo they are purely decorative.
 *
 * @param parent      Oggetto genitore (il container grid).
 *                    Parent object (the grid container).
 * @param symbol      Stringa simbolo LVGL (es. LV_SYMBOL_WIFI).
 *                    LVGL symbol string (e.g. LV_SYMBOL_WIFI).
 * @param label_text  Testo etichetta sotto l'icona. Label text below the icon.
 * @param icon_color  Colore dell'icona. Icon color.
 * @return            Puntatore alla card creata. Pointer to the created card.
 */
static lv_obj_t* make_home_btn(lv_obj_t* parent,
                                const char* symbol,
                                const char* label_text,
                                lv_color_t  icon_color) {
    // ── Card principale ───────────────────────────────────────────────────────
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 210, 190);                          // dimensione fissa card
    lv_obj_set_style_bg_color(card, HM_WHITE, 0);             // sfondo bianco normale
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 22, 0);                     // angoli molto arrotondati
    lv_obj_set_style_border_width(card, 0, 0);                // nessun bordo visibile
    // Ombra soft che dà l'effetto "floating card"
    // Soft shadow that gives the "floating card" effect
    lv_obj_set_style_shadow_color(card, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);               // ombra larga e diffusa
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);                // offset verso il basso
    lv_obj_set_style_pad_all(card, 0, 0);                     // nessun padding (gestito manualmente)
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);          // la card non scrolla

    // Feedback visivo al press: sfondo leggermente grigio + ombra ridotta
    // Visual feedback on press: slightly grey background + reduced shadow
    lv_obj_set_style_bg_color(card, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(card, 8, LV_STATE_PRESSED); // ombra più piccola = "premuto"

    // ── Icona (simbolo LVGL grande) ───────────────────────────────────────────
    // I simboli LVGL (LV_SYMBOL_*) sono caratteri speciali del font Montserrat
    // codificati in UTF-8. Usano font 48pt per essere grandi e leggibili.
    // LVGL symbols (LV_SYMBOL_*) are special characters in the Montserrat font
    // encoded in UTF-8. They use the 48pt font to be large and readable.
    lv_obj_t* ico = lv_label_create(card);
    lv_label_set_text(ico, symbol);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    // Centrato nella card, spostato -22px in Y per fare spazio all'etichetta sotto
    // Centered in card, shifted -22px Y to make room for label below
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -22);

    // ── Etichetta testo ───────────────────────────────────────────────────────
    lv_obj_t* lbl = lv_label_create(card);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, HM_TEXT, 0);
    // Centrato nella card, +52px in Y = sotto l'icona
    // Centered in card, +52px Y = below the icon
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 52);

    return card;
}

// ─── Costruzione Home ─────────────────────────────────────────────────────────

/**
 * @brief Crea e restituisce la home screen Antralux demo.
 *        Creates and returns the Antralux demo home screen.
 *
 * L'ordine di creazione degli oggetti LVGL determina il z-order:
 * The LVGL object creation order determines z-order:
 *   1. scr (radice)
 *   2. hdr (header)
 *   3. grid (griglia pulsanti) ← sotto overlay e drawer
 *   4. s_overlay (overlay buio) ← sopra grid, sotto drawer
 *   5. s_drawer (tendina) ← sopra tutto
 *
 * La funzione azzera anche lo stato globale del drawer per gestire
 * correttamente il caso in cui la home venga ricreata.
 * The function also resets the drawer global state to correctly handle
 * the case where the home is recreated.
 *
 * @return Puntatore alla screen LVGL. NON viene attivata automaticamente.
 * @return Pointer to the LVGL screen. NOT automatically activated.
 */
lv_obj_t* ui_antralux_home_create(void) {

    // ── Reset stato drawer ────────────────────────────────────────────────────
    // Necessario se la home viene ricreata (es. ritorno da altra schermata).
    // Needed if home is recreated (e.g. returning from another screen).
    // I vecchi s_drawer/s_overlay saranno eliminati automaticamente quando
    // LVGL elimina la vecchia schermata (auto_del=true in scr_load_anim).
    // Old s_drawer/s_overlay will be deleted automatically when LVGL
    // deletes the old screen (auto_del=true in scr_load_anim).
    s_drawer  = NULL;
    s_overlay = NULL;
    s_open    = false;

    // ── Schermata radice ──────────────────────────────────────────────────────
    // parent=NULL crea una nuova schermata (non ha un genitore visivo)
    // parent=NULL creates a new screen (has no visual parent)
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, HM_BG, 0);         // sfondo azzurro chiaro
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);           // nessun bordo interno
    lv_obj_set_style_pad_all(scr, 0, 0);                // nessun padding
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);    // la schermata non scrolla

    // ── Header 70px ───────────────────────────────────────────────────────────
    // Barra superiore con titolo "EasyConnect" e hint gesture a destra.
    // Top bar with "EasyConnect" title and gesture hint on the right.
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, 70);
    lv_obj_set_pos(hdr, 0, 0);                          // angolo top-left della schermata
    lv_obj_set_style_bg_color(hdr, HM_WHITE, 0);        // header bianco
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hdr, 0, 0);                 // angoli vivi (rettangolo)
    lv_obj_set_style_border_width(hdr, 0, 0);
    // Ombra verso il basso per separare visivamente l'header dal contenuto
    // Shadow downward to visually separate header from content
    lv_obj_set_style_shadow_color(hdr, HM_SHADOW, 0);
    lv_obj_set_style_shadow_width(hdr, 18, 0);
    lv_obj_set_style_shadow_ofs_y(hdr, 4, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    // Disabilita scroll e click sull'header (è solo decorativo)
    // Disable scroll and click on header (it's decorative only)
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Titolo "EasyConnect" in arancione Antralux
    // "EasyConnect" title in Antralux orange
    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "EasyConnect");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, HM_ORANGE, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 28, 0);      // 28px dal bordo sinistro

    // Hint "scorri dall'alto" con simbolo freccia-giù
    // "swipe from top" hint with down-arrow symbol
    lv_obj_t* hint = lv_label_create(hdr);
    lv_label_set_text(hint, LV_SYMBOL_DOWN "  Scorri dall'alto");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, HM_DIM, 0);       // grigio attenuato per essere discreto
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -28, 0);     // 28px dal bordo destro

    // ── Griglia 2×2 pulsanti ──────────────────────────────────────────────────
    // Container flex-wrap che dispone i 4 pulsanti in 2 righe × 2 colonne.
    // Flex-wrap container that lays out 4 buttons in 2 rows × 2 columns.
    //
    // Calcolo posizione Y del centro griglia:
    // Y position calculation for grid center:
    //   Area contenuto: Y = 70..600 = 530px, centro = 70 + 265 = 335px
    //   Content area: Y = 70..600 = 530px, center = 70 + 265 = 335px
    //   Centro schermo: Y = 300px
    //   Screen center: Y = 300px
    //   Offset necessario: 335 - 300 = +35px
    //   Required offset: 335 - 300 = +35px
    lv_obj_t* grid = lv_obj_create(scr);
    lv_obj_set_size(grid, 500, 420);                     // dimensione sufficiente per 2×2 con gap
    lv_obj_align(grid, LV_ALIGN_CENTER, 0, 35);          // +35px per centrare nell'area contenuto
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);    // sfondo trasparente (si vede HM_BG)
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_column(grid, 40, 0);            // gap orizzontale tra card: 40px
    lv_obj_set_style_pad_row(grid, 40, 0);               // gap verticale tra card: 40px
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Layout FLEX ROW_WRAP: i figli si dispongono in riga e vanno a capo
    // FLEX ROW_WRAP layout: children are placed in a row and wrap to next row
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    // Allineamento: centrato su tutti e tre gli assi
    // Alignment: centered on all three axes
    lv_obj_set_flex_align(grid,
                          LV_FLEX_ALIGN_CENTER,   // asse principale (orizzontale)
                          LV_FLEX_ALIGN_CENTER,   // asse trasversale (verticale per riga)
                          LV_FLEX_ALIGN_CENTER);  // asse trasversale globale

    // Crea i 4 pulsanti: ogni make_home_btn aggiunge una card come figlio di grid
    // Creates the 4 buttons: each make_home_btn adds a card as a child of grid
    make_home_btn(grid, LV_SYMBOL_WIFI,     "WiFi",          HM_ORANGE);
    make_home_btn(grid, LV_SYMBOL_SETTINGS, "Impostazioni",  lv_color_hex(0x3A6BC8));  // blu
    make_home_btn(grid, LV_SYMBOL_BELL,     "Notifiche",     lv_color_hex(0x28A745));  // verde
    make_home_btn(grid, LV_SYMBOL_LIST,     "Stato",         lv_color_hex(0x8C44B8));  // viola

    // ── Gesture listener su schermata ─────────────────────────────────────────
    // Le gesture "fanno bubble" dai figli verso la schermata radice.
    // Gestures "bubble up" from children to the root screen.
    // Registrando il callback sulla schermata catturiamo swipe ovunque.
    // Registering the callback on the screen captures swipe anywhere.
    lv_obj_add_event_cb(scr, _gesture_cb, LV_EVENT_GESTURE, NULL);

    // ── Overlay scuro ─────────────────────────────────────────────────────────
    // IMPORTANTE: creato DOPO la grid → z-order superiore alla grid.
    // IMPORTANT: created AFTER the grid → z-order above the grid.
    // Copre tutta la schermata (1024×600), inizialmente nascosto.
    // Covers the full screen (1024×600), initially hidden.
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 1024, 600);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x000000), 0);  // nero
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_50, 0);    // 50% opacità (target dell'animazione)
    lv_obj_set_style_opa(s_overlay, LV_OPA_TRANSP, 0);   // inizia completamente trasparente
    lv_obj_set_style_radius(s_overlay, 0, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);      // inizialmente nascosto
    // Click su overlay → chiude il drawer
    // Click on overlay → closes the drawer
    lv_obj_add_event_cb(s_overlay, _overlay_click_cb, LV_EVENT_CLICKED, NULL);

    // ── Drawer panel ──────────────────────────────────────────────────────────
    // IMPORTANTE: creato per ULTIMO → z-order massimo (sopra overlay).
    // IMPORTANT: created LAST → maximum z-order (above overlay).
    // Larghezza piena (1024px), altezza DRAWER_H (220px), parte da y=0.
    // Full width (1024px), height DRAWER_H (220px), starts at y=0.
    // Posizionato fuori schermo con translate_y=-DRAWER_H (-220px).
    // Positioned off-screen with translate_y=-DRAWER_H (-220px).
    s_drawer = lv_obj_create(scr);
    lv_obj_set_size(s_drawer, 1024, DRAWER_H);
    lv_obj_set_pos(s_drawer, 0, 0);
    // Traslazione iniziale: il drawer è sopra lo schermo (fuori dalla viewport)
    // Initial translation: drawer is above the screen (outside viewport)
    lv_obj_set_style_translate_y(s_drawer, -DRAWER_H, 0);
    lv_obj_set_style_bg_color(s_drawer, HM_WHITE, 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);             // angoli vivi (non arrotondati)
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    // Ombra verso il basso per far capire che il drawer è "sopra" il contenuto
    // Shadow downward to show that the drawer is "above" the content
    lv_obj_set_style_shadow_color(s_drawer, lv_color_hex(0x7090B0), 0);
    lv_obj_set_style_shadow_width(s_drawer, 30, 0);
    lv_obj_set_style_shadow_ofs_y(s_drawer, 12, 0);
    // Padding laterale per il contenuto interno
    // Side padding for internal content
    lv_obj_set_style_pad_left(s_drawer, 40, 0);
    lv_obj_set_style_pad_right(s_drawer, 40, 0);
    lv_obj_set_style_pad_top(s_drawer, 0, 0);
    lv_obj_set_style_pad_bottom(s_drawer, 0, 0);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_drawer, LV_OBJ_FLAG_HIDDEN);       // inizialmente nascosto

    // ── Contenuto del drawer ──────────────────────────────────────────────────

    // Testo "Hello" centrato — placeholder per il contenuto reale del drawer
    // "Hello" text centered — placeholder for the drawer's real content
    lv_obj_t* hello = lv_label_create(s_drawer);
    lv_label_set_text(hello, "Hello");
    lv_obj_set_style_text_font(hello, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(hello, HM_ORANGE, 0);
    lv_obj_align(hello, LV_ALIGN_CENTER, 0, 0);

    // Hint "scorri su per chiudere" in basso al drawer
    // "swipe up to close" hint at the bottom of the drawer
    lv_obj_t* swipe_hint = lv_label_create(s_drawer);
    lv_label_set_text(swipe_hint, LV_SYMBOL_UP "  scorri su per chiudere");
    lv_obj_set_style_text_font(swipe_hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(swipe_hint, HM_DIM, 0);
    lv_obj_align(swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    // ── Pulsante [X] in alto a destra del drawer ──────────────────────────────
    // Pulsante circolare 48×48px con sfondo grigio chiaro.
    // Circular 48×48px button with light grey background.
    lv_obj_t* close_btn = lv_btn_create(s_drawer);
    lv_obj_set_size(close_btn, 48, 48);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -4, 4);  // 4px dai bordi top e right
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xF0F0F0), 0);             // grigio chiaro
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xE0E0E0), LV_STATE_PRESSED); // grigio scuro al press
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(close_btn, 24, 0);           // raggio = metà altezza → cerchio perfetto
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);      // nessuna ombra sul pulsante
    lv_obj_add_event_cb(close_btn, _close_btn_cb, LV_EVENT_CLICKED, NULL);

    // Icona [X] (LV_SYMBOL_CLOSE) centrata nel pulsante
    // [X] icon (LV_SYMBOL_CLOSE) centered in the button
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, HM_TEXT, 0);
    lv_obj_center(close_lbl);

    return scr;
}
