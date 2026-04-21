#include "ui_notifications.h"
#include "ui_styles.h"       // UI_COLOR_*, UI_FONT_*, UI_SCREEN_W/H, UI_HEADER_H
#include "ui_dc_clock.h"     // ui_dc_clock_get_local_tm() per il timestamp

#include <string.h>

/**
 * @file ui_notifications.cpp
 * @brief Sistema notifiche UI — lista persistente con pannello a tendina.
 *        UI notification system — persistent list with drop-down panel.
 *
 * ARCHITETTURA / ARCHITECTURE
 * ───────────────────────────
 * Il modulo usa un buffer statico di 24 slot (s_entries[]). Ogni slot contiene
 * i dati di una notifica (key, severity, title, body, timestamp, numero di
 * sequenza). Il pannello LVGL viene ricostruito completamente ogni volta che
 * la lista cambia (_notif_rebuild_list()).
 *
 * The module uses a static buffer of 24 slots (s_entries[]). Each slot contains
 * the data for a notification (key, severity, title, body, timestamp, sequence
 * number). The LVGL panel is fully rebuilt every time the list changes
 * (_notif_rebuild_list()).
 *
 * ORDINAMENTO / SORTING
 * ─────────────────────
 * Le notifiche sono ordinate per gravità decrescente (ALERT > INFO > NONE),
 * poi per numero di sequenza decrescente (LIFO — la più recente prima).
 * L'ordinamento è implementato con insertion sort in _notif_rebuild_list().
 *
 * Notifications are sorted by decreasing severity (ALERT > INFO > NONE),
 * then by decreasing sequence number (LIFO — most recent first).
 * Sorting is implemented with insertion sort in _notif_rebuild_list().
 *
 * CICLO DI VITA OGGETTI LVGL / LVGL OBJECT LIFECYCLE
 * ────────────────────────────────────────────────────
 * Il backdrop e il panel sono allegati alla schermata home (s_scr).
 * Quando home viene eliminata (scr_delete_cb), i puntatori vengono azzerati.
 * Alla prossima chiamata di ui_notif_panel_init() tutto viene ricreato.
 *
 * The backdrop and panel are attached to the home screen (s_scr).
 * When home is deleted (scr_delete_cb), pointers are reset.
 * On the next call to ui_notif_panel_init() everything is recreated.
 */

// ─── Costanti ────────────────────────────────────────────────────────────────

/** Numero massimo di notifiche simultanee nel buffer. / Max simultaneous notifications. */
static constexpr int k_notif_max = 24;

/** Altezza totale del pannello notifiche in pixel (header + lista). / Panel total height. */
static constexpr int k_panel_h = 320;

/** Altezza dell'header interno al pannello (titolo + pulsanti). / Panel internal header height. */
static constexpr int k_header_h = 52;

/** Altezza di ogni item notifica nella lista. / Height of each notification item. */
static constexpr int k_item_h = 74;

#define NOTIF_BACKDROP_BG       lv_color_hex(0xDDE7F2)
#define NOTIF_PANEL_BG          lv_color_hex(0xF4F7FB)
#define NOTIF_HEADER_BG         lv_color_hex(0xE8EEF6)
#define NOTIF_ITEM_BG           lv_color_hex(0xFFFFFF)
#define NOTIF_ITEM_PRESSED_BG   lv_color_hex(0xEAF0F7)
#define NOTIF_BUTTON_BG         lv_color_hex(0xF8FAFD)
#define NOTIF_BUTTON_PRESSED_BG lv_color_hex(0xE1E9F2)
#define NOTIF_BORDER            lv_color_hex(0xD3DEEA)
#define NOTIF_TEXT              lv_color_hex(0x243447)
#define NOTIF_TEXT_MUTED        lv_color_hex(0x6D8198)

// ─── Struttura dati notifica ──────────────────────────────────────────────────

/**
 * @brief Rappresenta una singola notifica nel buffer statico.
 *        Represents a single notification in the static buffer.
 *
 * Tutti i campi stringa sono buffer fissi per evitare allocazioni dinamiche.
 * All string fields are fixed buffers to avoid dynamic allocations.
 */
struct UiNotifEntry {
    bool active;             ///< true = slot in uso. true = slot in use.
    UiNotifSeverity severity;///< Livello di gravità. Severity level.
    uint32_t seq;            ///< Numero di sequenza crescente (usato per LIFO sort). Growing sequence (LIFO sort).
    char key[40];            ///< Chiave univoca. Unique key.
    char title[64];          ///< Titolo breve. Short title.
    char body[160];          ///< Testo descrittivo. Descriptive text.
    char ts[16];             ///< Timestamp formattato "HH:MM". Formatted timestamp "HH:MM".
};

