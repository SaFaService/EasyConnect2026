#include "ui_dc_maintenance.h"

#include <string.h>

/**
 * @file ui_dc_maintenance.cpp
 * @brief Popup di autenticazione PIN per operazioni riservate alla manutenzione.
 *        PIN authentication popup for maintenance-only operations.
 *
 * Il popup occupa tutta la schermata (1024×600) e si sovrappone a qualsiasi
 * contenuto già visualizzato. Struttura visiva:
 * The popup covers the full screen (1024×600) and overlays any existing content.
 * Visual structure:
 *
 *   ┌──────────────────────────────────────────────────────────────────────┐
 *   │ (mask nera 70% opacità — blocca l'interazione col fondo)            │
 *   │  ┌────────────────────────────────────────────────────────────────┐  │
 *   │  │ Titolo (es. "Eliminazione periferica")                         │  │ ← top panel 168px
 *   │  │ Hint: "Inserire il PIN numerico per continuare"                │  │
 *   │  │                             [Annulla]  [Conferma / Azione]    │  │
 *   │  │ ████████████ (textarea PIN, password •••, max 4 cifre)        │  │
 *   │  │ "PIN non valido"  (label errore, vuota se nessun errore)       │  │
 *   │  └────────────────────────────────────────────────────────────────┘  │
 *   │                                                                      │
 *   │  ┌────────────────────────────────────────────────────────────────┐  │
 *   │  │                                                                │  │
 *   │  │      [tastiera numerica LVGL    1024×432]                      │  │
 *   │  │                                                                │  │
 *   │  └────────────────────────────────────────────────────────────────┘  │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 * Flusso / Flow:
 *   1. Utente digita PIN sulla tastiera numerica
 *      User types PIN on numeric keyboard
 *   2. Conferma con [Conferma] o tasto ENTER della tastiera
 *      Confirms with [Confirm] or keyboard ENTER key
 *   3a. PIN corretto: popup si chiude, callback on_success() viene chiamato
 *       Correct PIN: popup closes, on_success() callback is called
 *   3b. PIN errato: campo svuotato, label errore "PIN non valido" mostrata
 *       Wrong PIN: field cleared, "PIN non valido" error label shown
 *   4. [Annulla] o tasto ESC tastiera: chiude senza azione
 *      [Cancel] or keyboard ESC: closes without action
 *
 * Memoria / Memory:
 *   MaintenancePinCtx viene allocato con new in request_pin() e liberato
 *   automaticamente nell'evento LV_EVENT_DELETE del mask, tramite _popup_delete_cb.
 *   MaintenancePinCtx is allocated with new in request_pin() and freed
 *   automatically in the LV_EVENT_DELETE event of the mask, via _popup_delete_cb.
 */

// ─── PIN hardcoded ────────────────────────────────────────────────────────────
// PIN di accesso alla manutenzione. Solo cifre, 4 caratteri.
// Maintenance access PIN. Digits only, 4 characters.
static constexpr const char* k_maintenance_pin = "0805";

// ─── Palette ──────────────────────────────────────────────────────────────────
#define MT_WHITE   lv_color_hex(0xFFFFFF)   ///< Bianco per sfondo pannello top
#define MT_BG      lv_color_hex(0xEEF3F8)   ///< Azzurro chiaro per sfondo tastiera e bottone cancel
#define MT_TEXT    lv_color_hex(0x243447)   ///< Blu scuro testo principale
#define MT_DIM     lv_color_hex(0x7A92B0)   ///< Grigio-blu per testi secondari (hint)
#define MT_BORDER  lv_color_hex(0xDDE5EE)   ///< Bordo grigio chiaro
#define MT_ORANGE  lv_color_hex(0xE84820)   ///< Arancione Antralux (pulsante conferma)
#define MT_ORANGE2 lv_color_hex(0xB02810)   ///< Arancione scuro (feedback press pulsante)
#define MT_ERROR   lv_color_hex(0xC0392B)   ///< Rosso per la label "PIN non valido"

// ─── Struttura contesto popup ─────────────────────────────────────────────────

