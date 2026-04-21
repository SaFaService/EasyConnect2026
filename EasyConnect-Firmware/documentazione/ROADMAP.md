# EasyConnect Display Controller — ROADMAP Implementazione

> **Questo file è la guida operativa per ogni sessione di lavoro.**
> Aggiornare lo stato dei task ad ogni sessione completata.
> Per la visione architetturale completa: `documentazione/Architettura_DataModel_e_Template.md`

---

## Stato attuale: FASE 1 — Task 1.5 prossimo

```
FASE 1 [▓▓▓▓▓░░░░░]  5/11 giorni
FASE 2 [░░░░░░░░░░]  non iniziata
FASE 3 [░░░░░░░░░░]  non iniziata
FASE 4 [░░░░░░░░░░]  non iniziata
```

---

## Regole per ogni sessione di lavoro

1. **Leggere questo file per primo** — capire dove si è rimasti
2. **Leggere solo i file del task corrente** — non esplorare liberamente
3. **Compilare mentalmente** prima di scrivere — nessuna modifica senza aver letto il file target
4. **Aggiornare questo file** al termine di ogni sessione (stato task, prossimo task)
5. **Non toccare** RS485, WebHandler, OTA_Manager, Calibration salvo richiesta esplicita
6. **Al completamento di ogni task**: chiedere all'utente di eseguire i comandi git riportati nella sezione "Checkpoint git" del task prima di procedere al successivo

---

## FASE 1 — Fondamenta DataModel / Controller

**Obiettivo:** Separare completamente logica e UI. Al termine di questa fase, `ui_dc_home.cpp`
non conterrà più chiamate dirette a RS485 o NVS.

### Task 1.1 — Header contratti ✅ COMPLETATO

File creati (non modificare senza aggiornare la doc):
- `include/dc_data_model.h` — struct DcDataModel e tutte le sotto-struct
- `include/dc_controller.h` — API comandi (dc_cmd_*) e dc_controller_service()
- `include/dc_settings.h`   — API impostazioni (dc_settings_*_get/set)

### Task 1.2 — dc_settings.cpp ✅ COMPLETATO

> **Checkpoint git eseguito:**
> ```
> git add include/dc_data_model.h include/dc_controller.h include/dc_settings.h src/dc_settings.cpp src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.2: dc_settings — NVS logic separated from UI"
> git tag phase1-task1.2
> ```

**Obiettivo:** Spostare tutta la logica NVS fuori da `ui_dc_home.cpp`.

File da creare: `src/dc_settings.cpp`
File da leggere prima di iniziare:
- `include/dc_settings.h` (contratto da implementare)
- `src/ui/ui_dc_home.cpp` righe 56–548 (logica NVS attuale da migrare)

Operazioni:
1. Creare `src/dc_settings.cpp` che implementa tutte le funzioni di `dc_settings.h`
2. La variabile `g_dc_model.settings` viene popolata da `dc_settings_load()` all'avvio
3. Ogni `dc_settings_X_set()` salva in NVS **e** aggiorna `g_dc_model.settings`
4. Le funzioni `ui_brightness_*`, `ui_screensaver_*`, `ui_temperature_unit_*`,
   `ui_plant_name_*`, `ui_ventilation_*`, `ui_air_safeguard_*` in `ui_dc_home.cpp`
   diventano thin wrapper che chiamano il corrispondente `dc_settings_X_set/get()`
5. Non eliminare ancora le funzioni `ui_*` — solo reindirizzarle (backward compat)

NVS namespace: `easy_disp` (invariato — non rompere dati già salvati dai clienti)
NVS chiavi esistenti da preservare: `br_pct`, `scr_min`, `temp_u`, `plant_name`,
`vent_min`, `vent_max`, `vent_steps`, `imm_bar`, `imm_pct`, `sg_en`, `sg_tmax`, `sg_hmax`

Criteri di completamento:
- `src/dc_settings.cpp` compila senza errori
- `ui_dc_home.cpp` non contiene più chiamate dirette a `Preferences` / `g_ui_pref`
- I valori letti/scritti sono identici a prima (nessuna regressione NVS)

