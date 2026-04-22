#pragma once

#include "lvgl.h"

// Impostazioni condivise tra tutti i temi.
// La schermata espone 6 sezioni:
// Utente, Connessione, Setup Sistema, Ventilazione, Filtraggio, Sensori.
lv_obj_t* ui_settings_shared_create(void);
