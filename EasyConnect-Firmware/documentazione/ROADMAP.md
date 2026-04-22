# EasyConnect Display Controller — ROADMAP Implementazione

> **Questo file è la guida operativa per ogni sessione di lavoro.**
> Aggiornare lo stato dei task ad ogni sessione completata.
> Per la visione architetturale completa: `documentazione/Architettura_DataModel_e_Template.md`

---

## Stato attuale: FASE 3 Task 3.2 ✅ COMPLETATO → prossimo: Fix Prioritari

```
FASE 1 [██████████]  completata
FASE 2 [██████████]  completata
FASE 3 [████░░░░░░]  in corso
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

### Task 1.4b — Fix build e snapshot ✅ COMPLETATO

Problemi emersi da audit post-1.4:

1. **build_src_filter** — `dc_settings.cpp` e `dc_controller.cpp` mancavano dalla build del target `controller_display` → link error.
2. **doppia definizione `g_dc_model`** — definita in `dc_settings.cpp` e `dc_controller.cpp`; rimossa da `dc_settings.cpp` (extern già in `dc_data_model.h`).
3. **`_snapshot_device()` — AIR_010** — `speed_pct` restava sempre 0 (`dev.h` = velocità per AIR_010); `temp_valid`/`hum_valid` marcati erroneamente su AIR_010.
4. **`dc_air_safeguard_service()`** — non aggiornava `g_dc_model.safeguard`; ora popola `active`, `duct_temp_ema`, `duct_hum_ema`, `boost_speed_pct`, `base_speed_pct`, `active_since_ms`.
5. **`_update_wifi()`** — non rispettava `settings.wifi_enabled`; `connected_since_ms` mai aggiornato.

> **Checkpoint git da eseguire al completamento:**
> ```
> git add platformio.ini src/dc_settings.cpp src/dc_controller.cpp
> git commit -m "Phase1 Task1.4b: fix build, snapshot AIR_010, safeguard state, wifi"
> git tag phase1-task1.4b
> ```

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

### Task 1.5 — Aggiornamento main_display_controller.cpp ✅ COMPLETATO

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

### Task 1.6 — Aggiornamento ui_dc_home.cpp (lettura da DataModel) ✅ COMPLETATO

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

### Task 1.7 — Unificazione WiFi/API/safety nel DataModel ✅ COMPLETATO

**Obiettivo:** Chiudere i gap di coerenza trovati dall'analisi post-Fase1.

Problemi risolti:
1. **WiFi**: `ui_dc_settings.cpp` scriveva NVS direttamente bypassando `g_dc_model.settings.wifi_enabled`.
   → Rimossi `_wifi_pref_enabled_get/set` e `_wifi_pref_save_credentials`; sostituiti con
     `dc_settings_wifi_set()` / `dc_settings_wifi_enabled_get()`.
2. **API customer**: `DisplayApi_Manager` usava chiavi NVS `"custApiUrl"`/`"custApiKey"` diverse da
   `dc_settings` (`"cust_url"`/`"cust_key"`). Aggiunto anche `_wifi_api_enabled()` → DataModel.
   → `displayApiSetCustomerUrl/Key()` ora scrivono `"cust_url"`/`"cust_key"`;
     `displayApiLoadConfig()` legge new key con fallback a old key per migrazione dati esistenti;
     `_api_popup_submit()` aggiorna `g_dc_model.settings.api_customer_url` in real-time.
3. **Safety/bypass**: variabili locali `g_system_bypass_active` / `g_system_safety_trip_active`
   in `ui_dc_home.cpp` erano invisibili al DataModel.
   → Rimosse; sostituite con `g_dc_model.system_bypass_active` / `g_dc_model.system_safety_trip`.

> **Checkpoint git da eseguire:**
> ```
> git add src/DisplayApi_Manager.cpp src/ui/ui_dc_settings.cpp src/ui/ui_dc_home.cpp documentazione/ROADMAP.md
> git commit -m "Phase1 Task1.7: unify WiFi/API/safety via DataModel"
> git tag phase1-task1.7
> ```

---

## FASE 2 — UI condivisa e isolamento tema Classic

*Iniziare solo dopo completamento Fase 1.*

### Task 2.1 — Splash condivisa con progresso reale ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/ui/shared/ui_splash_shared.cpp src/ui/shared/ui_splash_shared.h src/main_display_controller.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase2 Task2.1: shared splash with real boot progress"
> git tag phase2-task2.1
> ```

