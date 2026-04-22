# EasyConnect Display Controller â€” ROADMAP Implementazione

> **Questo file Ã¨ la guida operativa per ogni sessione di lavoro.**
> Aggiornare lo stato dei task ad ogni sessione completata.
> Per la visione architetturale completa: `documentazione/Architettura_DataModel_e_Template.md`

---

## Stato attuale: FASE 3 Task 3.2 ✅ COMPLETATO → prossimo: FASE 3 Task 3.3

```
FASE 1 [â–“â–“â–“â–“â–“â–“â–“â–“â–“â–“]  completata
FASE 2 [██████████]  completata
FASE 3 [â–“â–“â–“â–“â–‘â–‘â–‘â–‘â–‘â–‘]  in corso
FASE 4 [â–‘â–‘â–‘â–‘â–‘â–‘â–‘â–‘â–‘â–‘]  non iniziata
```

---

## Regole per ogni sessione di lavoro

1. **Leggere questo file per primo** â€” capire dove si Ã¨ rimasti
2. **Leggere solo i file del task corrente** â€” non esplorare liberamente
3. **Compilare mentalmente** prima di scrivere â€” nessuna modifica senza aver letto il file target
4. **Aggiornare questo file** al termine di ogni sessione (stato task, prossimo task)
5. **Non toccare** RS485, WebHandler, OTA_Manager, Calibration salvo richiesta esplicita
6. **Al completamento di ogni task**: chiedere all'utente di eseguire i comandi git riportati nella sezione "Checkpoint git" del task prima di procedere al successivo

---

## FASE 1 â€” Fondamenta DataModel / Controller

**Obiettivo:** Separare completamente logica e UI. Al termine di questa fase, `ui_dc_home.cpp`
non conterrÃ  piÃ¹ chiamate dirette a RS485 o NVS.

### Task 1.1 â€” Header contratti âœ… COMPLETATO

File creati (non modificare senza aggiornare la doc):
- `include/dc_data_model.h` â€” struct DcDataModel e tutte le sotto-struct
- `include/dc_controller.h` â€” API comandi (dc_cmd_*) e dc_controller_service()
- `include/dc_settings.h`   â€” API impostazioni (dc_settings_*_get/set)

### Task 1.2 â€” dc_settings.cpp âœ… COMPLETATO

> **Checkpoint git eseguito:**
> ```
> git add include/dc_data_model.h include/dc_controller.h include/dc_settings.h src/dc_settings.cpp src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.2: dc_settings â€” NVS logic separated from UI"
> git tag phase1-task1.2
> ```

**Obiettivo:** Spostare tutta la logica NVS fuori da `ui_dc_home.cpp`.

File da creare: `src/dc_settings.cpp`
File da leggere prima di iniziare:
- `include/dc_settings.h` (contratto da implementare)
- `src/ui/ui_dc_home.cpp` righe 56â€“548 (logica NVS attuale da migrare)

Operazioni:
1. Creare `src/dc_settings.cpp` che implementa tutte le funzioni di `dc_settings.h`
2. La variabile `g_dc_model.settings` viene popolata da `dc_settings_load()` all'avvio
3. Ogni `dc_settings_X_set()` salva in NVS **e** aggiorna `g_dc_model.settings`
4. Le funzioni `ui_brightness_*`, `ui_screensaver_*`, `ui_temperature_unit_*`,
   `ui_plant_name_*`, `ui_ventilation_*`, `ui_air_safeguard_*` in `ui_dc_home.cpp`
   diventano thin wrapper che chiamano il corrispondente `dc_settings_X_set/get()`
5. Non eliminare ancora le funzioni `ui_*` â€” solo reindirizzarle (backward compat)

NVS namespace: `easy_disp` (invariato â€” non rompere dati giÃ  salvati dai clienti)
NVS chiavi esistenti da preservare: `br_pct`, `scr_min`, `temp_u`, `plant_name`,
`vent_min`, `vent_max`, `vent_steps`, `imm_bar`, `imm_pct`, `sg_en`, `sg_tmax`, `sg_hmax`

