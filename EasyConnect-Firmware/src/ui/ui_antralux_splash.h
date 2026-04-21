#pragma once

/**
 * @file ui_antralux_splash.h
 * @brief Splash screen demo Antralux — logo fade-in + zoom, poi Home dopo 5s.
 *        Antralux demo splash screen — logo fade-in + zoom, then Home after 5s.
 *
 * Questo file è parte della UI "demo/sandbox" Antralux, NON del target di produzione.
 * This file is part of the Antralux "demo/sandbox" UI, NOT the production target.
 *
 * Sequenza animazioni / Animation sequence:
 *   t=200ms   Logo fade-in (opacità 0→255, durata 1200ms, ease-out)
 *             Logo fade-in (opacity 0→255, duration 1200ms, ease-out)
 *   t=200ms   Logo zoom-in (40%→100%, 1800ms, ease-out)
 *             Logo zoom-in (40%→100%, 1800ms, ease-out)
 *   t=1600ms  Tagline "Antralux Cloud System" fade-in (600ms, ease-out)
 *             Tagline "Antralux Cloud System" fade-in (600ms, ease-out)
 *   t=5000ms  lv_timer one-shot → carica Home con transizione fade (600ms)
 *             lv_timer one-shot → loads Home with fade transition (600ms)
 *
 * Layout 1024×600:
 *   ┌─────────────────────────────────────────────────────┐
 *   │                                                     │
 *   │         [LOGO ANTRALUX 460×184 — fade+zoom]        │
 *   │                                                     │
 *   │           "Antralux Cloud System"  (tagline)        │
 *   │                                                     │
 *   │                       v1.0.0                        │
 *   └─────────────────────────────────────────────────────┘
 *
 * Utilizzo / Usage:
 *   ui_antralux_splash_create();   // attiva immediatamente la schermata
 *                                  // immediately activates the screen
 *
 * NOTA: chiamare dentro lvgl_port_lock / lvgl_port_unlock se non si è
 *       già nel task LVGL.
 * NOTE: call inside lvgl_port_lock / lvgl_port_unlock if not already
 *       in the LVGL task.
 */

/**
 * @brief Crea, avvia le animazioni e attiva la splash screen Antralux demo.
 *        Creates, starts animations and activates the Antralux demo splash screen.
 *
 * Internamente chiama lv_scr_load() — la splash diventa subito la schermata
 * attiva. Dopo 5 secondi un timer one-shot chiama ui_antralux_home_create()
 * e carica la home con fade da 600ms.
 *
 * Internally calls lv_scr_load() — the splash immediately becomes the active
 * screen. After 5 seconds a one-shot timer calls ui_antralux_home_create()
 * and loads the home with a 600ms fade.
 */
void ui_antralux_splash_create(void);
