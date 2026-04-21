# EasyConnect Firmware — Guida per Claude

## Regole CRITICHE
- **Leggi SOLO i file citati esplicitamente.** Il progetto è grande; non esplorare altro.
- **Non toccare** RS485, WebHandler, API_Manager, OTA_Manager, Calibration, Serial_*, GestioneMemoria salvo richiesta esplicita.
- **Non aggiungere** commenti, docstring, error handling speculativo o codice non richiesto.
- `vendors/PlatformIO/` → solo consultazione, NON compilata. Codice portato → `src/display_port/`.

---

## Build Targets (platformio.ini)

| Target / Alias | Board | Scopo |
|----------------|-------|-------|
| `easyconnect` / `controller_display` | ESP32-S3 | Display Controller (produzione) |
| `controller_display_vendor_baseline` | ESP32-S3 | Baseline fornitore |
| `controller_standalone_rewamping` / `master_rewamping` | ESP32-C3 | Controller standalone |
| `peripheral_pressure` / `pressione` | ESP32-C3 | Periferica pressione |
| `peripheral_relay` / `relay` | ESP32-C3 | Periferica relay |
| `peripheral_motor` / `motore` | ESP32-C3 | Periferica motore |
| `peripheral_0v10v` / `inverter` | ESP32-C3 | Inverter 0-10V (Treedom Rev.1.0) |
| `diagnostic_hello` / `diagnostic_hard` | ESP32-C3 | Diagnostica |

---

## Refactoring in corso — leggere prima di lavorare

**Roadmap operativa:** `documentazione/ROADMAP.md` — contiene fase corrente, task attivo e file da leggere.
**Architettura completa:** `documentazione/Architettura_DataModel_e_Template.md`
**Fase attuale:** Fase 1, Task 1.2 (dc_settings.cpp)

Contratti già scritti (non modificare senza aggiornare la doc):
- `include/dc_data_model.h` — struct DcDataModel (fonte di verità UI)
- `include/dc_controller.h` — API comandi dc_cmd_* e dc_controller_service()
- `include/dc_settings.h`   — API impostazioni persistite

Regola: l'UI legge solo `g_dc_model`, comanda solo via `dc_cmd_*`. Mai RS485 o NVS dall'UI.

---

## File principali — target `easyconnect`

- Entry point: `src/main_display_controller.cpp`
- UI: `src/ui/ui_dc_splash.cpp/.h`, `src/ui/ui_dc_home.cpp/.h`, `src/ui/ui_notifications.cpp/.h`
- Stili: `src/ui/ui_styles.h` | Icone: `src/ui/icons/` | Font: `src/vendor_fonts/`
- Driver: `src/display_port/` (lvgl_port, rgb_lcd_port, gt911, touch, i2c, io_extension)
- Pin: `include/Pins.h` | Logo: `src/DisplayLogoAsset.cpp`

---

## Hardware Display Controller

| Parametro | Valore |
|-----------|--------|
| MCU | ESP32-S3 + PSRAM |
| Display | RGB LCD 1024×600, doppio buffer in PSRAM |
| Touch | GT911 I2C, max 5 punti |
| UI | LVGL v8.4.0, task Core 1, priorità 2 |
| Backlight | IO Expander PWM (`IO_EXTENSION_Pwm_Output`) |
| Font | Montserrat 12/16/20/24/32/48 (build flags, `LV_CONF_SKIP=1`) |
| Anti-tear | double-buffer + direct-mode (vedi `src/display_port/lvgl_port.h`) |

---

## Convenzioni LVGL

- Lock LVGL: `lvgl_port_lock(-1)` / `lvgl_port_unlock()` — obbligatorio fuori dai callback
- Ogni schermata: `ui_XXX_create()` → `lv_obj_t*`
- File UI: `ui_dc_<nome>.cpp/.h` (prefisso `ui_dc_` = Display Controller; `ui_` = sandbox)
- Palette: sfondo `#EEF3F8`, arancione `#E84820`, testo `#243447`
- Icone: `LV_IMG_CF_TRUE_COLOR_ALPHA`, generate da SVG con `icon_converter.py`
- Transizioni: `lv_scr_load_anim(..., LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, true)`
- Idle dim: 3 min senza touch → luminosità 10%; al primo touch torna al livello impostato

---

## Pagine UI — stato

| Pagina | File | Stato |
|--------|------|-------|
| Splash | `ui/ui_dc_splash.cpp` | OK — logo fade+zoom, shimmer, progress bar 3D (~6.3s) |
| Home | `ui/ui_dc_home.cpp` | OK — header datetime+temp, icona settings |
| Impostazioni | `ui/ui_dc_home.cpp` | OK — 6 menu: Utente, Connessione, Setup Sistema, Ventilazione, Filtraggio, Sensori |
| Notifiche | `ui/ui_notifications.cpp` | Parziale |
| Record Errori | — | Da fare |

Helper in `ui_dc_home.cpp`: `make_header()`, `make_panel()`, `make_setting_row()`, `make_two_choice()`, `make_on_off_switch()`, `make_primary_button()`

---

## NON fare (salvo richiesta esplicita)

- Non leggere `RS485_*.cpp`, `WebHandler.cpp`, `API_Manager.cpp`, `OTA_Manager.cpp`, file periferiche
- Non modificare `vendors/` né `display_port/` salvo richiesta esplicita sul driver display
