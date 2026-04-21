#pragma once

#include <stdint.h>
#include "lvgl.h"

/**
 * @file ui_notifications.h
 * @brief Sistema di notifiche UI — pannello a tendina con lista messaggi.
 *        UI notification system — drop-down panel with message list.
 *
 * Questo modulo gestisce un pannello notifiche persistente che:
 * This module manages a persistent notification panel that:
 *
 *   - Contiene al massimo 24 notifiche simultanee
 *   - Holds at most 24 simultaneous notifications
 *   - Supporta 3 livelli di gravità: NONE, INFO, ALERT
 *   - Supports 3 severity levels: NONE, INFO, ALERT
 *   - Identifica ogni notifica tramite una chiave stringa univoca
 *   - Identifies each notification by a unique string key
 *   - Permette aggiornamento in-place (stessa chiave → aggiorna)
 *   - Allows in-place update (same key → updates existing entry)
 *   - Ordina le notifiche per gravità decrescente, poi per sequenza (LIFO)
 *   - Sorts notifications by decreasing severity, then by sequence (LIFO)
 *
 * Architettura / Architecture:
 *   - La lista notifiche è in memoria statica (s_entries[24])
 *   - The notification list is in static memory (s_entries[24])
 *   - Il pannello LVGL è allegato a una schermata specifica
 *   - The LVGL panel is attached to a specific screen
 *   - Quando la schermata viene eliminata, il pannello si azzera
 *   - When the screen is deleted, the panel resets
 *   - Il pannello overlay + panel sono figli della screen passata a init
 *   - The overlay + panel are children of the screen passed to init
 *
 * Flusso tipico / Typical flow:
 *   1. Chiamare ui_notif_panel_init() dopo aver creato la schermata home
 *      Call ui_notif_panel_init() after creating the home screen
 *   2. Usare ui_notif_push_or_update() per aggiungere/aggiornare notifiche
 *      Use ui_notif_push_or_update() to add/update notifications
 *   3. Aprire/chiudere il pannello con ui_notif_panel_toggle()
 *      Open/close the panel with ui_notif_panel_toggle()
 *   4. Tappare una notifica nella lista la segna come letta (la rimuove)
 *      Tapping a notification in the list marks it as read (removes it)
 *
 * Layout pannello (320px di altezza, appare sotto l'header):
 * Panel layout (320px height, appears below header):
 *   ┌────────────────────────────────────────────────────────────┐
 *   │ 🔔 Notifiche                [🗑 Cancella] [✕]  (52px)     │ ← header panel
 *   ├────────────────────────────────────────────────────────────┤
 *   │ █ ⚠ Titolo notifica 1                         14:32       │ ← item 74px
 *   │   Testo body della notifica 1                             │
 *   ├────────────────────────────────────────────────────────────┤
 *   │ █ 🔔 Titolo notifica 2                         14:31       │
 *   │   Testo body della notifica 2                             │
 *   └────────────────────────────────────────────────────────────┘
 *   Colonna colorata a sinistra (5px): rosso=ALERT, giallo=INFO, verde=NONE
 *   Colored left column (5px): red=ALERT, yellow=INFO, green=NONE
 */

// ─────────────────────────────────────────────────────────────────────────────
// Tipo di gravità / Severity type
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Livello di gravità di una notifica.
 *        Notification severity level.
 *
 * I valori sono ordinati in modo che il confronto numerico rispecchi la
 * priorità: ALERT > INFO > NONE.
 * Values are ordered so numeric comparison reflects priority: ALERT > INFO > NONE.
 */
enum UiNotifSeverity : uint8_t {
    UI_NOTIF_NONE  = 0,   ///< Nessuna gravità / notifica di successo. No severity / success notification.
    UI_NOTIF_INFO  = 1,   ///< Informativa, attenzione. Informational, attention needed.
    UI_NOTIF_ALERT = 2,   ///< Errore critico, intervento richiesto. Critical error, action required.
};

