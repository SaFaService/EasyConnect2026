#pragma once
/**
 * @file ui_splash.h
 * @brief Splash screen EasyConnect / Antralux — versione sandbox.
 *        EasyConnect / Antralux splash screen — sandbox version.
 *
 * Questo file appartiene alla UI "sandbox" (target: controller_display_vendor_baseline
 * o simili), NON al firmware di produzione (target: easyconnect).
 * This file belongs to the "sandbox" UI (target: controller_display_vendor_baseline
 * or similar), NOT the production firmware (target: easyconnect).
 *
 * Differenza rispetto a ui_dc_splash / Difference from ui_dc_splash:
 *   - Usa la palette sandbox dark-mode (UI_COLOR_* da ui_styles.h)
 *   - Uses sandbox dark-mode palette (UI_COLOR_* from ui_styles.h)
 *   - Logo: doppio arco animato (no immagine bitmap), testo "ANTRALUX"
 *   - Logo: animated double arc (no bitmap image), "ANTRALUX" text
 *   - Al termine della barra carica ui_home_create() (home sandbox)
 *   - On bar completion loads ui_home_create() (sandbox home)
 *   - NON avvia alcuna scansione RS485
 *   - Does NOT start any RS485 scan
 *
 * Sequenza animazioni / Animation sequence:
 *   t=0ms     Fade-in arco esterno (Ø260px, 300ms)
 *             Outer arc fade-in (Ø260px, 300ms)
 *   t=0ms     Disegno arco esterno 0°→360° (1500ms, ease-out)
 *             Outer arc draw 0°→360° (1500ms, ease-out)
 *   t=200ms   Fade-in arco interno (Ø200px, 300ms)
 *             Inner arc fade-in (Ø200px, 300ms)
 *   t=200ms   Disegno arco interno 0°→360° (1300ms, ease-out)
 *             Inner arc draw 0°→360° (1300ms, ease-out)
 *   t=700ms   Fade-in testo "ANTRALUX" (900ms)
 *             "ANTRALUX" text fade-in (900ms)
 *   t=1500ms  Slide-up + fade-in sottotitolo "EasyConnect Display" (700ms)
 *             Subtitle "EasyConnect Display" slide-up + fade-in (700ms)
 *   t=2000ms  Fade-in label versione (500ms)
 *             Version label fade-in (500ms)
 *   t=200ms   Barra di progresso 0→100% in 5800ms (lineare)
 *             Progress bar 0→100% in 5800ms (linear)
 *             → Al completamento: carica Home con LV_SCR_LOAD_ANIM_FADE_ON 600ms
 *             → On completion: loads Home with LV_SCR_LOAD_ANIM_FADE_ON 600ms
 *
 * Layout 1024×600 (dark):
 *   ┌──────────────────────────────────────────────────────────────┐
 *   │                                                              │
 *   │         [Arco doppio 260/200px]   [ANTRALUX in mezzo]       │
 *   │                                                              │
 *   │              EasyConnect  Display  (sottotitolo)            │
 *   │                         v1.0.0                              │
 *   │                                                              │
 *   │       ▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░ (580×14)   Avvio... 42%         │
 *   │                                                              │
 *   └──────────────────────────────────────────────────────────────┘
 *
 * NOTA: chiamare SEMPRE dentro lvgl_port_lock / lvgl_port_unlock.
 * NOTE: ALWAYS call inside lvgl_port_lock / lvgl_port_unlock.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea e avvia lo splash screen sandbox.
 *        Creates and starts the sandbox splash screen.
 *
 * Chiama lv_scr_load() internamente — la splash diventa subito attiva.
 * Internally calls lv_scr_load() — the splash immediately becomes active.
 *
 * Al termine della progress bar (~6s) viene chiamata on_splash_complete()
 * che carica ui_home_create() con un fade da 600ms.
 * When the progress bar completes (~6s) on_splash_complete() is called,
 * which loads ui_home_create() with a 600ms fade.
 */
void ui_splash_create(void);

#ifdef __cplusplus
}
#endif
