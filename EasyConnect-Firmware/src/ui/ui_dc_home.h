#pragma once

#include "lvgl.h"
#include <stddef.h>

enum UiTempUnit {
    UI_TEMP_C = 0,
    UI_TEMP_F = 1,
};

/** Crea e restituisce la home screen. */
lv_obj_t* ui_dc_home_create(void);

/**
 * Carica le impostazioni salvate e applica subito la luminosita all'hardware.
 * Da chiamare nel setup(), dopo che il backlight e l'I2C sono gia' attivi.
 */
void ui_brightness_init(void);

/**
 * Imposta la luminosita del display (5-100 %).
 * Usare da qualsiasi schermata (settings, home, ecc.).
 */
void ui_brightness_set(int pct);
int ui_brightness_get(void);

/**
 * Imposta il timeout screensaver (safe dim) in minuti.
 * Valori supportati: 3, 5, 10, 15.
 */
void ui_screensaver_minutes_set(int minutes);

/** Restituisce il timeout screensaver corrente in minuti. */
int ui_screensaver_minutes_get(void);

/** Imposta/legge unita temperatura UI (C/F). */
void ui_temperature_unit_set(UiTempUnit unit);
UiTempUnit ui_temperature_unit_get(void);

/** Imposta/legge il nome impianto persistente usato nella home. */
void ui_plant_name_set(const char* name);
void ui_plant_name_get(char* out, size_t out_size);

/** Imposta/legge la velocita minima usata dalla barra ventilazione in Home. */
void ui_ventilation_min_speed_set(int pct);
int ui_ventilation_min_speed_get(void);
void ui_ventilation_max_speed_set(int pct);
int ui_ventilation_max_speed_get(void);

/**
 * 0 = regolazione continua; altrimenti numero posizioni fisse supportate:
 * 2, 3, 5, 7, 10.
 */
void ui_ventilation_step_count_set(int steps);
int ui_ventilation_step_count_get(void);

/**
 * Aggiorna i valori ambientali mostrati nell'header della Home.
 * Se valid e' false, mostra placeholder ("--").
 */
void ui_dc_home_set_environment(float temp_c, float hum_rh, bool valid);
