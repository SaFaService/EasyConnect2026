# EasyConnect Display Controller — Architettura DataModel e Sistema Template

> Documento di progettazione. Descrive l'architettura target dopo la separazione logica/UI.
> Non riflette lo stato attuale del codice ma la direzione di implementazione concordata.

---

## 1. Visione generale

L'obiettivo è separare completamente il **motore** (business logic, protocolli, persistenza) dall'**interfaccia grafica** (LVGL, layout, animazioni), in modo che:

- Lo stesso firmware possa presentare **template grafici diversi** a seconda del cliente, senza toccare la logica
- Un nuovo template non possa rompere la logica di controllo (nessun include di RS485, WiFi o NVS)
- Aggiornamenti al motore non rompano i template già sviluppati
- Il cliente possa sviluppare un portale web usando un **contratto JSON immutabile** garantito versione per versione

```
┌────────────────────────────────────────────────────────────────────┐
│                        FIRMWARE LAYERS                             │
├────────────────┬───────────────────────────────────────────────────┤
│  TEMPLATE UI   │  ui_theme_classic/  ui_theme_compact/  ui_theme_X │
│  (LVGL only)   │  Legge g_dc_model — scrive via dc_cmd_*()         │
├────────────────┴───────────────────────────────────────────────────┤
│  SHARED UI     │  Splash condiviso + Pagina Impostazioni condivisa  │
├────────────────────────────────────────────────────────────────────┤
│  DATA MODEL    │  include/dc_data_model.h — struct DcDataModel      │
│  (read-only    │  g_dc_model — aggiornato solo dal Controller       │
│   per la UI)   │                                                    │
├────────────────────────────────────────────────────────────────────┤
│  CONTROLLER    │  src/dc_controller.cpp                             │
│                │  Aggiorna snapshot RS485, WiFi, ambiente           │
│                │  Esegue safeguard, comandi, OTA                    │
├──────────┬─────┴───────┬──────────┬──────────┬─────────────────────┤
│ RS485    │  WiFi/API   │ NVS/     │ Serial   │ OTA                 │
│ network  │  Manager    │ Settings │ CLI      │ Manager             │
└──────────┴─────────────┴──────────┴──────────┴─────────────────────┘
```

---

## 2. DataModel — `include/dc_data_model.h`

Unica fonte di verità per tutta l'interfaccia grafica. Aggiornato periodicamente dal Controller (loop principale, Core 0 o task dedicato). Letto dall'UI sempre e solo in sola lettura.

### 2.1 Snapshot dispositivo RS485

```cpp
struct DcDeviceSnapshot {
    bool     valid;
    uint8_t  address;
    uint8_t  group;           // gruppo impianto (1=aspirazione, 2=immissione, …)
    uint8_t  type;            // Rs485DevType: SENSOR, RELAY, MOTOR_010V

    // — Relay —
    bool     relay_on;
    uint8_t  relay_mode;      // 0=manuale, 1=auto, 2=timer

    // — Motore 0/10V —
    bool     motor_enabled;
    uint8_t  speed_pct;       // 0-100

    // — Sensore pressione/temperatura/umidità —
    float    pressure_pa;
    float    temp_c;
    float    hum_rh;
    bool     pressure_valid;
    bool     temp_valid;
    bool     hum_valid;

    // — Diagnostica —
    bool     online;
    bool     safety_fault;
    bool     feedback_fault;
    uint16_t relay_starts;
    uint32_t uptime_hours;
    char     sn[16];
    char     fw[8];
};
```

### 2.2 Snapshot rete RS485

```cpp
#define DC_MAX_DEVICES 32

struct DcNetworkSnapshot {
    int              device_count;
    DcDeviceSnapshot devices[DC_MAX_DEVICES];
    unsigned long    last_update_ms;
    bool             scan_running;
    int              scan_progress;   // 0-200
};
```

### 2.3 Ambiente (sensore SHTC3 locale)

```cpp
struct DcEnvironment {
    float temp_c;
    float hum_rh;
    bool  valid;
    unsigned long last_read_ms;
};
```