Criteri di completamento:
- `src/dc_settings.cpp` compila senza errori
- `ui_dc_home.cpp` non contiene piÃ¹ chiamate dirette a `Preferences` / `g_ui_pref`
- I valori letti/scritti sono identici a prima (nessuna regressione NVS)

### Task 1.3 â€” dc_controller.cpp (parte 1: snapshot RS485 + environment) âœ… COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_controller.cpp
> git commit -m "Phase1 Task1.3: dc_controller â€” RS485/wifi/env snapshot"
> git tag phase1-task1.3
> ```

### Task 1.4 â€” dc_controller.cpp (parte 2: air safeguard) âœ… COMPLETATO

### Task 1.4b â€” Fix build e snapshot âœ… COMPLETATO

Problemi emersi da audit post-1.4:

1. **build_src_filter** â€” `dc_settings.cpp` e `dc_controller.cpp` mancavano dalla build del target `controller_display` â†’ link error.
2. **doppia definizione `g_dc_model`** â€” definita in `dc_settings.cpp` e `dc_controller.cpp`; rimossa da `dc_settings.cpp` (extern giÃ  in `dc_data_model.h`).
3. **`_snapshot_device()` â€” AIR_010** â€” `speed_pct` restava sempre 0 (`dev.h` = velocitÃ  per AIR_010); `temp_valid`/`hum_valid` marcati erroneamente su AIR_010.
4. **`dc_air_safeguard_service()`** â€” non aggiornava `g_dc_model.safeguard`; ora popola `active`, `duct_temp_ema`, `duct_hum_ema`, `boost_speed_pct`, `base_speed_pct`, `active_since_ms`.
5. **`_update_wifi()`** â€” non rispettava `settings.wifi_enabled`; `connected_since_ms` mai aggiornato.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add platformio.ini src/dc_settings.cpp src/dc_controller.cpp
> git commit -m "Phase1 Task1.4b: fix build, snapshot AIR_010, safeguard state, wifi"
> git tag phase1-task1.4b
> ```

File da leggere prima di iniziare:
- `src/ui/ui_dc_home.cpp` righe 1229â€“1356 (helper safeguard) + 1357â€“1530 (service)
- `include/dc_controller.h`

Operazioni:
1. Spostare le struct `AirSafeguardDuctSample`, `AirSafeguardMotorGroup`, `AirSafeguardRuntime`
   in `src/dc_controller.cpp` (sono implementation detail, non nel header)
2. Spostare le funzioni `_air_safeguard_*` da `ui_dc_home.cpp` a `dc_controller.cpp`
3. Rinominare `ui_air_safeguard_service()` â†’ `dc_air_safeguard_service()` (implementazione in controller)
4. In `ui_dc_home.cpp`: `ui_air_safeguard_service()` diventa stub che chiama `dc_air_safeguard_service()`
5. In `main_display_controller.cpp`: la chiamata rimane invariata per ora

âš ï¸ **Test: FLASH OBBLIGATORIO** â€” verificare che il safeguard si comporti identicamente.
Testare: ventilatore a velocitÃ  ridotta â†’ superare soglia â†’ boost attivo â†’ rientro.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/dc_controller.cpp src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.4: air safeguard moved to dc_controller"
> git tag phase1-task1.4
> ```

### Task 1.5 â€” Aggiornamento main_display_controller.cpp âœ… COMPLETATO

File da leggere prima di iniziare:
- `src/main_display_controller.cpp` (tutto)
- `include/dc_controller.h`

Operazioni:
1. `setup()`: aggiungere `dc_controller_init()` dopo init display
2. `loop()`: sostituire il blocco SHTC3 + `ui_dc_home_set_environment()` con
   `dc_controller_service(t, h, valid)` che fa tutto internamente
3. La chiamata `ui_air_safeguard_service()` nel loop viene rimossa
   (ora Ã¨ dentro `dc_controller_service()`)

âš ï¸ **Test: FLASH OBBLIGATORIO** â€” verificare comportamento completo del loop:
WiFi boot, lettura SHTC3 in header, RS485 scan, safeguard attivo.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/main_display_controller.cpp
> git commit -m "Phase1 Task1.5: main loop uses dc_controller_service"
> git tag phase1-task1.5
> ```

