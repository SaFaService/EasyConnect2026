#pragma once
#include <Arduino.h>
#include <stdint.h>

// Numero massimo di dispositivi rilevabili in una scansione.
#define RS485_NET_MAX_DEVICES 200

// Tipo di dispositivo RS485 rilevato dalla risposta.
enum class Rs485DevType : uint8_t {
    SENSOR  = 0,   // Risposta: OK,T,H,P,SIC,GRP,SN,VER!
    RELAY   = 1,   // Risposta: OK,RELAY,mode,...!
    UNKNOWN = 2,
};

// Profilo dispositivo sensore (non relay).
enum class Rs485SensorProfile : uint8_t {
    PRESSURE = 0,   // Scheda pressione (TT=04)
    AIR_010  = 1,   // Scheda 0/10 (gruppo intake/extraction)
    UNKNOWN  = 2,
};

// Dati di un dispositivo rilevato sulla rete RS485.
struct Rs485Device {
    uint8_t      address;        // Indirizzo RS485 (1-200)
    char         sn[32];         // Seriale (YYYYMMXXNNNN)
    char         version[16];    // Versione firmware
    Rs485DevType type;
    float        t;              // Temperatura (solo SENSOR)
    float        h;              // Umidita     (solo SENSOR)
    float        p;              // Pressione   (solo SENSOR)
    uint8_t      group;          // Gruppo dispositivo (se disponibile)
    bool         sensor_active;  // Stato digitale sensore/0-10 (se disponibile)
    bool         sensor_feedback_ok;            // Feedback 0-10V, se disponibile
    bool         sensor_feedback_fault_latched; // Fault feedback 0-10V temporizzato
    Rs485SensorProfile sensor_profile;
    uint8_t      sensor_mode;    // 1=TH, 2=P, 3=ALL (se esposto dal firmware sensore)
    bool         data_valid;     // false se la periferica risponde ma non ha ancora dati/config coerenti
    uint8_t      relay_mode;         // 1=LUCE,2=UVC,3=ELETTROSTATICO,4=GAS,5=COMANDO
    bool         relay_on;           // Stato uscita relay
    bool         relay_safety_closed;
    bool         relay_feedback_ok;
    bool         relay_feedback_fault_latched;
    bool         relay_life_expired;
    bool         online;             // true se la periferica ha risposto agli ultimi controlli
    bool         in_plant;           // true se appartiene alla fotografia impianto salvata
    uint8_t      comm_failures;      // contatore runtime di miss consecutivi
    char         relay_state[24];    // Stato testuale relay (es. RUNNING/FAULT)
    char         sensor_state[24];   // Stato testuale sensore/0-10V (es. RUNNING/FAULT)
};

// Stato della scansione RS485.
enum class Rs485ScanState : uint8_t {
    IDLE    = 0,
    RUNNING = 1,
    DONE    = 2,
};

// Stato del controllo rapido eseguito in boot (splash) sulle periferiche in cache.
enum class Rs485BootProbeState : uint8_t {
    IDLE    = 0,
    RUNNING = 1,
    DONE    = 2,
};

// Inizializza Serial1 e il pin DIR. Chiamare una volta nel setup().
void rs485_network_init();

// Esegue una query puntuale su un indirizzo RS485 (formato richiesta: ?<id>!).
// Ritorna true se riceve una risposta "OK,...!" entro il timeout.
// raw_response contiene la risposta senza '!' finale (vuota in caso di timeout/errore).
bool rs485_network_ping(uint8_t address, String& raw_response);

// Come ping, ma in piu' effettua il parsing dei dati in out.
// out.type puo' essere SENSOR, RELAY o UNKNOWN.
bool rs485_network_query_device(uint8_t address, Rs485Device& out, String& raw_response);

// Invia comando relay diretto all'indirizzo (frame: RLY,<id>,<ACTION>!).
// action supportate: ON, OFF, TOGGLE.
// Ritorna true se la periferica risponde con OK,RELAY,CMD,...
bool rs485_network_relay_command(uint8_t address, const char* action, String& raw_response);

// Invia il comando velocita' alla scheda 0/10V (frame: SPD<id>:<percent>!).
// percent e' espresso 0-100 e viene validato lato controller.
bool rs485_network_motor_speed_command(uint8_t address, uint8_t percent, String& raw_response);

// Abilita/disabilita la scheda 0/10V (frame: ENA<id>:<0|1>!).
bool rs485_network_motor_enable_command(uint8_t address, bool enable, String& raw_response);

// Avvia una scansione asincrona degli indirizzi 1-200.
// No-op se una scansione e' gia' in corso.
void rs485_network_scan_start();

// Stato corrente della scansione.
Rs485ScanState rs485_network_scan_state();

// Indirizzo correntemente scansionato (1-200, 0 se IDLE).
int rs485_network_scan_progress();

// Numero di dispositivi trovati (aggiornato anche durante la scansione).
int rs485_network_device_count();

// Restituisce il dispositivo all'indice idx (0-based, ordinati per indirizzo).
// Ritorna false se idx e' fuori range.
bool rs485_network_get_device(int idx, Rs485Device& out);

// Restituisce il device con address specificato, se presente nella lista runtime.
bool rs485_network_get_device_by_address(uint8_t address, Rs485Device& out);

// Numero di periferiche presenti nella cache persistente (ultima scansione salvata).
int rs485_network_cached_device_count();

// Numero di periferiche presenti nella fotografia impianto salvata.
int rs485_network_plant_device_count();

// True se e' presente una fotografia impianto salvata.
bool rs485_network_has_saved_plant();

// Salva come fotografia impianto lo stato runtime corrente.
// Vengono salvate solo le periferiche attualmente online.
// Ritorna false se la rete e' occupata o l'operazione non e' consentita.
bool rs485_network_save_current_as_plant();

// Elimina una periferica dalla fotografia impianto usando address + seriale salvato.
// Ritorna false se la periferica non esiste o se la rete e' occupata.
bool rs485_network_remove_device_from_plant(uint8_t address, const char* serial_number);

// Avvia il controllo rapido durante splash sugli indirizzi in cache (no scansione 1..200).
// Aggiorna la lista runtime con i soli dispositivi che rispondono.
void rs485_network_boot_probe_start();

// Stato corrente del boot probe.
Rs485BootProbeState rs485_network_boot_probe_state();

// Abilita/disabilita monitor seriale RS485 TX/RX.
// Quando attivo stampa su Serial i frame inviati e ricevuti dal controller.
void rs485_network_set_monitor_enabled(bool enabled);
bool rs485_network_is_monitor_enabled();
