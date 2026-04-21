#pragma once

#include "lvgl.h"
#include <stddef.h>

/**
 * @file ui_dc_home.h
 * @brief Home screen Display Controller + API impostazioni UI persistenti.
 *        Display Controller home screen + persistent UI settings API.
 *
 * Questo header espone due famiglie di funzioni:
 * This header exposes two families of functions:
 *
 * 1. SCHERMATA HOME / HOME SCREEN
 *    ui_dc_home_create() — crea e restituisce la home del Display Controller.
 *    ui_dc_home_create() — creates and returns the Display Controller home.
 *
 * 2. API IMPOSTAZIONI UI (thin wrapper su dc_settings)
 *    Le funzioni ui_*_set/get delegano a dc_settings.h, che persiste in NVS
 *    e aggiorna g_dc_model.settings. Nessun accesso diretto a Preferences qui.
 *    Wrapper functions ui_*_set/get delegate to dc_settings.h, which persists
 *    to NVS and updates g_dc_model.settings. No direct Preferences access here.
 *
 * Layout Home 1024×600:
 *   ┌──────────────────────────────────────────────────────────────────┐
 *   │  00:00:00   03 Apr 2026         🌡 25.3C   💧 60.2%RH    🔔 ⚙  │ ← header 60px
 *   ├──────────────────────────────────────────────────────────────────┤
 *   │  [Tile 1]  [Tile 2]  [Tile 3]  [Tile 4]                         │
 *   │  (relay/ventilazione/sensori dinamici da RS485)                  │
 *   │  [Pressione delta (pannello separato se dati disponibili)]       │
 *   └──────────────────────────────────────────────────────────────────┘
 *
 * Le tile della home sono generate dinamicamente a partire dai dispositivi
 * RS485 registrati nell'impianto (rs485_network.h). Se non ci sono dispositivi,
 * viene mostrato un messaggio placeholder.
 *
 * The home tiles are generated dynamically from RS485 devices registered
 * in the plant (rs485_network.h). If no devices, a placeholder is shown.
 */

// ─────────────────────────────────────────────────────────────────────────────
// Tipi / Types
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Unità di misura per la temperatura nell'interfaccia utente.
 *        Temperature unit for the user interface.
 */
enum UiTempUnit {
    UI_TEMP_C = 0,   ///< Gradi Celsius / Celsius degrees
    UI_TEMP_F = 1,   ///< Gradi Fahrenheit / Fahrenheit degrees
};

// ─────────────────────────────────────────────────────────────────────────────
// Schermata Home / Home Screen
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Crea e restituisce la home screen del Display Controller.
 *        Creates and returns the Display Controller home screen.
 *
 * Costruisce dinamicamente le tile a partire dai dispositivi RS485 in impianto.
 * Dynamically builds tiles from RS485 devices in the plant.
 *
 * Avvia i timer LVGL per:
 * Starts LVGL timers for:
 *   - Aggiornamento orologio header (ogni 1000ms)
 *   - Header clock update (every 1000ms)
 *   - Sincronizzazione stato rete RS485 e notifiche (ogni 1000ms)
 *   - RS485 network state and notifications sync (every 1000ms)
 *   - Idle dim: spegne il backlight al 10% dopo il timeout screensaver
 *   - Idle dim: dims backlight to 10% after screensaver timeout
 *
 * @return Puntatore alla screen LVGL. Non viene attivata automaticamente.
 * @return Pointer to the LVGL screen. Not automatically activated.
 */
lv_obj_t* ui_dc_home_create(void);

// ─────────────────────────────────────────────────────────────────────────────
// Luminosità / Brightness
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Carica le impostazioni salvate e applica subito la luminosità all'hardware.
 *        Loads saved settings and immediately applies brightness to hardware.
 *
 * Da chiamare nel setup(), DOPO che il backlight (IO Expander) e l'I2C sono attivi.
 * Call in setup(), AFTER the backlight (IO Expander) and I2C are active.
 *
 * Internamente chiama dc_settings_load() e dc_settings_brightness_apply_hw().
 * Internally calls dc_settings_load() and dc_settings_brightness_apply_hw().
 */
void ui_brightness_init(void);