### Task 1.6 â€” Aggiornamento ui_dc_home.cpp (lettura da DataModel) âœ… COMPLETATO

**Questo Ã¨ il task piÃ¹ lungo e delicato della Fase 1.**

File da leggere prima di iniziare:
- `src/ui/ui_dc_home.cpp` (tutto â€” 2928 righe)
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
- `ui_dc_home.cpp` non include piÃ¹ `rs485_network.h`
- `ui_dc_home.cpp` non include piÃ¹ `<Preferences.h>`
- Il firmware compila e il comportamento visivo Ã¨ identico a prima

âš ï¸ **Test: FLASH + REGRESSIONE COMPLETA** â€” questo Ã¨ il task piÃ¹ grande.
Verificare: tile RS485, slider velocitÃ , comandi relay, WiFi indicator, notifiche, safeguard.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add src/ui/ui_dc_home.cpp
> git commit -m "Phase1 Task1.6: ui_dc_home reads only g_dc_model"
> git tag phase1-task1.6
> git tag phase1-complete
> ```

---

## FASE 2 â€” UI condivisa e isolamento tema Classic

*Iniziare solo dopo completamento Fase 1.*

### Task 2.1 â€” Splash condivisa con progresso reale âœ… COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/ui/shared/ui_splash_shared.cpp src/ui/shared/ui_splash_shared.h src/main_display_controller.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase2 Task2.1: shared splash with real boot progress"
> git tag phase2-task2.1
> ```

File creati:
- `src/ui/shared/ui_splash_shared.cpp/.h` â€” splash condivisa, timer 150 ms legge g_dc_model.boot

Modifiche:
- `main_display_controller.cpp` â€” splash creata subito dopo LVGL; dc_boot_set_step() a ogni passo
- `platformio.ini` â€” sostituito ui_dc_splash.cpp con ui/shared/ui_splash_shared.cpp

Note post-migrazione:
- `src/ui/ui_dc_splash.cpp` non Ã¨ piÃ¹ compilata; puÃ² essere eliminata dopo test su hardware.
- rs485_network_boot_probe_start() spostata da splash a setup() (step 3).
- Home si carica quando boot.complete==true E tâ‰¥3500 ms dalla splash creation.

### Task 2.2 — Impostazioni condivise ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add include/dc_settings.h src/dc_settings.cpp src/ui/shared/ui_settings_shared.cpp src/ui/shared/ui_settings_shared.h src/ui/ui_dc_home.cpp src/ui/ui_dc_settings.h platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase2 Task2.2: shared settings with system PIN lock"
> git tag phase2-task2.2
> ```

File creati:
- `src/ui/shared/ui_settings_shared.cpp/.h` — nuova schermata impostazioni condivisa

Modifiche:
- `src/dc_settings.cpp/.h` — aggiunta gestione PIN sistema hashato in NVS `easy_sys` chiave `sys_pin_hash`
- `src/ui/ui_dc_home.cpp` — apertura schermata shared
- `src/ui/ui_dc_settings.h` — wrapper di compatibilità verso la schermata shared
- `platformio.ini` — sostituito `ui/ui_dc_settings.cpp` con `ui/shared/ui_settings_shared.cpp`

Note post-migrazione:
- `src/ui/ui_dc_settings.cpp` rimosso dalla codebase
- Le impostazioni sono ora organizzate in 6 sezioni: Utente, Connessione, Setup Sistema, Ventilazione, Filtraggio, Sensori
- Data e ora sono confluite nella sezione Utente
- La sezione Setup Sistema richiede PIN a 6 cifre; al primo accesso il PIN viene configurato e salvato come hash SHA-256
- `controller_display` compila correttamente dopo la migrazione

### Task 2.3 â€” Isolamento tema Classic âœ… COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add include/ui_theme_interface.h src/ui/ui_theme_registry.cpp src/ui/ui_dc_home.cpp src/ui/ui_dc_network.cpp src/ui/theme_classic/ui_tc_home.cpp src/ui/theme_classic/ui_tc_home.h src/ui/theme_classic/ui_tc_network.cpp src/ui/theme_classic/ui_tc_network.h src/ui/theme_classic/ui_theme_classic.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase2 Task2.3: isolate Classic theme behind registry"
> git tag phase2-task2.3
> git tag phase2-complete
> ```