// ─── Buffer statico e contatori ──────────────────────────────────────────────

/** Buffer statico di 24 slot notifiche (zero-inizializzato). / Static 24-slot buffer (zero-initialized). */
static UiNotifEntry s_entries[k_notif_max];

/** Contatore sequenza monotonica crescente. / Monotonically increasing sequence counter. */
static uint32_t s_seq_counter = 0;

// ─── Stato LVGL (puntatori agli oggetti) ─────────────────────────────────────
// Tutti i puntatori vengono azzerati quando la schermata home viene eliminata.
// All pointers are cleared when the home screen is deleted.

static lv_obj_t* s_scr      = nullptr; ///< Schermata home a cui il pannello è allegato. Home screen the panel is attached to.
static lv_obj_t* s_backdrop = nullptr; ///< Overlay scuro semi-trasparente. Dark semi-transparent overlay.
static lv_obj_t* s_panel    = nullptr; ///< Pannello notifiche (header + lista). Notifications panel (header + list).
static lv_obj_t* s_list     = nullptr; ///< Container scrollabile della lista. Scrollable list container.
static bool      s_open     = false;   ///< true = pannello visibile. true = panel visible.

// ─── Helper: colore e icona per severità ─────────────────────────────────────

/**
 * @brief Restituisce il colore LVGL associato a una severità.
 *        Returns the LVGL color associated with a severity.
 *
 * Usato per la barra colorata sinistra di ogni item e per il testo dell'icona.
 * Used for the colored left bar of each item and for the icon text.
 *
 * @param severity  Livello di gravità. Severity level.
 * @return Colore LVGL. LVGL color.
 */
static lv_color_t _notif_color(UiNotifSeverity severity) {
    switch (severity) {
        case UI_NOTIF_ALERT: return UI_COLOR_ERROR;    // rosso per errori critici
        case UI_NOTIF_INFO:  return UI_COLOR_WARNING;  // giallo per informazioni
        default:             return UI_COLOR_SUCCESS;  // verde per notifiche di ok/successo
    }
}

/**
 * @brief Restituisce il simbolo LVGL associato a una severità.
 *        Returns the LVGL symbol associated with a severity.
 *
 * @param severity  Livello di gravità. Severity level.
 * @return Stringa simbolo LVGL. LVGL symbol string.
 */
static const char* _notif_icon(UiNotifSeverity severity) {
    switch (severity) {
        case UI_NOTIF_ALERT: return LV_SYMBOL_WARNING;  // triangolo attenzione
        case UI_NOTIF_INFO:  return LV_SYMBOL_BELL;     // campanella
        default:             return LV_SYMBOL_OK;       // spunta verde
    }
}

// ─── Helper: timestamp ───────────────────────────────────────────────────────

/**
 * @brief Genera il timestamp corrente nel formato "HH:MM".
 *        Generates the current timestamp in "HH:MM" format.
 *
 * Usa ui_dc_clock_get_local_tm() che legge il contatore software dell'orologio
 * condiviso. Se l'orologio non è disponibile, scrive "--:--".
 *
 * Uses ui_dc_clock_get_local_tm() which reads the shared software clock counter.
 * If the clock is not available, writes "--:--".
 *
 * @param out       Buffer di destinazione. Destination buffer.
 * @param out_size  Dimensione del buffer (almeno 6). Buffer size (at least 6).
 */
static void _notif_make_timestamp(char* out, size_t out_size) {
    if (!out || out_size == 0) return;

    struct tm tm_local = {};
    if (ui_dc_clock_get_local_tm(&tm_local)) {
        // Formato "HH:MM" usando lv_snprintf (sicuro per buffer LVGL)
        // "HH:MM" format using lv_snprintf (safe for LVGL buffers)
        lv_snprintf(out, (uint32_t)out_size, "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
    } else {
        // Fallback se l'orologio non è disponibile
        // Fallback if clock is not available
        lv_snprintf(out, (uint32_t)out_size, "--:--");
    }
}

// ─── Gestione slot ───────────────────────────────────────────────────────────

/**
 * @brief Cerca uno slot attivo con la chiave data.
 *        Searches for an active slot with the given key.
 *
 * Scansione lineare su tutti gli slot (k_notif_max = 24, overhead trascurabile).
 * Linear scan over all slots (k_notif_max = 24, negligible overhead).
 *
 * @param key  Chiave da cercare. Key to search.
 * @return Indice dello slot se trovato, -1 altrimenti. Slot index if found, -1 otherwise.
 */
static int _notif_find_slot(const char* key) {
    if (!key || !key[0]) return -1;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        if (strncmp(s_entries[i].key, key, sizeof(s_entries[i].key)) == 0) return i;
    }
    return -1;
}