/**
 * @brief Imposta la luminosità del backlight display.
 *        Sets the display backlight brightness.
 *
 * Il valore viene salvato in NVS e applicato immediatamente all'hardware
 * tramite IO_EXTENSION_Pwm_Output().
 * The value is saved to NVS and immediately applied to hardware
 * via IO_EXTENSION_Pwm_Output().
 *
 * NOTA: il backlight è active-low, quindi internamente il valore viene
 * invertito: 100% UI → 0% PWM (massima luminosità).
 * NOTE: the backlight is active-low, so internally the value is inverted:
 * 100% UI → 0% PWM (maximum brightness).
 *
 * @param pct  Percentuale luminosità (5-100). Clampata automaticamente.
 *             Brightness percentage (5-100). Automatically clamped.
 */
void ui_brightness_set(int pct);

/**
 * @brief Restituisce la luminosità corrente (5-100%).
 *        Returns the current brightness (5-100%).
 */
int ui_brightness_get(void);

// ─────────────────────────────────────────────────────────────────────────────
// Screensaver / Screensaver
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Imposta il timeout screensaver (idle dim) in minuti.
 *        Sets the screensaver (idle dim) timeout in minutes.
 *
 * Valori supportati: 3, 5, 10, 15. Valori non validi vengono ignorati
 * e si usa il default (IDLE_MIN_DEFAULT = 5).
 * Supported values: 3, 5, 10, 15. Invalid values are ignored
 * and the default is used (IDLE_MIN_DEFAULT = 5).
 *
 * Salvato in NVS (chiave "scr_min").
 * Saved to NVS (key "scr_min").
 *
 * @param minutes  Minuti di inattività prima del dimming. Idle minutes before dimming.
 */
void ui_screensaver_minutes_set(int minutes);

/**
 * @brief Restituisce il timeout screensaver corrente in minuti.
 *        Returns the current screensaver timeout in minutes.
 */
int ui_screensaver_minutes_get(void);

// ─────────────────────────────────────────────────────────────────────────────
// Unità temperatura / Temperature unit
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Imposta l'unità di temperatura per la visualizzazione nell'header.
 *        Sets the temperature unit for header display.
 *
 * Salva in NVS e aggiorna immediatamente le label di temperatura visibili.
 * Saves to NVS and immediately updates visible temperature labels.
 *
 * @param unit  UI_TEMP_C o UI_TEMP_F. UI_TEMP_C or UI_TEMP_F.
 */
void ui_temperature_unit_set(UiTempUnit unit);

/**
 * @brief Restituisce l'unità di temperatura corrente.
 *        Returns the current temperature unit.
 */
UiTempUnit ui_temperature_unit_get(void);

// ─────────────────────────────────────────────────────────────────────────────
// Nome impianto / Plant name
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Imposta il nome dell'impianto mostrato nella home.
 *        Sets the plant name shown in the home screen.
 *
 * Il nome viene trimmed, sanitizzato e salvato in NVS (chiave "plant_name").
 * Se vuoto dopo il trim, viene usato il default "Il mio Impianto".
 * The name is trimmed, sanitized and saved to NVS (key "plant_name").
 * If empty after trim, default "Il mio Impianto" is used.
 *
 * Lunghezza massima: 47 caratteri (k_plant_name_max_len - 1).
 * Maximum length: 47 characters (k_plant_name_max_len - 1).
 *
 * @param name  Stringa C del nuovo nome impianto. C string of new plant name.
 */
void ui_plant_name_set(const char* name);

/**
 * @brief Copia il nome impianto corrente nel buffer dato.
 *        Copies the current plant name into the given buffer.
 *
 * @param out       Buffer di destinazione. Destination buffer.
 * @param out_size  Dimensione buffer. Buffer size.
 */
void ui_plant_name_get(char* out, size_t out_size);

// ─────────────────────────────────────────────────────────────────────────────
// Ventilazione / Ventilation
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Imposta la velocità minima del ventilatore in percentuale (0-90%).
 *        Sets the minimum fan speed in percentage (0-90%).
 *
 * Se il nuovo minimo supera il massimo corrente, anche il massimo viene aggiornato.
 * If the new minimum exceeds the current maximum, the maximum is also updated.
 * Salvato in NVS (chiave "vent_min"). Saved to NVS (key "vent_min").
 *
 * @param pct  Velocità minima 0-90. Minimum speed 0-90.
 */
void ui_ventilation_min_speed_set(int pct);
int  ui_ventilation_min_speed_get(void);

/**
 * @brief Imposta la velocità massima del ventilatore in percentuale (10-100%).
 *        Sets the maximum fan speed in percentage (10-100%).
 *
 * Se il nuovo massimo è minore del minimo corrente, viene clampato al minimo.
 * If the new maximum is less than the current minimum, it is clamped to minimum.
 * Salvato in NVS (chiave "vent_max"). Saved to NVS (key "vent_max").
 *
 * @param pct  Velocità massima 10-100. Maximum speed 10-100.
 */
