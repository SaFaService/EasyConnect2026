#pragma once
/**
 * @file ui_splash.h
 * @brief Splash screen EasyConnect / Antralux
 *
 * Splash screen con:
 *  - Animazione doppio arco (logo decorativo Antralux)
 *  - Testo "ANTRALUX" con fade-in
 *  - Sottotitolo "EasyConnect Display" con fade-in ritardato
 *  - Barra di progresso che si riempie in ~6 secondi
 *  - Transizione automatica alla home screen
 *
 * NOTA: chiamare SEMPRE dentro lvgl_port_lock / lvgl_port_unlock
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea e avvia lo splash screen.
 *        Al termine (6s) carica automaticamente la home screen.
 */
void ui_splash_create(void);

#ifdef __cplusplus
}
#endif