### 2.4 WiFi

```cpp
enum class DcWifiState : uint8_t {
    DISABLED,
    SCANNING,
    CONNECTING,
    CONNECTED,
    FAILED
};

struct DcWifi {
    DcWifiState state;
    int         rssi;
    char        ssid[33];
    char        ip[16];
    bool        internet_reachable;
    unsigned long connected_since_ms;
};
```

### 2.5 API Server

```cpp
enum class DcApiState : uint8_t {
    IDLE,
    SENDING,
    OK,
    ERROR
};

struct DcApi {
    DcApiState    state;
    unsigned long last_ok_ms;
    unsigned long last_attempt_ms;
    uint32_t      send_count;
    uint32_t      error_count;
    int           last_http_code;
    char          last_error[64];
};
```

### 2.6 Air Safeguard runtime

```cpp
struct DcSafeguardState {
    bool  active;
    float duct_temp_ema;
    float duct_hum_ema;
    int   boost_speed_pct;
    int   base_speed_pct;
    unsigned long active_since_ms;
};
```

### 2.7 Impostazioni persistite

```cpp
struct DcSettings {
    // Display
    int     brightness_pct;       // 5–100
    int     screensaver_min;      // 3/5/10/15
    int     temp_unit;            // 0=C, 1=F
    char    plant_name[48];
    uint8_t ui_theme_id;          // 0=Classic, 1=Compact, …

    // Ventilazione
    int     vent_min_pct;
    int     vent_max_pct;
    int     vent_steps;           // 0=continuo
    bool    intake_bar_enabled;
    int     intake_diff_pct;

    // Air Safeguard
    bool    safeguard_enabled;
    int     safeguard_temp_max_c;
    int     safeguard_hum_max_rh;

    // Rete
    bool    wifi_enabled;
    char    wifi_ssid[33];
    // password mai nel DataModel — rimane in NVS cifrata

    // API
    bool    api_factory_enabled;
    bool    api_customer_enabled;
    char    api_customer_url[128];
    // chiavi API mai nel DataModel — rimangono in NVS

    // OTA
    bool    ota_auto_enabled;
    char    ota_channel[16];      // "stable" / "beta"
};
```

### 2.8 Notifiche aggregate

```cpp
#define DC_MAX_NOTIF 32

struct DcNotification {
    uint8_t  severity;    // 0=info, 1=warning, 2=alarm
    uint8_t  device_addr;
    uint8_t  code;
    char     message[48];
    unsigned long timestamp_ms;
};

struct DcNotifications {
    DcNotification items[DC_MAX_NOTIF];
    int            count;
    int            unread_count;
    int            alarm_count;
    int            warning_count;
};
```

### 2.9 DataModel principale

```cpp
struct DcDataModel {
    DcNetworkSnapshot  network;
    DcEnvironment      environment;
    DcWifi             wifi;
    DcApi              api;
    DcSafeguardState   safeguard;
    DcSettings         settings;
    DcNotifications    notifications;

    bool  system_safety_trip;
    bool  system_bypass_active;
    char  fw_version[16];
    char  device_serial[24];
};

extern DcDataModel g_dc_model;
```

---

## 3. Controller — `src/dc_controller.cpp`

### 3.1 Responsabilità

- Aggiorna `g_dc_model.network` interrogando RS485 (snapshot periodico, es. ogni 1s)
- Aggiorna `g_dc_model.environment` dal sensore SHTC3
- Aggiorna `g_dc_model.wifi` dallo stato del driver WiFi
- Aggiorna `g_dc_model.api` dall'API Manager
- Esegue `dc_air_safeguard_service()` (ex `ui_air_safeguard_service`)
- Gestisce la state machine OTA
- **Non include mai header LVGL**

### 3.2 API comandi (UI → Controller)

L'UI non chiama mai RS485 direttamente. Tutte le azioni utente passano qui:

```cpp
// src/dc_controller.h

// Dispositivi
bool dc_cmd_relay_set(uint8_t address, bool on);
bool dc_cmd_motor_enable(uint8_t address, bool enable);
bool dc_cmd_motor_speed(uint8_t address, uint8_t speed_pct);

// Impostazioni (salvano in NVS e aggiornano g_dc_model.settings)
void dc_settings_brightness_set(int pct);
void dc_settings_plant_name_set(const char* name);
void dc_settings_wifi_set(bool enabled, const char* ssid, const char* pass);
void dc_settings_theme_set(uint8_t theme_id);
// … (una funzione per ogni setting)

// Azioni di sistema
void dc_wifi_reconnect();
void dc_scan_rs485();
void dc_ota_check();
void dc_ota_start();
void dc_factory_reset();

// Loop principale
void dc_controller_init();
void dc_controller_service();   // chiamato da loop()
```

### 3.3 Aggiornamento snapshot RS485

Il controller aggiorna lo snapshot a intervalli (default 1s), **fuori dal lock LVGL**. L'UI legge lo snapshot (già pronto, nessuna chiamata RS485) dentro il timer `_home_sync_cb`.

```
Core 0 loop():  dc_controller_service() → aggiorna g_dc_model.network (senza LVGL)
Core 1 LVGL:    _home_sync_cb()        → legge g_dc_model.network     (senza RS485)
```

Questo elimina il cross-core coupling attuale.

---

## 4. Gestione WiFi

### 4.1 Vincolo hardware critico

Sul Display Controller (ESP32-S3), il driver WiFi **deve essere inizializzato prima di LVGL** per riservare memoria DMA contigua (~80 KB). Dopo l'init LVGL la heap DMA è troppo frammentata. Questo vincolo è fisso e non dipende dall'architettura.

### 4.2 State machine WiFi (nel Controller)

```
DISABLED → (utente abilita) → BOOT_CONNECT → CONNECTING → CONNECTED
                                                        ↘ RETRY_WAIT → CONNECTING
                                                        ↘ FAILED (max tentativi esauriti)
CONNECTED → (perdita segnale) → RECONNECTING → CONNECTED / FAILED
```

Il controller gestisce la state machine, aggiorna `g_dc_model.wifi`, e l'UI mostra solo lo stato. La pagina Impostazioni chiama `dc_settings_wifi_set()` e `dc_wifi_reconnect()`.

### 4.3 Display guard

Il meccanismo `waveshare_rgb_lcd_activity_guard_acquire/release()` rimane nel controller. Viene attivato automaticamente durante le fasi di connessione WiFi per proteggere il bus display da interferenze DMA.

### 4.4 WiFi dalla CLI seriale (admin)

```
WIFI SCAN                  — elenca reti disponibili
WIFI CONNECT <ssid> <pass> — avvia connessione (richiede password admin)
WIFI STATUS                — mostra stato corrente
WIFI DISABLE               — disabilita WiFi
```

---

## 5. Connessione API Server

### 5.1 Architettura

Il modulo `DisplayApi_Manager` (già esistente) rimane invariato come layer di trasporto. Il controller lo interroga tramite `displayApiService()` e aggiorna `g_dc_model.api`.

### 5.2 Doppio endpoint

| Endpoint | Scopo | Configurabile da |
|---|---|---|
| **Factory** (Antralux) | Telemetria ufficiale, OTA, supervisione | Solo CLI seriale admin |
| **Customer** | Portale cliente custom | Impostazioni UI + CLI |

### 5.3 Frequenza invio

Il controller decide la frequenza di invio in base allo stato:
- WiFi connesso + dati stabili → ogni 60s (configurabile)
- Post-comando (relay/motore azionato) → invio immediato
- WiFi non disponibile → accumulo locale in ring buffer (max 100 record), invio batch al reconnect

---

## 6. Gestione Impostazioni di Sistema

Le impostazioni sono divise in due categorie:

### 6.1 Impostazioni UI (visibili all'utente finale)

Gestite da `dc_settings.cpp`, salvate in NVS namespace `easy_disp`:

- Luminosità, screensaver, unità temperatura, nome impianto, tema
- Parametri ventilazione (min/max/steps/immissione)
- Air safeguard (soglie temp/umidità)
- WiFi (abilitazione, SSID)

### 6.2 Impostazioni di sistema (solo admin)

Gestite da `dc_settings.cpp`, salvate in NVS namespace `easy_sys`. Accessibili solo via:
- CLI seriale con password admin
- API con chiave factory

Includono:
- Calibrazioni sensori (offset pressione, temperatura)
- Configurazione motori (indirizzo RS485, gruppo, direzione)
- Configurazione relay (tipo, gruppo, logica di sicurezza)
- Soglie di allarme impianto
- Endpoint API factory, chiavi API
- Seriale centralina
- Canale OTA (stable/beta)
- Password admin CLI

```cpp
struct DcSystemConfig {
    // Calibrazioni
    float   pressure_offset_pa[DC_MAX_DEVICES];
    float   temp_offset_c[DC_MAX_DEVICES];

    // Motori
    uint8_t motor_group[DC_MAX_DEVICES];
    bool    motor_reversed[DC_MAX_DEVICES];

    // Relay
    uint8_t relay_type[DC_MAX_DEVICES];   // LIGHT, UVC, ELECTRO, …
    uint8_t relay_group[DC_MAX_DEVICES];
    bool    relay_safety_protected[DC_MAX_DEVICES];

    // Admin
    char    admin_password_hash[65];  // SHA-256 hex, mai in chiaro
    char    device_serial[24];
    char    api_factory_url[128];
    char    api_factory_key_hash[65];
    char    ota_channel[16];
};
```

---

## 7. CLI Seriale con Password Admin

### 7.1 Modalità operativa

La CLI ha due livelli di accesso:

| Livello | Comando sblocco | Comandi disponibili |
|---|---|---|
| **User** | nessuno | INFO, STATUS, HELP, 485list, 485scan |
| **Admin** | `AUTH <password>` | Tutti i comandi User + comandi di sistema |

La sessione admin dura 5 minuti di inattività, poi torna automaticamente a User.

### 7.2 Comandi admin aggiuntivi

```
AUTH <password>            — sblocca sessione admin
LOGOUT                     — chiude sessione admin

SETSERIAL <sn>             — imposta seriale centralina
SETAPIURL <url>            — endpoint API factory
SETAPIKEY <key>            — chiave API factory (non mostrata in INFO)
SETADMINPW <old> <new>     — cambia password admin

CALPRESS <addr> <offset>   — calibrazione offset pressione
CALTEMP <addr> <offset>    — calibrazione offset temperatura
SETGROUP <addr> <group>    — assegna dispositivo a gruppo impianto
SETRELAYTYPE <addr> <type> — LIGHT|UVC|ELECTRO|COMMAND

WIFISCAN                   — scansione reti
WIFICONNECT <ssid> <pass>  — connette a rete
WIFIDISABLE                — disabilita WiFi

OTACHECK                   — verifica aggiornamenti disponibili
OTASTART                   — avvia aggiornamento OTA
OTACHANNEL <stable|beta>   — imposta canale

FACTORYRESET               — reset completo (richiede conferma: FACTORYRESET CONFIRM)
```

### 7.3 Sicurezza password

- La password non viene mai salvata in chiaro
- Salvato solo il digest SHA-256 in NVS namespace `easy_sys`
- Default di fabbrica: una password derivata dal seriale centralina
- Il comando `AUTH` calcola SHA-256 dell'input e confronta il digest

---

## 8. JSON API Contract — Versione 1.0 (IMMUTABILE)

### 8.1 Principio di immutabilità

Una volta rilasciata la versione 1.0 del contratto JSON, **nessun campo esistente viene rimosso o modificato**. Nuovi campi possono essere aggiunti in versioni future (1.1, 1.2, …) ma rimangono opzionali e non rompono i client esistenti. I clienti che sviluppano un portale custom possono fare affidamento su questo contratto a tempo indeterminato.

### 8.2 Struttura payload telemetria (Controller → Server)

