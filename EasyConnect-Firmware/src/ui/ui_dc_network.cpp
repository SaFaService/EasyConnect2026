/**
 * @file ui_dc_network.cpp
 * @brief Schermata "Dispositivi RS485" del Display Controller
 *        "RS485 Devices" screen for the Display Controller
 *
 * Questa schermata mostra la lista di tutti i dispositivi RS485 rilevati (runtime)
 * o salvati nell'impianto (plant). Permette di:
 * This screen shows the list of all RS485 devices detected (runtime) or saved
 * in the plant. It allows:
 *   - Avviare una scansione protetta da PIN per scoprire nuove periferiche
 *     Starting a PIN-protected scan to discover new peripherals
 *   - Salvare la configurazione attuale come "fotografia impianto"
 *     Saving the current configuration as "plant snapshot"
 *   - Visualizzare il dettaglio di ogni dispositivo (popup modale)
 *     Viewing the detail of each device (modal popup)
 *   - Eliminare un dispositivo dall'impianto (protezione PIN)
 *     Removing a device from the plant (PIN protection)
 *
 * Architettura / Architecture:
 * ```
 * scr (1024×600)
 *   ├── hdr (header 1024×60) — back btn + titolo + stato + btn Salva + btn Scansiona
 *   └── body (area lista 1024×540)
 *         └── s_list_cont (flex column, scrollabile)
 *               ├── _make_device_row() × N  ← righe dispositivi
 *               └── lbl "Nessuna periferica"  ← se lista vuota
 * ```
 *
 * Popup dettaglio (aperto su click riga):
 * ```
 * mask (1024×600, sfondo semitrasparente)
 *   └── card (620×460, centrata)
 *         ├── titolo "Periferica IP: X"
 *         ├── separatore
 *         ├── righe info (add_row lambda)
 *         ├── btn "Elimina periferica" (rosso, solo se in_plant, PIN protetto)
 *         └── btn "Chiudi" (arancione)
 * ```
 *
 * Timer / Timer:
 *   - s_refresh_timer: 500ms durante scansione, 2000ms a riposo
 *     s_refresh_timer: 500ms during scan, 2000ms at rest
 *
 * Dipendenze / Dependencies:
 *   - ui_dc_network.h      → dichiarazione di ui_dc_network_create()
 *   - ui_dc_home.h         → ui_dc_home_create() per il "Torna indietro"
 *   - ui_dc_maintenance.h  → ui_dc_maintenance_request_pin() per operazioni protette
 *   - rs485_network.h      → API RS485: scan, device list, plant management
 *   - icons/icons_index.h  → icone dispositivi (lv_img_dsc_t)
 */

#include "ui_dc_network.h"
#include "ui_dc_home.h"
#include "ui_dc_maintenance.h"
#include "rs485_network.h"
#include "icons/icons_index.h"
#include "lvgl.h"
#include <math.h>

// ─── Palette (stessa della Home) ─────────────────────────────────────────────
// Palette locale — ridefinita qui anziché usare ui_styles.h per maggiore
// indipendenza del modulo e portabilità futura.
// Local palette — redefined here instead of using ui_styles.h for greater
// module independence and future portability.
#define NT_BG      lv_color_hex(0xEEF3F8)   ///< Sfondo principale (#EEF3F8) / Main background
#define NT_WHITE   lv_color_hex(0xFFFFFF)   ///< Bianco puro / Pure white
#define NT_ORANGE  lv_color_hex(0xE84820)   ///< Arancione Antralux / Antralux orange
#define NT_ORANGE2 lv_color_hex(0xB02810)   ///< Arancione scuro (stato pressed) / Dark orange (pressed state)
#define NT_TEXT    lv_color_hex(0x243447)   ///< Testo principale scuro / Dark main text
#define NT_DIM     lv_color_hex(0x7A92B0)   ///< Testo secondario grigio-blu / Grey-blue secondary text
#define NT_BORDER  lv_color_hex(0xDDE5EE)   ///< Colore bordi / Border color
#define NT_SHADOW  lv_color_hex(0xBBCCDD)   ///< Colore ombra card / Card shadow color

/** @brief Altezza dell'header in pixel / Header height in pixels */
#define HEADER_H  60

// ─── Stato locale alla schermata ─────────────────────────────────────────────
// Variabili statiche = private al modulo (pattern "singleton per schermata")
// Static variables = private to module (pattern "per-screen singleton")
// Vengono azzerate in _on_delete quando la schermata viene distrutta.
// They are cleared in _on_delete when the screen is destroyed.

static lv_obj_t*   s_list_cont      = NULL;               ///< Contenitore flex column delle righe / Flex column container of rows
static lv_obj_t*   s_status_lbl     = NULL;               ///< Label stato/contatore in header / Status/counter label in header
static lv_obj_t*   s_scan_btn_lbl   = NULL;               ///< Label del pulsante "Scansiona" / "Scan" button label
static lv_obj_t*   s_save_btn_lbl   = NULL;               ///< Label del pulsante "Salva" / "Save" button label
static lv_timer_t* s_refresh_timer  = NULL;               ///< Timer aggiornamento lista / List refresh timer
static int         s_last_count     = -1;                  ///< Ultimo numero di dispositivi noto / Last known device count
static Rs485ScanState s_last_scan_state = Rs485ScanState::IDLE;  ///< Ultimo stato scansione noto / Last known scan state

// ─── Helper testo dispositivi ────────────────────────────────────────────────
// Funzioni di conversione da enum/byte a stringa leggibile per la UI.
// Conversion functions from enum/byte to human-readable string for the UI.

/**
 * @brief Converte la modalità relay in stringa leggibile
 *        Converts relay mode to human-readable string
 *
 * Usato nel sottotitolo della riga dispositivo e nel popup dettaglio.
 * Used in the device row subtitle and in the detail popup.
 *
 * @param mode  Codice modalità relay (1-5) / Relay mode code (1-5)
 * @return Stringa costante descrittiva / Constant descriptive string
 */
static const char* _relay_mode_to_text(uint8_t mode) {
    switch (mode) {
        case 1: return "Lampada";
        case 2: return "UVC";
        case 3: return "Elettrostatico";
        case 4: return "Gas";
        case 5: return "Comando";
        default: return "Relay";
    }
}

/**
 * @brief Converte la modalità sensore I2C in stringa leggibile
 *        Converts I2C sensor mode to human-readable string
 *
 * @param mode  Codice modalità sensore (1-3) / Sensor mode code (1-3)
 * @return Stringa costante descrittiva / Constant descriptive string
 */
static const char* _sensor_mode_to_text(uint8_t mode) {
    switch (mode) {
        case 1: return "Temperatura e Umidita'";
        case 2: return "Pressione";
        case 3: return "Temperatura, Umidita' e Pressione";
        default: return "N/D";
    }
}

/**
 * @brief Converte il gruppo aria del dispositivo 0/10V in stringa ruolo
 *        Converts the 0/10V device air group to role string
 *
 * I dispositivi 0/10V (inverter ventilatori) hanno un gruppo che identifica
 * se gestiscono l'aria di aspirazione (estrazione) o immissione.
 * 0/10V devices (fan inverters) have a group that identifies whether they
 * manage extraction or intake air.
 *
 * @param group  Gruppo (1=Aspirazione/Estrazione, 2=Immissione) / Group (1=Extraction, 2=Intake)
 * @return Stringa costante / Constant string
 */
static const char* _air_role_text(uint8_t group) {
    switch (group) {
        case 1: return "Aspirazione";
        case 2: return "Immissione";
        default: return "Gruppo non configurato";
    }
}

/**
 * @brief Formatta un valore float come decimale con 1 cifra dopo la virgola + unità
 *        Formats a float value as decimal with 1 digit after comma + unit
 *
 * Usa aritmetica intera per evitare problemi di formattazione con `printf` su ESP32.
 * Uses integer arithmetic to avoid printf formatting issues on ESP32.
 *
 * Esempio / Example: value=23.5, unit="C" → "23.5 C"
 * Esempio / Example: value=-1.2, unit="Pa" → "-1.2 Pa"
 *
 * @param value   Valore float da formattare / Float value to format
 * @param unit    Stringa unità (può essere "" per nessuna unità) / Unit string (can be "" for no unit)
 * @param out     Buffer di output / Output buffer
 * @param out_sz  Dimensione del buffer / Buffer size
 */