/**
 * @brief Contesto del popup PIN — mantiene i riferimenti agli oggetti LVGL
 *        e ai dati necessari per la gestione del ciclo di vita.
 *        PIN popup context — holds references to LVGL objects and data
 *        needed for lifecycle management.
 *
 * Allocato dinamicamente con new da ui_dc_maintenance_request_pin().
 * Dynamically allocated with new by ui_dc_maintenance_request_pin().
 *
 * La proprietà della memoria è ceduta all'evento LV_EVENT_DELETE del mask:
 * Memory ownership is transferred to the LV_EVENT_DELETE event of the mask:
 *   - Finché il mask esiste, il ctx è valido.
 *   - As long as the mask exists, ctx is valid.
 *   - Quando il mask viene eliminato (lv_obj_del), _popup_delete_cb chiama delete ctx.
 *   - When the mask is deleted (lv_obj_del), _popup_delete_cb calls delete ctx.
 */
struct MaintenancePinCtx {
    lv_obj_t* mask;          ///< Puntatore al mask radice (oggetto padre). Root mask pointer (parent object).
    lv_obj_t* ta;            ///< Puntatore alla textarea PIN. PIN textarea pointer.
    lv_obj_t* error_lbl;     ///< Puntatore alla label errore. Error label pointer.
    UiDcMaintenanceSuccessCb on_success;  ///< Callback da chiamare al PIN corretto. Callback on correct PIN.
    void* user_data;         ///< Dati utente passati al callback. User data passed to callback.
};

// ─── Singleton istanza popup ──────────────────────────────────────────────────
// Solo un popup di manutenzione alla volta. Se viene aperto un secondo popup,
// quello precedente viene eliminato.
// Only one maintenance popup at a time. If a second popup is opened,
// the previous one is deleted.
static MaintenancePinCtx* s_ctx = nullptr;

// ─── Callbacks ciclo di vita ──────────────────────────────────────────────────

/**
 * @brief Callback LV_EVENT_DELETE sul mask — libera la memoria del contesto.
 *        LV_EVENT_DELETE callback on mask — frees the context memory.
 *
 * Quando LVGL elimina il mask (e tutti i suoi figli), questo callback viene
 * chiamato automaticamente. Azzera il singleton s_ctx e chiama delete ctx
 * per liberare la memoria allocata con new.
 *
 * When LVGL deletes the mask (and all its children), this callback is
 * automatically called. Clears the s_ctx singleton and calls delete ctx
 * to free the memory allocated with new.
 *
 * @param e  Evento LVGL con user_data = puntatore al MaintenancePinCtx.
 */
static void _popup_delete_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    MaintenancePinCtx* ctx = static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e));
    // Azzera il singleton prima di delete (evita dangling pointer nel singleton)
    // Clear singleton before delete (avoids dangling pointer in singleton)
    if (ctx == s_ctx) s_ctx = nullptr;
    delete ctx;  // libera memoria contesto / free context memory
}

// ─── Funzioni interne di gestione popup ──────────────────────────────────────

/**
 * @brief Chiude il popup eliminando il mask LVGL.
 *        Closes the popup by deleting the LVGL mask.
 *
 * lv_obj_del() elimina l'oggetto e tutti i suoi figli (top panel, keyboard,
 * textarea, labels). Il callback _popup_delete_cb verrà chiamato automaticamente
 * da LVGL durante la distruzione, liberando il ctx.
 *
 * lv_obj_del() deletes the object and all its children (top panel, keyboard,
 * textarea, labels). The _popup_delete_cb will be automatically called by LVGL
 * during destruction, freeing the ctx.
 *
 * @param ctx  Contesto del popup. Popup context.
 */
static void _popup_close(MaintenancePinCtx* ctx) {
    if (!ctx || !ctx->mask) return;
    lv_obj_del(ctx->mask);  // elimina mask + tutti i figli + triggera _popup_delete_cb
}

/**
 * @brief Gestisce il caso di PIN errato.
 *        Handles the case of an incorrect PIN.
 *
 * Mostra la label "PIN non valido" in rosso e svuota la textarea.
 * Shows the "PIN non valido" label in red and clears the textarea.
 * L'utente può riprovare.
 * The user can try again.
 *
 * @param ctx  Contesto del popup. Popup context.
 */
