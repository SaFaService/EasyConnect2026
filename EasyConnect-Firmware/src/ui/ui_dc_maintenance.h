#pragma once

#include "lvgl.h"

/**
 * @file ui_dc_maintenance.h
 * @brief Popup di autenticazione PIN per operazioni riservate alla manutenzione.
 *        PIN authentication popup for maintenance-only operations.
 *
 * Questo modulo mostra una schermata modale che richiede l'inserimento di un
 * PIN numerico prima di eseguire un'operazione privilegiata (es. eliminazione
 * di una periferica dall'impianto).
 *
 * This module displays a modal screen that requires a numeric PIN entry before
 * executing a privileged operation (e.g. removing a device from the plant).
 *
 * Layout popup (occupa tutta la schermata, 1024×600):
 * Popup layout (occupies the full screen, 1024×600):
 *
 *   ┌──────────────────────────────────────────────────────────────────────┐
 *   │ Accesso manutenzione (o titolo personalizzato)                       │  ← header 168px
 *   │ Operazione riservata alla manutenzione. Inserire il PIN...           │
 *   │                                     [Annulla]  [Conferma / azione]  │
 *   │ ████████████████████████████████ (textarea PIN, password mode)       │
 *   │ "PIN non valido"  (solo se errore)                                   │
 *   ├──────────────────────────────────────────────────────────────────────┤
 *   │                                                                      │
 *   │  [tastiera numerica LVGL  1024×432]                                  │  ← keyboard
 *   │                                                                      │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 * Comportamento / Behavior:
 *   - Il popup viene sovrapposto alla schermata corrente (lv_obj_move_foreground)
 *   - The popup is overlaid on the current screen (lv_obj_move_foreground)
 *   - L'overlay scuro (70% opacità) blocca l'interazione con lo sfondo
 *   - The dark overlay (70% opacity) blocks interaction with the background
 *   - Il PIN è hardcoded ("0805"), solo cifre, max 4 caratteri, modalità password
 *   - PIN is hardcoded ("0805"), digits only, max 4 chars, password mode
 *   - Se il PIN è corretto: chiude il popup e chiama on_success(user_data)
 *   - If PIN is correct: closes popup and calls on_success(user_data)
 *   - Se il PIN è errato: mostra "PIN non valido" e svuota il campo
 *   - If PIN is wrong: shows "PIN non valido" and clears the field
 *   - Il tasto [Annulla] o il tasto CANCEL della tastiera chiude senza azione
 *   - [Annulla] button or keyboard CANCEL key closes without action
 *
 * Gestione istanza singola / Single instance management:
 *   - Può essere aperto un solo popup alla volta (s_ctx singleton)
 *   - Only one popup can be open at a time (s_ctx singleton)
 *   - Se viene chiamato mentre un popup è già aperto, quello vecchio viene
 *     eliminato e sostituito dal nuovo
 *   - If called while a popup is already open, the old one is deleted and
 *     replaced by the new one
 *
 * Memoria / Memory:
 *   - Il contesto (MaintenancePinCtx) viene allocato con new e liberato
 *     automaticamente nell'evento LV_EVENT_DELETE del mask padre
 *   - Context (MaintenancePinCtx) is allocated with new and freed
 *     automatically in the LV_EVENT_DELETE event of the parent mask
 */

/**
 * @brief Tipo del callback chiamato quando il PIN è corretto.
 *        Type of the callback called when the PIN is correct.
 *
 * Il callback viene chiamato DOPO la chiusura del popup.
 * The callback is called AFTER the popup is closed.
 *
 * @param user_data  Dati utente passati a ui_dc_maintenance_request_pin.
 *                   User data passed to ui_dc_maintenance_request_pin.
 */
typedef void (*UiDcMaintenanceSuccessCb)(void* user_data);

/**
 * @brief Apre il popup di autenticazione PIN per un'operazione di manutenzione.
 *        Opens the PIN authentication popup for a maintenance operation.
 *
 * La funzione è non-bloccante: il popup viene creato e mostrato immediatamente,
 * poi l'esecuzione ritorna al chiamante. L'azione viene eseguita in modo asincrono
 * tramite il callback on_success quando l'utente inserisce il PIN corretto.
 *
 * The function is non-blocking: the popup is created and shown immediately,
 * then execution returns to the caller. The action is executed asynchronously
 * via the on_success callback when the user enters the correct PIN.
 *
 * @param action_title  Titolo mostrato nel popup (es. "Eliminazione periferica").
 *                      Se NULL o "", usa "Accesso manutenzione".
 *                      Title shown in the popup (e.g. "Device Deletion").
 *                      If NULL or "", uses "Accesso manutenzione".
 *
 * @param action_label  Testo del pulsante di conferma (es. "Elimina periferica").
 *                      Se NULL o "", usa "Conferma".
 *                      Text of the confirm button (e.g. "Delete device").
 *                      If NULL or "", uses "Conferma".
 *
 * @param on_success    Callback chiamato se il PIN è corretto. Può essere NULL
 *                      (il popup si chiude comunque).
 *                      Callback called if PIN is correct. Can be NULL
 *                      (popup still closes).
 *
 * @param user_data     Puntatore opaco passato al callback on_success.
 *                      Opaque pointer passed to the on_success callback.
 *                      ATTENZIONE: se allochi memoria per user_data, liberala
 *                      all'interno del callback on_success.
 *                      WARNING: if you allocate memory for user_data, free it
 *                      inside the on_success callback.
 */
void ui_dc_maintenance_request_pin(const char* action_title,
                                   const char* action_label,
                                   UiDcMaintenanceSuccessCb on_success,
                                   void* user_data);