### Task 1.3 — dc_controller.cpp (parte 1: snapshot RS485 + environment) ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_controller.cpp
> git commit -m "Phase1 Task1.3: dc_controller — RS485/wifi/env snapshot"
> git tag phase1-task1.3
> ```

### Task 1.4 — dc_controller.cpp (parte 2: air safeguard) ✅ COMPLETATO

File da leggere prima di iniziare:
- `src/ui/ui_dc_home.cpp` righe 1229–1356 (helper safeguard) + 1357–1530 (service)
- `include/dc_controller.h`

Operazioni:
1. Spostare le struct `AirSafeguardDuctSample`, `AirSafeguardMotorGroup`, `AirSafeguardRuntime`
   in `src/dc_controller.cpp` (sono implementation detail, non nel header)
2. Spostare le funzioni `_air_safeguard_*` da `ui_dc_home.cpp` a `dc_controller.cpp`
3. Rinominare `ui_air_safeguard_service()` → `dc_air_safeguard_service()` (implementazione in controller)
4. In `ui_dc_home.cpp`: `ui_air_safeguard_service()` diventa stub che chiama `dc_air_safeguard_service()`
5. In `main_display_controller.cpp`: la chiamata rimane invariata per ora

⚠️ **Test: FLASH OBBLIGATORIO** — verificare che il safeguard si comporti identicamente.
Testare: ventilatore a velocità ridotta → superare soglia → boost attivo → rientro.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/dc_controller.cpp src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.4: air safeguard moved to dc_controller"
> git tag phase1-task1.4
> ```

### Task 1.5 — Aggiornamento main_display_controller.cpp 🔲 PROSSIMO

File da leggere prima di iniziare:
- `src/main_display_controller.cpp` (tutto)
- `include/dc_controller.h`

Operazioni:
1. `setup()`: aggiungere `dc_controller_init()` dopo init display
2. `loop()`: sostituire il blocco SHTC3 + `ui_dc_home_set_environment()` con
   `dc_controller_service(t, h, valid)` che fa tutto internamente
3. La chiamata `ui_air_safeguard_service()` nel loop viene rimossa
   (ora è dentro `dc_controller_service()`)

⚠️ **Test: FLASH OBBLIGATORIO** — verificare comportamento completo del loop:
WiFi boot, lettura SHTC3 in header, RS485 scan, safeguard attivo.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/main_display_controller.cpp
> git commit -m "Phase1 Task1.5: main loop uses dc_controller_service"
> git tag phase1-task1.5
> ```

### Task 1.6 — Aggiornamento ui_dc_home.cpp (lettura da DataModel) 🔲

**Questo è il task più lungo e delicato della Fase 1.**

File da leggere prima di iniziare:
- `src/ui/ui_dc_home.cpp` (tutto — 2928 righe)
- `include/dc_data_model.h`
- `include/dc_controller.h`

Punti di intervento (in ordine):

**A. Timer `_home_sync_cb` (riga ~2084)**
Attuale: chiama direttamente RS485 tramite _sync_home_tiles_from_network_state() ecc.
Target: leggere `g_dc_model.network.devices[]` e `g_dc_model.wifi`

**B. Build tile al primo render (riga ~666, ~880, ~906, ~923, ~938, ~981)**
Attuale: `rs485_network_device_count()` + `rs485_network_get_device()`
Target: `g_dc_model.network.device_count` + `g_dc_model.network.devices[i]`

**C. Comandi touch nei callback (riga ~1827, ~1856, ~1910, ~2110, ~2147)**
Attuale: `rs485_network_motor_speed_command()`, `rs485_network_relay_command()`
Target: `dc_cmd_motor_speed()`, `dc_cmd_relay_set()`

**D. WiFi indicator (riga ~2803)**
Attuale: `rs485_network_device_count() > 0`
Target: `g_dc_model.network.device_count > 0`

Criteri di completamento:
- `ui_dc_home.cpp` non include più `rs485_network.h`
- `ui_dc_home.cpp` non include più `<Preferences.h>`
- Il firmware compila e il comportamento visivo è identico a prima

⚠️ **Test: FLASH + REGRESSIONE COMPLETA** — questo è il task più grande.
Verificare: tile RS485, slider velocità, comandi relay, WiFi indicator, notifiche, safeguard.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.6: ui_dc_home reads only g_dc_model"
> git tag phase1-task1.6
> git tag phase1-complete
> ```

---

## FASE 2 — UI condivisa e isolamento tema Classic

*Iniziare solo dopo completamento Fase 1.*