static void _popup_fail(MaintenancePinCtx* ctx) {
    if (!ctx) return;
    // Mostra messaggio di errore
    // Show error message
    if (ctx->error_lbl) {
        lv_label_set_text(ctx->error_lbl, "PIN non valido");
    }
    // Svuota il campo PIN per permettere un nuovo tentativo
    // Clear the PIN field to allow a new attempt
    if (ctx->ta) {
        lv_textarea_set_text(ctx->ta, "");
    }
}

/**
 * @brief Verifica il PIN e decide se procedere o mostrare errore.
 *        Verifies the PIN and decides whether to proceed or show error.
 *
 * Legge il testo dalla textarea e confronta con k_maintenance_pin.
 * Reads the text from the textarea and compares with k_maintenance_pin.
 *
 * Salva il callback PRIMA di chiudere il popup perché _popup_close()
 * elimina il ctx (tramite _popup_delete_cb). Dopo la chiusura, chiama
 * il callback con i dati salvati.
 *
 * Saves the callback BEFORE closing the popup because _popup_close()
 * deletes ctx (via _popup_delete_cb). After closing, calls the callback
 * with the saved data.
 *
 * @param ctx  Contesto del popup. Popup context.
 */
static void _popup_submit(MaintenancePinCtx* ctx) {
    if (!ctx || !ctx->ta) return;

    // Confronto PIN: strcmp() — semplice confronto stringa
    // PIN comparison: strcmp() — simple string comparison
    if (strcmp(lv_textarea_get_text(ctx->ta), k_maintenance_pin) != 0) {
        // PIN errato: mostra errore e lascia aperto il popup
        // Wrong PIN: show error and leave popup open
        _popup_fail(ctx);
        return;
    }

    // PIN corretto: salva callback e user_data PRIMA di chiudere
    // (il close elimina ctx, dopo non possiamo più accedervi)
    // Correct PIN: save callback and user_data BEFORE closing
    // (close deletes ctx, afterwards we can't access it)
    UiDcMaintenanceSuccessCb cb = ctx->on_success;
    void* user_data = ctx->user_data;

    _popup_close(ctx);  // ctx non è più valido dopo questa riga / ctx invalid after this line

    // Chiama il callback dell'azione solo se non è NULL
    // Call the action callback only if not NULL
    if (cb) cb(user_data);
}

// ─── Event callbacks pulsanti/tastiera ───────────────────────────────────────

/**
 * @brief Callback pulsante [Annulla] — chiude senza azione.
 *        [Cancel] button callback — closes without action.
 */
static void _popup_cancel_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _popup_close(static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e)));
}

/**
 * @brief Callback pulsante [Conferma] — verifica il PIN.
 *        [Confirm] button callback — verifies the PIN.
 */
static void _popup_confirm_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _popup_submit(static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e)));
}

/**
 * @brief Callback tastiera LVGL — gestisce ENTER (conferma) e ESC (annulla).
 *        LVGL keyboard callback — handles ENTER (confirm) and ESC (cancel).
 *
 * La tastiera numerica genera LV_EVENT_READY (tasto ✓/ENTER) e LV_EVENT_CANCEL (ESC/×).
 * The numeric keyboard generates LV_EVENT_READY (✓/ENTER key) and LV_EVENT_CANCEL (ESC/×).
 *
 * @param e  Evento LVGL dalla tastiera.
 */
static void _popup_keyboard_cb(lv_event_t* e) {
    const lv_event_code_t code = lv_event_get_code(e);
    MaintenancePinCtx* ctx = static_cast<MaintenancePinCtx*>(lv_event_get_user_data(e));
    if (code == LV_EVENT_READY) {
        // Tasto ENTER della tastiera numerica → tenta conferma
        // Numeric keyboard ENTER key → attempt confirm
        _popup_submit(ctx);
    } else if (code == LV_EVENT_CANCEL) {
        // Tasto ESC/× della tastiera → chiude senza azione
        // Keyboard ESC/× key → closes without action
        _popup_close(ctx);
    }
}

// ─── Funzione pubblica principale ─────────────────────────────────────────────

/**
 * @brief Apre il popup di autenticazione PIN per un'operazione di manutenzione.
 *        Opens the PIN authentication popup for a maintenance operation.
 *
 * @see ui_dc_maintenance.h per la documentazione completa dei parametri.
 * @see ui_dc_maintenance.h for complete parameter documentation.
 */
