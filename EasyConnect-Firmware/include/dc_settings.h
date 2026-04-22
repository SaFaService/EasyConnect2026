#pragma once

// API impostazioni persistite.
// Ogni set salva in NVS e aggiorna g_dc_model.settings.
// Ogni get legge da g_dc_model.settings (già caricato in RAM).
// NVS namespace "easy_disp" — le chiavi esistenti non cambiano mai.

#include "dc_data_model.h"
#include <stddef.h>

// ─── Lifecycle ───────────────────────────────────────────────────────────────

// Carica tutte le impostazioni da NVS e popola g_dc_model.settings.
// Chiamare in setup() prima di dc_controller_init().
void dc_settings_load(void);

// ─── Display ─────────────────────────────────────────────────────────────────

// NVS key: "br_pct"  — range 5–100, default 80
void dc_settings_brightness_set(int pct);
int  dc_settings_brightness_get(void);

// NVS key: "scr_min" — valori validi: 3, 5, 10, 15 (default 5)
void dc_settings_screensaver_set(int minutes);
int  dc_settings_screensaver_get(void);

// NVS key: "temp_u"  — DC_TEMP_C o DC_TEMP_F
void dc_settings_temp_unit_set(DcTempUnit unit);
DcTempUnit dc_settings_temp_unit_get(void);

// NVS key: "plant_name" — max 47 caratteri, default "Il mio Impianto"
void dc_settings_plant_name_set(const char* name);
void dc_settings_plant_name_get(char* out, size_t out_size);

// NVS key: "ui_theme" — 0=Classic, 1=Compact, …
void dc_settings_theme_set(uint8_t theme_id);
uint8_t dc_settings_theme_get(void);

// ─── Ventilazione ─────────────────────────────────────────────────────────────

// NVS key: "vent_min" — 0–90 %, default 20
void dc_settings_vent_min_set(int pct);
int  dc_settings_vent_min_get(void);

// NVS key: "vent_max" — 10–100 %, default 100
void dc_settings_vent_max_set(int pct);
int  dc_settings_vent_max_get(void);

// NVS key: "vent_steps" — 0=continuo, oppure 2/3/5/7/10
void dc_settings_vent_steps_set(int steps);
int  dc_settings_vent_steps_get(void);

// NVS key: "imm_bar" — true se presente barra immissione separata
void dc_settings_intake_bar_set(bool enabled);
bool dc_settings_intake_bar_get(void);

// NVS key: "imm_pct" — 25–90 %, default 25
void dc_settings_intake_diff_set(int pct);
int  dc_settings_intake_diff_get(void);

// ─── Air safeguard ────────────────────────────────────────────────────────────

// NVS key: "sg_en"
void dc_settings_safeguard_enabled_set(bool enabled);
bool dc_settings_safeguard_enabled_get(void);

// NVS key: "sg_tmax" — default 75 °C
void dc_settings_safeguard_temp_max_set(int temp_c);
int  dc_settings_safeguard_temp_max_get(void);

// NVS key: "sg_hmax" — default 85 %RH
void dc_settings_safeguard_hum_max_set(int hum_rh);
int  dc_settings_safeguard_hum_max_get(void);

// ─── Rete WiFi ───────────────────────────────────────────────────────────────

// NVS keys: "dc_wifi_enabled", "ssid", "pass" (namespace "easy")
// La password non viene mai caricata in g_dc_model — solo flag e ssid.
void dc_settings_wifi_set(bool enabled, const char* ssid, const char* pass);
bool dc_settings_wifi_enabled_get(void);
void dc_settings_wifi_ssid_get(char* out, size_t out_size);

// ─── API customer ─────────────────────────────────────────────────────────────

// NVS keys: "cust_url", "cust_key", "cust_en" (namespace "easy")
// La chiave API non viene mai caricata in g_dc_model.
void dc_settings_api_customer_set(bool enabled, const char* url, const char* key);
bool dc_settings_api_customer_enabled_get(void);
void dc_settings_api_customer_url_get(char* out, size_t out_size);

// NVS namespace: "easy_sys", key: "sys_pin_hash" (SHA-256 hex di 6 cifre)
bool dc_settings_system_pin_is_set(void);
bool dc_settings_system_pin_verify(const char* pin6);
bool dc_settings_system_pin_set(const char* pin6);

// ─── Backlight hardware ───────────────────────────────────────────────────────

// Applica la luminosità salvata all'hardware (IO Expander PWM).
// Chiamare in setup() dopo dc_settings_load() e dopo che l'IO Expander è attivo.
void dc_settings_brightness_apply_hw(void);
