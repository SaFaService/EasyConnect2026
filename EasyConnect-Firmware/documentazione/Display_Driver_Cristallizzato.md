# Display Driver Cristallizzato

Data lock: 2026-04-01
Firmware target: `env:controller_display` (alias `env:easyconnect`)

## Scopo
Congelare il layer driver display/touch/LVGL nello stato attuale, per poter
lavorare su grafica, impostazioni e comandi display senza modificare la parte
hardware che al momento risulta stabile.

## Blocco driver locked
File: `src/main_display_controller.cpp`
Funzione: `init_display_driver_locked(...)`

La funzione contiene la sequenza completa e congelata:
1. init touch GT911
2. init pannello RGB LCD
3. backlight ON
4. init `lvgl_port`

## Regola operativa
Modifiche consentite (normali):
- `src/ui/ui_dc_splash.cpp`
- `src/ui/ui_dc_home.cpp`
- asset UI (logo, testi, layout, schermate, comandi utente)

Modifiche da evitare (solo fix critici):
- corpo di `init_display_driver_locked(...)`
- ordine della sequenza di boot driver
- chiamate low-level a `touch_gt911_init`, `waveshare_esp32_s3_rgb_lcd_init`,
  `wavesahre_rgb_lcd_bl_on`, `lvgl_port_init`

## Firmware rimossi dalla configurazione
- `env:controller_display_sandbox`
- `env:controller_display_vendor_stack`

Note:
- `src/main_display_sandbox.cpp` e' stato rimosso per pulizia.
- Il target `env:controller_display_vendor_baseline` resta disponibile solo
  come baseline tecnica separata.
