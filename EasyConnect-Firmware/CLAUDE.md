# EasyConnect Firmware — Guida per Claude

## Regole generali
- **Non esplorare file non indicati esplicitamente.** Il progetto e' grande; leggi solo i file citati nella richiesta.
- **Non toccare** RS485, WebHandler, API_Manager, OTA_Manager, Calibration, Serial_*, GestioneMemoria salvo richiesta esplicita.
- **Non aggiungere** commenti, docstring, error handling speculativo o codice non richiesto.

---

## Politica vendors/

La cartella `vendors/PlatformIO/` contiene il codice originale del fornitore (Waveshare) a **solo scopo di consultazione**. NON viene compilata.

Quando serve del codice da un file del fornitore, lo si copia in un file nostro dentro `src/` o `include/`, mantenendo il file del fornitore immacolato. I file gia' portati nel progetto si trovano in `src/display_port/`.

---

## Struttura del progetto

```
EasyConnect-Firmware/
├── src/
│   ├── main_display_controller.cpp      ← Entry point Display Controller (target: easyconnect)
│   ├── main_display_vendor_baseline.cpp ← Baseline test driver fornitore
│   ├── main_standalone_rewamping_controller.cpp ← Controller standalone (C3, no display)
│   ├── main_pressure_peripheral.cpp     ← Periferica pressione
│   ├── main_relay_peripheral.cpp        ← Periferica relay
│   ├── main_motor_peripheral.cpp        ← Periferica motore
│   ├── main_hello.cpp / main_diag_boot.cpp ← Diagnostica
│   │
│   ├── DisplayLogoAsset.cpp             ← Asset logo Antralux (array RGB565)
│   ├── DisplayBoard.cpp                 ← HAL display legacy (non usato dal target easyconnect)
│   ├── DisplayVendorFonts.cpp           ← Font vendor (non usato dal target easyconnect)
│   │
│   ├── display_port/                    ← Driver display portati da vendors/ (nostri file)
│   │   ├── lvgl_port.cpp/.h            ← Init LVGL, flush callback, task FreeRTOS, mutex
│   │   ├── rgb_lcd_port.cpp/.h         ← Init RGB LCD panel, timing, GPIO, vsync callback
│   │   ├── gt911.cpp/.h                ← Driver touch GT911 (I2C)
│   │   ├── touch.cpp/.h                ← Init touch, wrapper gt911
│   │   ├── i2c.cpp/.h                  ← Init I2C bus
│   │   └── io_extension.cpp/.h         ← IO expander (backlight PWM, GPIO estesi)
│   │
│   ├── ui/                              ← Layer UI LVGL
│   │   ├── ui_dc_splash.cpp/.h         ← Splash screen (logo + progress bar, ~6.3s)
│   │   ├── ui_dc_home.cpp/.h           ← Home + Impostazioni (6 sotto-menu, WiFi, ecc.)
│   │   ├── ui_notifications.cpp/.h     ← Pannello notifiche (tendina dall'alto) [parziale]
│   │   ├── ui_styles.h                 ← Stili/temi LVGL globali
│   │   ├── icons/settings.cpp/.h       ← Icona settings 24x24 LV_IMG_CF_TRUE_COLOR_ALPHA
│   │   ├── ui_splash.cpp/.h            ← (vecchia splash, sandbox)
│   │   └── ui_home.cpp/.h              ← (vecchia home, sandbox)
│   │
│   ├── vendor_fonts/                    ← Font bitmap portati da vendors/
│   │   └── font8/12/16/20/24/48.cpp
│   │
│   └── (altri: RS485_*.cpp, Relay*.cpp, Calibration.cpp, ecc.)
│
├── include/
│   ├── Pins.h                           ← Tutti i pin del progetto
│   ├── DisplayBoard.h                   ← API HAL display legacy
│   ├── DisplayLogoAsset.h               ← Logo Antralux (extern array)
│   ├── icons/settings.h                 ← extern lv_img_dsc_t settings
│   ├── icons/icons_index.h              ← Indice icone
│   ├── vendor_fonts/fonts.h             ← Header font vendor
│   ├── DisplayVendorFonts.h             ← Header font vendor wrapper
│   ├── hal/, driver/                    ← Stub header ESP-IDF per compilazione
│   └── (altri: RS485_Manager.h, Relay*.h, Calibration.h, ecc.)
│
├── vendors/                             ← SOLO CONSULTAZIONE, non compilata
│   └── PlatformIO/13_LVGL_TRANSPLANT/  ← Esempio LVGL originale Waveshare
│
└── platformio.ini
```

