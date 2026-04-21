#pragma once

#include "lvgl.h"

/**
 * @file ui_dc_settings.h
 * @brief Pagina Impostazioni Display Controller (target: easyconnect)
 *        Display Controller Settings page (target: easyconnect)
 *
 * Questa schermata permette la configurazione completa del sistema tramite un
 * layout a due pannelli: menu di navigazione a sinistra, contenuto a destra.
 *
 * This screen allows full system configuration via a two-panel layout:
 * navigation menu on the left, content on the right.
 *
 * Layout 1024×600:
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │  [←]   Impostazioni                                         │  ← header 60px
 *   ├────────────┬─────────────────────────────────────────────────┤
 *   │ Profilo    │                                                 │
 *   │ Display    │       Contenuto del menu selezionato            │
 *   │ Data e     │       (pannello scrollabile)                    │
 *   │ Ora        │                                                 │
 *   │ WiFi e     │                                                 │
 *   │ Rete Int.  │                                                 │
 *   │ Rete e     │                                                 │
 *   │ Sistema    │                                                 │
 *   │ Ventilaz.  │                                                 │
 *   │ Impianto   │                                                 │
 *   │ Filtraggio │                                                 │
 *   │ Aria       │                                                 │
 *   │ Sensori    │                                                 │
 *   │ Diagnostica│                                                 │
 *   └────────────┴─────────────────────────────────────────────────┘
 *   ← 300px →   ←──────────────── 724px ──────────────────────────
 *
 * Menu a sinistra (7 voci) / Left menu (7 items):
 *
 *   0. Profilo Display:
 *      - Luminosità backlight (slider 5-100%)
 *      - Screen saver timeout (3/5/10/15 min)
 *      - Unità temperatura (Celsius / Fahrenheit)
 *      - Nome impianto (popup con tastiera alfabetica)
 *
 *   1. Data e Ora:
 *      - Switch automatico/manuale (NTP se RTC presente)
 *      - Dropdown fuso orario (Europe/Rome, UTC, London, New_York)
 *      - Pulsante imposta ora (popup con dropdown hh:mm:ss)
 *      - Pulsante imposta data (popup con dropdown gg/mm/aaaa)
 *      - Righe ora/data disabilitate se auto=true
 *
 *   2. WiFi e Rete Internet:
 *      - Switch WiFi ON/OFF
 *      - Label IP e stato connessione
 *      - Lista reti SSID (tap su rete → popup password)
 *      - Aggiornata ogni 2s da timer LVGL
 *
 *   3. Rete e Sistema (RS485):
 *      - Mostra conteggio periferiche rilevate / in impianto
 *      - Pulsante "Scansiona" (avvia scansione RS485 1-200)
 *      - Pulsante "Salva impianto" (fotografia dispositivi, richiede PIN)
 *      - Pulsante "Dispositivi" → naviga a ui_dc_network_create()
 *
 *   4. Ventilazione Impianto:
 *      - Slider velocità minima ventilatore (0-90%)
 *      - Slider velocità massima ventilatore (10-100%)
 *      - Dropdown numero di step (continuo/2/3/5/7/10 step)
 *
 *   5. Filtraggio Aria:
 *      - Stato calibrazione (N punti / non calibrato)
 *      - Switch monitoraggio attivo (solo se calibrato)
 *      - Slider soglia allarme (10-90%)
 *      - Delta P live dai sensori RS485
 *      - Pulsante "Calibra" → wizard guidato a N passi
 *      - Pulsante "Azzera" (solo se calibrato)
 *      - Info modalita: step (N posizioni) o continua (5 ancoraggi, interpolaz. lineare)
 *      Persistenza: NVS namespace "easy_filt" tramite ui_filter_calib.h
 *
 *   6. Sensori Diagnostica:
 *      - Placeholder (da implementare)
 *
 * Persistenza / Persistence:
 *   - Le impostazioni sono salvate in NVS tramite ui_dc_home.h (Preferences "easy_disp")
 *     per luminosità, screensaver, temperatura, ventilazione, nome impianto.
 *   - Settings are saved to NVS via ui_dc_home.h (Preferences "easy_disp")
 *     for brightness, screensaver, temperature, ventilation, plant name.
 *   - Il fuso orario e la modalità auto sono salvati in "easy_clock" (ui_dc_clock.h).
 *   - Timezone and auto mode are saved in "easy_clock" (ui_dc_clock.h).
 *   - Le credenziali WiFi sono gestite da WiFi.begin() (NVS nativo Arduino).
 *   - WiFi credentials are managed by WiFi.begin() (Arduino native NVS).
 *
 * Navigazione / Navigation:
 *   - Il pulsante [←] nell'header torna alla Home (ui_dc_home_create)
 *   - The [←] button in the header goes back to Home (ui_dc_home_create)
 *   - Il pulsante "Dispositivi" nel menu RS485 apre ui_dc_network_create()
 *   - The "Devices" button in the RS485 menu opens ui_dc_network_create()
 */

/**
 * @brief Crea e restituisce la schermata impostazioni del Display Controller.
 *        Creates and returns the Display Controller settings screen.
 *
 * La schermata NON viene attivata automaticamente: usare lv_scr_load_anim().
 * The screen is NOT automatically activated: use lv_scr_load_anim().
 *
 * Internamente avvia timer LVGL per:
 * Internally starts LVGL timers for:
 *   - Aggiornamento data/ora nel pannello "Data e Ora" (s_dt_timer)
 *   - Date/time update in the "Date & Time" panel (s_dt_timer)
 *   - Monitoraggio stato WiFi (s_wifi_status_timer)
 *   - WiFi status monitoring (s_wifi_status_timer)
 *   - Polling scansione WiFi (s_wifi_scan_timer, quando attiva)
 *   - WiFi scan polling (s_wifi_scan_timer, when active)
 *   - Polling stato scansione RS485 (s_sys_timer)
 *   - RS485 scan status polling (s_sys_timer)
 *
 * @return Puntatore alla screen LVGL creata.
 * @return Pointer to the created LVGL screen.
 */
lv_obj_t* ui_dc_settings_create(void);
