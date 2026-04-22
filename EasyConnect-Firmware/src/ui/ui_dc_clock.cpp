/**
 * @file ui_dc_clock.cpp
 * @brief Modulo orologio / data per il Display Controller
 *        Clock / date module for the Display Controller
 *
 * Questo modulo gestisce l'ora corrente con tre strategie a cascata:
 * This module manages the current time with three cascading strategies:
 *
 *   1. **RTC hardware** (DS3231/DS1307 su I2C, addr 0x68):
 *      Se il chip è presente e risponde, fornisce l'ora anche senza rete.
 *      If the chip is present and responds, it provides time even without network.
 *
 *   2. **NTP via WiFi** (`configTzTime` + `getLocalTime`):
 *      Se la rete è disponibile, sincronizza e, se presente, aggiorna l'RTC.
 *      If network is available, synchronizes and, if present, updates the RTC.
 *
 *   3. **Contatore software** (epoch base + `millis()` delta):
 *      Se né RTC né NTP sono disponibili, usa un orologio interno derivato
 *      da un'epoca base (default: 2026-01-01 00:00:00) più i millisecondi trascorsi.
 *      If neither RTC nor NTP are available, uses an internal clock derived from
 *      a base epoch (default: 2026-01-01 00:00:00) plus elapsed milliseconds.
 *
 * Persistenza / Persistence:
 *   - Timezone index e flag auto_enabled sono salvati in NVS via Arduino `Preferences`.
 *   - Namespace NVS: "easy_clock" / NVS namespace: "easy_clock"
 *   - Chiavi: "tz_idx" (uint8), "auto_en" (bool) / Keys: "tz_idx" (uint8), "auto_en" (bool)
 *
 * Timezone / Timezone:
 *   - Gestione tramite `setenv("TZ", ...)` + `tzset()` (POSIX standard)
 *   - Managed via `setenv("TZ", ...)` + `tzset()` (POSIX standard)
 *   - 4 timezone predefiniti: Europe/Rome, UTC, Europe/London, America/New_York
 *   - 4 predefined timezones: Europe/Rome, UTC, Europe/London, America/New_York
 *
 * Dipendenze / Dependencies:
 *   - Arduino.h            → `millis()`, `delay()`, `setenv()`, `tzset()`
 *   - Preferences.h        → NVS key-value store (namespace "easy_clock")
 *   - display_port/i2c.h   → `DEV_I2C_Get_Bus()`, `DEV_I2C_Set_Slave_Addr()`,
 *                            `i2c_master_transmit_receive()`, `i2c_master_transmit()`
 *   - time.h / sys/time.h  → `time_t`, `struct tm`, `mktime()`, `localtime_r()`, `strftime()`
 */

#include "ui_dc_clock.h"

#include <Arduino.h>
#include <Preferences.h>

#include "display_port/i2c.h"