// ─────────────────────────────────────────────────────────────────────────────
// Gestione pannello / Panel management
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Inizializza il pannello notifiche e lo allega alla schermata data.
 *        Initializes the notification panel and attaches it to the given screen.
 *
 * Deve essere chiamata ogni volta che si crea una nuova home screen.
 * Must be called every time a new home screen is created.
 *
 * Crea internamente il backdrop semitrasparente e il pannello, entrambi
 * inizialmente nascosti (LV_OBJ_FLAG_HIDDEN).
 * Internally creates the semi-transparent backdrop and the panel, both
 * initially hidden (LV_OBJ_FLAG_HIDDEN).
 *
 * @param scr     Schermata a cui allegare il pannello (di solito la home).
 *                Screen to attach the panel to (usually the home screen).
 * @param header  (non usato attualmente, riservato per estensioni future)
 *                (not currently used, reserved for future extensions)
 */
void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* header);

/**
 * @brief Rende visibile il pannello notifiche.
 *        Makes the notification panel visible.
 */
void ui_notif_panel_open(void);

/**
 * @brief Nasconde il pannello notifiche.
 *        Hides the notification panel.
 */
void ui_notif_panel_close(void);

/**
 * @brief Alterna la visibilità del pannello (apri se chiuso, chiudi se aperto).
 *        Toggles panel visibility (open if closed, close if open).
 */
void ui_notif_panel_toggle(void);

// ─────────────────────────────────────────────────────────────────────────────
// Gestione notifiche / Notification management
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Aggiunge una nuova notifica o aggiorna una esistente con la stessa chiave.
 *        Adds a new notification or updates an existing one with the same key.
 *
 * Comportamento / Behavior:
 *   - Se esiste già una notifica con la stessa key E gli stessi title/body/severity,
 *     la funzione non fa nulla (nessun rebuild UI).
 *   - If a notification with the same key AND same title/body/severity already exists,
 *     the function does nothing (no UI rebuild).
 *   - Se esiste con la stessa key ma dati diversi → aggiorna i dati e il timestamp.
 *   - If exists with same key but different data → updates data and timestamp.
 *   - Se non esiste → alloca uno slot (o sovrascrive la notifica più vecchia se pieni).
 *   - If not found → allocates a slot (or overwrites oldest if all full).
 *   - Dopo ogni modifica, ricostruisce la lista LVGL (rebuild).
 *   - After each modification, rebuilds the LVGL list (rebuild).
 *
 * @param key       Identificatore univoco stringa (es. "relay.addr1.safety").
 *                  Unique string identifier (e.g. "relay.addr1.safety").
 * @param severity  Gravità della notifica. Notification severity.
 * @param title     Titolo breve (max ~63 caratteri). Short title (max ~63 chars).
 * @param body      Testo descrittivo (max ~159 caratteri). Descriptive text (max ~159 chars).
 */
void ui_notif_push_or_update(const char* key, UiNotifSeverity severity,
                             const char* title, const char* body);

/**
 * @brief Rimuove la notifica con la chiave indicata.
 *        Removes the notification with the given key.
 *
 * Se la chiave non esiste non fa nulla.
 * If the key doesn't exist, does nothing.
 *
 * @param key  Chiave della notifica da rimuovere. Key of the notification to remove.
 */
void ui_notif_clear(const char* key);

/**
 * @brief Rimuove tutte le notifiche la cui chiave inizia con il prefisso dato.
 *        Removes all notifications whose key starts with the given prefix.
 *
 * Utile per pulire in blocco le notifiche di un dispositivo (es. "relay.addr3.").
 * Useful for bulk-removing all notifications of a device (e.g. "relay.addr3.").
 *
 * @param prefix  Prefisso da confrontare con l'inizio delle chiavi.
 *                Prefix to match against the start of keys.
 */
void ui_notif_clear_prefix(const char* prefix);

/**
 * @brief Rimuove tutte le notifiche attive.
 *        Removes all active notifications.
 */
void ui_notif_clear_all(void);

// ─────────────────────────────────────────────────────────────────────────────
// Query / Queries
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Restituisce il livello di gravità più alto tra tutte le notifiche attive.
 *        Returns the highest severity level among all active notifications.
 *
 * Utile per colorare l'icona campanella nell'header.
 * Useful for coloring the bell icon in the header.
 *
 * @return UI_NOTIF_NONE se non ci sono notifiche, altrimenti il massimo.
 * @return UI_NOTIF_NONE if no notifications, otherwise the maximum.
 */
UiNotifSeverity ui_notif_highest_severity(void);

/**
 * @brief Restituisce il numero di notifiche attive.
 *        Returns the number of active notifications.
 *
 * @return Numero di notifiche (0..24). Number of notifications (0..24).
 */
int ui_notif_count(void);
