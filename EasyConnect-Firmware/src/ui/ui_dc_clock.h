#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/**
 * @file ui_dc_clock.h
 * @brief Gestione data/ora condivisa per il Display Controller.
 *        Shared date/time management for the Display Controller.
 *
 * Questo modulo implementa un orologio software persistente per il Display Controller.
 * This module implements a persistent software clock for the Display Controller.
 *
 * Strategia di inizializzazione / Initialization strategy:
 *   1. Prova a leggere data/ora dall'RTC hardware (DS3231/DS1307, I2C addr 0x68)
 *      Tries to read date/time from hardware RTC (DS3231/DS1307, I2C addr 0x68)
 *   2. Se l'RTC non è presente o non risponde, prova NTP una volta
 *      If RTC is absent or unresponsive, tries NTP once
 *   3. Se NTP non disponibile, parte da 01/01/2026 00:00:00 e conta avanti
 *      If NTP not available, starts from 01/01/2026 00:00:00 and counts forward
 *
 * Modalità automatica vs manuale / Automatic vs manual mode:
 *   - Se è presente un RTC, l'utente può attivare la modalità automatica
 *     (NTP + scrittura sull'RTC). Se non c'è RTC, la modalità automatica
 *     è forzatamente disabilitata.
 *   - If RTC is present, the user can enable automatic mode
 *     (NTP + write to RTC). If no RTC, automatic mode is forcibly disabled.
 *
 * Conteggio del tempo / Time counting:
 *   - Il clock non usa la RTC per ogni query: memorizza un epoch UTC di base
 *     (g_base_epoch_utc) e un timestamp millis() (g_base_ms), poi calcola
 *     l'ora corrente come base + elapsed_seconds.
 *   - The clock does not query the RTC for every read: it stores a base UTC epoch
 *     (g_base_epoch_utc) and a millis() timestamp (g_base_ms), then computes
 *     the current time as base + elapsed_seconds.
 *   - Questo approccio è leggero e non dipende dalla disponibilità dell'I2C.
 *   - This approach is lightweight and independent of I2C availability.
 *
 * Persistenza / Persistence:
 *   - Fuso orario e modalità auto vengono salvati in NVS con Arduino Preferences
 *     (namespace "easy_clock").
 *   - Timezone and auto mode are saved in NVS via Arduino Preferences
 *     (namespace "easy_clock").
 *
 * Thread safety / Thread safety:
 *   - Questo modulo NON è thread-safe internamente. Chiamare tutte le funzioni
 *     pubbliche solo dall'interno del task LVGL (dentro lvgl_port_lock).
 *   - This module is NOT internally thread-safe. Call all public functions
 *     only from within the LVGL task (inside lvgl_port_lock).
 */

/**
 * @brief Inizializza lo stato data/ora condiviso.
 *        Initializes the shared date/time state.
 *
 * Deve essere chiamata una sola volta nel setup(), dopo che l'I2C è attivo.
 * Must be called once in setup(), after I2C is active.
 *
 * Se chiamata più volte, le chiamate successive non hanno effetto (guard g_initialized).
 * If called multiple times, subsequent calls have no effect (g_initialized guard).
 *
 * Politica di avvio / Startup policy:
 *   - Carica preferenze (timezone, auto_en) da NVS
 *   - Loads preferences (timezone, auto_en) from NVS
 *   - Applica il fuso orario (setenv TZ + tzset)
 *   - Applies the timezone (setenv TZ + tzset)
 *   - Prova l'RTC; se presente, legge l'ora da esso
 *   - Probes RTC; if present, reads time from it
 *   - Se RTC assente: forza auto=false, tenta NTP
 *   - If RTC absent: forces auto=false, tries NTP
 *   - Se RTC presente e auto=true: tenta NTP (scrittura sull'RTC se ok)
 *   - If RTC present and auto=true: tries NTP (writes to RTC if ok)
 *   - Fallback finale: 2026-01-01 00:00:00
 *   - Final fallback: 2026-01-01 00:00:00
 */
void ui_dc_clock_init(void);

/**
 * @brief Restituisce true se è stato rilevato un RTC sull'I2C.
 *        Returns true if an RTC was detected on I2C.
 *
 * Un RTC assente NON è un errore: il sistema funziona normalmente
 * (solo senza backup di batteria dell'ora).
 * An absent RTC is NOT an error: the system works normally
 * (just without battery-backed time).
 */
bool ui_dc_clock_has_rtc(void);

/**
 * @brief Restituisce true se la modalità "data/ora automatica" è attiva.
 *        Returns true if "automatic date/time" mode is active.
 *
 * La modalità automatica usa NTP per sincronizzarsi. Richiede un RTC presente:
 * se non c'è RTC, questa funzione restituisce sempre false.
 * Automatic mode uses NTP to synchronize. Requires RTC present:
 * if no RTC, this function always returns false.
 */
bool ui_dc_clock_is_auto_enabled(void);

/**
 * @brief Abilita/disabilita la modalità data/ora automatica (NTP).
 *        Enables/disables automatic date/time mode (NTP).
 *
 * Se viene abilitata e l'RTC è presente, tenta immediatamente una
 * sincronizzazione NTP. Il risultato viene salvato in NVS.
 * If enabled and RTC is present, immediately attempts NTP sync.
 * The result is saved to NVS.
 *
 * Se l'RTC non è presente, la funzione forza enabled=false e salva.
 * If no RTC is present, the function forces enabled=false and saves.
 *
 * @param enabled  true = modalità auto, false = orario manuale.
 *                 true = automatic mode, false = manual time.
 */
void ui_dc_clock_set_auto_enabled(bool enabled);

/**
 * @brief Restituisce la stringa di opzioni per il dropdown LVGL dei fusi orari.
 *        Returns the options string for the LVGL timezone dropdown.
 *
 * Il formato è compatibile con lv_dropdown_set_options(): voci separate da '\n'.
 * The format is compatible with lv_dropdown_set_options(): entries separated by '\n'.
 *
 * Fusi supportati / Supported timezones:
 *   "Europe/Rome\nUTC\nEurope/London\nAmerica/New_York"
 *
 * @return Puntatore a stringa letterale (non modificare!).
 * @return Pointer to string literal (do not modify!).
 */
const char* ui_dc_clock_timezone_options(void);

/**
 * @brief Restituisce l'indice del fuso orario corrente nel dropdown.
 *        Returns the index of the current timezone in the dropdown.
 *
 * @return Indice 0-based. 0-based index.
 */
int ui_dc_clock_timezone_index_get(void);

/**
 * @brief Imposta il fuso orario tramite indice del dropdown.
 *        Sets the timezone by dropdown index.
 *
 * Clamp automatico al range valido [0, n_timezones-1].
 * Automatically clamped to valid range [0, n_timezones-1].
 *
 * Effetti collaterali / Side effects:
 *   - Applica il fuso orario (setenv TZ + tzset)
 *   - Applies timezone (setenv TZ + tzset)
 *   - Salva in NVS
 *   - Saves to NVS
 *   - Se auto=true, tenta una nuova sincronizzazione NTP
 *   - If auto=true, attempts a new NTP sync
 *
 * @param index  Indice del fuso orario. Timezone index.
 */
void ui_dc_clock_timezone_index_set(int index);

/**
 * @brief Legge l'ora locale corrente in una struct tm.
 *        Reads the current local time into a struct tm.
 *
 * Usa il contatore software (base_epoch + elapsed_ms), non interroga l'RTC.
 * Uses the software counter (base_epoch + elapsed_ms), does not query the RTC.
 *
 * @param out_tm  Puntatore alla struttura da riempire. Pointer to struct to fill.
 * @return true se la conversione è riuscita, false se out_tm è NULL o l'epoch < 0.
 * @return true if conversion succeeded, false if out_tm is NULL or epoch < 0.
 */
bool ui_dc_clock_get_local_tm(struct tm* out_tm);

/**
 * @brief Imposta l'ora locale manualmente (usato dai popup delle impostazioni).
 *        Sets the local time manually (used by settings popups).
 *
 * Valida tutti i parametri prima di applicare. Scrive sull'RTC se presente.
 * Validates all parameters before applying. Writes to RTC if present.
 *
 * @param year    Anno (2020-2099) / Year (2020-2099)
 * @param month   Mese 1-12 / Month 1-12
 * @param day     Giorno 1-31 (validato per il mese/anno specifico) / Day 1-31 (validated for month/year)
 * @param hour    Ore 0-23 / Hours 0-23
 * @param minute  Minuti 0-59 / Minutes 0-59
 * @param second  Secondi 0-59 / Seconds 0-59
 * @return true se tutti i parametri sono validi e l'ora è stata impostata.
 * @return true if all parameters are valid and time was set.
 */
bool ui_dc_clock_set_manual_local(int year, int month, int day,
                                  int hour, int minute, int second);

// ─────────────────────────────────────────────────────────────────────────────
// Helper di formattazione per le label UI
// Formatting helpers for UI labels
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Formatta l'ora corrente come "HH:MM:SS".
 *        Formats the current time as "HH:MM:SS".
 *
 * Scrive la stringa nel buffer out. In caso di errore, scrive "".
 * Writes the string to buffer out. On error, writes "".
 *
 * @param out       Buffer di destinazione. Destination buffer.
 * @param out_size  Dimensione del buffer (almeno 9 caratteri). Buffer size (at least 9 chars).
 */
void ui_dc_clock_format_time_hms(char* out, size_t out_size);

/**
 * @brief Formatta la data corrente come "DD/MM/YYYY".
 *        Formats the current date as "DD/MM/YYYY".
 *
 * @param out       Buffer di destinazione. Destination buffer.
 * @param out_size  Dimensione del buffer (almeno 11 caratteri). Buffer size (at least 11 chars).
 */
void ui_dc_clock_format_date_numeric(char* out, size_t out_size);

/**
 * @brief Formatta la data per la home screen come "DD Mon YYYY" (es. "03 Apr 2026").
 *        Formats the date for the home screen as "DD Mon YYYY" (e.g. "03 Apr 2026").
 *
 * Usa strftime con il pattern "%d %b %Y". Il mese viene abbreviato nella
 * lingua di sistema (di solito inglese su ESP-IDF/Arduino).
 * Uses strftime with pattern "%d %b %Y". The month is abbreviated in the
 * system locale (usually English on ESP-IDF/Arduino).
 *
 * @param out       Buffer di destinazione. Destination buffer.
 * @param out_size  Dimensione del buffer (almeno 12 caratteri). Buffer size (at least 12 chars).
 */
void ui_dc_clock_format_date_home(char* out, size_t out_size);