File creati:
- `include/ui_theme_interface.h` â€” contratto `UiTheme` + API registry/activate/create
- `src/ui/ui_theme_registry.cpp` â€” registry temi con attivazione per `ui_theme_id`
- `src/ui/theme_classic/ui_tc_home.h` / `ui_tc_network.h` â€” entrypoint tema Classic
- `src/ui/theme_classic/ui_theme_classic.cpp` â€” registrazione tema Classic (id 0)

Modifiche:
- `src/ui/ui_dc_home.cpp` â€” wrapper compatibile che inoltra la creazione home al tema attivo
- `src/ui/ui_dc_network.cpp` â€” wrapper compatibile che inoltra la creazione network al tema attivo
- `src/ui/theme_classic/ui_tc_home.cpp` â€” implementazione Classic spostata fuori dal layer pubblico
- `src/ui/theme_classic/ui_tc_network.cpp` â€” implementazione Classic spostata fuori dal layer pubblico
- `platformio.ini` â€” build aggiornata con registry e file `theme_classic`

Note post-migrazione:
- L'API pubblica legacy `ui_dc_home_create()` / `ui_dc_network_create()` resta invariata per i call site esistenti
- Il tema Classic Ã¨ registrato come `ui_theme_id = 0` ed Ã¨ fallback automatico se il tema richiesto non esiste
- Build `controller_display` verificata con successo dopo il refactor

---

## FASE 3 â€” CLI admin + API JSON v1.0

*Iniziare solo dopo completamento Fase 2.*

### Task 3.1 â€” CLI seriale livelli admin âœ… COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp src/dc_admin_cli.h src/main_display_controller.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase3 Task3.1: add admin serial CLI with auth levels"
> git tag phase3-task3.1
> ```

File creati:
- `src/dc_admin_cli.cpp/.h` â€” nuova CLI seriale modulare con livelli User/Admin

Modifiche:
- `src/main_display_controller.cpp` â€” rimossa la CLI inline; setup/loop ora delegano a `dc_admin_cli_*`
- `platformio.ini` â€” aggiunto `dc_admin_cli.cpp` alla build `controller_display`

Note post-implementazione:
- Livello default `User`; sblocco `Admin` via `AUTH <password>` con timeout di inattivita di 5 minuti
- Password admin hashata SHA-256 in NVS namespace `easy_sys`, chiave `adm_pw_hash`
- Se `adm_pw_hash` non esiste, la password di default e derivata dal seriale centralina
- I comandi architetturali non ancora supportati dai task successivi (`CAL*`, `SETGROUP`, `SETRELAYTYPE`, `OTA*`, `OTACHANNEL`) sono esposti e rispondono esplicitamente come pending
- Build `controller_display` verificata con successo dopo la migrazione

### Task 3.2 â€” JSON contract v1.0 âœ… COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_api_json.cpp src/dc_api_json.h src/DisplayApi_Manager.cpp src/dc_controller.cpp include/dc_controller.h platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase3 Task3.2: implement JSON API contract v1.0"
> git tag phase3-task3.2
> ```

File creati:
- `src/dc_api_json.cpp/.h` â€” builder payload v1.0 e parser comandi API con livelli Customer/Factory

Modifiche:
- `src/DisplayApi_Manager.cpp` â€” usa `dc_api_build_payload()`, aggiorna `g_dc_model.api` e processa `pending_commands`
- `src/dc_controller.cpp` / `include/dc_controller.h` â€” aggiunta API `dc_factory_reset()` per il comando remoto omonimo
- `platformio.ini` â€” aggiunto `dc_api_json.cpp` alla build `controller_display`

Note post-implementazione:
- Il payload telemetria e il parsing comandi leggono solo `g_dc_model` e chiamano solo `dc_cmd_*` / `dc_settings_*`
- I comandi `relay_set`, `motor_enable`, `motor_speed`, `settings_set`, `rs485_scan` e `factory_reset` sono gestiti
- I comandi OTA e quelli di calibrazione/gruppo restano esplicitamente pending fino ai task successivi
- Build `controller_display` da verificare dopo integrazione

