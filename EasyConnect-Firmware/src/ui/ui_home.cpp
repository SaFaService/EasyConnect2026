/**
 * @file ui_home.cpp
 * @brief Home screen EasyConnect UI Sandbox — 4 tab LVGL
 *        Home screen EasyConnect UI Sandbox — 4 LVGL tabs
 *
 * Questo file implementa la **home screen della versione sandbox** (tema scuro, prefisso `ui_`),
 * distinta dalla home di produzione (`ui_dc_home.cpp`, tema chiaro, prefisso `ui_dc_`).
 * This file implements the **sandbox version home screen** (dark theme, `ui_` prefix),
 * distinct from the production home (`ui_dc_home.cpp`, light theme, `ui_dc_` prefix).
 *
 * Struttura schermata / Screen structure:
 * ```
 * scr (1024×600)
 *   ├── header (1024×52) — brand + sottotitolo + versione FW
 *   ├── tabview (1024×548) — 4 tab
 *   │     ├── tab1 "Controlli" — pulsanti ON/OFF/TOGGLE, slider, switch ×3, dropdown
 *   │     ├── tab2 "Misure"    — arc gauge DeltaP 0-150Pa + line chart storico (60 punti)
 *   │     ├── tab3 "Touch"     — multi-touch tester fino a 5 dita (GT911)
 *   │     └── tab4 "Info"      — info hardware, display, firmware
 *   └── pannello notifiche (inizializzato da ui_notif_panel_init)
 * ```
 *
 * Timer LVGL attivi / Active LVGL timers:
 *   - misure_timer_cb:  400ms — simula DeltaP e aggiorna gauge + chart
 *   - touch_timer_cb:    30ms — legge coordinate touch GT911 e aggiorna cerchi
 *
 * Dipendenze / Dependencies:
 *   - ui_home.h            → dichiarazione di ui_home_create()
 *   - ui_styles.h          → palette, font alias, costanti dimensionali sandbox
 *   - ui_notifications.h   → ui_notif_panel_init() per il pannello notifiche
 *   - touch.h              → esp_lcd_touch_read_data / esp_lcd_touch_get_coordinates (GT911)
 */

#include <initializer_list>
#include "ui_home.h"
#include "ui_styles.h"
#include "ui_notifications.h"
#include "touch.h"   // per esp_lcd_touch_read_data / get_coordinates

// Handle touch globale (definito nell'entrypoint firmware display)
// Global touch handle (defined in the display firmware entry point)
// Dichiarato extern: il linker lo troverà in main_display_controller.cpp (o equivalente)
// Declared extern: the linker will find it in main_display_controller.cpp (or equivalent)
extern esp_lcd_touch_handle_t g_tp_handle;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: crea una card con titolo / Helper: creates a card with a title
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Crea un pannello "card" con bordi arrotondati, sfondo e titolo opzionale
 *        Creates a "card" panel with rounded borders, background and optional title
 *
 * Le card sono i blocchi contenitore della UI sandbox: rettangoli con sfondo
 * leggermente diverso dallo sfondo principale, bordo sottile, angoli arrotondati
 * e un titolo accentato in alto a sinistra.
 * Cards are the container blocks of the sandbox UI: rectangles with a slightly
 * different background than the main background, thin border, rounded corners
 * and an accented title at the top left.
 *
 * @param parent  Oggetto padre LVGL / Parent LVGL object
 * @param title   Stringa del titolo (NULL o "" per nessun titolo) / Title string (NULL or "" for no title)
 * @param x,y     Posizione relativa al parent / Position relative to parent
 * @param w,h     Dimensioni della card / Card dimensions
 * @return Puntatore alla card creata / Pointer to the created card
 */
static lv_obj_t* make_card(lv_obj_t* parent, const char* title,
                            int32_t x, int32_t y, int32_t w, int32_t h) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);

    // Sfondo della card: leggermente più chiaro dello sfondo principale
    // Card background: slightly lighter than the main background
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);

    // Angoli arrotondati e bordo sottile
    // Rounded corners and thin border
    lv_obj_set_style_radius(card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, UI_BORDER_W, 0);

    // Padding interno + spazio extra in alto per il titolo (36px)
    // Internal padding + extra top space for the title (36px)
    lv_obj_set_style_pad_all(card, UI_PADDING, 0);
    lv_obj_set_style_pad_top(card, 36, 0);  // spazio per il titolo / space for title

    // La card non è scrollabile (dimensione fissa)
    // Card is not scrollable (fixed size)
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Aggiunge il titolo se fornito
    // Adds the title if provided
    if (title && title[0]) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);  // Teal accent
        // -20px: posizionato sopra l'area di contenuto (nella zona di padding)
        // -20px: positioned above the content area (in the padding zone)
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, -20);
    }
    return card;
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 – CONTROLLI / CONTROLS TAB
// ─────────────────────────────────────────────────────────────────────────────
// Questo tab dimostra i controlli interattivi LVGL: pulsanti, slider, switch, dropdown.
// This tab demonstrates interactive LVGL controls: buttons, slider, switch, dropdown.

// Stato globale del relay simulato / Simulated relay global state
// NOTA: queste variabili globali statiche sopravvivono per tutta la vita del processo,
// anche se la schermata viene ricreata. Questo è intenzionale per il sandbox.
// NOTE: these static global variables survive for the entire process lifetime,
// even if the screen is recreated. This is intentional for the sandbox.
static lv_obj_t* g_btn_status_label = NULL;  ///< Label che mostra lo stato del relay / Label showing relay state
static bool g_relay_state = false;             ///< Stato corrente del relay (true=ON) / Current relay state (true=ON)

/**
 * @brief Callback pulsante "ACCENDI" / "ON" button callback
 *
 * Attiva il relay simulato (g_relay_state = true) e aggiorna la label di stato
 * con colore verde (UI_COLOR_SUCCESS) e simbolo spunta.
 * Activates the simulated relay (g_relay_state = true) and updates the status label
 * with green color (UI_COLOR_SUCCESS) and checkmark symbol.
 */
static void btn_on_cb(lv_event_t* e) {
    (void)e;  // Evento non usato / Event not used
    g_relay_state = true;
    if (g_btn_status_label)
        lv_label_set_text(g_btn_status_label, LV_SYMBOL_OK "  Relay: ON");
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_SUCCESS, 0);
}

/**
 * @brief Callback pulsante "SPEGNI" / "OFF" button callback
 *
 * Disattiva il relay simulato e aggiorna la label con colore rosso e simbolo X.
 * Deactivates the simulated relay and updates the label with red color and X symbol.
 */
static void btn_off_cb(lv_event_t* e) {
    (void)e;
    g_relay_state = false;
    if (g_btn_status_label)
        lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
}

/**
 * @brief Callback pulsante "TOGGLE" / Toggle button callback
 *
 * Inverte lo stato del relay (ON↔OFF) e aggiorna la label di conseguenza.
 * Inverts the relay state (ON↔OFF) and updates the label accordingly.
 */
static void btn_toggle_cb(lv_event_t* e) {
    (void)e;
    g_relay_state = !g_relay_state;  // Inversione booleana / Boolean inversion
    if (g_btn_status_label) {
        if (g_relay_state) {
            lv_label_set_text(g_btn_status_label, LV_SYMBOL_OK "  Relay: ON");
            lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_SUCCESS, 0);
        } else {
            lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
            lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
        }
    }
}