void ui_dc_maintenance_request_pin(const char* action_title,
                                   const char* action_label,
                                   UiDcMaintenanceSuccessCb on_success,
                                   void* user_data) {
    // Ottieni la schermata corrente come genitore del popup
    // Get the current screen as the popup parent
    lv_obj_t* parent = lv_scr_act();
    if (!parent) return;

    // ── Gestione singleton: se c'è un popup aperto, chiudilo prima ────────────
    // Handling singleton: if there's an open popup, close it first
    if (s_ctx && s_ctx->mask) {
        lv_obj_del(s_ctx->mask);  // _popup_delete_cb azzererà s_ctx
    }

    // ── Alloca contesto ───────────────────────────────────────────────────────
    MaintenancePinCtx* ctx = new MaintenancePinCtx();
    if (!ctx) return;  // allocazione fallita (OOM) / allocation failed (OOM)
    memset(ctx, 0, sizeof(*ctx));  // azzeramento per sicurezza / zero for safety
    ctx->on_success = on_success;
    ctx->user_data  = user_data;

    // ── Mask (overlay semitrasparente — blocca interazione con lo sfondo) ─────
    // The mask covers the full screen with 70% black — it's the "modal backdrop"
    lv_obj_t* mask = lv_obj_create(parent);
    ctx->mask = mask;
    s_ctx = ctx;  // aggiorna singleton / update singleton

    // Registra la callback di delete per liberare il ctx quando il mask viene eliminato
    // Register delete callback to free ctx when mask is deleted
    lv_obj_add_event_cb(mask, _popup_delete_cb, LV_EVENT_DELETE, ctx);

    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_70, 0);    // 70% opacità nera = sfondo modale
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_set_style_pad_all(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    // ── Pannello superiore bianco (168px) — contiene titolo, hint, pulsanti, PIN ──
    // Top white panel (168px) — contains title, hint, buttons, PIN field
    lv_obj_t* top = lv_obj_create(mask);
    lv_obj_set_size(top, 1024, 168);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_color(top, MT_WHITE, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_left(top, 20, 0);
    lv_obj_set_style_pad_right(top, 20, 0);
    lv_obj_set_style_pad_top(top, 14, 0);
    lv_obj_set_style_pad_bottom(top, 14, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    // ── Titolo ────────────────────────────────────────────────────────────────
    // Se action_title è NULL o "", usa il default "Accesso manutenzione"
    // If action_title is NULL or "", use default "Accesso manutenzione"
    lv_obj_t* title = lv_label_create(top);
    lv_label_set_text(title, (action_title && action_title[0]) ? action_title : "Accesso manutenzione");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, MT_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // ── Hint testuale ─────────────────────────────────────────────────────────
    lv_obj_t* hint = lv_label_create(top);
    lv_label_set_text(hint,
        "Operazione riservata alla manutenzione.\nInserire il PIN numerico per continuare.");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, MT_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 34);  // sotto il titolo

    // ── Pulsante [Annulla] ────────────────────────────────────────────────────
    // Posizionato a destra, -190px dal bordo destro (lascia spazio a [Conferma])
    // Positioned right, -190px from right edge (leaves room for [Confirm])
    lv_obj_t* btn_cancel = lv_btn_create(top);
    lv_obj_set_size(btn_cancel, 150, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_TOP_RIGHT, -190, 0);
    lv_obj_set_style_bg_color(btn_cancel, MT_BG, 0);          // sfondo azzurro chiaro
    lv_obj_set_style_border_color(btn_cancel, MT_BORDER, 0);  // bordo grigio
    lv_obj_set_style_border_width(btn_cancel, 1, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);           // nessuna ombra
    lv_obj_add_event_cb(btn_cancel, _popup_cancel_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annulla");
    lv_obj_set_style_text_color(lbl_cancel, MT_TEXT, 0);
    lv_obj_center(lbl_cancel);

    // ── Pulsante [Conferma] ───────────────────────────────────────────────────
    // Arancione Antralux, testo personalizzato o default "Conferma"
    // Antralux orange, custom text or default "Conferma"
    lv_obj_t* btn_confirm = lv_btn_create(top);
    lv_obj_set_size(btn_confirm, 170, 44);
    lv_obj_align(btn_confirm, LV_ALIGN_TOP_RIGHT, 0, 0);      // bordo destro
    lv_obj_set_style_bg_color(btn_confirm, MT_ORANGE, 0);
    lv_obj_set_style_bg_color(btn_confirm, MT_ORANGE2, LV_STATE_PRESSED);  // scurisce al press
    lv_obj_set_style_border_width(btn_confirm, 0, 0);
    lv_obj_set_style_shadow_width(btn_confirm, 0, 0);
    lv_obj_add_event_cb(btn_confirm, _popup_confirm_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_t* lbl_confirm = lv_label_create(btn_confirm);
    lv_label_set_text(lbl_confirm, (action_label && action_label[0]) ? action_label : "Conferma");
    lv_obj_set_style_text_color(lbl_confirm, lv_color_white(), 0);
    lv_obj_center(lbl_confirm);

    // ── Textarea PIN ──────────────────────────────────────────────────────────
    // La textarea è in modalità password: mostra punti (•) invece delle cifre.
    // La password_show_time=0 significa che il carattere digitato NON viene
    // mostrato brevemente prima di diventare •.
    // The textarea is in password mode: shows dots (•) instead of digits.
    // password_show_time=0 means the typed char is NOT briefly shown
    // before becoming •.
    lv_obj_t* ta = lv_textarea_create(top);
    ctx->ta = ta;
    lv_obj_set_size(ta, 984, 56);                              // quasi tutta la larghezza del panel
    lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);              // in fondo al pannello top
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_CENTER, 0);
    lv_textarea_set_one_line(ta, true);                        // una sola riga
    lv_textarea_set_max_length(ta, 4);                         // max 4 cifre (lunghezza PIN)
    lv_textarea_set_password_mode(ta, true);                   // nasconde le cifre
    lv_textarea_set_password_show_time(ta, 0);                 // nessuna preview del carattere
    lv_textarea_set_accepted_chars(ta, "0123456789");          // solo cifre accettate
    lv_textarea_set_placeholder_text(ta, "PIN manutenzione");  // placeholder quando vuota

    // ── Label errore ──────────────────────────────────────────────────────────
    // Inizialmente vuota; diventa "PIN non valido" se l'utente sbaglia PIN.
    // Initially empty; becomes "PIN non valido" if user enters wrong PIN.
    ctx->error_lbl = lv_label_create(top);
    lv_label_set_text(ctx->error_lbl, "");                     // vuota inizialmente
    lv_obj_set_style_text_font(ctx->error_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ctx->error_lbl, MT_ERROR, 0);  // rosso
    lv_obj_align(ctx->error_lbl, LV_ALIGN_BOTTOM_LEFT, 0, -4); // angolo bottom-left del top panel

    // ── Tastiera numerica LVGL ────────────────────────────────────────────────
    // Altezza 432px: complementare ai 168px del pannello top = 600px totali
    // Height 432px: complementary to the 168px top panel = 600px total
    lv_obj_t* kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 1024, 432);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);               // in fondo al mask
    lv_obj_set_style_bg_color(kb, MT_BG, 0);                   // sfondo azzurro chiaro
    lv_obj_set_style_anim_time(kb, 0, LV_PART_ITEMS);          // tocco immediato senza animazione
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);          // solo cifre (0-9 + . + BACK + OK)
    lv_keyboard_set_textarea(kb, ta);                           // collega tastiera alla textarea PIN

    // Callbacks tastiera: ENTER → verifica, ESC → chiude
    // Keyboard callbacks: ENTER → verify, ESC → close
    lv_obj_add_event_cb(kb, _popup_keyboard_cb, LV_EVENT_READY,  ctx);
    lv_obj_add_event_cb(kb, _popup_keyboard_cb, LV_EVENT_CANCEL, ctx);

    // ── Porta il popup in primo piano ─────────────────────────────────────────
    // lv_obj_move_foreground assicura che il popup sia sopra qualsiasi altro
    // oggetto figlio della schermata (es. eventuali altri pannelli aperti).
    // lv_obj_move_foreground ensures the popup is above any other child of
    // the screen (e.g. other open panels).
    lv_obj_move_foreground(mask);
}