static void _format_signed_tenths(float value, const char* unit, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;

    // Protezione: isfinite() gestisce NaN e infinito
    // Protection: isfinite() handles NaN and infinity
    if (!isfinite(value)) {
        lv_snprintf(out, out_sz, "N/D");
        return;
    }

    // Moltiplica per 10 per ottenere i "decimi" come intero
    // Multiply by 10 to get "tenths" as integer
    const float scaled_f = value * 10.0f;

    // Protezione overflow per la conversione a long
    // Overflow protection for conversion to long
    if (!isfinite(scaled_f) || scaled_f > 214748000.0f || scaled_f < -214748000.0f) {
        lv_snprintf(out, out_sz, "N/D");
        return;
    }

    // lroundf: arrotondamento al più vicino (evita errori di troncamento)
    // lroundf: rounds to nearest (avoids truncation errors)
    long scaled = lroundf(scaled_f);
    const bool negative = scaled < 0;
    unsigned long abs_scaled = (scaled < 0) ? (unsigned long)(-scaled) : (unsigned long)scaled;

    // Formatta come "±X.Y unit" usando divisione e modulo interi
    // Format as "±X.Y unit" using integer division and modulo
    if (unit && unit[0]) {
        lv_snprintf(out, out_sz, "%s%lu.%lu %s",
                    negative ? "-" : "",
                    abs_scaled / 10UL,   // Cifre intere
                    abs_scaled % 10UL,   // Prima cifra decimale
                    unit);
    } else {
        lv_snprintf(out, out_sz, "%s%lu.%lu",
                    negative ? "-" : "",
                    abs_scaled / 10UL,
                    abs_scaled % 10UL);
    }
}

/**
 * @brief Restituisce il tipo di dispositivo come stringa / Returns device type as string
 *
 * Disambigua: i sensori con profilo AIR_010 sono "0/10V" (inverter),
 * quelli senza sono "Sensore I2C".
 * Disambiguates: sensors with AIR_010 profile are "0/10V" (inverters),
 * others are "Sensore I2C".
 *
 * @param dev  Riferimento al dispositivo RS485 / Reference to RS485 device
 * @return Stringa costante / Constant string
 */
static const char* _device_type_text(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) return "Relay";
    if (dev.type == Rs485DevType::SENSOR) {
        if (dev.sensor_profile == Rs485SensorProfile::AIR_010) return "0/10V";
        return "Sensore I2C";
    }
    return "Sconosciuto";
}

// ─── Helper diagnostica errori ────────────────────────────────────────────────
// Funzioni che determinano lo stato di errore di un dispositivo.
// Functions that determine the error state of a device.

/**
 * @brief Verifica se un dispositivo ha un problema di comunicazione
 *        Checks if a device has a communication problem
 *
 * Un dispositivo ha un problema di comunicazione se è nell'impianto (in_plant=true)
 * ma non risponde più sul bus RS485 (online=false).
 * A device has a communication problem if it's in the plant (in_plant=true)
 * but is no longer responding on the RS485 bus (online=false).
 */
static bool _device_has_comm_issue(const Rs485Device& dev) {
    return dev.in_plant && !dev.online;
}

/**
 * @brief Verifica se un relay ha un problema di sicurezza
 *        Checks if a relay has a safety issue
 *
 * Un relay UVC (mode=2) o elettrostatico (mode=3) ha un problema di sicurezza
 * se è online ma la sicurezza risulta aperta (relay_safety_closed=false).
 * A UVC (mode=2) or electrostatic (mode=3) relay has a safety issue if it's online
 * but the safety is open (relay_safety_closed=false).
 */
static bool _relay_has_safety_issue(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::RELAY) return false;
    if (dev.relay_mode != 2 && dev.relay_mode != 3) return false;  // Solo UVC e elettrostatico
    if (!dev.online) return false;
    return !dev.relay_safety_closed;
}

/**
 * @brief Verifica se un relay ha un fault di feedback
 *        Checks if a relay has a feedback fault
 *
 * Il feedback indica se il relay è fisicamente nello stato comandato.
 * Un fault di feedback si verifica se:
 * - È latched (relay_feedback_fault_latched=true), oppure
 * - La sicurezza è chiusa, il feedback non è ok E lo stato contiene "FAULT"
 * The feedback indicates whether the relay is physically in the commanded state.
 * A feedback fault occurs if:
 * - It's latched (relay_feedback_fault_latched=true), or
 * - Safety is closed, feedback is not ok AND state contains "FAULT"
 */
static bool _relay_has_feedback_fault(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::RELAY) return false;
    if (dev.relay_mode != 2 && dev.relay_mode != 3) return false;
    if (!dev.online) return false;
    if (dev.relay_feedback_fault_latched) return true;

    // Verifica via stringa di stato (case-insensitive)
    // Check via state string (case-insensitive)
    String state = dev.relay_state;
    state.toUpperCase();
    return dev.relay_safety_closed &&
           !dev.relay_feedback_ok &&
           state.indexOf("FAULT") >= 0;
}

/**
 * @brief Verifica se un dispositivo 0/10V ha un fault di feedback
 *        Checks if a 0/10V device has a feedback fault
 *
 * Simile al relay, ma per i dispositivi inverter (profilo AIR_010).
 * Similar to relay, but for inverter devices (AIR_010 profile).
 */
static bool _air010_has_feedback_fault(const Rs485Device& dev) {
    if (dev.type != Rs485DevType::SENSOR) return false;
    if (dev.sensor_profile != Rs485SensorProfile::AIR_010) return false;
    if (!dev.online || !dev.sensor_active) return false;
    if (dev.sensor_feedback_fault_latched) return true;

    String state = dev.sensor_state;
    state.toUpperCase();
    return !dev.sensor_feedback_ok && state.indexOf("FAULT") >= 0;
}

/**
 * @brief Verifica se un dispositivo ha QUALSIASI errore attivo
 *        Checks if a device has ANY active error
 *
 * Aggrega tutti i check di errore in un unico booleano.
 * Aggregates all error checks into a single boolean.
 * Usato per decidere se mostrare l'icona warning sulla riga.
 * Used to decide whether to show the warning icon on the row.
 */
static bool _device_has_any_error(const Rs485Device& dev) {
    if (!dev.data_valid) return true;  // Dati non validi = errore generico
    return _device_has_comm_issue(dev) ||
           _relay_has_safety_issue(dev) ||
           _relay_has_feedback_fault(dev) ||
           _air010_has_feedback_fault(dev);
}

/**
 * @brief Verifica se un dispositivo è "congelato" (frozen)
 *        Checks if a device is "frozen"
 *
 * Un dispositivo congelato ha dati non validi o è nell'impianto ma offline.
 * A frozen device has invalid data or is in the plant but offline.
 * In questo stato, vengono mostrati gli ultimi dati validi noti.
 * In this state, the last known valid data is displayed.
 */
static bool _device_is_frozen(const Rs485Device& dev) {
    return !dev.data_valid || (dev.in_plant && !dev.online);
}

/**
 * @brief Genera un testo di sommario degli errori del dispositivo
 *        Generates an error summary text for the device
 *
 * Produce una stringa corta da mostrare come sottotitolo nella riga dispositivo
 * quando c'è un errore. Se non ci sono errori, out[0] = '\0'.
 * Produces a short string to show as subtitle in the device row when there's an error.
 * If no errors, out[0] = '\0'.
 *
 * @param dev    Dispositivo RS485 / RS485 device
 * @param out    Buffer output / Output buffer
 * @param out_sz Dimensione buffer / Buffer size
 */
static void _device_error_summary(const Rs485Device& dev, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;

    if (_device_is_frozen(dev)) {
        if (!dev.data_valid) {
            lv_snprintf(out, out_sz, "Configurazione incompleta");
        } else {
            lv_snprintf(out, out_sz, "Nessuna comunicazione 485");
        }
        return;
    }
    if (!dev.in_plant) {
        // Dispositivo rilevato ma non salvato nella fotografia impianto
        // Device detected but not saved in the plant snapshot
        lv_snprintf(out, out_sz, "Rilevata ma non salvata");
        return;
    }
    if (_relay_has_safety_issue(dev)) {
        lv_snprintf(out, out_sz, "Errore: sicurezza aperta");
        return;
    }
    if (_relay_has_feedback_fault(dev)) {
        lv_snprintf(out, out_sz, "Errore: feedback mancato");
        return;
    }
    if (_air010_has_feedback_fault(dev)) {
        lv_snprintf(out, out_sz, "Errore: feedback inverter");
        return;
    }

    out[0] = '\0';  // Nessun errore → stringa vuota
}