### Task 3.3 â€” OTA dal controller ðŸ”²
- Integrare OTA trigger nella risposta API (campo `ota.update_available`)
- Aggiungere overlay UI condiviso "Aggiornamento in corsoâ€¦" (non dipende dal tema)
- Trigger da CLI admin: `OTACHECK`, `OTASTART`

---

## FASE 4 â€” Secondo template

*Iniziare solo dopo completamento Fase 3.*

### Task 4.1 â€” Design grafico secondo tema ðŸ”²
- Da concordare con il cliente prima di scrivere codice
- Definire: layout, palette, animazioni, widget custom

### Task 4.2 â€” Implementazione tema ðŸ”²
- Creare `src/ui/theme_X/`
- Implementare le funzioni `create_home()` e `create_network()` della struct UiTheme
- Il tema legge SOLO `g_dc_model`, chiama SOLO `dc_cmd_*`

### Task 4.3 â€” Selezione tema in impostazioni ðŸ”²
- Aggiungere voce "Tema interfaccia" nella sezione Utente delle impostazioni
- `dc_settings_theme_set(uint8_t id)` â†’ salva in NVS + aggiorna g_dc_model.settings.ui_theme_id
- Al boot: `ui_theme_activate(g_dc_model.settings.ui_theme_id)` dopo splash

---

### Task 1.7 â€” Unificazione WiFi/API/safety nel DataModel âœ… COMPLETATO

**Obiettivo:** Chiudere i gap di coerenza trovati dall'analisi post-Fase1.

Problemi risolti:
1. **WiFi**: `ui_dc_settings.cpp` scriveva NVS direttamente bypassando `g_dc_model.settings.wifi_enabled`.
   â†’ Rimossi `_wifi_pref_enabled_get/set` e `_wifi_pref_save_credentials`; sostituiti con
     `dc_settings_wifi_set()` / `dc_settings_wifi_enabled_get()`.
2. **API customer**: `DisplayApi_Manager` usava chiavi NVS `"custApiUrl"`/`"custApiKey"` diverse da
   `dc_settings` (`"cust_url"`/`"cust_key"`). Aggiunto anche `_wifi_api_enabled()` â†’ DataModel.
   â†’ `displayApiSetCustomerUrl/Key()` ora scrivono `"cust_url"`/`"cust_key"`;
     `displayApiLoadConfig()` legge new key con fallback a old key per migrazione dati esistenti;
     `_api_popup_submit()` aggiorna `g_dc_model.settings.api_customer_url` in real-time.
3. **Safety/bypass**: variabili locali `g_system_bypass_active` / `g_system_safety_trip_active`
   in `ui_dc_home.cpp` erano invisibili al DataModel.
   â†’ Rimosse; sostituite con `g_dc_model.system_bypass_active` / `g_dc_model.system_safety_trip`.

> **Checkpoint git da eseguire:**
> ```
> git add src/DisplayApi_Manager.cpp src/ui/ui_dc_settings.cpp src/ui/ui_dc_home.cpp documentazione/ROADMAP.md
> git commit -m "Phase1 Task1.7: unify WiFi/API/safety via DataModel"
> git tag phase1-task1.7
> ```

---

## Note per la sessione corrente

- Branch di lavoro corrente: `freeze/controller_display-2026-04-03`
- Prossimo task da eseguire: **FASE 3 — Task 3.3 — OTA dal controller**
- File da leggere all'inizio della prossima sessione:
  1. Questo file (ROADMAP.md)
  2. `documentazione/Architettura_DataModel_e_Template.md` (Â§8 JSON API, Â§9 OTA)
  3. `src/dc_api_json.cpp`
  4. `src/DisplayApi_Manager.cpp`

---

## Guida ai checkpoint git

Ad ogni task completato Claude chiederÃ  di eseguire questi comandi nel terminale PlatformIO.
Eseguirli **prima** di chiedere il task successivo. In caso di problemi:

```bash
# Torna all'ultimo checkpoint funzionante
git checkout phase1-task1.X   # sostituire X con il numero del tag

# Vedi tutti i tag salvati
git tag

# Vedi lo storico dei checkpoint
git log --oneline --decorate
```
