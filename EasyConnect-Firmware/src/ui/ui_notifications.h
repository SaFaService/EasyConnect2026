#pragma once
/**
 * @file ui_notifications.h
 * @brief Pannello notifiche pull-down (Android-style notification shade)
 *
 * Attivazione : trascina verso il basso dall'header
 * Chiusura    : tap sul backdrop oppure trascina verso l'alto sull'handle
 * Dismiss     : swipe sinistro o destro sulla singola notifica
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inizializza il pannello notifiche su uno schermo esistente.
 * @param scr    La screen LVGL (da ui_home_create)
 * @param header L'header bar – vi aggancia il gesto pull-down
 */
void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* header);

#ifdef __cplusplus
}
#endif