/**
 * @brief Trova uno slot libero, o il più vecchio se tutti sono occupati.
 *        Finds a free slot, or the oldest one if all are occupied.
 *
 * Politica di allocazione (in ordine):
 * Allocation policy (in order):
 *   1. Cerca il primo slot con active=false → slot libero
 *   1. Find first slot with active=false → free slot
 *   2. Se non trovato → trova lo slot con il numero di sequenza più basso
 *      (la notifica più vecchia) e lo sovrascrive
 *   2. If not found → find slot with lowest sequence number
 *      (the oldest notification) and overwrite it
 *
 * @return Indice dello slot da usare (0..k_notif_max-1). Slot index to use.
 */
static int _notif_allocate_slot() {
    // Cerca slot libero
    // Look for free slot
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) return i;
    }

    // Tutti occupati: sovrascrive il più vecchio (seq minimo)
    // All occupied: overwrite the oldest (minimum seq)
    int oldest = 0;
    for (int i = 1; i < k_notif_max; i++) {
        if (s_entries[i].seq < s_entries[oldest].seq) oldest = i;
    }
    return oldest;
}

/**
 * @brief Determina se la notifica 'a' deve venire prima di 'b' nella lista ordinata.
 *        Determines if notification 'a' should come before 'b' in the sorted list.
 *
 * Criteri di ordinamento / Sorting criteria:
 *   1. Gravità decrescente: ALERT (2) > INFO (1) > NONE (0)
 *      Decreasing severity: ALERT (2) > INFO (1) > NONE (0)
 *   2. A parità di gravità, sequenza decrescente (LIFO: più recente prima)
 *      Same severity: decreasing sequence (LIFO: most recent first)
 *
 * @param a  Prima notifica. First notification.
 * @param b  Seconda notifica. Second notification.
 * @return true se 'a' deve stare prima di 'b'. true if 'a' should come before 'b'.
 */
static bool _notif_sort_before(const UiNotifEntry& a, const UiNotifEntry& b) {
    // Prima criterio: gravità più alta prima
    // First criterion: higher severity first
    if (a.severity != b.severity) return a.severity > b.severity;
    // Secondo criterio: sequenza più alta prima (più recente)
    // Second criterion: higher sequence first (most recent)
    return a.seq > b.seq;
}

// Dichiarazione forward (necessaria perché usata prima della definizione)
// Forward declaration (needed because used before definition)
static void _notif_rebuild_list();

// ─── Controllo visibilità pannello ───────────────────────────────────────────

/**
 * @brief Imposta la visibilità del pannello notifiche.
 *        Sets the visibility of the notification panel.
 *
 * Quando aperto, porta il panel in primo piano (lv_obj_move_foreground)
 * in modo da stare sopra qualsiasi altro elemento della home.
 * When opened, brings the panel to foreground (lv_obj_move_foreground)
 * so it stays above any other home element.
 *
 * @param open  true = mostra pannello, false = nasconde. true = show panel, false = hide.
 */
static void _notif_panel_set_open(bool open) {
    if (!s_panel || !s_backdrop) return;
    s_open = open;
    if (open) {
        // Rimuovi flag HIDDEN da entrambi e porta in primo piano
        // Remove HIDDEN flag from both and bring to foreground
        lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_panel);  // assicura che sia sopra tutto / ensures it's on top
    } else {
        // Nascondi entrambi (non elimina, solo nasconde per riuso)
        // Hide both (doesn't delete, just hides for reuse)
        lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─── Event callbacks ─────────────────────────────────────────────────────────

/**
 * @brief Click sul backdrop semitrasparente → chiude il pannello.
 *        Click on semi-transparent backdrop → closes the panel.
 */
static void _notif_backdrop_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_panel_close();
}

/**
 * @brief Click sul pulsante [X] del pannello → chiude il pannello.
 *        Click on panel [X] button → closes the panel.
 */
static void _notif_close_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_panel_close();
}

/**
 * @brief Click sul pulsante "Cancella tutto" → elimina tutte le notifiche.
 *        Click on "Clear all" button → deletes all notifications.
 */
static void _notif_clear_all_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_notif_clear_all();
}