### Task 2.1 — Splash condivisa con progresso reale 🔲
- Creare `src/ui/shared/ui_splash_shared.cpp/.h`
- Aggiungere `boot_step` e `boot_step_label` a DcDataModel
- 10 step reali mappati su operazioni di setup() (vedi doc architettura §10)
- Rimuovere la splash attuale `ui_dc_splash.cpp` dopo migrazione

### Task 2.2 — Impostazioni condivise 🔲
- Creare `src/ui/shared/ui_settings_shared.cpp/.h`
- Struttura a 6 sezioni (Utente, Connessione, Setup Sistema, Ventilazione, Filtraggio, Sensori)
- Sezione Sistema protetta da PIN 6 cifre (NVS `easy_sys`, chiave `sys_pin_hash`)
- Rimuovere `ui_dc_settings.cpp` dopo migrazione

### Task 2.3 — Isolamento tema Classic 🔲
- Creare directory `src/ui/theme_classic/`
- Spostare (non copiare) `ui_dc_home.cpp` → `theme_classic/ui_tc_home.cpp`
- Spostare `ui_dc_network.cpp` → `theme_classic/ui_tc_network.cpp`
- Creare `theme_classic/ui_theme_classic.cpp` che registra la struct UiTheme
- Aggiungere `include/ui_theme_interface.h` e `src/ui/ui_theme_registry.cpp`

---

## FASE 3 — CLI admin + API JSON v1.0

*Iniziare solo dopo completamento Fase 2.*

### Task 3.1 — CLI seriale livelli admin 🔲
- Aggiungere `src/dc_admin_cli.cpp/.h`
- Due livelli: User (default) / Admin (AUTH + password)
- Timeout sessione admin: 5 minuti
- Password → SHA-256, salvata in NVS `easy_sys` chiave `adm_pw_hash`
- Vedi elenco comandi in doc architettura §7

### Task 3.2 — JSON contract v1.0 🔲
- Creare `src/dc_api_json.cpp/.h`
- Funzione `dc_api_build_payload(char* buf, size_t len)` — serializza g_dc_model
- Funzione `dc_api_parse_command(const char* json)` — esegue comandi pending
- Struttura JSON: vedi doc architettura §8 (IMMUTABILE da questo punto in poi)
- Integrare con DisplayApi_Manager esistente

### Task 3.3 — OTA dal controller 🔲
- Integrare OTA trigger nella risposta API (campo `ota.update_available`)
- Aggiungere overlay UI condiviso "Aggiornamento in corso…" (non dipende dal tema)
- Trigger da CLI admin: `OTACHECK`, `OTASTART`

---

## FASE 4 — Secondo template

*Iniziare solo dopo completamento Fase 3.*

### Task 4.1 — Design grafico secondo tema 🔲
- Da concordare con il cliente prima di scrivere codice
- Definire: layout, palette, animazioni, widget custom

### Task 4.2 — Implementazione tema 🔲
- Creare `src/ui/theme_X/`
- Implementare le funzioni `create_home()` e `create_network()` della struct UiTheme
- Il tema legge SOLO `g_dc_model`, chiama SOLO `dc_cmd_*`

### Task 4.3 — Selezione tema in impostazioni 🔲
- Aggiungere voce "Tema interfaccia" nella sezione Utente delle impostazioni
- `dc_settings_theme_set(uint8_t id)` → salva in NVS + aggiorna g_dc_model.settings.ui_theme_id
- Al boot: `ui_theme_activate(g_dc_model.settings.ui_theme_id)` dopo splash

---

## Note per la sessione corrente

- Branch di lavoro consigliato: `refactor/datamodel-phase1`
  (creare con: `git checkout -b refactor/datamodel-phase1`)
- Branch di partenza: `freeze/controller_display-2026-04-03`
- Prossimo task da eseguire: **Task 1.5 — main_display_controller.cpp**
- File da leggere all'inizio della prossima sessione:
  1. Questo file (ROADMAP.md)
  2. `src/main_display_controller.cpp` (tutto)
  3. `include/dc_controller.h`

---

## Guida ai checkpoint git

Ad ogni task completato Claude chiederà di eseguire questi comandi nel terminale PlatformIO.
Eseguirli **prima** di chiedere il task successivo. In caso di problemi:

```bash
# Torna all'ultimo checkpoint funzionante
git checkout phase1-task1.X   # sostituire X con il numero del tag

# Vedi tutti i tag salvati
git tag

# Vedi lo storico dei checkpoint
git log --oneline --decorate
```