// Callback slider
// Slider callback
static lv_obj_t* g_slider_label = NULL;  ///< Label che mostra la percentuale del slider / Label showing slider percentage

/**
 * @brief Callback di modifica valore slider / Slider value change callback
 *
 * Chiamata da LVGL ogni volta che l'utente sposta il cursore dello slider.
 * Called by LVGL whenever the user moves the slider knob.
 * Aggiorna la label con il valore corrente in percentuale.
 * Updates the label with the current value as a percentage.
 *
 * @param e  Evento LVGL (contiene il target = lo slider) / LVGL event (contains target = the slider)
 */
static void slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);  // Recupera il widget slider dall'evento
    if (g_slider_label) {
        char buf[32];
        // lv_slider_get_value restituisce il valore corrente nel range impostato
        // lv_slider_get_value returns the current value in the set range
        lv_snprintf(buf, sizeof(buf), "Valore: %d%%", (int)lv_slider_get_value(slider));
        lv_label_set_text(g_slider_label, buf);
    }
}

/**
 * @brief Helper: crea un pulsante stilizzato con testo e colore personalizzati
 *        Helper: creates a styled button with custom text and color
 *
 * Pattern riusato per tutti i pulsanti del tab Controlli.
 * Reused pattern for all buttons in the Controls tab.
 *
 * @param parent  Contenitore padre / Parent container
 * @param text    Testo del pulsante (può contenere simboli LVGL) / Button text (can contain LVGL symbols)
 * @param color   Colore di sfondo del pulsante / Button background color
 * @param cb      Callback da eseguire al click / Callback to execute on click
 * @return Puntatore al pulsante creato / Pointer to the created button
 */
static lv_obj_t* make_styled_btn(lv_obj_t* parent, const char* text,
                                  lv_color_t color, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 120, 44);  // Dimensione fissa 120×44px

    // Colore di sfondo personalizzato (nessuna ombra, nessun bordo)
    // Custom background color (no shadow, no border)
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, UI_RADIUS_BTN, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    // Registra il callback per l'evento LV_EVENT_CLICKED
    // Register callback for LV_EVENT_CLICKED event
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    // Label dentro il pulsante, centrata
    // Label inside button, centered
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_center(lbl);
    return btn;
}

/**
 * @brief Crea il contenuto del tab "Controlli" / Creates the content of the "Controls" tab
 *
 * Layout: 2 righe × 2 colonne di card, ciascuna ~478×218 pixel.
 * Layout: 2 rows × 2 columns of cards, each ~478×218 pixels.
 *
 * ```
 * ┌────────────────────┬────────────────────┐
 * │ Card 1: Pulsanti   │ Card 2: Slider      │
 * │ relay (ON/OFF/TOG) │ 0-100% + labels     │
 * ├────────────────────┼────────────────────┤
 * │ Card 3: Switch ×3  │ Card 4: Dropdown    │
 * │ (Luci/Vent/Relay)  │ (modalità scheda)   │
 * └────────────────────┴────────────────────┘
 * ```
 *
 * @param parent  Il tab LVGL in cui creare il contenuto / The LVGL tab in which to create content
 */