```json
{
  "api_version": "1.0",
  "device": {
    "serial": "EC-00123",
    "fw_version": "1.1.26",
    "plant_name": "Impianto Rossi",
    "uptime_s": 86400
  },
  "timestamp_ms": 1745234567890,
  "environment": {
    "temp_c": 23.4,
    "hum_rh": 55.2,
    "valid": true
  },
  "wifi": {
    "connected": true,
    "rssi": -62,
    "ssid": "Rete_Azienda"
  },
  "devices": [
    {
      "address": 1,
      "group": 1,
      "type": "MOTOR_010V",
      "online": true,
      "motor_enabled": true,
      "speed_pct": 75,
      "temp_c": 28.1,
      "hum_rh": 48.0,
      "temp_valid": true,
      "hum_valid": true,
      "sn": "MT-0042",
      "fw": "2.3"
    },
    {
      "address": 5,
      "group": 1,
      "type": "RELAY",
      "online": true,
      "relay_on": false,
      "relay_mode": 0,
      "relay_starts": 142,
      "uptime_hours": 1204,
      "safety_fault": false,
      "feedback_fault": false,
      "sn": "RL-0017",
      "fw": "1.8"
    },
    {
      "address": 10,
      "group": 1,
      "type": "SENSOR",
      "online": true,
      "pressure_pa": 12.4,
      "temp_c": 22.8,
      "hum_rh": 51.0,
      "pressure_valid": true,
      "temp_valid": true,
      "hum_valid": true,
      "sn": "SN-0088",
      "fw": "1.2"
    }
  ],
  "alerts": [
    {
      "severity": "warning",
      "device_address": 5,
      "code": 3,
      "message": "Feedback fault relay",
      "timestamp_ms": 1745234500000
    }
  ],
  "safeguard": {
    "active": false,
    "duct_temp_c": 26.3,
    "duct_hum_rh": 52.1,
    "boost_speed_pct": 0
  }
}
```

### 8.3 Struttura comandi (Server → Controller) — API bidirezionale

```json
{
  "api_version": "1.0",
  "command": "relay_set",
  "device_address": 5,
  "params": {
    "on": true
  }
}
```

```json
{
  "api_version": "1.0",
  "command": "motor_speed",
  "device_address": 1,
  "params": {
    "speed_pct": 80
  }
}
```

```json
{
  "api_version": "1.0",
  "command": "motor_enable",
  "device_address": 1,
  "params": {
    "enable": true
  }
}
```

```json
{
  "api_version": "1.0",
  "command": "settings_set",
  "params": {
    "plant_name": "Nuovo Nome",
    "vent_min_pct": 25,
    "vent_max_pct": 90
  }
}
```

```json
{
  "api_version": "1.0",
  "command": "ota_start",
  "params": {
    "channel": "stable"
  }
}
```

### 8.4 Risposta standard del server al payload

```json
{
  "api_version": "1.0",
  "status": "ok",
  "pending_commands": [
    {
      "command": "relay_set",
      "device_address": 5,
      "params": { "on": true }
    }
  ],
  "ota": {
    "update_available": false,
    "version": "",
    "url": ""
  }
}
```

### 8.5 Comandi disponibili via API (elenco completo v1.0)

| Comando | Parametri | Accesso |
|---|---|---|
| `relay_set` | address, on | Customer + Factory |
| `motor_enable` | address, enable | Customer + Factory |
| `motor_speed` | address, speed_pct | Customer + Factory |
| `settings_set` | vedi DcSettings | Customer + Factory |
| `rs485_scan` | — | Factory |
| `ota_check` | — | Factory |
| `ota_start` | channel | Factory |
| `factory_reset` | confirm_token | Factory |
| `calibration_set` | address, pressure_offset, temp_offset | Factory |
| `device_group_set` | address, group | Factory |

---

## 9. OTA (Over-The-Air Update)

### 9.1 Trigger

L'aggiornamento OTA può essere avviato da:
- Risposta API server (campo `ota.update_available=true` con URL e versione)
- Comando API `ota_start` (solo endpoint factory)
- Comando CLI seriale `OTASTART` (solo sessione admin)
- Automaticamente se `ota_auto_enabled=true` e nuova versione disponibile

