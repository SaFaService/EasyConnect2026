#pragma once

#include "lvgl.h"

/**
 * @file ui_antralux_home.h
 * @brief Home screen demo con 4 pulsanti e tendina pull-down (drawer).
 *        Demo home screen with 4 buttons and a pull-down drawer.
 *
 * Questo file è parte della UI "demo/sandbox" Antralux, NON del target di produzione.
 * This file is part of the Antralux "demo/sandbox" UI, NOT the production target.
 *
 * La home creata da questa funzione mostra:
 * The home created by this function shows:
 *
 *   Layout 1024×600:
 *   ┌──────────────────────────────────────────────────────────────────────┐
 *   │  EasyConnect                            ↓ Scorri dall'alto          │  ← header 70px
 *   ├──────────────────────────────────────────────────────────────────────┤
 *   │          [ WiFi ]         [ Impostazioni ]                           │
 *   │          [ Notifiche ]    [ Stato ]                                  │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 *   Tendina (drawer, 220px) aperta con swipe verso il basso:
 *   Pull-down drawer (220px) opened by swiping downward:
 *   ┌──────────────────────────────────────────────────────────────────────┐
 *   │                         Hello                                   [X] │
 *   └──────────────────────────────────────────────────────────────────────┘
 *
 * Utilizzo / Usage:
 *   lv_obj_t* scr = ui_antralux_home_create();
 *   lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
 *
 * NOTA: chiamare dentro lvgl_port_lock / lvgl_port_unlock se non si è
 *       già nel task LVGL.
 * NOTE: call inside lvgl_port_lock / lvgl_port_unlock if not already
 *       in the LVGL task.
 */

/**
 * @brief Crea e restituisce la home screen Antralux demo.
 *        Creates and returns the Antralux demo home screen.
 *
 * La funzione alloca una nuova schermata LVGL con:
 * The function allocates a new LVGL screen with:
 *   - Header bianco con titolo "EasyConnect" e hint gesture
 *   - White header with "EasyConnect" title and gesture hint
 *   - Griglia 2×2 di pulsanti (WiFi, Impostazioni, Notifiche, Stato)
 *   - 2×2 grid of buttons (WiFi, Settings, Notifications, Status)
 *   - Overlay semitrasparente e drawer animato (inizialmente nascosti)
 *   - Semi-transparent overlay and animated drawer (initially hidden)
 *
 * Gestione drawer / Drawer management:
 *   - Apertura: swipe verso il basso (LV_DIR_BOTTOM) ovunque sullo schermo
 *   - Open: swipe downward (LV_DIR_BOTTOM) anywhere on screen
 *   - Chiusura: tap sull'overlay oppure pulsante [X] nel drawer
 *   - Close: tap on the overlay or [X] button inside the drawer
 *
 * @return Puntatore alla screen LVGL creata. Non chiamare lv_scr_load prima
 *         di aver inizializzato LVGL. La schermata NON viene automaticamente
 *         attivata: usare lv_scr_load o lv_scr_load_anim.
 * @return Pointer to the created LVGL screen. Do not call lv_scr_load before
 *         LVGL is initialized. The screen is NOT automatically activated:
 *         use lv_scr_load or lv_scr_load_anim.
 */
lv_obj_t* ui_antralux_home_create(void);
