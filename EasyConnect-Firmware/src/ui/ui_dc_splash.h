#pragma once

/**
 * @file ui_dc_splash.h
 * @brief Splash screen Display Controller (target: easyconnect)
 *        Display Controller splash screen (target: easyconnect)
 *
 * Questa è la splash screen del firmware di produzione del Display Controller.
 * This is the production firmware splash screen for the Display Controller.
 *
 * Sequenza animazioni (~5.5s totali) / Animation sequence (~5.5s total):
 *
 *   t=200ms   Logo Antralux fade-in (0→255, 1200ms, ease-out)
 *             + zoom-in 40%→100% (1800ms, ease-out)
 *   t=2000ms  Shimmer sweep — striscia bianca scorre sinistra→destra sul logo
 *             Shimmer sweep — white strip slides left→right over the logo
 *             (1200ms, ease-in-out, clippata al container del logo)
 *             (1200ms, ease-in-out, clipped to the logo container)
 *   t=1600ms  Tagline "Easy Connect Cloud System" fade-in (600ms, ease-out)
 *   t=400ms   Progress bar 0→100% (5000ms, ease-in-out)
 *             Al completamento (~t=5400ms): carica Home senza transizione
 *             On completion (~t=5400ms): loads Home with no transition
 *             (previo polling dello stato rs485_network_boot_probe)
 *             (after polling rs485_network_boot_probe state)
 *
 * Layout 1024×600:
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │                                                              │
 *   │              [LOGO ANTRALUX 460×184]                        │
 *   │          fade-in + zoom + shimmer dorato                    │
 *   │                                                              │
 *   │           "Easy Connect Cloud System"   (tagline)           │
 *   │                                                              │
 *   │       ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░  (progress bar 640×18)  │
 *   │                                                              │
 *   │                         v1.0.0                              │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * Dipendenze / Dependencies:
 *   - DisplayLogoAsset.h  : array RGB565 del logo Antralux
 *   - ui_dc_home.h        : ui_dc_home_create() per la transizione
 *   - rs485_network.h     : rs485_network_boot_probe_start/state per
 *                           attendere che la scansione RS485 sia completata
 *
 * Utilizzo / Usage:
 *   ui_dc_splash_create();   // attiva immediatamente la schermata
 *                            // immediately activates the screen
 *
 * NOTA: il boot probe RS485 viene avviato all'interno di questa funzione
 *       (rs485_network_boot_probe_start). La transizione alla home avviene
 *       solo quando il probe è terminato O dopo il timeout della progress bar.
 * NOTE: the RS485 boot probe is started inside this function
 *       (rs485_network_boot_probe_start). The transition to home occurs
 *       only when the probe is done OR after the progress bar timeout.
 */

/**
 * @brief Crea, avvia le animazioni e attiva la splash screen del Display Controller.
 *        Creates, starts animations and activates the Display Controller splash screen.
 *
 * Effetti collaterali / Side effects:
 *   - Avvia rs485_network_boot_probe_start() per la scoperta dei dispositivi
 *   - Starts rs485_network_boot_probe_start() for device discovery
 *   - Chiama lv_scr_load() — la splash diventa immediatamente la schermata attiva
 *   - Calls lv_scr_load() — the splash immediately becomes the active screen
 *   - Registra un timer LVGL che al completamento della progress bar carica la home
 *   - Registers an LVGL timer that loads the home when the progress bar completes
 */
void ui_dc_splash_create(void);