File creati:
- `src/ui/shared/ui_splash_shared.cpp/.h` — splash condivisa, timer 150 ms legge g_dc_model.boot

Modifiche:
- `main_display_controller.cpp` — splash creata subito dopo LVGL; dc_boot_set_step() a ogni passo
- `platformio.ini` — sostituito ui_dc_splash.cpp con ui/shared/ui_splash_shared.cpp

Note post-migrazione:
- `src/ui/ui_dc_splash.cpp` non è più compilata; può essere eliminata dopo test su hardware.
- rs485_network_boot_probe_start() spostata da splash a setup() (step 3).
- Home si carica quando boot.complete==true E t≥3500 ms dalla splash creation.

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

### Task 2.3 — Isolamento tema Classic ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add include/ui_theme_interface.h src/ui/ui_theme_registry.cpp src/ui/ui_dc_home.cpp src/ui/ui_dc_network.cpp src/ui/theme_classic/ui_tc_home.cpp src/ui/theme_classic/ui_tc_home.h src/ui/theme_classic/ui_tc_network.cpp src/ui/theme_classic/ui_tc_network.h src/ui/theme_classic/ui_theme_classic.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase2 Task2.3: isolate Classic theme behind registry"
> git tag phase2-task2.3
> git tag phase2-complete
> ```

File creati:
- `include/ui_theme_interface.h` — contratto `UiTheme` + API registry/activate/create
- `src/ui/ui_theme_registry.cpp` — registry temi con attivazione per `ui_theme_id`
- `src/ui/theme_classic/ui_tc_home.h` / `ui_tc_network.h` — entrypoint tema Classic
- `src/ui/theme_classic/ui_theme_classic.cpp` — registrazione tema Classic (id 0)

Modifiche:
- `src/ui/ui_dc_home.cpp` — wrapper compatibile che inoltra la creazione home al tema attivo
- `src/ui/ui_dc_network.cpp` — wrapper compatibile che inoltra la creazione network al tema attivo
- `src/ui/theme_classic/ui_tc_home.cpp` — implementazione Classic spostata fuori dal layer pubblico
- `src/ui/theme_classic/ui_tc_network.cpp` — implementazione Classic spostata fuori dal layer pubblico
- `platformio.ini` — build aggiornata con registry e file `theme_classic`

Note post-migrazione:
- L'API pubblica legacy `ui_dc_home_create()` / `ui_dc_network_create()` resta invariata per i call site esistenti
- Il tema Classic è registrato come `ui_theme_id = 0` ed è fallback automatico se il tema richiesto non esiste
- Build `controller_display` verificata con successo dopo il refactor

---

## FASE 3 — CLI admin + API JSON v1.0

*Iniziare solo dopo completamento Fase 2.*

### Task 3.1 — CLI seriale livelli admin ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp src/dc_admin_cli.h src/main_display_controller.cpp platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase3 Task3.1: add admin serial CLI with auth levels"
> git tag phase3-task3.1
> ```

File creati:
- `src/dc_admin_cli.cpp/.h` — nuova CLI seriale modulare con livelli User/Admin

Modifiche:
- `src/main_display_controller.cpp` — rimossa la CLI inline; setup/loop ora delegano a `dc_admin_cli_*`
- `platformio.ini` — aggiunto `dc_admin_cli.cpp` alla build `controller_display`

Note post-implementazione:
- Livello default `User`; sblocco `Admin` via `AUTH <password>` con timeout di inattività di 5 minuti
- Password admin hashata SHA-256 in NVS namespace `easy_sys`, chiave `adm_pw_hash`
- Se `adm_pw_hash` non esiste, la password di default è derivata dal seriale centralina
- I comandi architetturali non ancora supportati dai task successivi (`CAL*`, `SETGROUP`, `SETRELAYTYPE`, `OTA*`, `OTACHANNEL`) sono esposti e rispondono esplicitamente come pending
- Build `controller_display` verificata con successo dopo la migrazione