/**
 * @brief Verifica se un dispositivo è "acceso" (attivo/ON)
 *        Checks if a device is "on" (active/ON)
 *
 * Per i relay: relay_on
 * Per i 0/10V: sensor_active
 * Per i sensori I2C: false (non hanno stato on/off)
 * For relays: relay_on
 * For 0/10V: sensor_active
 * For I2C sensors: false (no on/off state)
 */
static bool _device_is_on(const Rs485Device& dev) {
    if (_device_is_frozen(dev)) return false;  // Congelato → sempre off per sicurezza
    if (dev.type == Rs485DevType::RELAY) return dev.relay_on;
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        return dev.sensor_active;
    }
    return false;
}

/**
 * @brief Seleziona l'icona appropriata per il dispositivo in base al tipo e stato
 *        Selects the appropriate icon for the device based on type and state
 *
 * Ogni tipo di relay ha icone on/off distinte (luce, UVC, elettrostatico, gas).
 * I dispositivi 0/10V hanno icone aspiration/immission con stati on/off.
 * I sensori I2C usano l'icona pressione.
 * Each relay type has distinct on/off icons (light, UVC, electrostatic, gas).
 * 0/10V devices have aspiration/immission icons with on/off states.
 * I2C sensors use the pressure icon.
 *
 * Le icone sono dichiarate in icons/icons_index.h come extern lv_img_dsc_t.
 * Icons are declared in icons/icons_index.h as extern lv_img_dsc_t.
 *
 * @param dev  Dispositivo RS485 / RS485 device
 * @return Puntatore all'immagine LVGL / Pointer to LVGL image
 */
static const lv_img_dsc_t* _device_icon(const Rs485Device& dev) {
    if (dev.type == Rs485DevType::RELAY) {
        switch (dev.relay_mode) {
            case 1: return dev.relay_on ? &light_on : &light_off;           // Lampada
            case 2: return dev.relay_on ? &uvc_on : &uvc_off;               // UVC
            case 3: return dev.relay_on ? &electrostatic_on : &electrostatic_off;  // Elettrostatico
            case 4: return dev.relay_on ? &gas_on : &gas_off;               // Gas
            case 5: return &settings;   // Modalità COMANDO: placeholder icona settings
            default: return dev.relay_on ? &light_on : &light_off;          // Fallback
        }
    }

    if (dev.type == Rs485DevType::SENSOR) {
        if (dev.sensor_profile == Rs485SensorProfile::AIR_010) {
            // Inverter 0/10V: icona diversa per aspirazione vs immissione
            // 0/10V inverter: different icon for extraction vs intake
            if (dev.group != 1 && dev.group != 2) return &airintake_off;  // Gruppo non configurato
            const bool extraction = (dev.group == 1);  // Gruppo 1 = aspirazione/estrazione
            if (extraction) return dev.sensor_active ? &airextraction_on : &airextraction_off;
            return dev.sensor_active ? &airintake_on : &airintake_off;
        }
        return &pressure;  // Sensore I2C generico (temperatura/umidità/pressione)
    }

    return &settings;  // Fallback generico
}

/**
 * @brief Genera il testo del sottotitolo per la riga dispositivo
 *        Generates the subtitle text for the device row
 *
 * Priorità / Priority:
 *   1. Se c'è un errore → mostra il sommario errore
 *   2. Per relay → "Modalità • ON/OFF"
 *   3. Per 0/10V → "Ruolo • ON/OFF"
 *   4. Per sensore I2C → "Sensore I2C • Gruppo X"
 *   5. Fallback → tipo generico
 *
 * @param dev    Dispositivo RS485 / RS485 device
 * @param out    Buffer output / Output buffer
 * @param out_sz Dimensione buffer / Buffer size
 */
static void _device_subtitle(const Rs485Device& dev, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;

    // Priorità 1: errore
    // Priority 1: error
    _device_error_summary(dev, out, out_sz);
    if (out[0]) return;  // Se c'è un errore, usa quello come sottotitolo

    // Priorità 2: relay normale
    // Priority 2: normal relay
    if (dev.type == Rs485DevType::RELAY) {
        lv_snprintf(out, out_sz, "%s • %s", _relay_mode_to_text(dev.relay_mode), dev.relay_on ? "ON" : "OFF");
        return;
    }

    // Priorità 3: inverter 0/10V
    // Priority 3: 0/10V inverter
    if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        const char* role = _air_role_text(dev.group);
        lv_snprintf(out, out_sz, "%s • %s", role, dev.sensor_active ? "ON" : "OFF");
        return;
    }

    // Priorità 4: sensore I2C generico
    // Priority 4: generic I2C sensor
    if (dev.type == Rs485DevType::SENSOR) {
        lv_snprintf(out, out_sz, "Sensore I2C • Gruppo %u", (unsigned)dev.group);
        return;
    }

    // Fallback: tipo generico
    // Fallback: generic type
    lv_snprintf(out, out_sz, "%s", _device_type_text(dev));
}

// ─── Navigazione ─────────────────────────────────────────────────────────────

/**
 * @brief Callback pulsante "Indietro" / "Back" button callback
 *
 * Torna alla schermata home principale senza animazione (LV_SCR_LOAD_ANIM_NONE).
 * Returns to the main home screen without animation (LV_SCR_LOAD_ANIM_NONE).
 * Il parametro `true` elimina automaticamente la schermata network corrente.
 * The `true` parameter automatically deletes the current network screen.
 */
static void _back_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t* home = ui_dc_home_create();
    // LV_SCR_LOAD_ANIM_NONE: transizione istantanea (nessuna animazione)
    // LV_SCR_LOAD_ANIM_NONE: instant transition (no animation)
    lv_scr_load_anim(home, LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

// ─── Popup dettaglio dispositivo ─────────────────────────────────────────────
// Il popup è un overlay modale: maschera semitrasparente + card centrata.
// The popup is a modal overlay: semi-transparent mask + centered card.

/**
 * @brief Contesto per l'operazione di eliminazione dispositivo
 *        Context for device deletion operation
 *
 * Allocato su heap, passato come user_data ai callback.
 * Viene liberato tramite LV_EVENT_DELETE sul pulsante delete.
 * Allocated on heap, passed as user_data to callbacks.
 * Freed via LV_EVENT_DELETE on the delete button.
 */
struct NetworkDeleteCtx {
    lv_obj_t* detail_mask;  ///< Puntatore alla maschera del popup (per chiuderla) / Pointer to popup mask (to close it)
    uint8_t address;        ///< Indirizzo RS485 del dispositivo da eliminare / RS485 address of device to delete
    char sn[32];            ///< Numero seriale del dispositivo (per verifica) / Serial number (for verification)
};

/**
 * @brief Callback pulsante "Chiudi" del popup dettaglio
 *        Detail popup "Close" button callback
 *
 * Elimina la maschera (e tutta la gerarchia del popup sotto di essa).
 * Deletes the mask (and the entire popup hierarchy below it).
 */
static void _detail_close_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    // user_data = puntatore alla maschera / user_data = pointer to mask
    lv_obj_t* mask = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
    if (mask) lv_obj_del(mask);  // Elimina la maschera → distrugge tutto il popup
}

/**
 * @brief Callback eseguita DOPO conferma PIN per eliminare un dispositivo
 *        Callback executed AFTER PIN confirmation to delete a device
 *
 * Chiamata da ui_dc_maintenance_request_pin() quando il PIN è confermato.
 * Called by ui_dc_maintenance_request_pin() when PIN is confirmed.
 * Rimuove il dispositivo dall'impianto e aggiorna la lista.
 * Removes the device from the plant and updates the list.
 *
 * IMPORTANTE: libera il contesto NetworkDeleteCtx qui (non in LV_EVENT_DELETE,
 * perché non stiamo usufruendo del ciclo di vita degli oggetti LVGL per questo ctx).
 * IMPORTANT: frees the NetworkDeleteCtx context here (not in LV_EVENT_DELETE,
 * because we're not relying on LVGL object lifecycle for this ctx).
 *
 * @param user_data  Puntatore al NetworkDeleteCtx allocato da _detail_delete_pin_cb
 *                   Pointer to NetworkDeleteCtx allocated by _detail_delete_pin_cb
 */
