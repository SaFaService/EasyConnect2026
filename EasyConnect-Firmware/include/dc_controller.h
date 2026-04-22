#pragma once

// Interfaccia Controller → UI e UI → Controller.
// L'UI chiama dc_cmd_* per inviare comandi ai dispositivi.
// Il Controller chiama dc_controller_service() dal loop principale.
// Nessun header LVGL incluso qui.

#include <stdint.h>
#include <stdbool.h>

// ─── Lifecycle ───────────────────────────────────────────────────────────────

// Chiamare in setup(), dopo rs485_network_init() e dc_settings_load().
void dc_controller_init(void);

// Chiamare nel loop() principale. Aggiorna g_dc_model (network, wifi, api).
// Esegue dc_air_safeguard_service() internamente.
// temp_c / hum_rh / env_valid: lettura SHTC3 già effettuata dal caller.
void dc_controller_service(float temp_c, float hum_rh, bool env_valid);

// ─── Comandi dispositivi ─────────────────────────────────────────────────────

// Invia comando relay ON/OFF. Restituisce true se il dispositivo risponde.
bool dc_cmd_relay_set(uint8_t address, bool on);

// Abilita/disabilita motore 0/10V.
bool dc_cmd_motor_enable(uint8_t address, bool enable);

// Imposta velocità motore 0/10V (0–100 %).
bool dc_cmd_motor_speed(uint8_t address, uint8_t speed_pct);

// ─── Azioni di sistema ───────────────────────────────────────────────────────

// Avvia scansione RS485 (asincrona).
void dc_scan_rs485(void);

// Tenta riconnessione WiFi con le credenziali salvate.
void dc_wifi_reconnect(void);

// Interrompe il tentativo di connessione WiFi in corso.
void dc_wifi_abort(void);

// Verifica disponibilità aggiornamento OTA (async — risultato in g_dc_model.api).
void dc_ota_check(void);

// Avvia download e installazione OTA.
void dc_ota_start(void);

// Cancella la configurazione persistita e riavvia il controller.
void dc_factory_reset(void);

// ─── Air safeguard ───────────────────────────────────────────────────────────

// Logica di controllo safeguard temperatura/umidità gruppo 1.
// Chiamata internamente da dc_controller_service(). Esposta per test.
void dc_air_safeguard_service(void);

// ─── Boot progress (usato dalla splash condivisa) ────────────────────────────

// Imposta lo step corrente e la label mostrata nella splash.
// step: 0–10  (vedi ROADMAP.md §Task 2.1 per la tabella step/label)
void dc_boot_set_step(int step, const char* label);

// Segnala completamento boot — la splash può avviare il fade verso la home.
void dc_boot_complete(void);
