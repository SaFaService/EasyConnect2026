#pragma once

#include "lvgl.h"

/**
 * @file ui_dc_home.h
 * @brief Home screen Display Controller — 4 pulsanti principali
 */

/** Crea e restituisce la home screen. */
lv_obj_t* ui_dc_home_create(void);

/**
 * Imposta la luminosità del display (5-100 %).
 * Chiama IO_EXTENSION_Pwm_Output con il valore mappato su 0-255.
 * Usare da qualsiasi schermata (settings, home, ecc.).
 */
void ui_brightness_set(int pct);