static void _detail_delete_success_cb(void* user_data) {
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(user_data);
    if (!ctx) return;

    // Tenta di rimuovere il dispositivo dall'impianto
    // Attempt to remove the device from the plant
    if (rs485_network_remove_device_from_plant(ctx->address, ctx->sn)) {
        // Successo: chiudi il popup e aggiorna la lista
        // Success: close popup and refresh list
        if (ctx->detail_mask) lv_obj_del(ctx->detail_mask);
        if (s_refresh_timer) lv_timer_reset(s_refresh_timer);  // Forza aggiornamento immediato
    }
    delete ctx;  // Libera il contesto (allocato in _detail_delete_pin_cb)
}

/**
 * @brief Callback pulsante "Elimina periferica" del popup dettaglio
 *        Detail popup "Delete peripheral" button callback
 *
 * Avvia il flusso di autenticazione PIN. Se confermato, viene chiamata
 * _detail_delete_success_cb con una COPIA del contesto (action_ctx).
 * Starts the PIN authentication flow. If confirmed, _detail_delete_success_cb
 * is called with a COPY of the context (action_ctx).
 *
 * PATTERN: il contesto originale (ctx) è legato al ciclo di vita del pulsante
 * (liberato da _detail_delete_ctx_free_cb in LV_EVENT_DELETE). L'action_ctx è
 * una copia usata solo per l'operazione di eliminazione.
 * PATTERN: the original context (ctx) is tied to the button lifecycle
 * (freed by _detail_delete_ctx_free_cb in LV_EVENT_DELETE). The action_ctx is
 * a copy used only for the delete operation.
 */
static void _detail_delete_pin_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(lv_event_get_user_data(e));
    if (!ctx) return;

    // Alloca una copia del contesto per l'operazione asincrona
    // Allocate a copy of the context for the async operation
    NetworkDeleteCtx* action_ctx = new NetworkDeleteCtx();
    if (!action_ctx) return;
    *action_ctx = *ctx;  // Copia i dati (address, sn, detail_mask)

    // Richiede il PIN di manutenzione prima di procedere
    // Request maintenance PIN before proceeding
    // "Eliminazione periferica" = titolo del popup PIN
    // "Elimina periferica" = testo del pulsante di conferma
    ui_dc_maintenance_request_pin("Eliminazione periferica", "Elimina periferica",
                                  _detail_delete_success_cb, action_ctx);
}

/**
 * @brief Callback LV_EVENT_DELETE sul pulsante elimina — libera il contesto originale
 *        LV_EVENT_DELETE callback on delete button — frees the original context
 *
 * Quando il pulsante delete viene distrutto (insieme alla card del popup),
 * questo callback libera il NetworkDeleteCtx originale che era legato ad esso.
 * When the delete button is destroyed (along with the popup card),
 * this callback frees the original NetworkDeleteCtx that was bound to it.
 *
 * IMPORTANTE: questo ctx è il ctx ORIGINALE (non l'action_ctx della copia).
 * IMPORTANT: this ctx is the ORIGINAL ctx (not the copy's action_ctx).
 */
static void _detail_delete_ctx_free_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DELETE) return;
    NetworkDeleteCtx* ctx = static_cast<NetworkDeleteCtx*>(lv_event_get_user_data(e));
    delete ctx;
}

/**
 * @brief Apre il popup dettaglio per un dispositivo RS485
 *        Opens the detail popup for an RS485 device
 *
 * Struttura del popup / Popup structure:
 * ```
 * mask (1024×600, overlay nero 50%)
 *   └── card (620×460, centrata, ombra)
 *         ├── titolo "Periferica IP: X"
 *         ├── separatore orizzontale (1px)
 *         ├── icona dispositivo (top-right)
 *         ├── righe info (add_row lambda)
 *         │     Seriale, Versione FW, Impianto, Comunicazione, Tipo,
 *         │     + campi specifici per tipo (relay/sensore/0-10V)
 *         ├── campo errore (colorato in arancione se errore)
 *         ├── icona warning (se errore, top-right accanto all'icona)
 *         ├── btn "Elimina periferica" (solo se in_plant, rosso, PIN)
 *         └── btn "Chiudi" (arancione, bottom-right)
 * ```
 *
 * @param dev  Dispositivo RS485 da mostrare / RS485 device to display
 */