### 9.2 Flusso

```
1. Controller riceve trigger OTA (URL firmware + hash SHA-256)
2. Controller aggiorna g_dc_model con stato OTA: DOWNLOADING
3. UI condivisa mostra overlay "Aggiornamento in corso…" (non dipende dal template)
4. Download in PSRAM/flash partition OTA1
5. Verifica hash SHA-256
6. Se OK → reboot su partizione OTA1
7. Se KO → rollback partizione precedente, notifica errore
```

### 9.3 Protezione display durante OTA

Come per il WiFi, viene attivato il display guard durante il download per proteggere il bus RGB.

---

## 10. Splash Screen condiviso

### 10.1 Motivazione

La splash screen gestisce la sequenza di inizializzazione hardware, che è **identica per qualsiasi template**. Mantenerla condivisa elimina il rischio di dimenticare un passaggio in un template nuovo.

### 10.2 Barra progresso reale

Invece di una progressione temporale simulata, la barra mostra avanzamento reale:

| Step | % | Operazione |
|---|---|---|
| 0 | 0% | Boot |
| 1 | 15% | Display + touch init |
| 2 | 25% | LVGL init |
| 3 | 35% | RS485 init |
| 4 | 45% | Sensore SHTC3 |
| 5 | 55% | RTC / clock init |
| 6 | 65% | Caricamento impostazioni NVS |
| 7 | 75% | Selezione tema UI |
| 8 | 85% | Creazione home screen (template) |
| 9 | 95% | Tentativo connessione WiFi |
| 10 | 100% | Pronto |

Il Controller espone `dc_boot_set_progress(int step)` che la splash legge da `g_dc_model.boot_step`.

### 10.3 Interfaccia splash

```cpp
// Unica funzione chiamata da main, indipendente dal tema
lv_obj_t* ui_splash_shared_create(void);
void      ui_splash_shared_set_step(int step); // aggiorna barra
void      ui_splash_shared_complete(void);     // avvia fade-out verso home
```

---

## 11. Pagina Impostazioni condivisa

### 11.1 Motivazione

Le impostazioni coprono calibrazioni, ventilazione, sensori, WiFi, API — contenuto tecnico che richiede attenzione agli errori. Tenerle condivise tra tutti i template garantisce che ogni correzione o aggiunta venga applicata a tutti.

### 11.2 Struttura

```
Impostazioni (condivisa, ui_settings_shared.cpp)
├── Utente
│   ├── Nome impianto
│   ├── Luminosità / Screensaver
│   └── Unità temperatura
├── Connessione
│   ├── WiFi (scan, SSID, stato)
│   ├── API Cliente (URL, test connessione)
│   └── Stato API Factory (solo lettura)
├── Setup Sistema  [solo admin — sblocco con PIN]
│   ├── Calibrazioni sensori
│   ├── Configurazione dispositivi RS485
│   ├── Tipi relay e protezioni
│   └── Seriale centralina
├── Ventilazione
│   ├── Velocità min/max
│   ├── Step
│   └── Barra immissione
├── Filtraggio
│   └── Air safeguard (soglie, abilita)
└── Sensori
    └── Visualizzazione calibrazioni attive
```

### 11.3 Sblocco sezione Sistema

La sezione "Setup Sistema" è protetta da PIN a 6 cifre (diverso dalla password admin CLI). Il PIN viene salvato in NVS namespace `easy_sys` come hash.

---

## 12. Sistema Template

### 12.1 Struttura file

```
src/ui/
├── shared/
│   ├── ui_splash_shared.cpp/.h       ← splash unica per tutti
│   ├── ui_settings_shared.cpp/.h     ← impostazioni uniche per tutti
│   └── ui_notifications_shared.cpp/.h
├── theme_classic/
│   ├── ui_theme_classic.cpp/.h       ← registra UiTheme
│   ├── ui_tc_home.cpp/.h
│   └── ui_tc_network.cpp/.h
├── theme_compact/
│   ├── ui_theme_compact.cpp/.h
│   └── ui_tc_home.cpp/.h
└── ui_theme_registry.cpp/.h          ← gestione selezione tema runtime
```