/**
 * @brief Callback LV_EVENT_DELETE su singoli oggetti UI (backdrop, panel, list).
 *        LV_EVENT_DELETE callback on individual UI objects (backdrop, panel, list).
 *
 * Azzera i puntatori statici corrispondenti quando gli oggetti vengono eliminati.
 * Clears the corresponding static pointers when objects are deleted.
 * Evita dangling pointer se LVGL elimina gli oggetti (es. cambio schermata).
 * Prevents dangling pointers if LVGL deletes objects (e.g. screen change).
 */
static void _notif_obj_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = lv_event_get_target(e);
    if (obj == s_backdrop) s_backdrop = nullptr;
    if (obj == s_panel)    s_panel    = nullptr;
    if (obj == s_list)     s_list     = nullptr;
}

/**
 * @brief Callback LV_EVENT_DELETE sulla schermata home.
 *        LV_EVENT_DELETE callback on the home screen.
 *
 * Quando la home viene eliminata (cambio schermata), azzera TUTTI i puntatori
 * statici e resetta lo stato s_open. Così al prossimo ui_notif_panel_init()
 * tutto viene ricreato da zero.
 *
 * When home is deleted (screen change), clears ALL static pointers and
 * resets s_open state. So the next ui_notif_panel_init() recreates everything.
 */
static void _notif_scr_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    lv_obj_t* obj = lv_event_get_target(e);
    if (obj != s_scr) return;  // non è la nostra schermata / not our screen
    // Azzera tutti i puntatori agli oggetti della schermata
    // Clear all pointers to screen objects
    s_scr      = nullptr;
    s_backdrop = nullptr;
    s_panel    = nullptr;
    s_list     = nullptr;
    s_open     = false;
}

/**
 * @brief Callback LV_EVENT_DELETE su un item della lista — libera lo slot pointer.
 *        LV_EVENT_DELETE callback on a list item — frees the slot pointer.
 *
 * Ogni item ha un puntatore a int allocato con new (user_data = puntatore al
 * numero di slot). Questo callback lo libera quando l'item viene eliminato.
 * Each item has an int pointer allocated with new (user_data = pointer to
 * slot number). This callback frees it when the item is deleted.
 */
static void _notif_item_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    int* slot = static_cast<int*>(lv_event_get_user_data(e));
    delete slot;  // libera la memoria dello slot pointer / free slot pointer memory
}

/**
 * @brief Click su un item → segna la notifica come letta (la rimuove).
 *        Click on an item → marks the notification as read (removes it).
 *
 * Imposta active=false nello slot corrispondente e ricostruisce la lista.
 * Sets active=false in the corresponding slot and rebuilds the list.
 */
static void _notif_item_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int* slot = static_cast<int*>(lv_event_get_user_data(e));
    if (!slot) return;
    if (*slot < 0 || *slot >= k_notif_max) return;
    s_entries[*slot].active = false;  // segna come letta / mark as read
    _notif_rebuild_list();            // aggiorna la UI / update the UI
}

// ─── Creazione item lista ─────────────────────────────────────────────────────

/**
 * @brief Crea un singolo item notifica nel container lista.
 *        Creates a single notification item in the list container.
 *
 * Struttura visiva di ogni item (74px di altezza):
 * Visual structure of each item (74px height):
 *
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │ █ [icona]  TITOLO NOTIFICA                          14:32       │
 *   │ │          Testo body della notifica                            │
 *   └─────────────────────────────────────────────────────────────────┘
 *   │ ← 5px colonna colorata (rosso/giallo/verde per severità)
 *
 * - La colonna colorata (5px) indica visivamente la severità.
 * - The colored column (5px) visually indicates severity.
 * - Il click sull'item rimuove la notifica dalla lista.
 * - Clicking the item removes the notification from the list.
 * - La memoria del puntatore slot viene liberata in _notif_item_delete_cb.
 * - The slot pointer memory is freed in _notif_item_delete_cb.
 *
 * @param parent  Container genitore (la lista scrollabile).
 *                Parent container (the scrollable list).
 * @param slot    Indice dello slot in s_entries[]. Slot index in s_entries[].
 */