static void _open_device_detail(const Rs485Device& dev) {
    lv_obj_t* parent = lv_scr_act();  // Schermata corrente
    if (!parent) return;

    // ── Maschera semitrasparente (copre tutta la schermata) ─────────────────
    // La maschera serve sia come sfondo oscurato sia come "contenitore" del popup.
    // The mask serves both as darkened background and as popup "container".
    // Cliccando fuori dal popup (sulla maschera) NON si chiude — solo il btn Chiudi lo fa.
    // Clicking outside the popup (on the mask) does NOT close it — only the Close btn does.
    lv_obj_t* mask = lv_obj_create(parent);
    lv_obj_set_size(mask, 1024, 600);
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);  // 50% opacità → sfondo visibile ma oscurato
    lv_obj_set_style_border_width(mask, 0, 0);
    lv_obj_set_style_radius(mask, 0, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    // ── Card centrale del popup ─────────────────────────────────────────────
    lv_obj_t* card = lv_obj_create(mask);
    lv_obj_set_size(card, 620, 460);
    lv_obj_center(card);  // Centrata nella maschera (= centrata nel display)
    lv_obj_set_style_bg_color(card, NT_WHITE, 0);
    lv_obj_set_style_border_color(card, NT_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 14, 0);    // Angoli più arrotondati del solito
    lv_obj_set_style_pad_all(card, 24, 0);   // Padding generoso per leggibilità
    lv_obj_set_style_shadow_color(card, NT_SHADOW, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_ofs_y(card, 6, 0);    // Ombra verso il basso
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── Titolo ──────────────────────────────────────────────────────────────
    char title[48];
    lv_snprintf(title, sizeof(title), "Periferica IP: %d", (int)dev.address);
    lv_obj_t* ttl = lv_label_create(card);
    lv_label_set_text(ttl, title);
    lv_obj_set_style_text_font(ttl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ttl, NT_TEXT, 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, 0, 0);

    // ── Separatore ──────────────────────────────────────────────────────────
    lv_obj_t* sep = lv_obj_create(card);
    lv_obj_set_size(sep, 572, 1);           // Larghezza = card width - 2*padding
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_style_bg_color(sep, NT_BORDER, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ── Lambda helper per riga info (key + value) ───────────────────────────
    // Lambda C++11 che cattura `card` per creare righe di info (etichetta + valore).
    // C++11 lambda that captures `card` to create info rows (label + value).
    // y_offset: offset verticale relativo a 50px dall'alto (sotto il separatore)
    // y_offset: vertical offset relative to 50px from top (below separator)
    auto add_row = [&](const char* label, const char* value, int y_offset) {
        lv_obj_t* lbl = lv_label_create(card);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, NT_DIM, 0);  // Grigio per le etichette
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 50 + y_offset);

        lv_obj_t* val = lv_label_create(card);
        lv_label_set_text(val, value);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, NT_TEXT, 0);  // Scuro per i valori
        lv_obj_align(val, LV_ALIGN_TOP_LEFT, 180, 50 + y_offset);  // 180px a destra
    };

    // ── Determina il testo del tipo dispositivo ─────────────────────────────
    const char* type_str = _device_type_text(dev);
    const char* sensor_type_str = _sensor_mode_to_text(dev.sensor_mode);
    // Per sensori I2C non-AIR_010, mostra il tipo di sensore specifico
    // For non-AIR_010 I2C sensors, show the specific sensor type
    const char* detail_type_value =
        (dev.type == Rs485DevType::SENSOR && dev.sensor_profile != Rs485SensorProfile::AIR_010)
            ? sensor_type_str
            : type_str;

    // Numero seriale (fallback "N/D" se assente)
    // Serial number (fallback "N/D" if absent)
    char sn_buf[34];
    lv_snprintf(sn_buf, sizeof(sn_buf), "%s", dev.sn[0] ? dev.sn : "N/D");

    // ── Icona dispositivo (top-right della card) ────────────────────────────
    lv_obj_t* icon = lv_img_create(card);
    lv_img_set_src(icon, _device_icon(dev));
    lv_obj_align(icon, LV_ALIGN_TOP_RIGHT, 0, 4);  // Angolo in alto a destra

    // ── Righe info comuni a tutti i dispositivi ─────────────────────────────
    add_row("Seriale:",       sn_buf,        0);
    add_row("Versione FW:",   dev.version[0] ? dev.version : "N/D", 30);
    add_row("Impianto:",      dev.in_plant ? "Salvata nella fotografia" : "Rilevata ma ignorata", 60);
    add_row("Comunicazione:", dev.online ? "OK" : "Nessuna comunicazione 485", 90);
    add_row("Tipo:",          detail_type_value, 120);

    // ── Righe info specifiche per tipo dispositivo ──────────────────────────
    if (dev.type == Rs485DevType::RELAY) {
        // Relay: modalità, stato, sicurezza, feedback, stato testuale
        // Relay: mode, state, safety, feedback, text state
        add_row("Modalita':", _relay_mode_to_text(dev.relay_mode), 160);
        add_row("Relay:", dev.relay_on ? "ON" : "OFF", 190);
        add_row("Safety:", dev.relay_safety_closed ? "CHIUSA" : "APERTA", 220);
        add_row("Feedback:", dev.relay_feedback_ok ? "OK" : "FAIL", 250);
        add_row("Stato:", dev.relay_state[0] ? dev.relay_state : "N/D", 280);

    } else if (dev.type == Rs485DevType::SENSOR && dev.sensor_profile == Rs485SensorProfile::AIR_010) {
        // Inverter 0/10V: ruolo aria, stato, velocità %, feedback, stato testuale, gruppo
        // 0/10V inverter: air role, state, speed %, feedback, text state, group
        add_row("Ruolo:", _air_role_text(dev.group), 160);
        add_row("Stato:", dev.sensor_active ? "ON" : "OFF", 190);

        char spd_buf[16];
        // dev.h contiene la velocità percentuale per i dispositivi AIR_010
        // dev.h contains the speed percentage for AIR_010 devices
        lv_snprintf(spd_buf, sizeof(spd_buf), "%d %%", (int)(dev.h + 0.5f));
        add_row("Velocita':", spd_buf, 220);
        add_row("Feedback:", dev.sensor_feedback_ok ? "OK" : "FAIL", 250);
        add_row("Run state:", dev.sensor_state[0] ? dev.sensor_state : "N/D", 280);

        char grp_buf[8];
        lv_snprintf(grp_buf, sizeof(grp_buf), "%u", dev.group);
        add_row("Gruppo:", grp_buf, 310);

    } else if (dev.type == Rs485DevType::SENSOR) {
        // Sensore I2C: temperatura, umidità, pressione (con formattazione decimale), gruppo
        // I2C sensor: temperature, humidity, pressure (with decimal formatting), group
        char t_buf[24], h_buf[24], p_buf[24];
        _format_signed_tenths(dev.t, "C", t_buf, sizeof(t_buf));
        _format_signed_tenths(dev.h, "%RH", h_buf, sizeof(h_buf));
        _format_signed_tenths(dev.p, "Pa", p_buf, sizeof(p_buf));
        add_row("Temperatura:", t_buf, 160);
        add_row("Umidità:",     h_buf, 190);
        add_row("Pressione:",   p_buf, 220);
        char grp_buf[8];
        lv_snprintf(grp_buf, sizeof(grp_buf), "%u", dev.group);
        add_row("Gruppo:",      grp_buf, 250);
    }

    // ── Campo errore ────────────────────────────────────────────────────────
    // Mostra il testo dell'errore attivo (se esiste) o "Nessun errore attivo"
    // Shows the active error text (if any) or "Nessun errore attivo"
    String error_text = "Nessun errore attivo";
    if (!dev.data_valid) {
        error_text = "Periferica congelata.\nSeriale, gruppo o payload non coerenti.";
    } else if (_device_is_frozen(dev)) {
        error_text = "Periferica freezata.\nUltimi dati salvati mostrati a video.";
    } else if (!dev.in_plant) {
        error_text = "Periferica non inclusa nella fotografia impianto.";
    } else if (_device_has_comm_issue(dev)) {
        error_text = "Mancata comunicazione";
    } else if (_relay_has_safety_issue(dev) && _relay_has_feedback_fault(dev)) {
        error_text = "Sicurezza aperta\nFeedback mancato";
    } else if (_relay_has_safety_issue(dev)) {
        error_text = "Sicurezza aperta";
    } else if (_relay_has_feedback_fault(dev)) {
        error_text = "Feedback mancato";
    } else if (_air010_has_feedback_fault(dev)) {
        error_text = "Feedback inverter mancato";
    }

    // Label "Errore:" (grigia, etichetta)
    lv_obj_t* err_lbl = lv_label_create(card);
    lv_label_set_text(err_lbl, "Errore:");
    lv_obj_set_style_text_font(err_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(err_lbl, NT_DIM, 0);
    lv_obj_align(err_lbl, LV_ALIGN_TOP_LEFT, 0, 340);

    // Valore errore (arancione se errore, scuro se ok)
    // Error value (orange if error, dark if ok)
    lv_obj_t* err_val = lv_label_create(card);
    lv_label_set_text(err_val, error_text.c_str());
    lv_obj_set_width(err_val, 360);  // Limita larghezza per wrapping testo
    lv_obj_set_style_text_font(err_val, &lv_font_montserrat_16, 0);
    // Colore arancione per errori, scuro per ok
    // Orange color for errors, dark for ok
    lv_obj_set_style_text_color(err_val,
        (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE : NT_TEXT, 0);
    lv_obj_align(err_val, LV_ALIGN_TOP_LEFT, 180, 370);

    // Icona warning (mostrata solo se c'è un errore)
    // Warning icon (shown only if there's an error)
    if (_device_has_any_error(dev)) {
        lv_obj_t* warn = lv_img_create(card);
        lv_img_set_src(warn, &warning);
        lv_img_set_zoom(warn, 180);  // 180/256 ≈ 70% della dimensione originale
        lv_obj_align(warn, LV_ALIGN_TOP_RIGHT, -4, 44);  // Accanto all'icona dispositivo
    }

    // ── Pulsante "Elimina periferica" (solo se in_plant) ────────────────────
    // Il pulsante è mostrato solo per i dispositivi già nell'impianto.
    // The button is shown only for devices already in the plant.
    // Richiede conferma PIN via ui_dc_maintenance_request_pin.
    // Requires PIN confirmation via ui_dc_maintenance_request_pin.
    if (dev.in_plant) {
        NetworkDeleteCtx* del_ctx = new NetworkDeleteCtx();
        if (del_ctx) {
            memset(del_ctx, 0, sizeof(*del_ctx));
            del_ctx->detail_mask = mask;          // Per chiudere il popup dopo eliminazione
            del_ctx->address = dev.address;       // Indirizzo RS485 da eliminare
            strncpy(del_ctx->sn, dev.sn, sizeof(del_ctx->sn) - 1);  // Seriale per verifica

            lv_obj_t* delete_btn = lv_btn_create(card);
            lv_obj_set_size(delete_btn, 220, 40);
            lv_obj_align(delete_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0xB93A32), 0);         // Rosso scuro
            lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x8F2B24), LV_STATE_PRESSED);  // Più scuro al press
            lv_obj_set_style_radius(delete_btn, 8, 0);
            lv_obj_set_style_shadow_width(delete_btn, 0, 0);
            lv_obj_set_style_border_width(delete_btn, 0, 0);

            // Callback click → richiedi PIN
            // Click callback → request PIN
            lv_obj_add_event_cb(delete_btn, _detail_delete_pin_cb, LV_EVENT_CLICKED, del_ctx);
            // Callback delete → libera del_ctx (RAII pattern)
            // Delete callback → free del_ctx (RAII pattern)
            lv_obj_add_event_cb(delete_btn, _detail_delete_ctx_free_cb, LV_EVENT_DELETE, del_ctx);

            lv_obj_t* delete_lbl = lv_label_create(delete_btn);
            lv_label_set_text(delete_lbl, "Elimina periferica");
            lv_obj_set_style_text_font(delete_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(delete_lbl, lv_color_white(), 0);
            lv_obj_center(delete_lbl);
        }
    }

    // ── Pulsante "Chiudi" (sempre presente) ────────────────────────────────
    lv_obj_t* close_btn = lv_btn_create(card);
    lv_obj_set_size(close_btn, 120, 40);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, NT_ORANGE, 0);
    lv_obj_set_style_bg_color(close_btn, NT_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(close_btn, 8, 0);
    lv_obj_set_style_shadow_width(close_btn, 0, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);

    lv_obj_t* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "Chiudi");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);

    // user_data = mask → il callback lo eliminerà al click
    // user_data = mask → the callback will delete it on click
    lv_obj_add_event_cb(close_btn, _detail_close_cb, LV_EVENT_CLICKED, mask);
}