---

## Hardware Display Controller

| Parametro       | Valore |
|-----------------|--------|
| MCU             | ESP32-S3 (PSRAM abilitata) |
| Scheda          | Waveshare ESP32-S3-Touch-LCD-7B |
| Display         | RGB LCD 1024x600 px, doppio buffer in PSRAM |
| Touch           | GT911 capacitivo, max 5 punti, I2C |
| UI Framework    | LVGL v8.4.0 (task su Core 1, priorita' 2) |
| Backlight       | Controllato via IO Expander (`IO_EXTENSION_Pwm_Output`) |
| Pixel Clock     | 30 MHz |
| Bounce Buffer   | 1024 x 10 px (SRAM, per trasferimento DMA da PSRAM) |

---

## Configurazione LVGL (build flags, no lv_conf.h)

Il progetto usa `-DLV_CONF_SKIP=1` e definisce le opzioni via build flags:
```
-DLV_FONT_MONTSERRAT_12=1
-DLV_FONT_MONTSERRAT_16=1
-DLV_FONT_MONTSERRAT_20=1
-DLV_FONT_MONTSERRAT_24=1
-DLV_FONT_MONTSERRAT_32=1
-DLV_FONT_MONTSERRAT_48=1
```
Tutti gli altri parametri LVGL usano i default di `lv_conf_internal.h`.

### Anti-tearing (lvgl_port.h)
```
LVGL_PORT_AVOID_TEAR_ENABLE  = 1
LVGL_PORT_AVOID_TEAR_MODE    = 3   → double-buffer + LVGL direct-mode
LVGL_PORT_LCD_RGB_BUFFER_NUMS = 2
LVGL_PORT_DIRECT_MODE         = 1
EXAMPLE_LVGL_PORT_ROTATION_DEGREE = 0
```

### Flush callback (lvgl_port.cpp) — direct mode, no rotation
1. LVGL renderizza le dirty area nel buffer corrente
2. `flush_callback` su `lv_disp_flush_is_last()`:
   - Salva le dirty area (`flush_dirty_save`)
   - Swap buffer con `esp_lcd_panel_draw_bitmap`
   - Attende vsync (`ulTaskNotifyTake`)
   - Copia le dirty area sull'altro buffer (`flush_dirty_copy_no_rotate`)
3. Chiama `lv_disp_flush_ready()`

---

## Build Targets (platformio.ini)

| Alias / Target               | Board | Scopo |
|------------------------------|-------|-------|
| `easyconnect` / `controller_display` | ESP32-S3-Touch-LCD-7B | **Display Controller** — produzione (S3 + LVGL) |
| `controller_display_vendor_baseline` | ESP32-S3-Touch-LCD-7B | Baseline test con codice fornitore |
| `controller_standalone_rewamping` / `master_rewamping` | ESP32-C3 | Controller standalone (no display) |
| `peripheral_pressure` / `pressione`  | ESP32-C3 | Periferica pressione |
| `peripheral_relay` / `relay`         | ESP32-C3 | Periferica relay |
| `peripheral_motor` / `motore`        | ESP32-C3 | Periferica motore |
| `peripheral_0v10v` / `inverter`      | ESP32-C3 | Periferica inverter 0-10V (scheda Treedom Rev.1.0) |
| `diagnostic_hello` / `diagnostic_hard` | ESP32-C3 | Firmware diagnostico |

**File inclusi nel target `easyconnect` (controller_display):**
- `main_display_controller.cpp`
- `ui/ui_dc_splash.cpp`, `ui/ui_dc_home.cpp`, `ui/icons/settings.cpp`
- `DisplayLogoAsset.cpp`
- `display_port/i2c.cpp`, `display_port/io_extension.cpp`
- `display_port/rgb_lcd_port.cpp`, `display_port/touch.cpp`, `display_port/gt911.cpp`
- `display_port/lvgl_port.cpp`

---

## Sequenza di boot Display Controller

```
setup() [main_display_controller.cpp]:
  1. touch_gt911_init()                    → GT911 via I2C
  2. waveshare_esp32_s3_rgb_lcd_init()     → panel_handle (config timing, GPIO, PSRAM fb)
  3. wavesahre_rgb_lcd_bl_on()             → backlight ON via IO expander
  4. lvgl_port_init(panel, touch)           → LVGL su Core 1 (task 6KB stack, priorita' 2)
  5. ui_dc_splash_create()                  → splash ~6.3s → poi ui_dc_home_create()

loop():
  // LVGL gestito dal task FreeRTOS su Core 1; loop() e' libero (vTaskDelay 2s)
```

---

## Pagine UI

| Pagina | File | Stato |
|--------|------|-------|
| Splash screen | `ui/ui_dc_splash.cpp` | Implementata (logo fade+zoom, shimmer, progress bar 3D) |
| Home | `ui/ui_dc_home.cpp` | Implementata (header datetime+temp, icona settings) |
| Impostazioni | `ui/ui_dc_home.cpp` | Implementata (6 menu: Utente, Connessione, Setup Sistema, Ventilazione, Filtraggio, Sensori) |
| Notifiche (tendina) | `ui/ui_notifications.cpp` | Parziale |
| Record Errori | — | Da fare |

### Dettaglio Impostazioni (ui_dc_home.cpp)

**Struttura**: header con tasto "Indietro" + pannello sinistro (6 pulsanti menu) + pannello destro (contenuto dinamico).

**Menu implementati:**
- **Impostazioni Utente**: tema chiaro/scuro, lingua (IT/EN/ES/FR), luminosita' (slider + idle dim 3min), data (dialog con dropdown), ora auto/manuale, timezone, gradi C/F, buzzer on/off
- **Connessione**: WiFi on/off, scansione SSID, connessione con password
- **Setup Sistema**: accesso con PIN (default "1234"), placeholder
- **Ventilazione / Filtraggio / Sensori**: placeholder

**Pattern UI:**
- `create_home_screen()` → schermata home con header datetime + pulsante settings
- `create_settings_screen()` → schermata impostazioni con menu sinistro + contenuto destro
- `make_header()` → header riutilizzabile (con/senza tasto indietro, modo home/standard)
- `make_panel()` → pannello con bordi arrotondati e sfondo bianco
- `make_setting_row()` → riga impostazione (label + valore + controllo)
- `make_two_choice()` → btnmatrix a 2 opzioni (tipo segmented control)
- `make_on_off_switch()` → switch on/off
- `make_primary_button()` → pulsante arancione (#E84820)

**Transizioni**: `lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 220, 0, true)`

**Idle dim**: dopo 3 minuti senza touch, luminosita' scende al 10%. Al primo touch torna al livello impostato.

---

## Pin del progetto (Pins.h)

| Gruppo | Pin |
|--------|-----|
| RS485 (tutti) | DIR=7, TX=21, RX=20 |
| Controller LEDs | Verde=9, Rosso=8, Safety=2 |
| Keyboard LEDs | WiFi=10, Sens1=4, Sens2=6, Aux1=5, Safety=3, Aux2=1, Button=0 |
| Pressure | Verde=9, Rosso=8, I2C SDA=0, SCL=1, Safety=10 |
| Relay | Output=3, Feedback=6, Safety=2, Red=8, Green=9 |
| Display | Backlight via IO Expander (IO_EXTENSION_IO_2 + PWM) |

---

## Convenzioni LVGL nel progetto

- Accesso LVGL **sempre** dentro `lvgl_port_lock(-1)` / `lvgl_port_unlock()` (tranne dentro callback LVGL che girano gia' nel task)
- Ogni schermata e' creata da una funzione `ui_XXX_create()` che restituisce `lv_obj_t*`
- I file UI seguono il pattern: `ui_dc_<nome>.cpp` + `ui_dc_<nome>.h`
- Il prefisso `ui_dc_` identifica i file del Display Controller (vs `ui_` del sandbox)
- Versione corrente firmware display: `1.0.0` (define in `main_display_controller.cpp`)
- Palette principale: sfondo `#EEF3F8`, arancione Antralux `#E84820`, testo `#243447`
- Icone: formato `LV_IMG_CF_TRUE_COLOR_ALPHA`, generate da SVG con `icon_converter.py`
- Simboli LVGL: `LV_SYMBOL_SETTINGS`, `LV_SYMBOL_LEFT` (disponibili nelle font Montserrat abilitate)

---

## Cosa NON fare (a meno di richiesta esplicita)

- Non leggere `RS485_Master.cpp`, `RS485_Slave.cpp`, `WebHandler.cpp`, `API_Manager.cpp`, `OTA_Manager.cpp`
- Non leggere file di periferiche (pressure, relay, motor) se la richiesta riguarda il display
- Non modificare `vendors/` (solo consultazione)
- Non modificare i file in `display_port/` salvo richiesta esplicita sul driver display
