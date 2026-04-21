#pragma once
#include "lvgl.h"

/**
 * @file ui_dc_network.h
 * @brief Schermata lista dispositivi RS485 — "Rete Periferiche".
 *        RS485 device list screen — "Network Peripherals".
 *
 * Questa schermata mostra tutti i dispositivi RS485 conosciuti dal sistema
 * (sia quelli salvati nell'impianto che quelli solo rilevati), con il loro
 * stato attuale e la possibilità di aprire un popup di dettaglio.
 *
 * This screen shows all RS485 devices known to the system (both those saved
 * in the plant and those only detected), with their current state and the
 * ability to open a detail popup.
 *
 * Layout 1024×600:
 *   ┌──────────────────────────────────────────────────────────────────┐
 *   │  [←]   Dispositivi RS485          [stato]    [Scansiona]        │  ← header 60px
 *   ├──────────────────────────────────────────────────────────────────┤
 *   │  [icona]  IP  Seriale              Tipo            Stato         │  ← riga intestazione
 *   │  [icon]    1  20240101ABCD1        Relay           ON / Errore   │
 *   │  [icon]    3  20240101ABCD2        Sensore I2C     25.3C 60%RH   │
 *   │  [icon]    7  20240101ABCD3        0/10V (inv.)    ON 72%        │
 *   │  ...                                                             │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * Tap su una riga → popup dettaglio dispositivo (620×460) con:
 * Tap on a row → device detail popup (620×460) with:
 *   - Seriale, versione FW, stato impianto, comunicazione
 *   - Serial, FW version, plant status, communication
 *   - Dati specifici per tipo (relay: stato safety/feedback; sensore: T/H/P)
 *   - Type-specific data (relay: safety/feedback state; sensor: T/H/P)
 *   - Errori attivi
 *   - Active errors
 *   - Pulsante "Elimina periferica" (richiede PIN manutenzione)
 *   - "Delete device" button (requires maintenance PIN)
 *
 * Pulsante "Scansiona":
 *   - Avvia una nuova scansione RS485 (indirizzi 1-200)
 *   - Starts a new RS485 scan (addresses 1-200)
 *   - Disabilitato mentre la scansione è in corso
 *   - Disabled while scan is in progress
 *
 * Pulsante "Salva impianto":
 *   - Salva la fotografia attuale dei dispositivi rilevati come "impianto"
 *   - Saves the current snapshot of detected devices as the "plant"
 *   - Richiede PIN manutenzione
 *   - Requires maintenance PIN
 *
 * Aggiornamento lista:
 *   - Un timer LVGL (ogni 500ms) interroga rs485_network_get_device_count() e
 *     aggiorna la lista solo se il numero di dispositivi è cambiato rispetto
 *     all'ultima lettura (s_last_count), per evitare rebuild inutili.
 *   - An LVGL timer (every 500ms) queries rs485_network_get_device_count() and
 *     only updates the list if the device count has changed from the last read
 *     (s_last_count), to avoid unnecessary rebuilds.
 *
 * Navigazione:
 *   - Il pulsante [←] torna alla home screen (ui_dc_home_create)
 *   - The [←] button goes back to the home screen (ui_dc_home_create)
 *
 * Dipendenze / Dependencies:
 *   - rs485_network.h : Rs485Device, Rs485ScanState, API di scansione
 *   - ui_dc_home.h    : ui_dc_home_create() per la navigazione indietro
 *   - ui_dc_maintenance.h : ui_dc_maintenance_request_pin() per PIN
 *   - icons/icons_index.h : icone dispositivi (light_on, uvc_on, pressure, ecc.)
 */

/**
 * @brief Crea e restituisce la schermata lista dispositivi RS485.
 *        Creates and returns the RS485 device list screen.
 *
 * La schermata NON viene attivata automaticamente: usare lv_scr_load_anim().
 * The screen is NOT automatically activated: use lv_scr_load_anim().
 *
 * Internamente avvia un timer LVGL (s_refresh_timer, 500ms) che mantiene
 * la lista sincronizzata con lo stato della rete RS485.
 * Internally starts an LVGL timer (s_refresh_timer, 500ms) that keeps
 * the list synchronized with the RS485 network state.
 *
 * @return Puntatore alla screen LVGL creata.
 * @return Pointer to the created LVGL screen.
 */
lv_obj_t* ui_dc_network_create(void);