// ─── Click su una riga dispositivo ───────────────────────────────────────────

/**
 * @brief Callback click su una riga dispositivo — apre il popup dettaglio
 *        Device row click callback — opens the detail popup
 *
 * L'indice del dispositivo è passato come user_data (cast int↔void*).
 * Legge il dispositivo dall'API RS485 e apre il popup.
 * The device index is passed as user_data (int↔void* cast).
 * Reads the device from the RS485 API and opens the popup.
 *
 * @param e  Evento LVGL / LVGL event
 */
static void _row_click_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    // Recupera l'indice del dispositivo da user_data
    // Retrieve device index from user_data
    // PATTERN: (void*)(intptr_t)idx → cast sicuro da intero a puntatore void
    // PATTERN: (void*)(intptr_t)idx → safe cast from integer to void pointer
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);

    Rs485Device dev;
    if (rs485_network_get_device(idx, dev)) {
        _open_device_detail(dev);
    }
}

// ─── Costruzione delle righe della lista ─────────────────────────────────────

/**
 * @brief Crea una riga nella lista dispositivi
 *        Creates a row in the device list
 *
 * Ogni riga è un oggetto LVGL con:
 * - Un badge circolare con icona del dispositivo (+ eventuale icona warning)
 * - Il numero seriale (grande, a sinistra) e sottotitolo (piccolo, sotto)
 * - L'indirizzo IP a destra (colorato in arancione se errore o se ON)
 * - Un bordo inferiore come separatore visivo tra le righe
 *
 * Each row is an LVGL object with:
 * - A circular badge with device icon (+ optional warning icon)
 * - The serial number (large, left) and subtitle (small, below)
 * - The IP address on the right (orange if error or if ON)
 * - A bottom border as visual separator between rows
 *
 * @param parent  Il contenitore flex column / The flex column container
 * @param dev     Il dispositivo RS485 / The RS485 device
 * @param idx     Indice nella lista per il callback click / Index in list for click callback
 * @return Puntatore alla riga creata / Pointer to the created row
 */
