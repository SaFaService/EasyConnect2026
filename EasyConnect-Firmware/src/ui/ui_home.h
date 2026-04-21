#pragma once
/**
 * @file ui_home.h
 * @brief Home screen EasyConnect UI Sandbox — 4 tab LVGL.
 *        EasyConnect UI Sandbox home screen — 4 LVGL tabs.
 *
 * Questo file appartiene alla UI "sandbox" (dark-mode Antralux), NON al
 * firmware di produzione (target: easyconnect).
 * This file belongs to the "sandbox" UI (Antralux dark-mode), NOT to the
 * production firmware (target: easyconnect).
 *
 * La home sandbox mostra 4 tab dimostrativi / The sandbox home shows 4 demo tabs:
 *
 *   Tab 1 — Controlli / Controls:
 *     Pulsanti ON / OFF / TOGGLE per un relay simulato.
 *     ON / OFF / TOGGLE buttons for a simulated relay.
 *     Slider 0-100% con aggiornamento label in tempo reale.
 *     0-100% slider with real-time label update.
 *     3 switch con etichette (Luci, Ventilazione, Relay Aux).
 *     3 switches with labels (Lights, Ventilation, Aux Relay).
 *     Dropdown per selezione modalità scheda.
 *     Dropdown for board mode selection.
 *
 *   Tab 2 — Misure / Measurements:
 *     Arc gauge DeltaP (0-150 Pa) con aggiornamento ogni 400ms.
 *     DeltaP arc gauge (0-150 Pa) updated every 400ms.
 *     Line chart storico degli ultimi 60 campioni.
 *     Historical line chart of the last 60 samples.
 *     Il valore è simulato (onda sinusoidale 0-140 Pa).
 *     The value is simulated (sine wave 0-140 Pa).
 *
 *   Tab 3 — Touch:
 *     Visualizzatore multi-touch fino a 5 punti simultanei (GT911).
 *     Multi-touch visualizer up to 5 simultaneous points (GT911).
 *     Ogni dito è rappresentato da un cerchio colorato.
 *     Each finger is represented by a colored circle.
 *     Aggiornato ogni 50ms dal timer LVGL.
 *     Updated every 50ms by LVGL timer.
 *
 *   Tab 4 — Info:
 *     Informazioni hardware (MCU, PSRAM, display).
 *     Hardware information (MCU, PSRAM, display).
 *     Versione firmware sandbox.
 *     Sandbox firmware version.
 *     Data/ora di compilazione.
 *     Compilation date/time.
 *
 * NOTA: chiamare SEMPRE dentro lvgl_port_lock / lvgl_port_unlock.
 * NOTE: ALWAYS call inside lvgl_port_lock / lvgl_port_unlock.
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea la home screen sandbox con header + tabview a 4 tab.
 *        Creates the sandbox home screen with header + 4-tab tabview.
 *
 * La funzione:
 * The function:
 *   1. Crea una nuova schermata LVGL (NULL parent = schermata radice)
 *      Creates a new LVGL screen (NULL parent = root screen)
 *   2. Aggiunge l'header Antralux con logo e pulsante notifiche
 *      Adds the Antralux header with logo and notifications button
 *   3. Aggiunge il tabview con 4 tab e ne popola il contenuto
 *      Adds the tabview with 4 tabs and populates their content
 *   4. Inizializza il pannello notifiche (ui_notif_panel_init)
 *      Initializes the notifications panel (ui_notif_panel_init)
 *   5. Avvia i timer LVGL per misure e touch
 *      Starts LVGL timers for measurements and touch
 *
 * @return Puntatore alla screen LVGL creata. Passare a lv_scr_load_anim()
 *         per visualizzarla.
 * @return Pointer to the created LVGL screen. Pass to lv_scr_load_anim()
 *         to display it.
 */
lv_obj_t* ui_home_create(void);

#ifdef __cplusplus
}
#endif