static void _notif_make_item(lv_obj_t* parent, int slot) {
    const UiNotifEntry& entry = s_entries[slot];

    // ── Card item ──────────────────────────────────────────────────────────────
    lv_obj_t* item = lv_btn_create(parent);
    lv_obj_set_width(item, LV_PCT(100));          // larghezza 100% del container
    lv_obj_set_height(item, k_item_h);            // altezza fissa 74px
    lv_obj_set_style_bg_color(item, NOTIF_ITEM_BG, 0);
    // Feedback visivo al press: sfondo più scuro
    // Visual feedback on press: darker background
    lv_obj_set_style_bg_color(item, NOTIF_ITEM_PRESSED_BG, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(item, NOTIF_BORDER, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_radius(item, 10, 0);
    lv_obj_set_style_shadow_width(item, 0, 0);
    lv_obj_set_style_pad_all(item, 0, 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

    // Alloca puntatore allo slot (per i callback delete e click)
    // Allocate slot pointer (for delete and click callbacks)
    int* user_slot = new int(slot);
    if (user_slot) {
        lv_obj_add_event_cb(item, _notif_item_delete_cb, LV_EVENT_DELETE,  user_slot);
        lv_obj_add_event_cb(item, _notif_item_click_cb,  LV_EVENT_CLICKED, user_slot);
    }

    // ── Barra colorata sinistra (5px, colore = severità) ──────────────────────
    // Indicatore visivo della gravità: rosso=ALERT, giallo=INFO, verde=NONE
    // Visual severity indicator: red=ALERT, yellow=INFO, green=NONE
    lv_obj_t* bar = lv_obj_create(item);
    lv_obj_set_size(bar, 5, LV_PCT(100));         // 5px larghezza, altezza piena
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, _notif_color(entry.severity), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    // Non clickabile né scrollabile (è solo decorativo)
    // Not clickable or scrollable (decorative only)
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // ── Icona severità ────────────────────────────────────────────────────────
    // Simbolo LVGL (⚠/🔔/✓) colorato in base alla severità
    // LVGL symbol (⚠/🔔/✓) colored according to severity
    lv_obj_t* ico = lv_label_create(item);
    lv_label_set_text(ico, _notif_icon(entry.severity));
    lv_obj_set_style_text_font(ico, UI_FONT_SUBTITLE, 0);   // font 24pt per icona leggibile
    lv_obj_set_style_text_color(ico, _notif_color(entry.severity), 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 18, -10);          // 18px dal bordo (dopo la barra 5px)

    // ── Titolo notifica ───────────────────────────────────────────────────────
    // Tronca con "..." se troppo lungo (LV_LABEL_LONG_DOT)
    // Truncates with "..." if too long (LV_LABEL_LONG_DOT)
    lv_obj_t* title = lv_label_create(item);
    lv_label_set_text(title, entry.title);
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, 0);    // font 16pt
    lv_obj_set_style_text_color(title, NOTIF_TEXT, 0);
    lv_obj_set_width(title, 760);                             // larghezza per il testo
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);        // tronca con ...
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 56, 10);          // 56px dal bordo (dopo barra+icona)

    // ── Testo body ────────────────────────────────────────────────────────────
    lv_obj_t* body = lv_label_create(item);
    lv_label_set_text(body, entry.body);
    lv_obj_set_style_text_font(body, UI_FONT_TINY, 0);       // font 12pt (più piccolo)
    lv_obj_set_style_text_color(body, NOTIF_TEXT_MUTED, 0);
    lv_obj_set_width(body, 820);
    lv_label_set_long_mode(body, LV_LABEL_LONG_DOT);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 56, 34);           // sotto il titolo

    // ── Timestamp (in alto a destra) ──────────────────────────────────────────
    lv_obj_t* ts = lv_label_create(item);
    lv_label_set_text(ts, entry.ts);
    lv_obj_set_style_text_font(ts, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(ts, NOTIF_TEXT_MUTED, 0);   // grigio scuro, discreto
    lv_obj_align(ts, LV_ALIGN_TOP_RIGHT, -16, 12);            // 16px dal bordo destro
}

// ─── Ricostruzione lista UI ───────────────────────────────────────────────────

/**
 * @brief Ricostruisce completamente la lista LVGL delle notifiche.
 *        Completely rebuilds the LVGL notification list.
 *
 * Algoritmo / Algorithm:
 *   1. Elimina tutti i figli del container lista (lv_obj_clean)
 *   1. Delete all children of the list container (lv_obj_clean)
 *   2. Raccoglie gli indici degli slot attivi in ordered[]
 *   2. Collect active slot indices into ordered[]
 *   3. Ordina ordered[] per inserimento (insertion sort) usando _notif_sort_before
 *   3. Sort ordered[] by insertion (insertion sort) using _notif_sort_before
 *   4. Se la lista è vuota, mostra il messaggio "Nessuna notifica presente."
 *   4. If list is empty, show "Nessuna notifica presente." message
 *   5. Crea un item LVGL per ogni slot nell'ordine risultante
 *   5. Create an LVGL item for each slot in the resulting order
 *
 * Questa funzione viene chiamata ogni volta che la lista cambia (push, clear, click).
 * This function is called every time the list changes (push, clear, click).
 */