### 12.2 Interfaccia tema

```cpp
// include/ui_theme_interface.h

struct UiTheme {
    uint8_t     id;
    const char* name;
    // Schermate che il tema DEVE implementare
    lv_obj_t* (*create_home)(void);
    lv_obj_t* (*create_network)(void);
    // Opzionali — se NULL viene usata la versione shared
    lv_obj_t* (*create_notifications)(void);   // NULL → usa shared
};

void      ui_theme_register(const UiTheme* theme);
void      ui_theme_activate(uint8_t theme_id);  // carica da g_dc_model.settings.ui_theme_id
UiTheme*  ui_theme_get_active(void);
```

### 12.3 Cosa vede il template

Il template include **solo**:
```cpp
#include "dc_data_model.h"      // lettura stato
#include "dc_controller.h"      // invio comandi (dc_cmd_*)
#include "lvgl.h"               // rendering
#include "ui_styles.h"          // palette e stili condivisi
#include "icons/icons_index.h"  // icone
```

Nessun include di `rs485_network.h`, `WiFi.h`, `Preferences.h`, `API_Manager.h`.

---

## 13. Roadmap implementazione

### Fase 1 — Fondamenta (prerequisito per tutto il resto)

1. Creare `include/dc_data_model.h` con tutte le struct (1 giorno)
2. Creare `src/dc_settings.cpp/.h` — sposta tutta la logica NVS fuori dall'UI (2 giorni)
3. Creare `src/dc_controller.cpp/.h` — snapshot RS485, environment, WiFi, safeguard (3 giorni)
4. Aggiornare `main_display_controller.cpp` — usa dc_controller_init/service (1 giorno)
5. Aggiornare `ui_dc_home.cpp` — legge da g_dc_model, comandi via dc_cmd_* (4 giorni)

**Totale Fase 1: ~11 giorni lavorativi**

### Fase 2 — UI condivisa e primo template

6. Splash condivisa con barra progresso reale (2 giorni)
7. Impostazioni condivise con struttura a sezioni (3 giorni)
8. Estrazione tema Classic (rinominare e isolare i file ui_dc_* esistenti) (2 giorni)

**Totale Fase 2: ~7 giorni lavorativi**

### Fase 3 — API e CLI admin

9. Estendere CLI seriale con livelli admin + password (2 giorni)
10. Definire e implementare JSON contract v1.0 (3 giorni)
11. Gestione comandi pending dalla risposta API (2 giorni)

**Totale Fase 3: ~7 giorni lavorativi**

### Fase 4 — Secondo template

12. Progettazione grafica tema Compact/alternativo
13. Implementazione (1–2 settimane a seconda della complessità grafica)
14. Selezione tema da Impostazioni + salvataggio NVS + reload al boot

**Totale Fase 4: ~10 giorni lavorativi**

### Timeline complessiva

| Fase | Contenuto | Giorni |
|---|---|---|
| 1 | Fondamenta DataModel/Controller | 11 |
| 2 | UI condivisa + tema Classic | 7 |
| 3 | API JSON v1.0 + CLI admin | 7 |
| 4 | Tema alternativo | 10 |
| **Totale** | | **~35 giorni** |

---

## 14. Invarianti da rispettare sempre

1. `g_dc_model` è scritto **solo** dal Controller (mai dall'UI)
2. Il Controller non include mai header LVGL
3. I template non includono mai `rs485_network.h`, `WiFi.h`, `Preferences.h`
4. Il JSON contract v1.0 non subisce breaking changes — nuovi campi solo in nuove versioni minori
5. Password e chiavi API non compaiono mai in `g_dc_model` — restano in NVS
6. Il lock LVGL (`lvgl_port_lock/unlock`) è usato solo nell'UI, mai nel Controller
7. Il display guard WiFi è gestito solo dal Controller