### Task 3.2 — JSON contract v1.0 ✅ COMPLETATO

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_api_json.cpp src/dc_api_json.h src/DisplayApi_Manager.cpp src/dc_controller.cpp include/dc_controller.h platformio.ini documentazione/ROADMAP.md
> git commit -m "Phase3 Task3.2: implement JSON API contract v1.0"
> git tag phase3-task3.2
> ```

File creati:
- `src/dc_api_json.cpp/.h` — builder payload v1.0 e parser comandi API con livelli Customer/Factory

Modifiche:
- `src/DisplayApi_Manager.cpp` — usa `dc_api_build_payload()`, aggiorna `g_dc_model.api` e processa `pending_commands`
- `src/dc_controller.cpp` / `include/dc_controller.h` — aggiunta API `dc_factory_reset()` per il comando remoto omonimo
- `platformio.ini` — aggiunto `dc_api_json.cpp` alla build `controller_display`

Note post-implementazione:
- Il payload telemetria e il parsing comandi leggono solo `g_dc_model` e chiamano solo `dc_cmd_*` / `dc_settings_*`
- I comandi `relay_set`, `motor_enable`, `motor_speed`, `settings_set`, `rs485_scan` e `factory_reset` sono gestiti
- I comandi OTA e quelli di calibrazione/gruppo restano esplicitamente pending fino ai task successivi
- Build `controller_display` da verificare dopo integrazione

---

## Fix Prioritari — da risolvere prima di Task 3.3

Questi bug/miglioramenti sono stati identificati in fase di test su hardware.
Affrontarli nell'ordine indicato (dal più semplice al più complesso).

---

### Fix A — Versione firmware nella splash non aggiornata ✅ COMPLETATO

**Problema:** La stringa di versione mostrata nella splash screen non corrisponde alla versione
effettiva del firmware (definita in `platformio.ini` o in un header dedicato).

File da leggere prima di iniziare:
- `src/ui/shared/ui_splash_shared.cpp` (punto in cui viene scritta la stringa versione)
- `platformio.ini` (cercare `build_flags` con `-D FIRMWARE_VERSION` o simile)

Operazioni:
1. Verificare dove è definita la versione ufficiale del firmware (macro o stringa in `platformio.ini`).
2. Se non esiste una macro centralizzata, aggiungere `-D FW_VERSION_STR=\"x.y.z\"` in `platformio.ini`
   nelle build_flags del target `controller_display`.
3. In `ui_splash_shared.cpp` sostituire la stringa hardcoded con `FW_VERSION_STR`.

Criteri di completamento:
- La versione in splash corrisponde esattamente a quella in `platformio.ini` / header.
- Nessun altro file hard-code la stringa di versione.

> **Checkpoint git da eseguire:**
> ```
> git add platformio.ini src/ui/shared/ui_splash_shared.cpp
> git commit -m "FixA: sync firmware version string in splash"
> git tag fix-a-fw-version
> ```

---

### Fix B — Sensore Temp/Hum (SHTC3) non visualizzato sulla Home all'avvio ✅ COMPLETATO

**Problema:** All'avvio, la Home non mostra la temperatura/umidità finché non arriva il primo
aggiornamento periodico dal loop. Il valore resta vuoto o a zero per i primi secondi.

File da leggere prima di iniziare:
- `src/main_display_controller.cpp` (setup e loop — lettura SHTC3 e chiamata dc_controller_service)
- `src/dc_controller.cpp` (come viene popolato `g_dc_model.environment`)
- `src/ui/shared/ui_splash_shared.cpp` (transizione splash → home)

Operazioni:
1. Verificare che la prima lettura SHTC3 avvenga **prima** che la Home venga caricata
   (o al più tardi al momento della transizione splash → home in `boot.complete`).
2. Se la lettura è già presente ma l'UI non aggiorna: assicurarsi che `ui_dc_home_set_environment()`
   (o equivalente nel DataModel) venga chiamata almeno una volta prima del primo render della Home.
3. Alternativa: far sì che la Home, al primo `_home_sync_cb`, legga subito `g_dc_model.environment`
   anche se `valid == false`, mostrando un placeholder "—" invece di un valore vuoto.

⚠️ Caso edge: SHTC3 non presente o risponde con errore → mostrare "—" senza crash.

> **Checkpoint git da eseguire:**
> ```
> git add src/main_display_controller.cpp src/dc_controller.cpp src/ui/shared/ui_splash_shared.cpp
> git commit -m "FixB: show temp/hum on home from first render"
> git tag fix-b-env-display
> ```

---

### Fix C — Tastiera PIN di configurazione non occupa tutto lo spazio disponibile ✅ COMPLETATO