static void _notif_rebuild_list() {
    if (!s_list) return;

    // Elimina tutti gli item esistenti (lv_obj_clean elimina i figli ma non il container)
    // Delete all existing items (lv_obj_clean deletes children but not the container)
    lv_obj_clean(s_list);

    // ── Insertion sort degli slot attivi ──────────────────────────────────────
    int ordered[k_notif_max];
    int count = 0;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;  // salta slot inattivi

        // Trova la posizione di inserimento nel array ordinato
        // Find the insertion position in the sorted array
        int insert_at = count;
        for (int j = 0; j < count; j++) {
            if (_notif_sort_before(s_entries[i], s_entries[ordered[j]])) {
                insert_at = j;
                break;
            }
        }
        // Sposta gli elementi a destra per fare spazio
        // Shift elements right to make room
        for (int j = count; j > insert_at; j--) {
            ordered[j] = ordered[j - 1];
        }
        ordered[insert_at] = i;
        count++;
    }

    // ── Lista vuota: mostra placeholder ───────────────────────────────────────
    if (count == 0) {
        lv_obj_t* empty = lv_label_create(s_list);
        lv_label_set_text(empty, "Nessuna notifica presente.");
        lv_obj_set_style_text_font(empty, UI_FONT_BODY, 0);
        lv_obj_set_style_text_color(empty, NOTIF_TEXT_MUTED, 0);
        lv_obj_center(empty);
        return;
    }

    // ── Crea gli item nell'ordine risultante ───────────────────────────────────
    for (int i = 0; i < count; i++) {
        _notif_make_item(s_list, ordered[i]);
    }
}

// ─── API pubblica: inizializzazione pannello ──────────────────────────────────

/**
 * @brief Inizializza il pannello notifiche e lo allega alla schermata data.
 *        Initializes the notification panel and attaches it to the given screen.
 *
 * @see ui_notifications.h per la documentazione completa.
 * @see ui_notifications.h for complete documentation.
 */