void ui_ventilation_max_speed_set(int pct);
int  ui_ventilation_max_speed_get(void);

/**
 * @brief Imposta il numero di step della ventilazione.
 *        Sets the ventilation step count.
 *
 * 0 = regolazione continua (nessun gradino).
 * 0 = continuous regulation (no steps).
 * Valori supportati: 0, 2, 3, 5, 7, 10. Valori non validi → 0.
 * Supported values: 0, 2, 3, 5, 7, 10. Invalid values → 0.
 * Salvato in NVS (chiave "vent_steps"). Saved to NVS (key "vent_steps").
 *
 * @param steps  Numero di step (0=continuo). Step count (0=continuous).
 */
void ui_ventilation_step_count_set(int steps);
int  ui_ventilation_step_count_get(void);

/**
 * @brief Abilita la barra separata per le 0/10V in immissione.
 *        Enables the separate bar for 0/10V intake devices.
 *
 * Usata quando nell'impianto sono presenti sia aspirazione (gruppo 1) sia
 * immissione (gruppo 2). Se false, la Home mostra solo la barra Aspirazione
 * e comanda l'immissione con la percentuale differenziale.
 * Used when both extraction (group 1) and intake (group 2) are present. If
 * false, Home shows only Extraction and links Intake with the differential.
 *
 * Salvato in NVS (chiave "imm_bar"). Saved to NVS (key "imm_bar").
 */
void ui_ventilation_intake_bar_enabled_set(bool enabled);
bool ui_ventilation_intake_bar_enabled_get(void);

/**
 * @brief Imposta la percentuale differenziale per l'immissione (25-90%).
 *        Sets the intake differential percentage (25-90%).
 *
 * Con Barra Immissione disattivata, al 100% della barra Aspirazione l'uscita
 * immissione diventa 100 - percentuale. A 0% rimane uguale al punto minimo
 * dell'aspirazione; i valori intermedi sono interpolati.
 * With Intake Bar disabled, at 100% Extraction bar the Intake output is
 * 100 - percentage. At 0% it equals the Extraction minimum; intermediate
 * values are interpolated.
 *
 * Salvato in NVS (chiave "imm_pct"). Saved to NVS (key "imm_pct").
 */
void ui_ventilation_intake_percentage_set(int pct);
int  ui_ventilation_intake_percentage_get(void);

// Salvaguardia temperatura / umidita filtro aria
void ui_air_safeguard_enabled_set(bool enabled);
bool ui_air_safeguard_enabled_get(void);
void ui_air_safeguard_temp_max_set(int temp_c);
int  ui_air_safeguard_temp_max_get(void);
void ui_air_safeguard_humidity_max_set(int hum_rh);
int  ui_air_safeguard_humidity_max_get(void);

/**
 * @brief Esegue il controllo automatico temperatura/umidita del gruppo 1.
 *
 * Da chiamare periodicamente dal controller. Usa i sensori pressione RS485 del
 * gruppo 1 come misura impianto; il sensore SHTC3 della centralina viene
 * passato come riferimento ambientale disponibile per evoluzioni diagnostiche.
 */
void ui_air_safeguard_service(float ambient_temp_c, float ambient_hum_rh, bool ambient_valid);

// ─────────────────────────────────────────────────────────────────────────────
// Dati ambientali / Environmental data
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Aggiorna i valori ambientali (temperatura e umidità) mostrati nell'header.
 *        Updates the environmental values (temperature and humidity) shown in the header.
 *
 * Questa funzione deve essere chiamata dall'esterno (es. dal layer RS485) ogni
 * volta che arrivano nuovi dati dal sensore ambientale.
 * This function should be called from outside (e.g. from the RS485 layer) every
 * time new data arrives from the environmental sensor.
 *
 * Se valid è false, le label mostrano placeholder ("-- C", "-- %RH").
 * If valid is false, labels show placeholders ("-- C", "-- %RH").
 *
 * La temperatura viene convertita automaticamente in °F se impostato.
 * Temperature is automatically converted to °F if set.
 *
 * @param temp_c  Temperatura in gradi Celsius. Temperature in Celsius.
 * @param hum_rh  Umidità relativa in % (0-100). Relative humidity in % (0-100).
 * @param valid   true se i dati sono affidabili. true if data is reliable.
 */
void ui_dc_home_set_environment(float temp_c, float hum_rh, bool valid);
