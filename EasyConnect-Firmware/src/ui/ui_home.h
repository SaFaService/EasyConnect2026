#pragma once
/**
 * @file ui_home.h
 * @brief Home screen EasyConnect UI Sandbox
 *
 * Home screen con header Antralux e 4 tab:
 *   1. Controlli  – pulsanti, slider, switch, dropdown
 *   2. Misure     – gauge ad arco + grafico lineare (DeltaP simulato)
 *   3. Touch      – visualizzatore multi-touch fino a 5 punti simultanei
 *   4. Info       – informazioni hardware, firmware e display
 *
 * NOTA: chiamare SEMPRE dentro lvgl_port_lock / lvgl_port_unlock
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea la home screen con header + tabview.
 * @return Puntatore alla screen LVGL creata (usato da lv_scr_load_anim)
 */
lv_obj_t* ui_home_create(void);

#ifdef __cplusplus
}
#endif