static void tab_controlli_create(lv_obj_t* parent) {
    // Costanti di layout: larghezza/altezza card e gap
    // Layout constants: card width/height and gap
    const int32_t CW = 478, CH = 218, GAP = 10;  // Card Width, Card Height, Gap
    const int32_t X1 = 0, X2 = CW + GAP;          // Colonna 1 e 2
    const int32_t Y1 = 0, Y2 = CH + GAP;           // Riga 1 e 2

    // ── Card 1: Pulsanti relay ──────────────────────────────────────────────
    // Tre pulsanti colorati: verde (ON), rosso (OFF), teal (TOGGLE)
    // Three colored buttons: green (ON), red (OFF), teal (TOGGLE)
    lv_obj_t* c1 = make_card(parent, "PULSANTI  RELAY", X1, Y1, CW, CH);

    // Pulsante ON — colore verde (UI_COLOR_SUCCESS)
    // ON button — green color (UI_COLOR_SUCCESS)
    lv_obj_t* btn_on = make_styled_btn(c1, LV_SYMBOL_OK " ACCENDI",
                                        UI_COLOR_SUCCESS, btn_on_cb);
    lv_obj_set_pos(btn_on, 0, 10);

    // Pulsante OFF — colore rosso (UI_COLOR_ERROR)
    // OFF button — red color (UI_COLOR_ERROR)
    lv_obj_t* btn_off = make_styled_btn(c1, LV_SYMBOL_CLOSE " SPEGNI",
                                         UI_COLOR_ERROR, btn_off_cb);
    lv_obj_set_pos(btn_off, 130, 10);

    // Pulsante TOGGLE — colore teal2 (UI_COLOR_ACCENT2)
    // TOGGLE button — teal2 color (UI_COLOR_ACCENT2)
    lv_obj_t* btn_tog = make_styled_btn(c1, LV_SYMBOL_REFRESH " TOGGLE",
                                         UI_COLOR_ACCENT2, btn_toggle_cb);
    lv_obj_set_pos(btn_tog, 260, 10);

    // Label di stato relay — inizialmente "OFF" in rosso
    // Relay status label — initially "OFF" in red
    g_btn_status_label = lv_label_create(c1);
    lv_label_set_text(g_btn_status_label, LV_SYMBOL_CLOSE "  Relay: OFF");
    lv_obj_set_style_text_font(g_btn_status_label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_btn_status_label, UI_COLOR_ERROR, 0);
    lv_obj_set_pos(g_btn_status_label, 0, 70);

    // ── Card 2: Slider ─────────────────────────────────────────────────────
    // Slider 0-100% con label aggiornata in tempo reale tramite callback
    // 0-100% slider with label updated in real time via callback
    lv_obj_t* c2 = make_card(parent, "SLIDER", X2, Y1, CW, CH);

    // Label che mostra il valore corrente dello slider
    // Label showing the current slider value
    g_slider_label = lv_label_create(c2);
    lv_label_set_text(g_slider_label, "Valore: 50%");
    lv_obj_set_style_text_font(g_slider_label, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_slider_label, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_pos(g_slider_label, 0, 10);

    // Slider LVGL — widget `lv_slider` con knob trascinabile
    // LVGL slider — `lv_slider` widget with draggable knob
    lv_obj_t* slider = lv_slider_create(c2);
    lv_obj_set_width(slider, CW - 2 * UI_PADDING);  // Larghezza = card width - padding
    lv_slider_set_range(slider, 0, 100);              // Range percentuale
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);    // Valore iniziale = 50%
    lv_obj_set_pos(slider, 0, 56);

    // Stile slider: MAIN=sfondo, INDICATOR=parte riempita, KNOB=cursore
    // Slider style: MAIN=background, INDICATOR=filled part, KNOB=cursor
    lv_obj_set_style_bg_color(slider, UI_COLOR_BG_CARD2, LV_PART_MAIN);       // Sfondo della traccia
    lv_obj_set_style_bg_color(slider, UI_COLOR_ACCENT, LV_PART_INDICATOR);    // Parte riempita (teal)
    lv_obj_set_style_bg_color(slider, UI_COLOR_TEXT_PRIMARY, LV_PART_KNOB);   // Cursore bianco
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);                         // Dimensione knob +8px

    // Callback LV_EVENT_VALUE_CHANGED: chiamata ogni volta che il valore cambia
    // LV_EVENT_VALUE_CHANGED callback: called every time the value changes
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Label min/max sotto lo slider per riferimento visivo
    // Min/max labels below slider for visual reference
    lv_obj_t* lbl_min = lv_label_create(c2);
    lv_label_set_text(lbl_min, "0%");
    lv_obj_set_style_text_font(lbl_min, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_min, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_pos(lbl_min, 0, 82);

    lv_obj_t* lbl_max = lv_label_create(c2);
    lv_label_set_text(lbl_max, "100%");
    lv_obj_set_style_text_font(lbl_max, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(lbl_max, UI_COLOR_TEXT_DIM, 0);
    // Allinea al bordo destro dello slider, sotto
    // Aligns to the right edge of the slider, below
    lv_obj_align_to(lbl_max, slider, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 4);

    // ── Card 3: Switch ─────────────────────────────────────────────────────
    // Tre switch on/off per Luci, Ventilazione, Relay Aux
    // Three on/off switches for Lights, Ventilation, Aux Relay
    lv_obj_t* c3 = make_card(parent, "SWITCH", X1, Y2, CW, CH);

    // Nomi dei tre switch — creati in loop per evitare codice ripetuto
    // Names of the three switches — created in loop to avoid repeated code
    const char* sw_labels[] = { "Luci", "Ventilazione", "Relay Aux" };
    for (int i = 0; i < 3; i++) {
        // Switch LVGL — si attiva/disattiva con un click
        // LVGL switch — activates/deactivates with a click
        lv_obj_t* sw = lv_switch_create(c3);
        // i * 50 = spacing verticale tra gli switch (50px ciascuno)
        // i * 50 = vertical spacing between switches (50px each)
        lv_obj_set_pos(sw, 0, i * 50 + 10);

        // Stile switch: MAIN=sfondo spento, INDICATOR=sfondo acceso, KNOB=cerchio
        // Switch style: MAIN=off background, INDICATOR=on background, KNOB=circle
        lv_obj_set_style_bg_color(sw, UI_COLOR_BG_CARD2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sw, UI_COLOR_ACCENT, LV_PART_INDICATOR);     // Teal quando ON
        lv_obj_set_style_bg_color(sw, UI_COLOR_TEXT_PRIMARY, LV_PART_KNOB);

        // Label a destra dello switch (lv_obj_align_to allinea relativamente allo switch)
        // Label to the right of the switch (lv_obj_align_to aligns relative to switch)
        lv_obj_t* sw_lbl = lv_label_create(c3);
        lv_label_set_text(sw_lbl, sw_labels[i]);
        lv_obj_set_style_text_font(sw_lbl, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(sw_lbl, UI_COLOR_TEXT_PRIMARY, 0);
        // LV_ALIGN_OUT_RIGHT_MID: fuori dallo switch, a destra, centrato verticalmente
        // LV_ALIGN_OUT_RIGHT_MID: outside the switch, to the right, vertically centered
        lv_obj_align_to(sw_lbl, sw, LV_ALIGN_OUT_RIGHT_MID, 16, 0);
    }

    // ── Card 4: Dropdown ───────────────────────────────────────────────────
    // Dropdown LVGL per selezionare la modalità della scheda
    // LVGL dropdown for selecting board mode
    lv_obj_t* c4 = make_card(parent, "DROPDOWN", X2, Y2, CW, CH);

    // Label descrittiva sopra il dropdown
    // Descriptive label above the dropdown
    lv_obj_t* lbl_dd = lv_label_create(c4);
    lv_label_set_text(lbl_dd, "Modalita' scheda:");
    lv_obj_set_style_text_font(lbl_dd, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_dd, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_pos(lbl_dd, 0, 10);

    // Dropdown LVGL — opzioni separate da '\n'
    // LVGL dropdown — options separated by '\n'
    lv_obj_t* dd = lv_dropdown_create(c4);
    lv_dropdown_set_options(dd, "Standalone\nRewamping\nDisplay\nRelay UVC\nSensore\nMotore");
    lv_obj_set_width(dd, CW - 2 * UI_PADDING);
    lv_obj_set_pos(dd, 0, 42);
    lv_obj_set_style_bg_color(dd, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_text_color(dd, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_border_color(dd, UI_COLOR_BORDER_ACTIVE, 0);
    lv_obj_set_style_text_font(dd, UI_FONT_LABEL, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 – MISURE / MEASUREMENTS TAB
// Arc gauge + line chart del delta pressione simulato
// Arc gauge + line chart of simulated delta pressure
// ─────────────────────────────────────────────────────────────────────────────

// Handles globali per i widget aggiornati dal timer
// Global handles for widgets updated by the timer
static lv_obj_t* g_gauge_arc      = NULL;  ///< Arco gauge principale / Main gauge arc
static lv_obj_t* g_gauge_val_lbl  = NULL;  ///< Label valore numerico al centro / Numeric value label at center
static lv_obj_t* g_gauge_unit_lbl = NULL;  ///< Label unità di misura "Pa" / Unit label "Pa"
static lv_obj_t* g_chart          = NULL;  ///< Widget chart storico / Historical chart widget
static lv_chart_series_t* g_chart_ser = NULL;  ///< Serie dati del chart / Chart data series
static float    g_deltap_sim      = 0.0f;  ///< Valore DeltaP simulato corrente (0-140 Pa) / Current simulated DeltaP (0-140 Pa)
static float    g_sim_dir         = 1.0f;  ///< Direzione simulazione: +1=salita, -1=discesa / Sim direction: +1=rise, -1=fall

/**
 * @brief Aggiorna il colore dell'indicatore dell'arco in base al valore
 *        Updates the arc indicator color based on the value
 *
 * Soglie di colore / Color thresholds:
 *   - [0, 50)   → verde (SUCCESS)   = pressione normale
 *   - [50, 100) → giallo (WARNING)  = attenzione
 *   - [100, +∞) → rosso (ERROR)     = pressione critica
 *
 * @param arc  L'oggetto arco da aggiornare / The arc object to update
 * @param val  Il valore di DeltaP (0-150 Pa) / The DeltaP value (0-150 Pa)
 */
static void update_gauge_color(lv_obj_t* arc, float val) {
    lv_color_t col;
    if (val < 50.0f)       col = UI_COLOR_SUCCESS;   // Verde: pressione ok
    else if (val < 100.0f) col = UI_COLOR_WARNING;   // Giallo: attenzione
    else                   col = UI_COLOR_ERROR;      // Rosso: critico
    lv_obj_set_style_arc_color(arc, col, LV_PART_INDICATOR);
}

/**
 * @brief Timer callback del tab Misure — eseguito ogni 400ms
 *        Measurements tab timer callback — runs every 400ms
 *
 * Simula un'onda triangolare lenta di DeltaP (0→140→0 Pa, +/−0.8 Pa per tick)
 * che rappresenta la pressione differenziale di un filtro dell'aria.
 * Simulates a slow triangular wave of DeltaP (0→140→0 Pa, +/−0.8 Pa per tick)
 * representing the differential pressure of an air filter.
 *
 * Ad ogni chiamata:
 * On each call:
 *   1. Avanza la simulazione / Advances the simulation
 *   2. Aggiorna l'angolo finale dell'arco (mapping lineare 0-150Pa → 0-260°)
 *   3. Aggiorna il colore dell'arco (verde/giallo/rosso per soglie)
 *   4. Aggiorna la label numerica al centro
 *   5. Aggiunge un punto al chart storico (scorrimento automatico LVGL)
 *
 * @param t  Puntatore al timer (non usato) / Timer pointer (unused)
 */
static void misure_timer_cb(lv_timer_t* t) {
    (void)t;
    if (!g_gauge_arc || !g_chart || !g_chart_ser) return;  // Protezione dangling pointer

    // Simulazione DeltaP: onde triangolari lente 0-140 Pa
    // DeltaP simulation: slow triangular waves 0-140 Pa
    g_deltap_sim += g_sim_dir * 0.8f;  // Avanza di 0.8 Pa per tick (400ms)
    if (g_deltap_sim >= 140.0f) { g_deltap_sim = 140.0f; g_sim_dir = -1.0f; }  // Rimbalza in cima
    if (g_deltap_sim <= 0.0f)   { g_deltap_sim = 0.0f;   g_sim_dir =  1.0f; }  // Rimbalza in fondo

    // Aggiorna arco: mapping lineare 0-150 Pa → 0-260° dell'indicatore
    // Update arc: linear mapping 0-150 Pa → 0-260° of indicator
    // L'arco inizia a 140° e ha 260° di sweep totale (lv_arc_set_bg_angles 140, 400)
    // The arc starts at 140° and has 260° total sweep (lv_arc_set_bg_angles 140, 400)
    int32_t arc_val = (int32_t)((g_deltap_sim / 150.0f) * 260.0f);
    lv_arc_set_end_angle(g_gauge_arc, (int16_t)(140 + arc_val));
    update_gauge_color(g_gauge_arc, g_deltap_sim);  // Aggiorna colore in base al valore

    // Aggiorna label valore numerico (1 decimale)
    // Update numeric value label (1 decimal place)
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%.1f", (double)g_deltap_sim);
    lv_label_set_text(g_gauge_val_lbl, buf);

    // Aggiorna chart (aggiungi punto e shifta)
    // Update chart (add point and shift)
    // lv_chart_set_next_value: aggiunge il punto più a destra e scorre il chart a sinistra
    // lv_chart_set_next_value: adds the rightmost point and scrolls the chart left
    lv_chart_set_next_value(g_chart, g_chart_ser, (lv_coord_t)g_deltap_sim);
}

/**
 * @brief Crea il contenuto del tab "Misure" / Creates the content of the "Measurements" tab
 *
 * Layout: due card affiancate orizzontalmente
 * Layout: two side-by-side cards
 *
 * ```
 * ┌─────────────────────┬───────────────────────────┐
 * │ Gauge (440×448)     │ Chart storico (536×448)    │
 * │ Arc circolare       │ Line chart 60 punti        │
 * │ 0-150 Pa (260°)     │ Y: 0-150 Pa / X: 60 punti │
 * └─────────────────────┴───────────────────────────┘
 * ```
 *
 * @param parent  Il tab LVGL in cui creare il contenuto / The LVGL tab in which to create content
 */
static void tab_misure_create(lv_obj_t* parent) {
    // ── Gauge arc a sinistra ────────────────────────────────────────────────
    // Card contenitore del gauge / Gauge container card
    lv_obj_t* gauge_card = lv_obj_create(parent);
    lv_obj_set_pos(gauge_card, 0, 0);
    lv_obj_set_size(gauge_card, 440, 448);
    lv_obj_set_style_bg_color(gauge_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(gauge_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(gauge_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(gauge_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(gauge_card, UI_PADDING, 0);
    lv_obj_clear_flag(gauge_card, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo della card gauge
    // Gauge card title
    lv_obj_t* lbl_title = lv_label_create(gauge_card);
    lv_label_set_text(lbl_title, "DELTA P  SIMULATO");
    lv_obj_set_style_text_font(lbl_title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_title, UI_COLOR_ACCENT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 0);

    // Arco gauge: bg 140°→400° (260° sweep), indicator parte da 140°
    // Gauge arc: bg 140°→400° (260° sweep), indicator starts at 140°
    // Il sweep di 260° corrisponde visivamente a 3/4 di cerchio (da ore 7 a ore 5)
    // The 260° sweep visually corresponds to 3/4 of a circle (from 7 o'clock to 5 o'clock)
    g_gauge_arc = lv_arc_create(gauge_card);
    lv_obj_set_size(g_gauge_arc, 300, 300);
    lv_obj_align(g_gauge_arc, LV_ALIGN_CENTER, 0, 10);  // +10px verso il basso per centrare visivamente

    // BG angles: definisce l'arco di sfondo fisso (la "traccia")
    // BG angles: defines the fixed background arc (the "track")
    lv_arc_set_bg_angles(g_gauge_arc, 140, 400);         // 140° start, 400° = 360+40 end
    lv_arc_set_start_angle(g_gauge_arc, 140);             // Angolo start indicatore
    lv_arc_set_end_angle(g_gauge_arc, 140);               // Angolo end iniziale = 0 Pa

    // Stile: MAIN=sfondo grigio, INDICATOR=colorato dinamicamente
    // Style: MAIN=grey background, INDICATOR=dynamically colored
    lv_obj_set_style_arc_color(g_gauge_arc, UI_COLOR_BG_CARD2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g_gauge_arc, 22, LV_PART_MAIN);               // 22px di spessore
    lv_obj_set_style_arc_color(g_gauge_arc, UI_COLOR_SUCCESS, LV_PART_INDICATOR);  // Verde iniziale
    lv_obj_set_style_arc_width(g_gauge_arc, 22, LV_PART_INDICATOR);
    lv_obj_remove_style(g_gauge_arc, NULL, LV_PART_KNOB);   // Rimuovi knob — solo decorativo
    lv_obj_set_style_bg_opa(g_gauge_arc, LV_OPA_TRANSP, 0); // Sfondo interno trasparente

    // Valore numerico al centro dell'arco — aggiornato dal timer
    // Numeric value at the center of the arc — updated by timer
    g_gauge_val_lbl = lv_label_create(gauge_card);
    lv_label_set_text(g_gauge_val_lbl, "0.0");
    lv_obj_set_style_text_font(g_gauge_val_lbl, UI_FONT_TITLE, 0);  // Font molto grande
    lv_obj_set_style_text_color(g_gauge_val_lbl, UI_COLOR_TEXT_PRIMARY, 0);
    lv_obj_align(g_gauge_val_lbl, LV_ALIGN_CENTER, 0, 10);

    // Label unità di misura sotto il valore
    // Unit label below the value
    g_gauge_unit_lbl = lv_label_create(gauge_card);
    lv_label_set_text(g_gauge_unit_lbl, "Pa");
    lv_obj_set_style_text_font(g_gauge_unit_lbl, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(g_gauge_unit_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(g_gauge_unit_lbl, LV_ALIGN_CENTER, 0, 46);  // Sotto il valore (+46px)

    // Label soglie (0 / 75 / 150) posizionate attorno all'arco
    // Threshold labels (0 / 75 / 150) positioned around the arc
    const char* thresholds[] = { "0", "75", "150 Pa" };
    int32_t th_x[] = { -130, 0, 106 };   // Offset X: sinistra, centro, destra
    int32_t th_y[] = { 120, 138, 120 };  // Offset Y: ai lati dell'arco
    for (int i = 0; i < 3; i++) {
        lv_obj_t* tl = lv_label_create(gauge_card);
        lv_label_set_text(tl, thresholds[i]);
        lv_obj_set_style_text_font(tl, UI_FONT_TINY, 0);
        lv_obj_set_style_text_color(tl, UI_COLOR_TEXT_DIM, 0);
        lv_obj_align(tl, LV_ALIGN_CENTER, th_x[i], th_y[i]);
    }

    // ── Chart a destra ──────────────────────────────────────────────────────
    // Card contenitore del chart storico / Historical chart container card
    lv_obj_t* chart_card = lv_obj_create(parent);
    lv_obj_set_pos(chart_card, 450, 0);    // 450px a destra del gauge (440 + 10 gap)
    lv_obj_set_size(chart_card, 536, 448);
    lv_obj_set_style_bg_color(chart_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(chart_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(chart_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(chart_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(chart_card, UI_PADDING, 0);
    lv_obj_set_style_pad_top(chart_card, 36, 0);
    lv_obj_clear_flag(chart_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_chart = lv_label_create(chart_card);
    lv_label_set_text(lbl_chart, "STORICO  DELTA P");
    lv_obj_set_style_text_font(lbl_chart, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(lbl_chart, UI_COLOR_ACCENT, 0);
    lv_obj_align(lbl_chart, LV_ALIGN_TOP_LEFT, 0, -20);

    // Chart LVGL — line chart con scorrimento automatico
    // LVGL chart — line chart with automatic scrolling
    g_chart = lv_chart_create(chart_card);
    lv_obj_set_size(g_chart, 504, 360);
    lv_obj_align(g_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);    // Tipo: linea continua
    lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 150);  // Y: 0-150 Pa
    lv_chart_set_point_count(g_chart, 60);              // 60 punti = 24 secondi @ 400ms/punto
    lv_chart_set_div_line_count(g_chart, 5, 6);         // 5 linee orizzontali, 6 verticali

    // Stile chart (sfondo, griglia)
    // Chart style (background, grid)
    lv_obj_set_style_bg_color(g_chart, UI_COLOR_BG_CARD2, 0);
    lv_obj_set_style_bg_opa(g_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_chart, 0, 0);
    lv_obj_set_style_line_color(g_chart, UI_COLOR_BORDER, LV_PART_MAIN);  // Colore linee griglia
    lv_obj_set_style_line_opa(g_chart, LV_OPA_30, LV_PART_MAIN);          // Griglia semi-trasparente

    // Aggiunge la serie dati al chart
    // Adds the data series to the chart
    // LV_CHART_AXIS_PRIMARY_Y = asse Y primario (quello da 0 a 150)
    g_chart_ser = lv_chart_add_series(g_chart, UI_COLOR_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(g_chart, 2, LV_PART_ITEMS);     // Linea dati: 2px
    lv_obj_set_style_size(g_chart, 0, LV_PART_INDICATOR);        // Niente punti, solo linea (size=0)

    // Pre-popola con zeri per avere 60 punti subito visibili
    // Pre-populate with zeros to have 60 visible points immediately
    for (int i = 0; i < 60; i++)
        lv_chart_set_next_value(g_chart, g_chart_ser, 0);

    // Timer aggiornamento misure (ogni 400ms)
    // Measurements update timer (every 400ms)
    // 400ms ≈ 2.5 Hz — abbastanza veloce per sembrare "live", non troppo per il render
    // 400ms ≈ 2.5 Hz — fast enough to appear "live", not too much for the render
    lv_timer_create(misure_timer_cb, 400, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 – TOUCH / MULTI-TOUCH TAB
// Visualizzatore multi-touch fino a 5 dita (GT911)
// Multi-touch visualizer up to 5 fingers (GT911)
// ─────────────────────────────────────────────────────────────────────────────

// Handles globali per il visualizzatore touch
// Global handles for the touch visualizer
static lv_obj_t* g_touch_circles[5]     = { NULL };  ///< 5 cerchi (uno per dito) / 5 circles (one per finger)
static lv_obj_t* g_touch_coord_lbl      = NULL;       ///< Label con le coordinate / Coordinates label
static lv_obj_t* g_touch_count_lbl      = NULL;       ///< Label con il numero di dita / Finger count label
static lv_color_t g_touch_colors[5];                   ///< Colori distinti per i 5 diti / Distinct colors for 5 fingers

/**
 * @brief Timer callback del tab Touch — eseguito ogni 30ms (~33 Hz)
 *        Touch tab timer callback — runs every 30ms (~33 Hz)
 *
 * Legge i dati di touch direttamente dal driver GT911 tramite l'API ESP-IDF LCD Touch,
 * poi aggiorna la posizione e visibilità dei 5 cerchi visualizzatori.
 * Reads touch data directly from the GT911 driver via the ESP-IDF LCD Touch API,
 * then updates the position and visibility of the 5 visualizer circles.
 *
 * Correzione offset / Offset correction:
 * Le coordinate raw del touch (0,0 = angolo in alto a sinistra del display fisico)
 * devono essere corrette per l'offset dell'area del tab dentro la schermata:
 * - Asse X: −40px (metà del raggio del cerchio visualizzatore) per centrare
 * - Asse Y: −102px (header 52px + tabbar 50px) per allineare con il contenuto del tab
 *           poi −40px (raggio cerchio) per centrare
 * Raw touch coordinates (0,0 = top-left of physical display) must be corrected
 * for the tab area offset within the screen:
 * - X axis: −40px (half of visualizer circle radius) to center
 * - Y axis: −102px (header 52px + tabbar 50px) to align with tab content, then −40px to center
 *
 * @param t  Puntatore al timer (non usato) / Timer pointer (unused)
 */
static void touch_timer_cb(lv_timer_t* t) {
    (void)t;
    if (!g_tp_handle) return;  // Handle touch non inizializzato / Touch handle not initialized

    // Buffer per le coordinate di 5 punti di tocco
    // Buffer for 5 touch point coordinates
    uint16_t x[5] = {0}, y[5] = {0};
    uint8_t  cnt = 0;

    // Step 1: Aggiorna i dati touch dall'hardware (polling I2C GT911)
    // Step 1: Update touch data from hardware (GT911 I2C polling)
    esp_lcd_touch_read_data(g_tp_handle);

    // Step 2: Leggi le coordinate (fino a 5 punti) nel buffer
    // Step 2: Read coordinates (up to 5 points) into buffer
    esp_lcd_touch_get_coordinates(g_tp_handle, x, y, NULL, &cnt, 5);

    // Aggiorna label contatore dei punti attivi
    // Update active points counter label
    char buf[40];
    lv_snprintf(buf, sizeof(buf), LV_SYMBOL_EYE_OPEN "  Punti attivi: %d / 5", (int)cnt);
    lv_label_set_text(g_touch_count_lbl, buf);

    // Aggiorna cerchi: mostra quelli attivi, nasconde quelli non usati
    // Update circles: show active ones, hide unused ones
    char coords[128] = "";
    for (int i = 0; i < 5; i++) {
        if (i < cnt) {
            // Dito i-esimo attivo: posiziona il cerchio nelle coordinate toccate
            // i-th finger active: position the circle at the touched coordinates
            // Correggi l'offset dei tab (header 52 + tabbar 50 = 102px)
            // Correct tab offset (header 52 + tabbar 50 = 102px)
            int32_t cx = (int32_t)x[i] - 40;        // -40 per centrare il cerchio sul punto
            int32_t cy = (int32_t)y[i] - 102 - 40;  // -102 offset tab, -40 per centrare
            if (cx < 0) cx = 0;  // Clamp per non uscire dall'area
            if (cy < 0) cy = 0;
            lv_obj_set_pos(g_touch_circles[i], cx, cy);
            lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_COVER, 0);  // Rendi visibile

            // Aggiunge la coordinata alla stringa di debug
            // Adds the coordinate to the debug string
            char tmp[24];
            lv_snprintf(tmp, sizeof(tmp), " P%d(%d,%d)", i + 1, (int)x[i], (int)y[i]);
            strncat(coords, tmp, sizeof(coords) - strlen(coords) - 1);
        } else {
            // Dito non attivo: rendi invisibile il cerchio
            // Inactive finger: make the circle invisible
            lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_TRANSP, 0);
        }
    }
    // Se nessun tocco, mostra messaggio di istruzione
    // If no touch, show instruction message
    if (cnt == 0) lv_snprintf(coords, sizeof(coords), "Tocca il display...");
    lv_label_set_text(g_touch_coord_lbl, coords);
}

/**
 * @brief Crea il contenuto del tab "Touch" / Creates the content of the "Touch" tab
 *
 * Crea:
 * - Un'area scura come "canvas" per i tocchi
 * - Un label di istruzioni al centro
 * - Un pannello info in alto a sinistra (contatore + coordinate)
 * - 5 cerchi colorati (uno per dito), inizialmente invisibili
 * - Un timer LVGL a 30ms per leggere il GT911 e aggiornare i cerchi
 *
 * Creates:
 * - A dark area as "canvas" for touches
 * - An instruction label at the center
 * - An info panel at the top left (counter + coordinates)
 * - 5 colored circles (one per finger), initially invisible
 * - A 30ms LVGL timer to read GT911 and update circles
 *
 * @param parent  Il tab LVGL in cui creare il contenuto / The LVGL tab in which to create content
 */
static void tab_touch_create(lv_obj_t* parent) {
    // Colori per i 5 diti (definiti in ui_styles.h)
    // Colors for the 5 fingers (defined in ui_styles.h)
    g_touch_colors[0] = UI_COLOR_TOUCH_0;
    g_touch_colors[1] = UI_COLOR_TOUCH_1;
    g_touch_colors[2] = UI_COLOR_TOUCH_2;
    g_touch_colors[3] = UI_COLOR_TOUCH_3;
    g_touch_colors[4] = UI_COLOR_TOUCH_4;

    // Sfondo scuro per l'area touch — aumenta il contrasto dei cerchi colorati
    // Dark background for the touch area — increases contrast of colored circles
    lv_obj_set_style_bg_color(parent, UI_COLOR_BG_DEEP, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);  // L'area touch non scorre

    // Label istruzioni (al centro, inizialmente visibile)
    // Instruction label (at center, initially visible)
    // Rimane visibile anche quando ci sono i cerchi (è sotto di essi in z-order)
    // Remains visible even when circles are shown (it's below them in z-order)
    lv_obj_t* hint = lv_label_create(parent);
    lv_label_set_text(hint,
        LV_SYMBOL_PREV " TOCCA CON PIU' DITA " LV_SYMBOL_NEXT "\n"
        "Supporta fino a 5 tocchi simultanei (GT911)");
    lv_obj_set_style_text_font(hint, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(hint, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);

    // Panel info in alto a sinistra — semitrasparente per non coprire l'area touch
    // Info panel at the top left — semi-transparent to not cover the touch area
    lv_obj_t* info_card = lv_obj_create(parent);
    lv_obj_set_size(info_card, 360, 80);
    lv_obj_set_pos(info_card, 0, 0);
    lv_obj_set_style_bg_color(info_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(info_card, LV_OPA_80, 0);  // 80% opaco (leggermente trasparente)
    lv_obj_set_style_radius(info_card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(info_card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(info_card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(info_card, 10, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    // Label contatore punti attivi (es: "● Punti attivi: 3 / 5")
    // Active points counter label (e.g., "● Punti attivi: 3 / 5")
    g_touch_count_lbl = lv_label_create(info_card);
    lv_label_set_text(g_touch_count_lbl, LV_SYMBOL_EYE_OPEN "  Punti attivi: 0 / 5");
    lv_obj_set_style_text_font(g_touch_count_lbl, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(g_touch_count_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_align(g_touch_count_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    // Label coordinate (es: " P1(512,300) P2(100,200)")
    // Coordinates label (e.g., " P1(512,300) P2(100,200)")
    g_touch_coord_lbl = lv_label_create(info_card);
    lv_label_set_text(g_touch_coord_lbl, "Tocca il display...");
    lv_obj_set_style_text_font(g_touch_coord_lbl, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g_touch_coord_lbl, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(g_touch_coord_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Crea 5 cerchi (uno per dito), inizialmente nascosti
    // Create 5 circles (one per finger), initially hidden
    for (int i = 0; i < 5; i++) {
        g_touch_circles[i] = lv_obj_create(parent);
        lv_obj_set_size(g_touch_circles[i], 80, 80);  // Ø 80px (raggio = 40px)

        // Cerchio: border_radius = metà della dimensione → forma circolare
        // Circle: border_radius = half of size → circular shape
        lv_obj_set_style_radius(g_touch_circles[i], UI_RADIUS_CIRCLE, 0);

        // Colore semi-trasparente specifico per ogni dito
        // Semi-transparent color specific to each finger
        lv_obj_set_style_bg_color(g_touch_circles[i], g_touch_colors[i], 0);
        lv_obj_set_style_bg_opa(g_touch_circles[i], LV_OPA_60, 0);  // 60% opaco

        // Bordo bianco semi-trasparente per visibilità su qualsiasi sfondo
        // Semi-transparent white border for visibility on any background
        lv_obj_set_style_border_color(g_touch_circles[i], UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_style_border_width(g_touch_circles[i], 3, 0);
        lv_obj_set_style_border_opa(g_touch_circles[i], LV_OPA_80, 0);

        // Inizia completamente invisibile — il timer li renderà visibili
        // Starts completely invisible — the timer will make them visible
        lv_obj_set_style_opa(g_touch_circles[i], LV_OPA_TRANSP, 0);

        // I cerchi non devono catturare eventi touch (passthrough)
        // Circles must not capture touch events (passthrough)
        lv_obj_clear_flag(g_touch_circles[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(g_touch_circles[i], LV_OBJ_FLAG_SCROLLABLE);

        // Numero del dito al centro del cerchio (1-5)
        // Finger number at the center of the circle (1-5)
        char num[4];
        lv_snprintf(num, sizeof(num), "%d", i + 1);
        lv_obj_t* nl = lv_label_create(g_touch_circles[i]);
        lv_label_set_text(nl, num);
        lv_obj_set_style_text_font(nl, UI_FONT_SUBTITLE, 0);
        lv_obj_set_style_text_color(nl, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_center(nl);
    }

    // Timer lettura touch (30ms = ~33Hz)
    // Touch reading timer (30ms = ~33Hz)
    // 33Hz per il touch è un buon compromesso: fluido per l'occhio, leggero per la CPU
    // 33Hz for touch is a good compromise: smooth to the eye, light on the CPU
    lv_timer_create(touch_timer_cb, 30, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 – INFO / INFO TAB
// Informazioni hardware, display, firmware — statico, nessun aggiornamento
// Hardware, display, firmware information — static, no updates
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Crea il contenuto del tab "Info" / Creates the content of the "Info" tab
 *
 * Mostra una card con una tabella a 2 colonne (chiave + valore) delle informazioni
 * di sistema: display, risoluzione, MCU, memoria, LVGL, firmware, progetto.
 * Shows a card with a 2-column table (key + value) of system information:
 * display, resolution, MCU, memory, LVGL, firmware, project.
 *
 * @param parent  Il tab LVGL in cui creare il contenuto / The LVGL tab in which to create content
 */
static void tab_info_create(lv_obj_t* parent) {
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);  // Tab non scrollabile

    // Card contenitore per le info — occupa quasi tutto il tab
    // Container card for info — occupies almost all of the tab
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_pos(card, 0, 0);
    lv_obj_set_size(card, 980, 430);
    lv_obj_set_style_bg_color(card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(card, UI_BORDER_W, 0);
    lv_obj_set_style_pad_all(card, UI_PADDING * 2, 0);  // Padding doppio per più spazio
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo della card
    // Card title
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, "INFORMAZIONI  SISTEMA");
    lv_obj_set_style_text_font(title, UI_FONT_SUBTITLE, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Separatore orizzontale sotto il titolo
    // Horizontal separator below the title
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_size(sep, 940, 2);
    lv_obj_set_style_bg_color(sep, UI_COLOR_BORDER, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 36);

    // Righe info: coppia chiave-valore
    // Info rows: key-value pairs
    // Struttura locale per chiarezza / Local structure for clarity
    struct InfoRow { const char* key; const char* val; };

    // Tabella delle informazioni di sistema
    // System information table
    InfoRow rows[] = {
        { "Display",      "Waveshare ESP32-S3-Touch-LCD-7B"       },
        { "Risoluzione",  "1024 x 600 pixel  (WSVGA)"             },
        { "Interfaccia",  "RGB565 parallelo 16-bit, 30 MHz"        },
        { "Touch",        "GT911 capacitivo, max 5 punti"          },
        { "MCU",          "ESP32-S3, dual-core 240 MHz"            },
        { "Memoria",      "16 MB Flash QIO  +  OPI PSRAM"          },
        { "LVGL",         "v8.4.0  (doppio buffer, anti-tearing)"  },
        { "Firmware",     "UI Sandbox v" UI_SANDBOX_VERSION        },  // Versione da macro
        { "Build",        UI_SANDBOX_BUILD                         },  // Data build da macro
        { "Progetto",     "EasyConnect 2026  -  Antralux"          },
    };
    const int n = sizeof(rows) / sizeof(rows[0]);  // Numero di righe

    // Crea le righe in loop: label chiave (grigio) + label valore (bianco)
    // Create rows in loop: key label (grey) + value label (white)
    for (int i = 0; i < n; i++) {
        // Y incrementale: 50px iniziali + 36px per ogni riga
        // Incremental Y: 50px initial + 36px per row
        int32_t y_pos = 50 + i * 36;

        // Chiave (colore secondario = grigio)
        // Key (secondary color = grey)
        lv_obj_t* lk = lv_label_create(card);
        lv_label_set_text(lk, rows[i].key);
        lv_obj_set_style_text_font(lk, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lk, UI_COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_pos(lk, 0, y_pos);

        // Valore (colore primario = bianco)
        // Value (primary color = white)
        // Nota: il nome `lv_` è usato per evitare conflitto con la funzione `lv_` di LVGL
        // Note: name `lv_` is used to avoid conflict with LVGL's `lv_` function
        lv_obj_t* lv_ = lv_label_create(card);
        lv_label_set_text(lv_, rows[i].val);
        lv_obj_set_style_text_font(lv_, UI_FONT_LABEL, 0);
        lv_obj_set_style_text_color(lv_, UI_COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_pos(lv_, 220, y_pos);  // 220px a destra per la colonna valori
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// HOME SCREEN PRINCIPALE / MAIN HOME SCREEN
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Crea e restituisce la schermata home sandbox
 *        Creates and returns the sandbox home screen
 *
 * Struttura gerarchica degli oggetti / Object hierarchy structure:
 * ```
 * scr (1024×600, sfondo UI_COLOR_BG_MAIN)
 *   ├── header (1024×UI_HEADER_H)
 *   │     ├── h_brand  ("ANTRALUX", sinistra, teal)
 *   │     ├── h_sub    (sottotitolo, centro, grigio)
 *   │     └── h_ver    (versione FW, destra, dim)
 *   ├── tabview (1024×(600-header), da UI_HEADER_H in giù)
 *   │     ├── tab_bar (pulsanti tab in alto, sfondo header)
 *   │     │     ├── tab attivo → sfondo teal, testo scuro
 *   │     │     └── tab inattivo → testo grigio
 *   │     ├── tab1 → tab_controlli_create()
 *   │     ├── tab2 → tab_misure_create()
 *   │     ├── tab3 → tab_touch_create()
 *   │     └── tab4 → tab_info_create()
 *   └── pannello notifiche → ui_notif_panel_init(scr, header)
 * ```
 *
 * Questa funzione NON chiama lv_scr_load() — lo fa il chiamante (es: on_splash_complete).
 * This function does NOT call lv_scr_load() — the caller does (e.g., on_splash_complete).
 *
 * @return Puntatore alla schermata creata / Pointer to the created screen
 */
lv_obj_t* ui_home_create(void) {
    // ── Schermata root ─────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);  // parent=NULL = schermata LVGL
    lv_obj_set_style_bg_color(scr, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    // ── HEADER ──────────────────────────────────────────────────────────────
    // Barra superiore fissa con brand + navigazione
    // Fixed top bar with brand + navigation
    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, UI_SCREEN_W, UI_HEADER_H);  // 1024 × altezza header (da ui_styles.h)
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_HEADER, 0);   // Colore sfondo header
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);

    // Rimuove bordo predefinito, aggiunge solo bordo inferiore sottile
    // Removes default border, adds only a thin bottom border
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, UI_COLOR_BORDER, 0);
    lv_obj_set_style_border_width(header, 1, 0);  // 1px = bordo sottile

    lv_obj_set_style_pad_hor(header, UI_PADDING, 0);  // Padding orizzontale
    lv_obj_set_style_pad_ver(header, 0, 0);            // Nessun padding verticale
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Logo testo header (sinistra) — brand name in teal
    // Header text logo (left) — brand name in teal
    lv_obj_t* h_brand = lv_label_create(header);
    lv_label_set_text(h_brand, "ANTRALUX");
    lv_obj_set_style_text_font(h_brand, UI_FONT_BODY, 0);
    lv_obj_set_style_text_color(h_brand, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_letter_space(h_brand, 3, 0);  // Spaziatura lettere per effetto premium
    lv_obj_align(h_brand, LV_ALIGN_LEFT_MID, 0, 0);

    // Sottotitolo header (centro) — modalità corrente
    // Header subtitle (center) — current mode
    lv_obj_t* h_sub = lv_label_create(header);
    lv_label_set_text(h_sub, "EasyConnect Display  |  UI Sandbox");
    lv_obj_set_style_text_font(h_sub, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(h_sub, UI_COLOR_TEXT_SECONDARY, 0);
    lv_obj_align(h_sub, LV_ALIGN_CENTER, 0, 0);

    // Versione firmware (destra) — riferimento rapido per lo sviluppatore
    // Firmware version (right) — quick reference for the developer
    lv_obj_t* h_ver = lv_label_create(header);
    lv_label_set_text(h_ver, "FW " UI_SANDBOX_VERSION);
    lv_obj_set_style_text_font(h_ver, UI_FONT_SMALL, 0);
    lv_obj_set_style_text_color(h_ver, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(h_ver, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── TABVIEW ─────────────────────────────────────────────────────────────
    // `lv_tabview`: widget LVGL che gestisce tab multipli con swipe orizzontale.
    // `lv_tabview`: LVGL widget that manages multiple tabs with horizontal swipe.
    // LV_DIR_TOP: la tab bar è in alto (il contenuto è sotto la tab bar)
    // LV_DIR_TOP: tab bar is at the top (content is below the tab bar)
    // UI_TAB_BAR_H: altezza della tab bar (definita in ui_styles.h)
    lv_obj_t* tabview = lv_tabview_create(scr, LV_DIR_TOP, UI_TAB_BAR_H);
    lv_obj_set_pos(tabview, 0, UI_HEADER_H);  // Sotto l'header
    // Altezza = totale schermo − altezza header
    // Height = total screen height − header height
    lv_obj_set_size(tabview, UI_SCREEN_W, UI_SCREEN_H - UI_HEADER_H);
    lv_obj_set_style_bg_color(tabview, UI_COLOR_BG_MAIN, 0);

    // ── Stile tab bar (il contenitore dei bottoni tab) ─────────────────────
    // La tab bar è accessibile tramite lv_tabview_get_tab_btns()
    // The tab bar is accessible via lv_tabview_get_tab_btns()
    lv_obj_t* tab_bar = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_bar, UI_COLOR_HEADER, 0);             // Sfondo = sfondo header
    lv_obj_set_style_text_color(tab_bar, UI_COLOR_TEXT_SECONDARY, 0);   // Testo tab inattivi
    lv_obj_set_style_text_font(tab_bar, UI_FONT_LABEL, 0);

    // Stile tab ATTIVO (LV_STATE_CHECKED = tab selezionato)
    // ACTIVE tab style (LV_STATE_CHECKED = selected tab)
    // LV_PART_ITEMS = si applica ai singoli pulsanti tab (non al contenitore)
    // LV_PART_ITEMS = applies to individual tab buttons (not the container)
    lv_obj_set_style_bg_color(tab_bar, UI_COLOR_ACCENT,
                              LV_PART_ITEMS | LV_STATE_CHECKED);           // Sfondo teal per tab attivo
    lv_obj_set_style_text_color(tab_bar, UI_COLOR_BG_DEEP,
                                LV_PART_ITEMS | LV_STATE_CHECKED);         // Testo scuro su teal
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM,
                                 LV_PART_ITEMS | LV_STATE_CHECKED);        // Solo bordo inferiore
    lv_obj_set_style_border_color(tab_bar, UI_COLOR_ACCENT,
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_bar, 3,
                                  LV_PART_ITEMS | LV_STATE_CHECKED);       // 3px = sottolineatura visibile

    // ── Crea i 4 tab ────────────────────────────────────────────────────────
    // lv_tabview_add_tab: crea un pannello figlio con la label specificata
    // lv_tabview_add_tab: creates a child panel with the specified label
    // LV_SYMBOL_*: simboli Unicode nella font Montserrat (caratteri speciali LVGL)
    // LV_SYMBOL_*: Unicode symbols in Montserrat font (special LVGL characters)
    lv_obj_t* tab1 = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "  Controlli");
    lv_obj_t* tab2 = lv_tabview_add_tab(tabview, LV_SYMBOL_CHARGE   "  Misure");
    lv_obj_t* tab3 = lv_tabview_add_tab(tabview, LV_SYMBOL_EDIT     "  Touch");
    lv_obj_t* tab4 = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST     "  Info");

    // ── Stile contenuto tab ─────────────────────────────────────────────────
    // lv_tabview_get_content: restituisce il contenitore che tiene i pannelli tab
    // lv_tabview_get_content: returns the container holding the tab panels
    lv_obj_t* tab_content = lv_tabview_get_content(tabview);
    lv_obj_set_style_bg_color(tab_content, UI_COLOR_BG_MAIN, 0);
    lv_obj_set_style_pad_all(tab_content, 0, 0);  // Nessun padding sul contenitore principale

    // Applica stile uniforme a tutti e 4 i tab
    // Applies uniform style to all 4 tabs
    // Usa initializer_list per iterare — sintassi C++11
    // Uses initializer_list for iteration — C++11 syntax
    for (lv_obj_t* t : { tab1, tab2, tab3, tab4 }) {
        lv_obj_set_style_bg_color(t, UI_COLOR_BG_MAIN, 0);
        lv_obj_set_style_pad_all(t, UI_PADDING, 0);
        lv_obj_set_style_pad_top(t, UI_PADDING, 0);
    }

    // ── Popola i tab ────────────────────────────────────────────────────────
    // Ogni funzione create() riceve il pannello del tab e lo popola con widget
    // Each create() function receives the tab panel and populates it with widgets
    tab_controlli_create(tab1);   // Pulsanti + slider + switch + dropdown
    tab_misure_create(tab2);      // Gauge arco + chart storico
    tab_touch_create(tab3);       // Visualizzatore multi-touch GT911
    tab_info_create(tab4);        // Tabella info hardware/firmware

    // ── Inizializza il pannello notifiche ────────────────────────────────────
    // ui_notif_panel_init: crea il pannello notifiche "pull-down" (tendina dall'alto)
    // ui_notif_panel_init: creates the "pull-down" notifications panel (top drawer)
    // Richiede sia la schermata sia l'header come parametri per il z-order corretto
    // Requires both screen and header as parameters for correct z-order
    ui_notif_panel_init(scr, header);

    return scr;
}