// ─── Namespace anonimo — tutto ciò che è qui è privato a questo file ─────────
// Anonymous namespace — everything here is private to this translation unit.
// Equivalente a `static` per file C, ma più idiomatico in C++.
// Equivalent to `static` for C files, but more idiomatic in C++.
namespace {

// ─── Struttura timezone ──────────────────────────────────────────────────────

/**
 * @brief Coppia (nome leggibile, stringa TZ POSIX) per un timezone
 *        Pair (display name, POSIX TZ string) for a timezone
 *
 * La stringa TZ POSIX (`tz`) è passata direttamente a `setenv("TZ", ...)`.
 * Formato: "STD offset DST,start/time,end/time" — es. CET-1CEST,M3.5.0/2,M10.5.0/3
 * The POSIX TZ string (`tz`) is passed directly to `setenv("TZ", ...)`.
 * Format: "STD offset DST,start/time,end/time" — e.g. CET-1CEST,M3.5.0/2,M10.5.0/3
 */
struct TimeZoneOption {
    const char* name;  ///< Nome leggibile per UI / Display name for UI
    const char* tz;    ///< Stringa TZ POSIX / POSIX TZ string
};

/**
 * @brief Array dei timezone supportati / Array of supported timezones
 *
 * Indice corrisponde a "tz_idx" salvato in NVS.
 * Index corresponds to "tz_idx" saved in NVS.
 * Dettaglio stringhe POSIX / POSIX string details:
 *   - "CET-1CEST,M3.5.0/2,M10.5.0/3" → Europa Centrale (Roma/Milano)
 *     CET = ora invernale UTC+1, CEST = ora legale UTC+2
 *     Inizia ultimo dom. di marzo alle 2:00, finisce ultimo dom. di ottobre alle 3:00
 *   - "UTC0" → Coordinato Universale, nessun offset, nessuna ora legale
 *   - "GMT0BST,M3.5.0/1,M10.5.0/2" → Londra (UTC+0 invernale, UTC+1 estivo)
 *   - "EST5EDT,M3.2.0/2,M11.1.0/2" → New York (UTC-5 invernale, UTC-4 estivo)
 */
static constexpr TimeZoneOption k_timezones[] = {
    {"Europe/Rome",      "CET-1CEST,M3.5.0/2,M10.5.0/3"},   // UTC+1/UTC+2 DST
    {"UTC",              "UTC0"},                              // Nessun offset / No offset
    {"Europe/London",    "GMT0BST,M3.5.0/1,M10.5.0/2"},      // UTC+0/UTC+1 DST
    {"America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2"},      // UTC-5/UTC-4 DST
};

/** @brief Numero di timezone supportati (calcolato staticamente) / Count of supported timezones (statically computed) */
static constexpr int k_timezone_count = (int)(sizeof(k_timezones) / sizeof(k_timezones[0]));

/** @brief Indirizzo I2C del chip RTC DS3231/DS1307 (standard industriale) / I2C address of DS3231/DS1307 RTC chip (industry standard) */
static constexpr uint8_t k_rtc_addr = 0x68;  // DS3231/DS1307 compatible

/**
 * @brief Stringa opzioni dropdown LVGL per la selezione timezone
 *        LVGL dropdown options string for timezone selection
 *
 * Le opzioni LVGL per `lv_dropdown` sono separate da '\n'.
 * LVGL options for `lv_dropdown` are separated by '\n'.
 * Deve corrispondere (per indice) all'array k_timezones.
 * Must correspond (by index) to the k_timezones array.
 */
static const char* k_tz_dropdown_opts =
    "Europe/Rome\n"
    "UTC\n"
    "Europe/London\n"
    "America/New_York";

// ─── Stato del modulo (variabili di istanza "globali" del modulo) ─────────────
// Module state (module-level "instance" variables — equivalent to private members)

/** @brief Handle NVS Preferences per il namespace "easy_clock" / NVS Preferences handle for "easy_clock" namespace */
static Preferences g_clock_pref;

/** @brief Indica se le Preferences sono state aperte con successo / Whether Preferences were opened successfully */
static bool g_pref_ready = false;

/** @brief Flag di inizializzazione — impedisce double-init / Initialization flag — prevents double-init */
static bool g_initialized = false;

/** @brief `true` se il chip RTC DS3231/DS1307 è stato trovato sul bus I2C / `true` if RTC DS3231/DS1307 chip was found on I2C bus */
static bool g_has_rtc = false;

/** @brief `true` se la sincronizzazione automatica (NTP) è abilitata / `true` if automatic sync (NTP) is enabled */
static bool g_auto_enabled = true;

/** @brief Indice corrente nella tabella k_timezones / Current index in the k_timezones table */
static int  g_tz_index = 0;

/** @brief Handle del device RTC sul bus I2C master (NULL se non presente) / I2C master device handle for RTC (NULL if not present) */
static i2c_master_dev_handle_t g_rtc_dev = NULL;

// ─── Variabili del contatore software ────────────────────────────────────────
// Software counter variables — implementano l'orologio "virtuale" senza RTC.
// Implement the "virtual" clock without RTC.
// L'ora corrente è: g_base_epoch_utc + (millis() - g_base_ms) / 1000
// Current time is:  g_base_epoch_utc + (millis() - g_base_ms) / 1000

/** @brief Epoch UTC di riferimento (secondi da 1970-01-01) / Reference UTC epoch (seconds since 1970-01-01) */
static time_t g_base_epoch_utc = 0;

/** @brief Valore di `millis()` al momento in cui g_base_epoch_utc è stato impostato / `millis()` value when g_base_epoch_utc was set */
static uint32_t g_base_ms = 0;

// ─── Helper BCD (Binary Coded Decimal) ───────────────────────────────────────
// I registri del DS3231/DS1307 usano BCD per memorizzare ore, minuti, secondi, ecc.
// DS3231/DS1307 registers use BCD to store hours, minutes, seconds, etc.
// BCD: la cifra delle decine è nei 4 bit alti, la cifra delle unità nei 4 bit bassi.
// BCD: tens digit in upper 4 bits, units digit in lower 4 bits.
// Esempio: 23 in BCD = 0x23 = 0b00100011 (2 nelle decine, 3 nelle unità)
// Example: 23 in BCD = 0x23 = 0b00100011 (2 in tens, 3 in units)

/**
 * @brief Converte un valore decimale in BCD / Converts a decimal value to BCD
 * @param value  Valore decimale [0…99] / Decimal value [0…99]
 * @return Valore BCD corrispondente / Corresponding BCD value
 */
static uint8_t _to_bcd(uint8_t value) {
    // (valore / 10) sono le decine → spostato di 4 bit a sinistra
    // (valore % 10) sono le unità → nei 4 bit bassi
    // (value / 10) is the tens digit → shifted 4 bits left
    // (value % 10) is the units digit → in lower 4 bits
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

/**
 * @brief Converte un valore BCD in decimale / Converts a BCD value to decimal
 * @param value  Valore BCD / BCD value
 * @return Valore decimale corrispondente / Corresponding decimal value
 */
static uint8_t _from_bcd(uint8_t value) {
    // Bit 7-4: decine → moltiplica per 10
    // Bit 3-0: unità → aggiunge direttamente
    // Bits 7-4: tens → multiply by 10
    // Bits 3-0: units → add directly
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

// ─── Helper calendario ────────────────────────────────────────────────────────
// Calendar helpers — usati per validare la data letta dall'RTC.
// Calendar helpers — used to validate the date read from the RTC.

/**
 * @brief Determina se un anno è bisestile / Determines if a year is a leap year
 *
 * Regole anno bisestile (calendario Gregoriano):
 * Leap year rules (Gregorian calendar):
 *   - Divisibile per 4: bisestile         / Divisible by 4: leap year
 *   - Divisibile per 100: NON bisestile   / Divisible by 100: NOT a leap year
 *   - Divisibile per 400: bisestile       / Divisible by 400: IS a leap year
 *
 * @param year  Anno (es. 2024) / Year (e.g. 2024)
 * @return `true` se l'anno è bisestile / `true` if the year is a leap year
 */
static bool _is_leap_year(int year) {
    if ((year % 4) != 0) return false;    // Non div. per 4 → non bisestile
    if ((year % 100) != 0) return true;   // Div. per 4, non per 100 → bisestile
    return (year % 400) == 0;             // Div. per 400 → bisestile, altrimenti no
}

/**
 * @brief Restituisce il numero di giorni in un mese / Returns the number of days in a month
 *
 * Tiene conto degli anni bisestili per febbraio.
 * Takes leap years into account for February.
 *
 * @param year   Anno (per determinare se bisestile) / Year (to determine if leap)
 * @param month  Mese [1…12] / Month [1…12]
 * @return Numero di giorni nel mese / Number of days in the month
 */
static int _days_in_month(int year, int month) {
    // Giorni per ogni mese (febbraio = 28 di default)
    // Days for each month (February = 28 by default)
    static const int k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;  // Fallback per mese non valido
    if (month == 2 && _is_leap_year(year)) return 29;  // Febbraio anno bisestile
    return k_days[month - 1];               // month-1 perché l'array è 0-indexed
}

// ─── Gestione timezone ────────────────────────────────────────────────────────

/**
 * @brief Applica il timezone corrente al sistema POSIX
 *        Applies the current timezone to the POSIX system
 *
 * Chiama `setenv("TZ", ...)` con la stringa POSIX del timezone corrente,
 * poi `tzset()` per aggiornare le strutture interne della libc.
 * Calls `setenv("TZ", ...)` with the POSIX string of the current timezone,
 * then `tzset()` to update the internal libc structures.
 *
 * Dopo questa chiamata, `localtime_r()` e `mktime()` useranno il nuovo timezone.
 * After this call, `localtime_r()` and `mktime()` will use the new timezone.
 */
static void _apply_timezone() {
    // Clamp dell'indice per sicurezza (non dovrebbe mai essere fuori range)
    // Index clamp for safety (should never be out of range)
    if (g_tz_index < 0) g_tz_index = 0;
    if (g_tz_index >= k_timezone_count) g_tz_index = k_timezone_count - 1;

    // setenv: imposta la variabile d'ambiente TZ nel processo
    // setenv: sets the TZ environment variable in the process
    // 1 = overwrite se già esiste / 1 = overwrite if already exists
    setenv("TZ", k_timezones[g_tz_index].tz, 1);

    // tzset: aggiorna le variabili interne della libc (timezone, daylight, tzname)
    // tzset: updates the internal libc variables (timezone, daylight, tzname)
    tzset();
}

// ─── NVS Preferences ─────────────────────────────────────────────────────────
// Persistenza delle impostazioni dell'orologio su flash NVS.
// Persistence of clock settings on NVS flash.

/**
 * @brief Carica le preferenze dell'orologio da NVS
 *        Loads clock preferences from NVS
 *
 * Apre il namespace "easy_clock" in modalità lettura/scrittura (`false` = R/W).
 * Opens the "easy_clock" namespace in read/write mode (`false` = R/W).
 * Legge `tz_idx` (default 0 = Europe/Rome) e `auto_en` (default true).
 * Reads `tz_idx` (default 0 = Europe/Rome) and `auto_en` (default true).
 * Se le Preferences non sono disponibili, usa i valori di default.
 * If Preferences are not available, uses default values.
 */
static void _load_prefs() {
    // Apre il namespace solo se non era già aperto
    // Opens the namespace only if not already open
    if (!g_pref_ready) {
        g_pref_ready = g_clock_pref.begin("easy_clock", false);  // false = R/W mode
    }
    if (!g_pref_ready) {
        // NVS non disponibile: usa valori di default e ritorna
        // NVS not available: use defaults and return
        g_tz_index = 0;
        g_auto_enabled = true;
        return;
    }

    // Legge l'indice timezone (uint8 per risparmiare spazio NVS)
    // Reads the timezone index (uint8 to save NVS space)
    g_tz_index = (int)g_clock_pref.getUChar("tz_idx", 0);
    // Clamp di sicurezza in caso di dato corrotto
    // Safety clamp in case of corrupted data
    if (g_tz_index < 0 || g_tz_index >= k_timezone_count) g_tz_index = 0;

    // Legge il flag di sincronizzazione automatica
    // Reads the automatic sync flag
    g_auto_enabled = g_clock_pref.getBool("auto_en", true);
}

/**
 * @brief Salva l'indice timezone corrente in NVS / Saves the current timezone index to NVS
 *
 * Chiamata ogni volta che `g_tz_index` cambia.
 * Called every time `g_tz_index` changes.
 */
static void _save_timezone_pref() {
    if (!g_pref_ready) return;  // NVS non disponibile, skip silenzioso
    g_clock_pref.putUChar("tz_idx", (uint8_t)g_tz_index);
}

/**
 * @brief Salva il flag auto_enabled corrente in NVS / Saves the current auto_enabled flag to NVS
 *
 * Chiamata ogni volta che `g_auto_enabled` cambia.
 * Called every time `g_auto_enabled` changes.
 */
static void _save_auto_pref() {
    if (!g_pref_ready) return;  // NVS non disponibile, skip silenzioso
    g_clock_pref.putBool("auto_en", g_auto_enabled);
}

// ─── Contatore software ───────────────────────────────────────────────────────
// Software counter — il "cuore" dell'orologio quando non c'è RTC né NTP.
// The "heart" of the clock when there is neither RTC nor NTP.

/**
 * @brief Calcola l'epoch UTC corrente tramite il contatore software
 *        Calculates the current UTC epoch via the software counter
 *
 * Formula: g_base_epoch_utc + (millis() - g_base_ms) / 1000
 * I millisecondi vengono convertiti in secondi interi (troncamento).
 * Milliseconds are converted to whole seconds (truncation).
 *
 * NOTA: `millis()` si resetta a 0 ogni ~49.7 giorni (uint32 overflow).
 * Per uso tipico (boot → splash → display), questo non è un problema.
 * NOTE: `millis()` resets to 0 every ~49.7 days (uint32 overflow).
 * For typical use (boot → splash → display), this is not a problem.
 *
 * @return Epoch UTC corrente in secondi / Current UTC epoch in seconds
 */
static time_t _current_epoch_utc() {
    // Calcola i secondi trascorsi dall'ultima sincronizzazione
    // Calculate seconds elapsed since last synchronization
    const uint32_t elapsed_s = (uint32_t)((millis() - g_base_ms) / 1000U);
    return (time_t)(g_base_epoch_utc + (time_t)elapsed_s);
}

/**
 * @brief Imposta l'epoch base del contatore software
 *        Sets the base epoch of the software counter
 *
 * "Ancora" il contatore all'epoch UTC fornito e salva il valore corrente di `millis()`.
 * "Anchors" the counter to the provided UTC epoch and saves the current `millis()` value.
 * Ogni chiamata successiva a `_current_epoch_utc()` calcolerà il delta da questo momento.
 * Every subsequent call to `_current_epoch_utc()` will calculate the delta from this moment.
 *
 * @param epoch_utc  Epoch UTC da impostare come base / UTC epoch to set as base
 */
static void _set_base_epoch_utc(time_t epoch_utc) {
    g_base_epoch_utc = epoch_utc;
    g_base_ms = millis();  // "Segna" il momento corrente
}

/**
 * @brief Imposta l'orologio da una `struct tm` in ora locale
 *        Sets the clock from a `struct tm` in local time
 *
 * Converte la struct tm locale in epoch UTC tramite `mktime()` (che rispetta
 * il timezone corrente impostato da `_apply_timezone()`), poi imposta la base.
 * Converts the local tm struct to UTC epoch via `mktime()` (which respects the
 * current timezone set by `_apply_timezone()`), then sets the base.
 *
 * @param local_tm  Puntatore alla struttura tm in ora locale / Pointer to local time tm struct
 * @return `true` se la conversione ha avuto successo / `true` if conversion succeeded
 */
static bool _set_from_local_tm(struct tm* local_tm) {
    if (!local_tm) return false;

    // tm_isdst = -1: chiede a mktime di determinare automaticamente l'ora legale
    // tm_isdst = -1: asks mktime to automatically determine daylight saving time
    local_tm->tm_isdst = -1;

    // mktime() converte l'ora locale in epoch POSIX (secondi da 1970-01-01 UTC)
    // mktime() converts local time to POSIX epoch (seconds from 1970-01-01 UTC)
    const time_t epoch = mktime(local_tm);
    if (epoch < 0) return false;  // Conversione fallita (data non valida)

    _set_base_epoch_utc(epoch);
    return true;
}

static bool _tm_to_epoch_local(const struct tm& tm_local, time_t* out_epoch) {
    struct tm tmp = tm_local;
    tmp.tm_isdst = -1;
    const time_t epoch = mktime(&tmp);
    if (epoch < 0) return false;
    if (out_epoch) *out_epoch = epoch;
    return true;
}

// ─── Driver RTC DS3231/DS1307 ─────────────────────────────────────────────────
// Driver per la comunicazione I2C con il chip RTC.
// Driver for I2C communication with the RTC chip.
// Il DS3231 ha 7 registri di data/ora a partire dall'indirizzo 0x00:
// The DS3231 has 7 date/time registers starting at address 0x00:
//   0x00: Secondi (BCD, bit 7 = CH stop clock)
//   0x01: Minuti (BCD)
//   0x02: Ore (BCD, bit 6 = 12h mode, bit 5 = AM/PM)
//   0x03: Giorno settimana (1=Lun, …, 7=Dom)
//   0x04: Giorno mese (BCD)
//   0x05: Mese (BCD, bit 7 = century)
//   0x06: Anno (BCD, anno dal 2000)

/**
 * @brief Legge N registri dall'RTC partendo da `reg`
 *        Reads N registers from the RTC starting at `reg`
 *
 * Usa una trasmissione I2C combinata: write(reg) → read(N bytes).
 * Uses a combined I2C transaction: write(reg) → read(N bytes).
 *
 * @param reg   Indirizzo registro di partenza / Starting register address
 * @param out   Buffer di output / Output buffer
 * @param len   Numero di byte da leggere / Number of bytes to read
 * @return `true` se la lettura ha avuto successo / `true` if read succeeded
 */
static bool _rtc_read_regs(uint8_t reg, uint8_t* out, size_t len) {
    if (!g_rtc_dev || !out || len == 0) return false;
    // i2c_master_transmit_receive: prima invia `reg`, poi legge `len` byte
    // i2c_master_transmit_receive: first sends `reg`, then reads `len` bytes
    // 120 = timeout in millisecondi / timeout in milliseconds
    const esp_err_t err = i2c_master_transmit_receive(g_rtc_dev, &reg, 1, out, len, 120);
    return err == ESP_OK;
}

/**
 * @brief Scrive N registri sull'RTC partendo da `reg`
 *        Writes N registers to the RTC starting at `reg`
 *
 * Il protocollo DS3231 richiede di inviare prima l'indirizzo registro,
 * poi i dati, in un unico frame I2C.
 * The DS3231 protocol requires sending the register address first, then data,
 * in a single I2C frame.
 *
 * @param reg   Indirizzo registro di partenza / Starting register address
 * @param data  Dati da scrivere / Data to write
 * @param len   Numero di byte da scrivere (max 7) / Number of bytes to write (max 7)
 * @return `true` se la scrittura ha avuto successo / `true` if write succeeded
 */
static bool _rtc_write_regs(uint8_t reg, const uint8_t* data, size_t len) {
    if (!g_rtc_dev || !data || len == 0) return false;

    // payload[0] = indirizzo registro, payload[1..n] = dati
    // payload[0] = register address, payload[1..n] = data
    uint8_t payload[1 + 7] = {0};  // Max 7 registri data/ora + 1 byte indirizzo
    if (len > 7) return false;       // Protezione overflow buffer
    payload[0] = reg;
    for (size_t i = 0; i < len; i++) payload[1 + i] = data[i];

    const esp_err_t err = i2c_master_transmit(g_rtc_dev, payload, len + 1, 120);
    return err == ESP_OK;
}

/**
 * @brief Registra il device RTC sul bus I2C master (se non già fatto)
 *        Registers the RTC device on the I2C master bus (if not already done)
 *
 * Usa `DEV_I2C_Get_Bus()` per ottenere il bus I2C già inizializzato dal driver display,
 * poi `DEV_I2C_Set_Slave_Addr()` per creare l'handle del device all'indirizzo 0x68.
 * Uses `DEV_I2C_Get_Bus()` to get the I2C bus already initialized by the display driver,
 * then `DEV_I2C_Set_Slave_Addr()` to create the device handle at address 0x68.
 *
 * @return `true` se il device è pronto / `true` if device is ready
 */
static bool _rtc_attach_device() {
    if (g_rtc_dev) return true;             // Già registrato / Already registered
    if (!DEV_I2C_Get_Bus()) return false;   // Bus I2C non disponibile / I2C bus not available
    DEV_I2C_Set_Slave_Addr(&g_rtc_dev, k_rtc_addr);  // Crea handle per addr 0x68
    return g_rtc_dev != NULL;
}

/**
 * @brief Testa la presenza del chip RTC sul bus I2C
 *        Tests the presence of the RTC chip on the I2C bus
 *
 * Tenta di leggere il registro 0x00 (secondi) e verifica che il valore decodificato
 * sia un numero valido di secondi [0…59]. Questo è il "probe" di esistenza del chip.
 * Attempts to read register 0x00 (seconds) and verifies that the decoded value
 * is a valid seconds number [0…59]. This is the chip "existence probe".
 *
 * Nota: il bit 7 del registro 0x00 del DS1307 è il "CH" (Clock Halt).
 * Se CH=1 il clock è fermo, ma il chip è ancora presente.
 * Usiamo la maschera 0x7F per ignorare il bit CH.
 * Note: bit 7 of DS1307 register 0x00 is the "CH" (Clock Halt) bit.
 * If CH=1 the clock is halted, but the chip is still present.
 * We use mask 0x7F to ignore the CH bit.
 *
 * @return `true` se il chip è presente e funzionante / `true` if chip is present and working
 */
static bool _rtc_probe() {
    if (!_rtc_attach_device()) return false;  // Registra device prima
    uint8_t sec = 0;
    if (!_rtc_read_regs(0x00, &sec, 1)) return false;  // Leggi registro secondi
    // Bit 7 = CH (Clock Halt) nel DS1307, maschera 0x7F per il valore BCD
    // Bit 7 = CH (Clock Halt) in DS1307, mask 0x7F for BCD value
    const uint8_t sec_bcd = (uint8_t)(sec & 0x7F);
    // Verifica che il valore decodificato sia un secondo valido
    // Verify that the decoded value is a valid second
    return _from_bcd(sec_bcd) <= 59;
}

/**
 * @brief Legge l'ora dall'RTC e la restituisce come `struct tm` in ora locale
 *        Reads the time from the RTC and returns it as a `struct tm` in local time
 *
 * Legge i 7 registri 0x00-0x06, decodifica BCD, gestisce sia la modalità 12h che 24h,
 * valida i valori e popola la `struct tm`.
 * Reads the 7 registers 0x00-0x06, decodes BCD, handles both 12h and 24h mode,
 * validates values and populates the `struct tm`.
 *
 * La `struct tm` risultante è in **ora locale** (non UTC).
 * The resulting `struct tm` is in **local time** (not UTC).
 *
 * @param out_tm  Puntatore alla struttura di output / Pointer to output structure
 * @return `true` se la lettura e la validazione hanno avuto successo / `true` if read and validation succeeded
 */
static bool _rtc_read_local_tm(struct tm* out_tm) {
    if (!out_tm) return false;

    // Legge i 7 registri data/ora dell'RTC
    // Read the 7 date/time registers of the RTC
    uint8_t raw[7] = {0};
    if (!_rtc_read_regs(0x00, raw, sizeof(raw))) return false;

    // ── Decodifica ore, minuti, secondi ────────────────────────────────────
    // Secondi: bit 7 = CH (Clock Halt), maschera 0x7F
    // Seconds: bit 7 = CH (Clock Halt), mask 0x7F
    int second = (int)_from_bcd((uint8_t)(raw[0] & 0x7F));

    // Minuti: bit 7 non usato, maschera 0x7F per sicurezza
    // Minutes: bit 7 unused, mask 0x7F for safety
    int minute = (int)_from_bcd((uint8_t)(raw[1] & 0x7F));

    int hour = 0;
    if ((raw[2] & 0x40) != 0) {
        // Modalità 12 ore (bit 6 = 1)
        // 12-hour mode (bit 6 = 1)
        // Bit 5: AM (0) / PM (1)
        // Bit 4-0: ore 1-12 in BCD
        int h12 = (int)_from_bcd((uint8_t)(raw[2] & 0x1F));  // Ore 1-12
        if (h12 == 12) h12 = 0;  // 12 AM = 0, 12 PM = 12
        // Se PM (bit 5 = 1): aggiungi 12 → formato 24h
        // If PM (bit 5 = 1): add 12 → 24h format
        hour = ((raw[2] & 0x20) != 0) ? (h12 + 12) : h12;
    } else {
        // Modalità 24 ore (bit 6 = 0)
        // 24-hour mode (bit 6 = 0)
        // Bit 5-4: decine delle ore (0-2), bit 3-0: unità
        hour = (int)_from_bcd((uint8_t)(raw[2] & 0x3F));
    }

    // ── Decodifica data ────────────────────────────────────────────────────
    // raw[3] = giorno settimana (1-7, non usato per la struct tm)
    // raw[3] = day of week (1-7, not used for struct tm)
    const int day   = (int)_from_bcd((uint8_t)(raw[4] & 0x3F));          // Giorno mese 1-31
    const int month = (int)_from_bcd((uint8_t)(raw[5] & 0x1F));          // Mese 1-12 (bit 5-7 ignorati)
    const int year  = 2000 + (int)_from_bcd(raw[6]);                     // Anno dal 2000 (es. 26 → 2026)

    // ── Validazione / Validation ────────────────────────────────────────────
    // Verifica che i valori siano in range plausibile prima di costruire la struct tm.
    // Verify values are in plausible range before constructing the struct tm.
    if (year < 2020 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > _days_in_month(year, month)) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;

    // ── Costruzione struct tm ────────────────────────────────────────────────
    // NOTA: struct tm usa convenzioni diverse dai valori umani:
    // NOTE: struct tm uses different conventions than human values:
    //   tm_year = anni dal 1900 (2026 → 126)
    //   tm_mon  = mesi da 0 (gennaio = 0, dicembre = 11)
    //   tm_mday = giorno del mese [1…31] (uguale al valore umano)
    struct tm tm_local = {};
    tm_local.tm_year = year - 1900;    // Anni dal 1900
    tm_local.tm_mon  = month - 1;      // Mesi da 0
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min  = minute;
    tm_local.tm_sec  = second;
    tm_local.tm_isdst = -1;            // Lascia che mktime determini l'ora legale

    *out_tm = tm_local;
    return true;
}

/**
 * @brief Scrive l'ora sull'RTC DS3231/DS1307
 *        Writes the time to the DS3231/DS1307 RTC
 *
 * Prima converte la `struct tm` locale in epoch (per normalizzare e calcolare il
 * giorno della settimana), poi riscrive in BCD sui 7 registri 0x00-0x06.
 * First converts the local `struct tm` to epoch (to normalize and calculate day of week),
 * then writes back as BCD to the 7 registers 0x00-0x06.
 *
 * Il chip RTC viene sempre programmato in **modalità 24 ore** (bit 6 del registro ore = 0).
 * The RTC chip is always programmed in **24-hour mode** (bit 6 of hours register = 0).
 *
 * @param tm_local  Struttura tm in ora locale / Local time tm structure
 * @return `true` se la scrittura ha avuto successo / `true` if write succeeded
 */
static bool _rtc_write_local_tm(const struct tm& tm_local) {
    if (!g_has_rtc) return false;
    if (!_rtc_attach_device()) return false;

    // Normalizza la struct tm tramite mktime (calcola anche tm_wday = giorno settimana)
    // Normalize the tm struct via mktime (also calculates tm_wday = day of week)
    struct tm tmp = tm_local;
    tmp.tm_isdst = -1;
    const time_t epoch = mktime(&tmp);  // Converte in epoch e normalizza
    if (epoch < 0) return false;        // Conversione fallita

    // Riconverte da epoch a tm locale normalizzata (per avere tm_wday corretto)
    // Re-converts from epoch to normalized local tm (to get correct tm_wday)
    localtime_r(&epoch, &tmp);

    // Giorno settimana: DS3231 usa 1-7 (1=domenica per alcuni chip, 1=lunedì per altri)
    // Day of week: DS3231 uses 1-7 (1=Sunday for some chips, 1=Monday for others)
    // Usiamo la convenzione del DS3231 che inizia da 1, ma tm_wday è 0=domenica.
    // We use DS3231 convention starting from 1, but tm_wday is 0=Sunday.
    int dow = tmp.tm_wday;  // 0=Dom, 1=Lun, …, 6=Sab (POSIX)
    if (dow <= 0) dow = 7;  // Adatta: 0 (domenica) → 7

    // Costruisce i 7 byte BCD per i registri 0x00-0x06
    // Builds the 7 BCD bytes for registers 0x00-0x06
    uint8_t raw[7] = {0};
    raw[0] = _to_bcd((uint8_t)tmp.tm_sec);                         // Secondi
    raw[1] = _to_bcd((uint8_t)tmp.tm_min);                         // Minuti
    raw[2] = _to_bcd((uint8_t)tmp.tm_hour);                        // Ore (24h mode, bit 6=0)
    raw[3] = _to_bcd((uint8_t)dow);                                 // Giorno settimana
    raw[4] = _to_bcd((uint8_t)tmp.tm_mday);                        // Giorno mese
    raw[5] = _to_bcd((uint8_t)(tmp.tm_mon + 1));                   // Mese (da 0 a 1-based)
    raw[6] = _to_bcd((uint8_t)((tmp.tm_year + 1900) % 100));       // Anno (solo ultime 2 cifre)
    return _rtc_write_regs(0x00, raw, sizeof(raw));
}

// ─── Sincronizzazione NTP ─────────────────────────────────────────────────────

/**
 * @brief Tenta una singola sincronizzazione NTP
 *        Attempts a single NTP synchronization
 *
 * Chiama `configTzTime()` per configurare il client NTP con il timezone corrente
 * e tre server di fallback, poi attende fino a 10 tentativi × 500ms = 5 secondi
 * per ricevere una risposta valida.
 * Calls `configTzTime()` to configure the NTP client with the current timezone
 * and three fallback servers, then waits up to 10 attempts × 500ms = 5 seconds
 * for a valid response.
 *
 * Se NTP ha successo: aggiorna il contatore software E (se presente) l'RTC hardware.
 * If NTP succeeds: updates the software counter AND (if present) the hardware RTC.
 *
 * ATTENZIONE: questa funzione è bloccante (può bloccare fino a 5 secondi).
 * WARNING: this function is blocking (can block for up to 5 seconds).
 * Viene chiamata solo durante l'init o quando l'utente cambia impostazioni,
 * mai in un loop display. / Called only during init or when user changes settings,
 * never in a display loop.
 *
 * @return `true` se la sincronizzazione ha avuto successo / `true` if sync succeeded
 */
static void _begin_ntp_sync() {
    // configTzTime: configura stack NTP dell'ESP-IDF con timezone POSIX e server
    // configTzTime: configures ESP-IDF NTP stack with POSIX timezone and servers
    configTzTime(k_timezones[g_tz_index].tz,
                 "pool.ntp.org",          // Server principale (pool globale)
                 "time.google.com",        // Fallback 1
                 "time.cloudflare.com");   // Fallback 2
}

/**
 * @brief Imposta l'orologio al valore zero di default (2026-01-01 00:00:00)
 *        Sets the clock to the default zero value (2026-01-01 00:00:00)
 *
 * Usato come fallback finale quando né RTC né NTP sono disponibili.
 * Used as the final fallback when neither RTC nor NTP are available.
 * Il contatore software partirà da questo momento e avanzierà con `millis()`.
 * The software counter will start from this moment and advance with `millis()`.
 *
 * Nota: tm_year = 126 perché struct tm conta gli anni dal 1900 (2026 - 1900 = 126).
 * Note: tm_year = 126 because struct tm counts years from 1900 (2026 - 1900 = 126).
 */
static void _set_default_zero_clock() {
    struct tm tm_local = {};
    tm_local.tm_year = 126;  // 2026 - 1900 = 126
    tm_local.tm_mon  = 0;    // Gennaio / January (0-based)
    tm_local.tm_mday = 1;    // 1° del mese / 1st of month
    tm_local.tm_hour = 0;
    tm_local.tm_min  = 0;
    tm_local.tm_sec  = 0;
    _set_from_local_tm(&tm_local);
}

}  // namespace — fine del namespace anonimo / end of anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// API PUBBLICA / PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Inizializza il modulo orologio / Initializes the clock module
 *
 * Questa funzione deve essere chiamata UNA SOLA VOLTA all'avvio, dopo l'init I2C
 * e prima di qualsiasi altra funzione del modulo.
 * This function must be called ONCE at startup, after I2C init and before any
 * other function of this module.
 *
 * Sequenza di inizializzazione / Initialization sequence:
 *   1. Carica preferenze NVS (timezone, auto_enabled)
 *   2. Applica timezone al sistema POSIX
 *   3. Proba l'RTC hardware (probe I2C)
 *   4a. Se RTC presente: legge l'ora dall'RTC
 *   4b. Se RTC assente: disabilita auto (NTP richiede RTC per persistenza)
 *       e tenta NTP una volta
 *   5. Se auto_enabled e RTC presente: tenta NTP (aggiorna anche RTC)
 *   6. Se non si è riusciti a ottenere l'ora: imposta 2026-01-01 00:00:00
 *
 * Il flag `g_initialized` impedisce la re-inizializzazione accidentale.
 * The `g_initialized` flag prevents accidental re-initialization.
 */
void ui_dc_clock_init(void) {
    if (g_initialized) return;  // Protezione da doppia init / Protection from double init

    // 1. Carica preferenze e applica timezone
    // 1. Load preferences and apply timezone
    _load_prefs();
    _apply_timezone();

    // 2. Proba l'RTC — determina se g_has_rtc è true o false
    // 2. Probe the RTC — determines if g_has_rtc is true or false
    g_has_rtc = _rtc_probe();

    bool have_time = false;
    struct tm tm_local = {};

    if (g_has_rtc && _rtc_read_local_tm(&tm_local)) {
        // 3a. RTC presente e leggibile: usa l'ora dell'RTC
        // 3a. RTC present and readable: use RTC time
        have_time = _set_from_local_tm(&tm_local);
    }

    if (false) {
        // 3b. Senza RTC: sincronizzazione automatica non è affidabile
        //     (non c'è dove persistere l'ora tra i riavvii)
        //     Without RTC: automatic sync is unreliable
        //     (there's nowhere to persist time between reboots)
        // Requirement: automatic date/time unavailable without RTC.
        // Auto mode remains whatever was loaded from NVS.
        // NTP sync is deferred until WiFi becomes available.
        // Tenta comunque NTP per avere un'ora iniziale corretta questa sessione
        // Still attempt NTP to have a correct initial time for this session
        // NTP sync deferred until controller detects WiFi connectivity.
    } else if (g_auto_enabled) {
        // 3c. RTC presente E auto abilitato: sincronizza con NTP (e aggiorna l'RTC)
        // 3c. RTC present AND auto enabled: sync with NTP (and update the RTC)
        // NB: il risultato viene ignorato — l'ora dall'RTC è già valida come fallback
        // NB: result is discarded — the time from RTC is already valid as fallback
        // NTP sync deferred until controller detects WiFi connectivity.
    }

    // 4. Fallback finale: imposta 2026-01-01 00:00:00
    // 4. Final fallback: set 2026-01-01 00:00:00
    if (!have_time) {
        _set_default_zero_clock();
    }

    g_initialized = true;
}

/**
 * @brief Indica se il chip RTC hardware è presente
 *        Indicates whether the hardware RTC chip is present
 * @return `true` se il DS3231/DS1307 è stato trovato durante l'init / `true` if DS3231/DS1307 was found during init
 */
bool ui_dc_clock_has_rtc(void) {
    return g_has_rtc;
}

/**
 * @brief Indica se la sincronizzazione automatica (NTP) è abilitata
 *        Indicates whether automatic sync (NTP) is enabled
 * @return `true` se NTP automatico è abilitato / `true` if automatic NTP is enabled
 */
bool ui_dc_clock_is_auto_enabled(void) {
    return g_auto_enabled;
}

/**
 * @brief Abilita o disabilita la sincronizzazione automatica (NTP)
 *        Enables or disables automatic synchronization (NTP)
 *
 * Se si tenta di abilitare la sync automatica senza RTC hardware, la richiesta
 * viene ignorata e auto_enabled rimane `false` (NTP senza RTC persistente non è utile).
 * If attempting to enable automatic sync without hardware RTC, the request is ignored
 * and auto_enabled stays `false` (NTP without persistent RTC is not useful).
 *
 * Se abilitato, tenta immediatamente una sincronizzazione NTP.
 * If enabled, immediately attempts an NTP synchronization.
 *
 * @param enabled  `true` per abilitare, `false` per disabilitare / `true` to enable, `false` to disable
 */
void ui_dc_clock_set_auto_enabled(bool enabled) {
    if (false) {
        // Forza auto_enabled = false se non c'è RTC
        // Force auto_enabled = false if no RTC
        g_auto_enabled = false;
        _save_auto_pref();
        return;
    }
    g_auto_enabled = enabled;
    _save_auto_pref();
    if (g_auto_enabled) {
        // Tenta subito la sincronizzazione (bloccante fino a 5s)
        // Immediately attempt sync (blocking up to 5s)
        // NTP sync deferred until controller detects WiFi connectivity.
    }
}

/**
 * @brief Restituisce la stringa delle opzioni timezone per `lv_dropdown`
 *        Returns the timezone options string for `lv_dropdown`
 *
 * Le opzioni sono separate da '\n', come richiesto dall'API LVGL.
 * Options are separated by '\n', as required by the LVGL API.
 * L'indice delle opzioni corrisponde all'indice in k_timezones.
 * The option index corresponds to the index in k_timezones.
 *
 * @return Stringa costante con le opzioni / Constant string with options
 */
const char* ui_dc_clock_timezone_options(void) {
    return k_tz_dropdown_opts;
}

/**
 * @brief Restituisce l'indice timezone corrente / Returns the current timezone index
 * @return Indice [0 … k_timezone_count-1] / Index [0 … k_timezone_count-1]
 */
int ui_dc_clock_timezone_index_get(void) {
    return g_tz_index;
}

/**
 * @brief Imposta il timezone tramite indice / Sets the timezone by index
 *
 * Se l'indice è fuori range, viene clampato al range valido.
 * If the index is out of range, it is clamped to the valid range.
 * Se l'indice non cambia, la funzione ritorna senza fare nulla (ottimizzazione).
 * If the index doesn't change, the function returns without doing anything (optimization).
 * Se auto_enabled, tenta immediatamente una re-sincronizzazione NTP con il nuovo timezone.
 * If auto_enabled, immediately attempts NTP re-sync with the new timezone.
 *
 * @param index  Nuovo indice timezone / New timezone index
 */
void ui_dc_clock_timezone_index_set(int index) {
    // Clamp e deduplicazione
    // Clamp and deduplication
    if (index < 0) index = 0;
    if (index >= k_timezone_count) index = k_timezone_count - 1;
    if (index == g_tz_index) return;  // Nessun cambiamento, evita I2C e NVS inutili

    g_tz_index = index;
    _apply_timezone();       // Applica la nuova stringa TZ POSIX
    _save_timezone_pref();   // Persiste in NVS

    if (g_auto_enabled) {
        // Re-sincronizza NTP con il nuovo timezone
        // Re-sync NTP with the new timezone
        // NTP sync deferred until controller detects WiFi connectivity.
    }
}

/**
 * @brief Legge l'ora locale corrente in una `struct tm`
 *        Reads the current local time into a `struct tm`
 *
 * Calcola l'epoch UTC corrente tramite il contatore software, poi converte
 * in ora locale usando `localtime_r()` (che rispetta il timezone POSIX corrente).
 * Calculates the current UTC epoch via the software counter, then converts to
 * local time using `localtime_r()` (which respects the current POSIX timezone).
 *
 * @param out_tm  Puntatore alla struttura di output / Pointer to output structure
 * @return `true` se la lettura ha avuto successo / `true` if read succeeded
 */
bool ui_dc_clock_get_local_tm(struct tm* out_tm) {
    if (!out_tm) return false;
    // Calcola l'epoch UTC corrente (base + delta millis)
    // Calculate the current UTC epoch (base + millis delta)
    const time_t now_epoch = _current_epoch_utc();
    // Converte in ora locale (rispetta TZ, DST) e scrive in out_tm
    // Converts to local time (respects TZ, DST) and writes to out_tm
    return localtime_r(&now_epoch, out_tm) != nullptr;
}

/**
 * @brief Imposta manualmente l'ora del sistema
 *        Manually sets the system time
 *
 * Valida tutti i parametri prima di applicare il cambiamento.
 * Validates all parameters before applying the change.
 * Aggiorna il contatore software E, se presente, l'RTC hardware.
 * Updates the software counter AND, if present, the hardware RTC.
 *
 * @param year    Anno [2020…2099]           / Year [2020…2099]
 * @param month   Mese [1…12]                / Month [1…12]
 * @param day     Giorno [1…giorni del mese] / Day [1…days in month]
 * @param hour    Ora [0…23]                 / Hour [0…23]
 * @param minute  Minuto [0…59]              / Minute [0…59]
 * @param second  Secondo [0…59]             / Second [0…59]
 * @return `true` se impostazione riuscita / `true` if setting succeeded
 */
bool ui_dc_clock_set_manual_local(int year, int month, int day,
                                  int hour, int minute, int second) {
    // Validazione completa di tutti i campi / Full validation of all fields
    if (year < 2020 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > _days_in_month(year, month)) return false;  // Considera anni bisestili
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;

    // Costruisce la struct tm locale
    // Builds the local tm struct
    struct tm tm_local = {};
    tm_local.tm_year = year - 1900;    // Anni dal 1900
    tm_local.tm_mon  = month - 1;      // Mesi da 0
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min  = minute;
    tm_local.tm_sec  = second;
    tm_local.tm_isdst = -1;

    // Imposta il contatore software
    // Set the software counter
    if (!_set_from_local_tm(&tm_local)) return false;

    // Aggiorna anche l'RTC se presente (ignora errori — il contatore sw è già aggiornato)
    // Also update the RTC if present (ignore errors — sw counter is already updated)
    if (g_has_rtc) (void)_rtc_write_local_tm(tm_local);
    return true;
}

// ─── Funzioni di formattazione ────────────────────────────────────────────────
// Formatting functions — convertono l'ora corrente in stringhe pronte per la UI.
// Convert the current time to strings ready for the UI.

/**
 * @brief Formatta l'ora corrente come "HH:MM:SS" / Formats current time as "HH:MM:SS"
 *
 * @param out       Buffer di output / Output buffer
 * @param out_size  Dimensione del buffer (min. 9 byte: "23:59:59\0") / Buffer size (min. 9 bytes)
 */
void ui_dc_clock_ntp_begin(void) {
    _begin_ntp_sync();
}

bool ui_dc_clock_ntp_try_finish(long rtc_drift_threshold_s,
                                long* out_rtc_drift_s,
                                bool* out_rtc_updated) {
    if (out_rtc_drift_s) *out_rtc_drift_s = 0;
    if (out_rtc_updated) *out_rtc_updated = false;

    struct tm ntp_local = {};
    if (!getLocalTime(&ntp_local, 0)) return false;

    const time_t ntp_epoch = time(nullptr);
    if (ntp_epoch <= 0) return false;

    _set_base_epoch_utc(ntp_epoch);

    if (!g_has_rtc) return true;
    if (rtc_drift_threshold_s < 0) rtc_drift_threshold_s = 0;

    long rtc_drift_s = 0;
    bool rtc_updated = false;
    struct tm rtc_local = {};
    if (_rtc_read_local_tm(&rtc_local)) {
        time_t rtc_epoch = 0;
        if (_tm_to_epoch_local(rtc_local, &rtc_epoch)) {
            long long drift_s = (long long)ntp_epoch - (long long)rtc_epoch;
            if (drift_s < 0) drift_s = -drift_s;
            rtc_drift_s = (long)drift_s;
            if (rtc_drift_s > rtc_drift_threshold_s) {
                rtc_updated = _rtc_write_local_tm(ntp_local);
            }
        } else {
            rtc_updated = _rtc_write_local_tm(ntp_local);
        }
    } else {
        rtc_updated = _rtc_write_local_tm(ntp_local);
    }

    if (out_rtc_drift_s) *out_rtc_drift_s = rtc_drift_s;
    if (out_rtc_updated) *out_rtc_updated = rtc_updated;
    return true;
}

void ui_dc_clock_format_time_hms(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';  // Errore: stringa vuota / Error: empty string
        return;
    }
    // Formato: "HH:MM:SS" — due cifre per ogni campo, zeri iniziali
    // Format: "HH:MM:SS" — two digits per field, leading zeros
    snprintf(out, out_size, "%02d:%02d:%02d",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
}

/**
 * @brief Formatta la data corrente come "DD/MM/YYYY" / Formats current date as "DD/MM/YYYY"
 *
 * Formato europeo (giorno prima del mese), usato nelle impostazioni.
 * European format (day before month), used in settings.
 *
 * @param out       Buffer di output / Output buffer
 * @param out_size  Dimensione del buffer (min. 11 byte: "31/12/2099\0") / Buffer size (min. 11 bytes)
 */
void ui_dc_clock_format_date_numeric(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';
        return;
    }
    // tm_mon è 0-based → aggiungi 1; tm_year è dal 1900 → aggiungi 1900
    // tm_mon is 0-based → add 1; tm_year is from 1900 → add 1900
    snprintf(out, out_size, "%02d/%02d/%04d",
             tm_local.tm_mday, tm_local.tm_mon + 1, tm_local.tm_year + 1900);
}

/**
 * @brief Formatta la data corrente per la home screen: "DD MMM YYYY"
 *        Formats the current date for the home screen: "DD MMM YYYY"
 *
 * Usa `strftime` con il formato "%d %b %Y" che produce es. "14 Apr 2026".
 * Il mese abbreviato è nella locale corrente (su ESP32 è sempre inglese se non
 * diversamente configurato: Jan, Feb, Mar, Apr, …).
 * Uses `strftime` with format "%d %b %Y" producing e.g. "14 Apr 2026".
 * The abbreviated month is in the current locale (on ESP32 it's always English
 * unless configured otherwise: Jan, Feb, Mar, Apr, …).
 *
 * @param out       Buffer di output / Output buffer
 * @param out_size  Dimensione del buffer (min. 12 byte: "31 Dec 2099\0") / Buffer size (min. 12 bytes)
 */
void ui_dc_clock_format_date_home(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';
        return;
    }
    // strftime: format string "%d %b %Y"
    //   %d = giorno del mese 01-31 / day of month 01-31
    //   %b = nome mese abbreviato (locale) / abbreviated month name (locale)
    //   %Y = anno 4 cifre / 4-digit year
    // Ritorna 0 se il buffer è troppo piccolo → imposta stringa vuota
    // Returns 0 if buffer is too small → set empty string
    if (strftime(out, out_size, "%d %b %Y", &tm_local) == 0) {
        out[0] = '\0';
    }
}