static lv_obj_t* _make_device_row(lv_obj_t* parent, const Rs485Device& dev, int idx) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 72);  // 100% larghezza, 72px alto

    // Sfondo bianco, leggermente più scuro al press
    // White background, slightly darker on press
    lv_obj_set_style_bg_color(row, NT_WHITE, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0xF0F4F8), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);

    // Bordo inferiore (separatore tra le righe)
    // Bottom border (separator between rows)
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(row, NT_BORDER, 0);

    lv_obj_set_style_radius(row, 0, 0);  // Nessun arrotondamento (lista piatta)
    lv_obj_set_style_pad_left(row, 16, 0);
    lv_obj_set_style_pad_right(row, 16, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Registra il callback click con l'indice come user_data
    // Register click callback with index as user_data
    lv_obj_add_event_cb(row, _row_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);

    // ── Badge circolare con icona ──────────────────────────────────────────
    // Sfondo grigio chiaro, bordo sottile, forma circolare (radius=25)
    // Light grey background, thin border, circular shape (radius=25)
    lv_obj_t* badge = lv_obj_create(row);
    lv_obj_set_size(badge, 50, 50);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0xF4F8FC), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, NT_BORDER, 0);
    lv_obj_set_style_radius(badge, 25, 0);   // 25 = metà di 50 → cerchio perfetto
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Icona del dispositivo centrata nel badge
    // Device icon centered in badge
    lv_obj_t* icon = lv_img_create(badge);
    lv_img_set_src(icon, _device_icon(dev));
    lv_obj_center(icon);

    // Icona warning sovrapposta in basso a destra del badge (se c'è un errore)
    // Warning icon overlaid at bottom-right of badge (if there's an error)
    if (_device_has_any_error(dev)) {
        lv_obj_t* warn = lv_img_create(badge);
        lv_img_set_src(warn, &warning);
        lv_img_set_zoom(warn, 170);  // 170/256 ≈ 66% della dimensione originale
        lv_obj_align(warn, LV_ALIGN_BOTTOM_RIGHT, 1, 1);  // Angolo in basso a destra
    }

    // ── Seriale + sottotitolo (centrati verticalmente, a 68px dal bordo sinistro) ──
    // 68px = 16px padding + 50px badge + 2px gap
    // 68px = 16px padding + 50px badge + 2px gap
    lv_obj_t* sn_lbl = lv_label_create(row);
    lv_label_set_text(sn_lbl, dev.sn[0] ? dev.sn : "N/D");
    lv_obj_set_style_text_font(sn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sn_lbl, NT_TEXT, 0);
    lv_obj_align(sn_lbl, LV_ALIGN_LEFT_MID, 68, -10);  // -10px: sopra il centro

    lv_obj_t* type_lbl = lv_label_create(row);
    char subtitle[64];
    _device_subtitle(dev, subtitle, sizeof(subtitle));
    lv_label_set_text(type_lbl, subtitle);
    lv_obj_set_style_text_font(type_lbl, &lv_font_montserrat_12, 0);
    // Arancione se errore o non in impianto, grigio altrimenti
    // Orange if error or not in plant, grey otherwise
    lv_obj_set_style_text_color(type_lbl,
        (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE : NT_DIM, 0);
    lv_obj_align(type_lbl, LV_ALIGN_LEFT_MID, 68, 12);  // +12px: sotto il centro

    // ── Indirizzo IP a destra ──────────────────────────────────────────────
    char ip_buf[16];
    lv_snprintf(ip_buf, sizeof(ip_buf), "IP %d", (int)dev.address);
    lv_obj_t* ip_lbl = lv_label_create(row);
    lv_label_set_text(ip_lbl, ip_buf);
    lv_obj_set_style_text_font(ip_lbl, &lv_font_montserrat_14, 0);
    // Colore: arancione se errore o non in impianto, arancione se ON, grigio altrimenti
    // Color: orange if error or not in plant, orange if ON, grey otherwise
    lv_obj_set_style_text_color(ip_lbl,
        (_device_has_any_error(dev) || !dev.in_plant) ? NT_ORANGE :
        (_device_is_on(dev) ? NT_ORANGE : NT_DIM), 0);
    lv_obj_align(ip_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    return row;
}

// ─── Aggiornamento header ─────────────────────────────────────────────────────

/**
 * @brief Aggiorna le label nell'header in base allo stato corrente
 *        Updates header labels based on current state
 *
 * Durante la scansione, mostra il progresso (X/200).
 * A riposo, mostra il conteggio dispositivi (impianto N - runtime M).
 * During scan, shows progress (X/200).
 * At rest, shows device count (plant N - runtime M).
 *
 * @param state        Stato corrente della scansione RS485 / Current RS485 scan state
 * @param count        Numero totale dispositivi runtime / Total runtime device count
 * @param plant_count  Numero dispositivi nell'impianto / Number of devices in plant
 */
static void _refresh_header_labels(Rs485ScanState state, int count, int plant_count) {
    if (s_status_lbl) {
        if (state == Rs485ScanState::RUNNING) {
            char buf[32];
            // rs485_network_scan_progress(): avanzamento da 0 a 200 (indirizzi scansionati)
            // rs485_network_scan_progress(): progress from 0 to 200 (addresses scanned)
            lv_snprintf(buf, sizeof(buf), "Scansione in corso... %d/200",
                        rs485_network_scan_progress());
            lv_label_set_text(s_status_lbl, buf);
        } else {
            // Formato: "Impianto X - Runtime Y"
            // Format: "Plant X - Runtime Y"
            char buf[48];
            lv_snprintf(buf, sizeof(buf), "Impianto %d - Runtime %d", plant_count, count);
            lv_label_set_text(s_status_lbl, buf);
        }
    }

    // Label del pulsante Scansiona: durante scan mostra avanzamento
    // Scan button label: shows progress during scan
    if (s_scan_btn_lbl) {
        if (state == Rs485ScanState::RUNNING) {
            char buf[28];
            lv_snprintf(buf, sizeof(buf), "Scansione %d/200",
                        rs485_network_scan_progress());
            lv_label_set_text(s_scan_btn_lbl, buf);
        } else {
            lv_label_set_text(s_scan_btn_lbl, LV_SYMBOL_REFRESH " Scansiona");
        }
    }

    // Label del pulsante Salva: sempre statica
    // Save button label: always static
    if (s_save_btn_lbl) {
        lv_label_set_text(s_save_btn_lbl, LV_SYMBOL_SAVE " Salva");
    }
}

// ─── Ricostruzione contenuto lista ───────────────────────────────────────────

/**
 * @brief Ricostruisce completamente la lista dei dispositivi
 *        Completely rebuilds the device list
 *
 * PATTERN: invece di aggiornare le righe esistenti, le elimina tutte
 * (lv_obj_clean) e le ricrea. Questo è più semplice da mantenere e
 * garantisce coerenza con lo stato attuale dell'API RS485.
 * PATTERN: instead of updating existing rows, deletes all of them
 * (lv_obj_clean) and recreates them. This is simpler to maintain and
 * guarantees consistency with the current RS485 API state.
 *
 * Se non ci sono dispositivi, mostra un messaggio appropriato.
 * If there are no devices, shows an appropriate message.
 */
static void _rebuild_list() {
    if (!s_list_cont) return;

    // Elimina tutti i widget figli (righe precedenti)
    // Delete all child widgets (previous rows)
    lv_obj_clean(s_list_cont);

    // Legge lo stato corrente dall'API RS485
    // Reads current state from RS485 API
    const int count = rs485_network_device_count();
    const int plant_count = rs485_network_plant_device_count();
    const Rs485ScanState state = rs485_network_scan_state();

    // Aggiorna lo stato locale (usato per rilevare cambiamenti nel timer)
    // Updates local state (used to detect changes in timer)
    s_last_count = count;
    s_last_scan_state = state;

    // Aggiorna le label nell'header
    // Updates labels in header
    _refresh_header_labels(state, count, plant_count);

    if (count == 0) {
        // Lista vuota: mostra messaggio contestuale
        // Empty list: show contextual message
        lv_obj_t* empty = lv_label_create(s_list_cont);
        lv_label_set_text(empty,
            state == Rs485ScanState::IDLE    ? "Nessuna periferica runtime.\nEseguire una scansione protetta per rilevare le schede." :
            state == Rs485ScanState::RUNNING ? "Scansione in corso..." :
                                               "Nessuna periferica rilevata.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, NT_DIM, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(empty);
        return;
    }

    // Crea una riga per ogni dispositivo nella lista RS485
    // Create a row for each device in the RS485 list
    Rs485Device dev;
    for (int i = 0; i < count; i++) {
        if (rs485_network_get_device(i, dev)) {
            _make_device_row(s_list_cont, dev, i);
        }
    }
}

// ─── Timer aggiornamento (durante scansione) ──────────────────────────────────

/**
 * @brief Timer callback di aggiornamento lista — eseguito ogni 500ms (scan) o 2000ms (riposo)
 *        List refresh timer callback — runs every 500ms (scan) or 2000ms (rest)
 *
 * Strategia di aggiornamento efficiente / Efficient update strategy:
 * - Aggiorna sempre le label dell'header (progresso scansione)
 * - Ricostruisce la lista SOLO se il conteggio o lo stato sono cambiati
 * - Abbassa la frequenza a 2000ms quando la scansione è terminata
 *
 * - Always updates header labels (scan progress)
 * - Rebuilds the list ONLY if count or state have changed
 * - Lowers frequency to 2000ms when scan is complete
 *
 * @param t  Puntatore al timer (non usato) / Timer pointer (unused)
 */
static void _refresh_timer_cb(lv_timer_t* /*t*/) {
    const Rs485ScanState state = rs485_network_scan_state();
    const int count = rs485_network_device_count();
    const int plant_count = rs485_network_plant_device_count();

    // Aggiorna sempre le label header (es: contatore progresso scansione)
    // Always update header labels (e.g., scan progress counter)
    _refresh_header_labels(state, count, plant_count);

    // Ricostruisce la lista solo se c'è stato un cambiamento
    // Rebuilds the list only if there's been a change
    const bool list_changed = (count != s_last_count) || (state != s_last_scan_state);
    if (list_changed) {
        _rebuild_list();
    }

    // Adatta la frequenza del timer allo stato della scansione:
    // - Durante scansione: 500ms (aggiornamento rapido del progresso)
    // - Riposo: 2000ms (risparmio CPU)
    // Adapts timer frequency to scan state:
    // - During scan: 500ms (rapid progress update)
    // - At rest: 2000ms (CPU saving)
    if (state != Rs485ScanState::RUNNING) {
        lv_timer_set_period(s_refresh_timer, 2000);
    } else {
        lv_timer_set_period(s_refresh_timer, 500);
    }
}

// ─── Callback per i pulsanti Scansiona / Salva ────────────────────────────────

/**
 * @brief Eseguita dopo conferma PIN — avvia la scansione RS485
 *        Executed after PIN confirmation — starts RS485 scan
 */
static void _scan_btn_confirm_cb(void* /*user_data*/) {
    rs485_network_scan_start();                          // Avvia la scansione
    lv_timer_set_period(s_refresh_timer, 500);           // Accelera il timer
    _rebuild_list();                                      // Aggiorna subito la lista
}

/**
 * @brief Eseguita dopo conferma PIN — salva la lista corrente come impianto
 *        Executed after PIN confirmation — saves current list as plant
 */
static void _save_btn_confirm_cb(void* /*user_data*/) {
    (void)rs485_network_save_current_as_plant();         // Salva fotografia impianto
    lv_timer_set_period(s_refresh_timer, 500);
    _rebuild_list();
}

/**
 * @brief Callback pulsante "Scansiona" — richiede PIN di manutenzione
 *        "Scan" button callback — requests maintenance PIN
 *
 * La scansione è protetta da PIN perché può interrompere temporaneamente
 * la comunicazione RS485 con i dispositivi già connessi.
 * The scan is PIN-protected because it can temporarily interrupt RS485
 * communication with already connected devices.
 */
static void _scan_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Scansione periferiche", "Avvia scansione",
                                  _scan_btn_confirm_cb, NULL);
}

/**
 * @brief Callback pulsante "Salva" — richiede PIN di manutenzione
 *        "Save" button callback — requests maintenance PIN
 *
 * Il salvataggio è protetto da PIN perché modifica la configurazione dell'impianto.
 * Saving is PIN-protected because it modifies the plant configuration.
 */
static void _save_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_dc_maintenance_request_pin("Salvataggio impianto", "Salva impianto",
                                  _save_btn_confirm_cb, NULL);
}

// ─── Pulizia alla delete della schermata ─────────────────────────────────────

/**
 * @brief Callback LV_EVENT_DELETE sulla schermata — pulizia delle risorse
 *        LV_EVENT_DELETE callback on screen — resource cleanup
 *
 * Chiamata automaticamente da LVGL quando la schermata viene distrutta
 * (es: durante la transizione verso Home).
 * Automatically called by LVGL when the screen is destroyed
 * (e.g., during transition to Home).
 *
 * IMPORTANTE: azzera tutti i puntatori statici per evitare dangling pointers
 * se la schermata viene ricreata in seguito.
 * IMPORTANT: clears all static pointers to avoid dangling pointers
 * if the screen is recreated later.
 */