**Problema:** Nella schermata di inserimento PIN (Setup Sistema in impostazioni condivise),
la tastiera numerica non si estende per tutta l'area rimanente sotto il campo di input,
lasciando spazio vuoto e rendendo la UI parziale/tagliata.

File da leggere prima di iniziare:
- `src/ui/shared/ui_settings_shared.cpp` (ricercare la creazione della tastiera PIN —
  `lv_keyboard_create` o widget custom; cercare anche `lv_obj_set_size` / `lv_obj_align`)

Operazioni:
1. Localizzare il punto in cui viene creato e dimensionato il widget tastiera.
2. Verificare che altezza e larghezza siano impostate in modo da coprire l'area residua
   del pannello/schermata (tipicamente con `lv_obj_set_size(kb, lv_pct(100), LV_SIZE_CONTENT)`
   o un'altezza fissa calibrata sulla risoluzione 1024×600).
3. Verificare l'allineamento: `lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0)` sul parent corretto.
4. Se il parent ha padding o il pannello è scrollabile, aggiustare di conseguenza.

⚠️ Testare sia il primo accesso al PIN (configurazione) sia i successivi (verifica).

> **Checkpoint git da eseguire:**
> ```
> git add src/ui/shared/ui_settings_shared.cpp
> git commit -m "FixC: PIN keyboard fills available screen area"
> git tag fix-c-keyboard-layout
> ```

---

### Fix D — All'avvio con impianto configurato, entrare direttamente nella Home ✅ COMPLETATO

**Problema:** Se l'impianto è già configurato e ci sono periferiche salvate in NVS, al termine
della splash viene mostrata la schermata "iniziale" con le tre icone (modalità primo avvio)
invece di caricare direttamente la Home con le tile periferiche.

File da leggere prima di iniziare:
- `src/main_display_controller.cpp` (punto in cui si decide quale schermata caricare dopo la splash)
- `src/dc_settings.cpp` / `include/dc_settings.h` (funzioni per leggere se l'impianto è configurato)
- `include/dc_data_model.h` (verificare se esiste già un flag `plant_configured` o simile)

Operazioni:
1. Definire la condizione "impianto configurato":
   - `dc_settings_plant_name_get()` restituisce una stringa non vuota, **oppure**
   - esiste un flag NVS dedicato `plant_configured` (da aggiungere se non presente).
2. Aggiungere `dc_settings_plant_configured_get() → bool` in `dc_settings.h/.cpp`.
3. Nel punto di transizione splash → schermata successiva (in `main_display_controller.cpp`
   o nel callback `boot.complete`), valutare:
   - se `plant_configured == true` → caricare `ui_dc_home_create()` direttamente
   - altrimenti → caricare la schermata di primo avvio (comportamento attuale)
4. Aggiornare `dc_settings_plant_configured_set(true)` nel flusso di configurazione iniziale,
   al momento in cui l'utente completa il setup.

> **Checkpoint git da eseguire:**
> ```
> git add include/dc_settings.h src/dc_settings.cpp src/main_display_controller.cpp
> git commit -m "FixD: skip to home if plant already configured"
> git tag fix-d-boot-to-home
> ```

---

### Fix E — Sincronizzazione orologio: NTP + RTC I²C con priorità internet ✅ COMPLETATO

**Problema:** Attualmente se non è presente un RTC su I²C l'orologio funziona in modalità
software (millis-based). Non viene tentata la sincronizzazione NTP anche quando WiFi è disponibile.

**Comportamento desiderato:**

| Scenario | Comportamento |
|----------|---------------|
| Modalità manuale (impostazioni) | Nessuna sincronizzazione automatica — invariato |
| Automatico, no RTC, no WiFi | Comportamento attuale (orologio software) |
| Automatico, no RTC, WiFi disponibile | Sincronizzazione NTP all'avvio + ogni ora |
| Automatico, RTC presente, no WiFi | Orologio dal RTC — invariato |
| Automatico, RTC presente, WiFi disponibile | NTP all'avvio → se discrepanza > soglia, aggiornare RTC |

File da leggere prima di iniziare:
- `src/dc_controller.cpp` (sezione `_update_wifi()` e init orologio)
- `include/dc_data_model.h` (verificare campi `clock` o `time` nel DataModel)
- `include/dc_settings.h` (impostazione `clock_mode`: automatico / manuale)
- `src/main_display_controller.cpp` (init I²C e rilevamento RTC)

Operazioni:
1. In `dc_controller.h` aggiungere `dc_clock_sync_ntp()` — funzione non bloccante che avvia
   la richiesta NTP (es. via `configTime()` ESP32) e aggiorna `g_dc_model.clock`.
2. In `dc_controller_service()`: se `settings.clock_mode == AUTO && wifi.connected`:
   - chiamare `dc_clock_sync_ntp()` una volta all'avvio (flag `_ntp_synced`)
   - risincronizzare ogni ora (confronto `millis()`)
3. Se RTC I²C rilevato **e** NTP disponibile:
   - confrontare l'ora NTP con quella RTC
   - se discrepanza > 30 secondi → aggiornare il RTC con l'ora NTP
   - loggare la discrepanza via `Serial` per diagnostica
4. Soglia di discrepanza configurabile via `#define DC_NTP_RTC_DRIFT_THRESHOLD_S 30`
   (mettere in `dc_controller.cpp`, non nel header — è implementation detail).

⚠️ NTP può richiedere fino a ~5 secondi: eseguire in modo non bloccante.
⚠️ Non modificare l'orologio se `clock_mode == MANUAL`.

> **Checkpoint git da eseguire:**
> ```
> git add include/dc_controller.h src/dc_controller.cpp src/main_display_controller.cpp
> git commit -m "FixE: NTP sync with RTC I2C priority logic"
> git tag fix-e-ntp-rtc
> ```

---

## FASE 3 (continua)

### Task 3.3 — OTA dal controller 📲

*Affrontare solo dopo aver completato i Fix A–E.*

- Integrare OTA trigger nella risposta API (campo `ota.update_available`)
- Aggiungere overlay UI condiviso "Aggiornamento in corso…" (non dipende dal tema)
- Trigger da CLI admin: `OTACHECK`, `OTASTART`

---

## FASE 4 — Secondo template

*Iniziare solo dopo completamento Fase 3.*

### Task 4.1 — Design grafico secondo tema 📲
- Da concordare con il cliente prima di scrivere codice
- Definire: layout, palette, animazioni, widget custom

### Task 4.2 — Implementazione tema 📲
- Creare `src/ui/theme_X/`
- Implementare le funzioni `create_home()` e `create_network()` della struct UiTheme
- Il tema legge SOLO `g_dc_model`, chiama SOLO `dc_cmd_*`

### Task 4.3 — Selezione tema in impostazioni 📲
- Aggiungere voce "Tema interfaccia" nella sezione Utente delle impostazioni
- `dc_settings_theme_set(uint8_t id)` → salva in NVS + aggiorna g_dc_model.settings.ui_theme_id
- Al boot: `ui_theme_activate(g_dc_model.settings.ui_theme_id)` dopo splash

---

## FASE 5 — CLI seriale debug estesa

*Priorità bassa. Iniziare solo dopo completamento Fase 4.*

**Obiettivo:** Estendere `dc_admin_cli.cpp` con comandi di diagnostica strutturati, utili durante
il collaudo e il supporto sul campo. Tutti i messaggi emessi dalla CLI devono essere prefissati
con timestamp `[YYYY-MM-DD HH:MM:SS]` per poter correlare gli eventi con log esterni.

### Task 5.1 — Timestamp su tutti i messaggi CLI 📲

**Prerequisito di tutto il resto della fase.** Aggiungere prima questo, poi i comandi.

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp` (struttura attuale dei `Serial.print`)
- `src/dc_admin_cli.h`

Operazioni:
1. Aggiungere in `dc_admin_cli.cpp` la funzione statica `_cli_ts() → const char*` che
   restituisce una stringa `"[YYYY-MM-DD HH:MM:SS] "` letta da `g_dc_model.clock` (o da
   `gettimeofday` se disponibile).
2. Sostituire tutti i `Serial.print` / `Serial.println` nel file con la macro
   `CLI_PRINT(x)` / `CLI_PRINTLN(x)` definita localmente come `Serial.print(_cli_ts()); Serial.print(x)`.
3. Verificare che i prompt interattivi (es. `"> "`) **non** abbiano il timestamp — solo le
   risposte ai comandi e i log autonomi lo devono avere.

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp src/dc_admin_cli.h
> git commit -m "Phase5 Task5.1: add timestamp prefix to all CLI output"
> git tag phase5-task5.1
> ```

---

### Task 5.2 — Comandi User (senza PIN) 📲

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp` (sezione comandi User esistenti)
- `include/dc_data_model.h` (campi da esporre)
- `platformio.ini` (macro `FW_VERSION_STR` — deve esistere dopo Fix A)

**Comando `INFO`**

Risponde con un blocco compatto di informazioni sulla scheda:

```
[timestamp] === EasyConnect Display Controller ===
[timestamp] Firmware   : <FW_VERSION_STR>
[timestamp] Chip ID    : <ESP.getChipId() hex>
[timestamp] Flash      : <ESP.getFlashChipSize() / 1024> kB
[timestamp] Heap free  : <ESP.getFreeHeap()> B
[timestamp] Uptime     : <giorni>d <ore>h <min>m <sec>s
[timestamp] IP         : <g_dc_model.wifi.ip_str>
[timestamp] SSID       : <g_dc_model.wifi.ssid> (<g_dc_model.wifi.rssi> dBm)
[timestamp] Ora corrente: <timestamp da g_dc_model.clock>
[timestamp] Nome impianto: <g_dc_model.settings.plant_name>
```

**Comando `READSERIAL`**

Legge e stampa il numero di serie univoco della centralina:
- Fonte primaria: NVS namespace `easy_sys`, chiave `serial_no` (se impostato in produzione)
- Fonte fallback: eFuse MAC address ESP32 formattato come `EC-XXXXXXXXXXXX`

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp
> git commit -m "Phase5 Task5.2: add INFO and READSERIAL user commands"
> git tag phase5-task5.2
> ```

---

### Task 5.3 — Comandi Admin: configurazione scheda 📲

*Il PIN Admin è già gestito dal meccanismo esistente (Task 3.1). Questi comandi richiedono
livello Admin attivo.*

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp` (struttura dispatch Admin)
- `include/dc_settings.h` (funzioni set/get disponibili)

| Comando | Descrizione |
|---------|-------------|
| `SETNAME <nome>` | Imposta il nome impianto — chiama `dc_settings_plant_name_set()` |
| `SETBRIGHT <0-100>` | Imposta luminosità backlight — chiama `dc_settings_brightness_set()` |
| `SETSCRSAVER <min>` | Imposta timeout screensaver in minuti (0 = disabilitato) |
| `SETTEMPUNIT <C\|F>` | Imposta unità temperatura |
| `SETPIN` | Cambia PIN admin: chiede nuovo PIN due volte, salva hash SHA-256 |
| `FACTORYRESET` | Ripristino impostazioni di fabbrica — richiede conferma interattiva `"YES"` |

Operazioni:
1. Per ogni comando, aggiungere il case nel dispatcher Admin di `_cli_process_line()`.
2. `FACTORYRESET` deve chiedere `"Confermare con YES: "`, attendere la risposta sulla seriale
   e procedere solo se riceve esattamente `"YES"` — altrimenti stampare `"Annullato."`.
3. `SETPIN` deve chiedere il nuovo PIN due volte e procedere solo se corrispondono.

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp
> git commit -m "Phase5 Task5.3: admin config commands (SETNAME, SETBRIGHT, SETSCRSAVER, SETTEMPUNIT, SETPIN, FACTORYRESET)"
> git tag phase5-task5.3
> ```

---

### Task 5.4 — Comandi Admin: debug WiFi 📲

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp`
- `src/dc_controller.cpp` (sezione `_update_wifi()` — per capire cosa è già nel DataModel)

| Comando | Descrizione |
|---------|-------------|
| `WIFISTATUS` | SSID, IP, gateway, netmask, RSSI, DNS primario, uptime connessione |
| `WIFISCAN` | Scansiona reti visibili — mostra SSID, RSSI, canale, sicurezza; max 10 risultati |
| `WIFIPING <host>` | Ping verso `<host>` (default `8.8.8.8`); mostra 4 RTT e media. Non bloccante >2 s |
| `NTPSYNC` | Forza sincronizzazione NTP; mostra ora prima e dopo, offset in secondi e se RTC è stato aggiornato |

Note implementative:
- `WIFISCAN`: usare `WiFi.scanNetworks()` in modalità sincrona (accettabile qui — comandi manuali);
  stampare risultati ordinati per RSSI decrescente.
- `WIFIPING`: usare `Ping.ping()` da `ESP32Ping` o implementare con raw socket; timeout 500 ms per ping.
- `NTPSYNC`: chiamare `dc_clock_sync_ntp()` (Fix E) e attendere max 5 s; stampare esito.

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp
> git commit -m "Phase5 Task5.4: admin WiFi debug commands (WIFISTATUS, WIFISCAN, WIFIPING, NTPSYNC)"
> git tag phase5-task5.4
> ```

---

### Task 5.5 — Comandi Admin: debug API 📲

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp`
- `src/DisplayApi_Manager.cpp` (struttura delle ultime risposta/richiesta)
- `include/dc_data_model.h` (campi `g_dc_model.api`)

| Comando | Descrizione |
|---------|-------------|
| `APISTATUS` | URL configurato, chiave mascherata (`****ultime4cifre`), stato connessione, HTTP code ultimo invio, timestamp ultimo invio riuscito |
| `APIPAYLOAD` | Dump dell'ultimo payload JSON inviato (pretty-print; troncato a 1024 char se più lungo) |
| `APITEST` | Invia un payload di test al server configurato; mostra HTTP code e i primi 256 byte della risposta |
| `APICLEAR` | Cancella URL e chiave API da NVS — richiede conferma `"YES"` |

Note implementative:
- La chiave API in `APISTATUS` va sempre mascherata — mai stampare in chiaro.
- `APIPAYLOAD`: se `g_dc_model.api.last_payload` non esiste ancora, stampare `"Nessun payload disponibile."`.
- `APITEST`: eseguire in modo non bloccante con timeout 10 s; stampare `"Timeout."` se non risponde.

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp src/DisplayApi_Manager.cpp include/dc_data_model.h
> git commit -m "Phase5 Task5.5: admin API debug commands (APISTATUS, APIPAYLOAD, APITEST, APICLEAR)"
> git tag phase5-task5.5
> ```

---

### Task 5.6 — Comandi Admin: debug RS485 📲

File da leggere prima di iniziare:
- `src/dc_admin_cli.cpp`
- `src/dc_controller.cpp` (sezione snapshot RS485)
- `include/dc_data_model.h` (struct `DcNetworkState`, `DcDeviceSnapshot`)

| Comando | Descrizione |
|---------|-------------|
| `RS485STATUS` | Stato rete: device count, timestamp ultimo scan, errori CRC accumulati, bus idle/busy |
| `RS485SCAN` | Forza una nuova scansione RS485; mostra dispositivi trovati con ID, tipo, stato online |
| `RS485DEV <id>` | Dettaglio dispositivo `id`: tipo, online, tutti i campi del snapshot (velocità, temperatura, stati relay, ecc.) |
| `RS485LOG <on\|off>` | Abilita/disabilita logging frame RS485 raw su Serial in tempo reale. Auto-spegnimento dopo 5 minuti se dimenticato attivo. |

Note implementative:
- `RS485SCAN`: usare `dc_cmd_rs485_scan()` già esistente; attendere fino a 3 s per la risposta
  e poi leggere `g_dc_model.network`.
- `RS485DEV <id>`: iterare `g_dc_model.network.devices[]` cercando `device_id == id`;
  se non trovato stampare `"Dispositivo <id> non presente nel DataModel."`.
- `RS485LOG`: aggiungere un flag `g_rs485_log_enabled` in `dc_controller.cpp` letto da
  `_snapshot_device()` per emettere i frame grezzi; il timer di auto-spegnimento usa `millis()`.

> **Checkpoint git da eseguire:**
> ```
> git add src/dc_admin_cli.cpp src/dc_controller.cpp include/dc_data_model.h
> git commit -m "Phase5 Task5.6: admin RS485 debug commands (RS485STATUS, RS485SCAN, RS485DEV, RS485LOG)"
> git tag phase5-task5.6
> git tag phase5-complete
> ```

---

## Note per la sessione corrente

- Branch di lavoro corrente: `freeze/controller_display-2026-04-03`
- Prossimo task da eseguire: **Task 3.3 — OTA dal controller**
- Ordine consigliato Fix: Task 3.3
- File da leggere all'inizio della prossima sessione:
  1. Questo file (ROADMAP.md)
  2. `src/DisplayApi_Manager.cpp` (Task 3.3 — integrazione trigger OTA/API)
  3. `src/dc_admin_cli.cpp` (Task 3.3 — trigger OTACHECK/OTASTART)
  4. `src/ui/shared/` (Task 3.3 — overlay aggiornamento condiviso)
  5. `include/dc_data_model.h` (Task 3.3 — stato OTA condiviso)

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