void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* /*header*/) {
    s_scr = scr;

    // Registra callback per azzerare i puntatori quando la schermata viene eliminata
    // Register callback to clear pointers when the screen is deleted
    lv_obj_add_event_cb(scr, _notif_scr_delete_cb, LV_EVENT_DELETE, nullptr);

    // ── Pulizia eventuali pannelli precedenti ─────────────────────────────────
    // Se la home viene ricreata, elimina il vecchio pannello prima di creare il nuovo
    // If home is recreated, delete the old panel before creating the new one
    if (s_backdrop) lv_obj_del(s_backdrop);
    if (s_panel)    lv_obj_del(s_panel);
    s_backdrop = nullptr;
    s_panel    = nullptr;
    s_list     = nullptr;
    s_open     = false;

    // ── Backdrop semitrasparente ──────────────────────────────────────────────
    // Copre tutta la schermata (1024×600) con nero al 40% opacità.
    // Tappando il backdrop si chiude il pannello.
    // Covers the full screen (1024×600) with 40% black opacity.
    // Tapping the backdrop closes the panel.
    s_backdrop = lv_obj_create(scr);
    lv_obj_set_size(s_backdrop, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_style_bg_color(s_backdrop, NOTIF_BACKDROP_BG, 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_backdrop, 0, 0);
    lv_obj_set_style_radius(s_backdrop, 0, 0);
    lv_obj_clear_flag(s_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    // CLICKABLE = true (inizialmente HIDDEN)
    // CLICKABLE = true (initially HIDDEN)
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_backdrop, _notif_backdrop_cb,    LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_backdrop, _notif_obj_delete_cb,  LV_EVENT_DELETE,  nullptr);

    // ── Pannello notifiche ────────────────────────────────────────────────────
    // Larghezza schermo × 320px, posizionato sotto l'header (UI_HEADER_H = 52px).
    // Screen width × 320px, positioned below the header (UI_HEADER_H = 52px).
    s_panel = lv_obj_create(scr);
    lv_obj_set_size(s_panel, UI_SCREEN_W, k_panel_h);
    lv_obj_set_pos(s_panel, 0, UI_HEADER_H);                   // inizia subito sotto l'header
    lv_obj_set_style_bg_color(s_panel, NOTIF_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, NOTIF_BORDER, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    // Ombra verso il basso per dare profondità al pannello
    // Shadow downward to give depth to the panel
    lv_obj_set_style_shadow_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(s_panel, 18, 0);
    lv_obj_set_style_shadow_opa(s_panel, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(s_panel, 6, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);              // inizialmente nascosto
    lv_obj_add_event_cb(s_panel, _notif_obj_delete_cb, LV_EVENT_DELETE, nullptr);

    // ── Header interno al pannello (52px) ─────────────────────────────────────
    // Contiene: titolo "🔔 Notifiche", pulsante [🗑 Cancella], pulsante [✕]
    // Contains: title "🔔 Notifiche", [🗑 Clear] button, [✕] button
    lv_obj_t* top = lv_obj_create(s_panel);
    lv_obj_set_size(top, UI_SCREEN_W, k_header_h);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, NOTIF_HEADER_BG, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 16, 0);
    lv_obj_set_style_pad_right(top, 16, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    // Titolo "🔔 Notifiche"
    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, LV_SYMBOL_BELL "  Notifiche");
    lv_obj_set_style_text_font(title, UI_FONT_LABEL, 0);
    lv_obj_set_style_text_color(title, NOTIF_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Pulsante [🗑 Cancella] — elimina tutte le notifiche
    // [🗑 Clear] button — deletes all notifications
    lv_obj_t* clear_btn = lv_btn_create(top);
    lv_obj_set_size(clear_btn, 150, 34);
    lv_obj_align(clear_btn, LV_ALIGN_RIGHT_MID, -72, 0);  // a sinistra del [✕]
    lv_obj_set_style_bg_color(clear_btn, NOTIF_BUTTON_BG, 0);
    lv_obj_set_style_bg_color(clear_btn, NOTIF_BUTTON_PRESSED_BG, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(clear_btn, NOTIF_BORDER, 0);
    lv_obj_set_style_border_width(clear_btn, 1, 0);
    lv_obj_set_style_radius(clear_btn, 8, 0);
    lv_obj_set_style_shadow_width(clear_btn, 0, 0);
    lv_obj_add_event_cb(clear_btn, _notif_clear_all_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* clear_lbl = lv_label_create(clear_btn);
    lv_label_set_text(clear_lbl, LV_SYMBOL_TRASH " Cancella");
    lv_obj_set_style_text_font(clear_lbl, UI_FONT_TINY, 0);
    lv_obj_set_style_text_color(clear_lbl, NOTIF_TEXT, 0);
    lv_obj_center(clear_lbl);

    // Pulsante [✕] — chiude il pannello
    // [✕] button — closes the panel
    lv_obj_t* close_btn = lv_btn_create(top);
    lv_obj_set_size(close_btn, 56, 34);
    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, NOTIF_BUTTON_BG, 0);
    lv_obj_set_style_bg_color(close_btn, NOTIF_BUTTON_PRESSED_BG, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(close_btn, NOTIF_BORDER, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_add_event_cb(close_btn, _notif_close_btn_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, NOTIF_TEXT, 0);
    lv_obj_center(close_lbl);

    // ── Container lista scrollabile ───────────────────────────────────────────
    // Occupa lo spazio sotto l'header interno: 320-52 = 268px.
    // Occupies the space below the internal header: 320-52 = 268px.
    // Usa layout FLEX COLUMN per impilare gli item verticalmente.
    // Uses FLEX COLUMN layout to stack items vertically.
    s_list = lv_obj_create(s_panel);
    lv_obj_set_size(s_list, UI_SCREEN_W, k_panel_h - k_header_h);
    lv_obj_set_pos(s_list, 0, k_header_h);
    lv_obj_set_style_bg_color(s_list, NOTIF_PANEL_BG, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 12, 0);   // padding esterno 12px
    lv_obj_set_style_pad_row(s_list, 8, 0);    // gap verticale tra item 8px
    // Layout FLEX COLUMN: item impilati in verticale dall'alto
    // FLEX COLUMN layout: items stacked vertically from top
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);          // permetti scroll verticale
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF); // nascondi scrollbar (troppo ingombrante)
    lv_obj_add_event_cb(s_list, _notif_obj_delete_cb, LV_EVENT_DELETE, nullptr);

    // Popola la lista con le notifiche esistenti (se ci sono)
    // Populate the list with existing notifications (if any)
    _notif_rebuild_list();
}

// ─── API pubblica: apertura/chiusura ─────────────────────────────────────────

void ui_notif_panel_open(void) {
    _notif_panel_set_open(true);
}

void ui_notif_panel_close(void) {
    _notif_panel_set_open(false);
}

void ui_notif_panel_toggle(void) {
    _notif_panel_set_open(!s_open);
}

// ─── API pubblica: gestione notifiche ────────────────────────────────────────

/**
 * @brief Aggiunge una nuova notifica o aggiorna una esistente con la stessa chiave.
 *        Adds a new notification or updates an existing one with the same key.
 *
 * @see ui_notifications.h per la documentazione completa.
 * @see ui_notifications.h for complete documentation.
 */
void ui_notif_push_or_update(const char* key, UiNotifSeverity severity,
                             const char* title, const char* body) {
    if (!key || !key[0]) return;

    // Normalizza i valori nulli/vuoti
    // Normalize null/empty values
    const char* safe_title = (title && title[0]) ? title : "Notifica";
    const char* safe_body  = (body  && body[0])  ? body  : "-";

    int slot = _notif_find_slot(key);

    if (slot >= 0 && s_entries[slot].active) {
        // Slot esistente: controlla se i dati sono cambiati
        // Existing slot: check if data has changed
        UiNotifEntry& existing = s_entries[slot];
        if (existing.severity == severity &&
            strncmp(existing.title, safe_title, sizeof(existing.title)) == 0 &&
            strncmp(existing.body,  safe_body,  sizeof(existing.body))  == 0) {
            // Nessun cambiamento: non fare nulla (evita rebuild inutile)
            // No change: do nothing (avoid unnecessary rebuild)
            return;
        }
        // Dati cambiati: aggiorna lo slot esistente (non alloca nuovo slot)
        // Data changed: update existing slot (no new slot allocation)
    } else {
        // Nessuno slot esistente: alloca nuovo slot (libero o il più vecchio)
        // No existing slot: allocate new slot (free or oldest)
        slot = _notif_allocate_slot();
    }

    // Scrivi i dati nel slot
    // Write data into slot
    UiNotifEntry& entry = s_entries[slot];
    memset(&entry, 0, sizeof(entry));                                    // azzeramento sicuro
    entry.active   = true;
    entry.severity = severity;
    entry.seq      = ++s_seq_counter;                                    // numero sequenza crescente
    strncpy(entry.key,   key,        sizeof(entry.key)   - 1);
    strncpy(entry.title, safe_title, sizeof(entry.title) - 1);
    strncpy(entry.body,  safe_body,  sizeof(entry.body)  - 1);
    _notif_make_timestamp(entry.ts, sizeof(entry.ts));                   // timestamp corrente

    // Ricostruisce la lista LVGL con i nuovi dati
    // Rebuild LVGL list with new data
    _notif_rebuild_list();
}

/**
 * @brief Rimuove la notifica con la chiave indicata.
 *        Removes the notification with the given key.
 */
void ui_notif_clear(const char* key) {
    const int slot = _notif_find_slot(key);
    if (slot < 0) return;                         // chiave non trovata / key not found
    s_entries[slot].active = false;
    _notif_rebuild_list();
}

/**
 * @brief Rimuove tutte le notifiche la cui chiave inizia con il prefisso dato.
 *        Removes all notifications whose key starts with the given prefix.
 */
void ui_notif_clear_prefix(const char* prefix) {
    if (!prefix || !prefix[0]) return;
    const size_t prefix_len = strlen(prefix);
    bool changed = false;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        if (strncmp(s_entries[i].key, prefix, prefix_len) != 0) continue;
        s_entries[i].active = false;
        changed = true;
    }
    // Ricostruisce solo se qualcosa è cambiato (ottimizzazione)
    // Rebuild only if something changed (optimization)
    if (changed) _notif_rebuild_list();
}

/**
 * @brief Rimuove tutte le notifiche attive.
 *        Removes all active notifications.
 */
void ui_notif_clear_all(void) {
    // memset azzera tutti gli struct (active=false automaticamente)
    // memset zeros all structs (active=false automatically)
    memset(s_entries, 0, sizeof(s_entries));
    _notif_rebuild_list();
}

// ─── API pubblica: query ──────────────────────────────────────────────────────

/**
 * @brief Restituisce il livello di gravità più alto tra tutte le notifiche attive.
 *        Returns the highest severity level among all active notifications.
 */
UiNotifSeverity ui_notif_highest_severity(void) {
    UiNotifSeverity highest = UI_NOTIF_NONE;
    for (int i = 0; i < k_notif_max; i++) {
        if (!s_entries[i].active) continue;
        // Confronto numerico: ALERT(2) > INFO(1) > NONE(0)
        // Numeric comparison: ALERT(2) > INFO(1) > NONE(0)
        if (s_entries[i].severity > highest) highest = s_entries[i].severity;
    }
    return highest;
}

/**
 * @brief Restituisce il numero di notifiche attive.
 *        Returns the number of active notifications.
 */
int ui_notif_count(void) {
    int count = 0;
    for (int i = 0; i < k_notif_max; i++) {
        if (s_entries[i].active) count++;
    }
    return count;
}