static void _on_delete(lv_event_t* /*e*/) {
    // Ferma e libera il timer di aggiornamento
    // Stop and free the refresh timer
    if (s_refresh_timer) {
        lv_timer_del(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    // Azzera tutti i puntatori ai widget (non validi dopo la distruzione della schermata)
    // Clear all widget pointers (invalid after screen destruction)
    s_list_cont    = NULL;
    s_status_lbl   = NULL;
    s_scan_btn_lbl = NULL;
    s_save_btn_lbl = NULL;
    s_last_count   = -1;
    s_last_scan_state = Rs485ScanState::IDLE;
}

// ─── Costruzione schermata ────────────────────────────────────────────────────

/**
 * @brief Crea e restituisce la schermata "Dispositivi RS485"
 *        Creates and returns the "RS485 Devices" screen
 *
 * Struttura gerarchica completa / Complete hierarchical structure:
 * ```
 * scr (1024×600, sfondo NT_BG)
 *   ├── hdr (1024×60)
 *   │     ├── back_btn (48×48, sinistra, → back_cb)
 *   │     ├── title "Dispositivi RS485" (a destra del back)
 *   │     ├── s_status_lbl (contatore / progresso)
 *   │     ├── save_btn (170×40, "Salva", blu, → save_btn_cb + PIN)
 *   │     └── scan_btn (190×40, "Scansiona", arancione, → scan_btn_cb + PIN)
 *   └── body (1024×540, sotto l'header)
 *         └── s_list_cont (984px wide, flex column, scrollabile)
 *               ← righe costruite da _rebuild_list() / _make_device_row()
 * ```
 *
 * @return Puntatore alla schermata creata / Pointer to the created screen
 */
lv_obj_t* ui_dc_network_create(void) {
    // ── Schermata root ─────────────────────────────────────────────────────
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, NT_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Registra il callback di delete per la pulizia delle risorse
    // Register delete callback for resource cleanup
    lv_obj_add_event_cb(scr, _on_delete, LV_EVENT_DELETE, NULL);

    // ── Header ───────────────────────────────────────────────────────────────
    // Header con gradiente verticale bianco→grigio e ombra inferiore
    // Header with vertical white→grey gradient and bottom shadow
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 1024, HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);

    // Gradiente: da bianco in alto a grigio-blu chiaro in basso
    // Gradient: from white at top to light grey-blue at bottom
    lv_obj_set_style_bg_color(hdr, NT_WHITE, 0);
    lv_obj_set_style_bg_grad_color(hdr, lv_color_hex(0xD8E4EE), 0);
    lv_obj_set_style_bg_grad_dir(hdr, LV_GRAD_DIR_VER, 0);  // Verticale top→bottom
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);

    // Bordo superiore bianco (per separare visivamente dall'eventuale status bar)
    // Top white border (to visually separate from any status bar)
    lv_obj_set_style_border_color(hdr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(hdr, 0, 0);

    // Ombra inferiore per dare profondità all'header
    // Bottom shadow to give depth to the header
    lv_obj_set_style_shadow_color(hdr, lv_color_hex(0x90A8C0), 0);
    lv_obj_set_style_shadow_width(hdr, 20, 0);
    lv_obj_set_style_shadow_ofs_y(hdr, 5, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Pulsante Indietro ────────────────────────────────────────────────────
    lv_obj_t* back_btn = lv_btn_create(hdr);
    lv_obj_set_size(back_btn, 48, 48);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xDDE5EE), 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0xC0D0E0), LV_STATE_PRESSED);
    lv_obj_set_style_radius(back_btn, 10, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);

    lv_obj_t* back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);  // Freccia sinistra Unicode LVGL
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_lbl, NT_TEXT, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, _back_cb, LV_EVENT_CLICKED, NULL);

    // ── Titolo schermata ────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(hdr);
    lv_label_set_text(title, "Dispositivi RS485");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, NT_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 68, 0);  // 68px = after back btn

    // ── Label stato (contatore / progresso scan) ────────────────────────────
    // Posizionata a 280px dal bordo sinistro, a destra del titolo
    // Positioned at 280px from left edge, right of title
    s_status_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_status_lbl, NT_DIM, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_LEFT_MID, 280, 0);

    // ── Pulsante "Salva impianto" ────────────────────────────────────────────
    // Blu → azione importante ma non pericolosa
    // Blue → important but not dangerous action
    lv_obj_t* save_btn = lv_btn_create(hdr);
    lv_obj_set_size(save_btn, 170, 40);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -214, 0);  // A sinistra del scan btn
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x355D9B), 0);         // Blu
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x24467A), LV_STATE_PRESSED);  // Blu scuro
    lv_obj_set_style_radius(save_btn, 8, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);

    s_save_btn_lbl = lv_label_create(save_btn);
    lv_label_set_text(s_save_btn_lbl, LV_SYMBOL_SAVE " Salva");
    lv_obj_set_style_text_font(s_save_btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_save_btn_lbl, lv_color_white(), 0);
    lv_obj_center(s_save_btn_lbl);
    lv_obj_add_event_cb(save_btn, _save_btn_cb, LV_EVENT_CLICKED, NULL);

    // ── Pulsante "Scansiona" (in alto a destra) ─────────────────────────────
    // Arancione → azione primaria / call-to-action
    // Orange → primary action / call-to-action
    lv_obj_t* scan_btn = lv_btn_create(hdr);
    lv_obj_set_size(scan_btn, 190, 40);
    lv_obj_align(scan_btn, LV_ALIGN_RIGHT_MID, -12, 0);   // 12px dal bordo destro
    lv_obj_set_style_bg_color(scan_btn, NT_ORANGE, 0);
    lv_obj_set_style_bg_color(scan_btn, NT_ORANGE2, LV_STATE_PRESSED);
    lv_obj_set_style_radius(scan_btn, 8, 0);
    lv_obj_set_style_shadow_width(scan_btn, 0, 0);
    lv_obj_set_style_border_width(scan_btn, 0, 0);

    s_scan_btn_lbl = lv_label_create(scan_btn);
    lv_label_set_text(s_scan_btn_lbl, LV_SYMBOL_REFRESH " Scansiona");
    lv_obj_set_style_text_font(s_scan_btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_scan_btn_lbl, lv_color_white(), 0);
    lv_obj_center(s_scan_btn_lbl);
    lv_obj_add_event_cb(scan_btn, _scan_btn_cb, LV_EVENT_CLICKED, NULL);

    // ── Area lista ───────────────────────────────────────────────────────────
    // Body = contenitore trasparente che occupa tutto lo spazio sotto l'header
    // Body = transparent container occupying all space below the header
    lv_obj_t* body = lv_obj_create(scr);
    lv_obj_set_size(body, 1024, 540);      // 600 - 60 = 540px di altezza disponibile
    lv_obj_set_pos(body, 0, HEADER_H);    // Inizia subito dopo l'header
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);

    // Contenitore scrollabile delle righe — card bianca con bordo e ombra
    // Scrollable container of rows — white card with border and shadow
    s_list_cont = lv_obj_create(body);
    lv_obj_set_size(s_list_cont, 984, LV_SIZE_CONTENT);  // Larghezza fissa, altezza dinamica
    lv_obj_align(s_list_cont, LV_ALIGN_TOP_MID, 0, 16);  // 16px di margine in alto
    lv_obj_set_style_bg_color(s_list_cont, NT_WHITE, 0);
    lv_obj_set_style_bg_opa(s_list_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_list_cont, NT_BORDER, 0);
    lv_obj_set_style_border_width(s_list_cont, 1, 0);
    lv_obj_set_style_radius(s_list_cont, 12, 0);         // Angoli arrotondati
    lv_obj_set_style_pad_all(s_list_cont, 0, 0);
    lv_obj_set_style_shadow_color(s_list_cont, NT_SHADOW, 0);
    lv_obj_set_style_shadow_width(s_list_cont, 16, 0);
    lv_obj_set_style_shadow_ofs_y(s_list_cont, 4, 0);

    // Layout flex column: le righe vengono impilate verticalmente
    // Flex column layout: rows are stacked vertically
    lv_obj_set_layout(s_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list_cont, LV_FLEX_FLOW_COLUMN);

    // ── Inizializzazione e prima costruzione della lista ─────────────────────
    s_last_count    = -1;   // Forza il primo aggiornamento nel timer
    // Timer inizia a 500ms (pronto per eventuale scansione in corso)
    // Timer starts at 500ms (ready for possible ongoing scan)
    s_refresh_timer = lv_timer_create(_refresh_timer_cb, 500, NULL);

    // Costruisce la lista iniziale prima che il timer scatti
    // Builds the initial list before the timer fires
    _rebuild_list();

    return scr;
}
