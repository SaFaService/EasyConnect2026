#pragma once
#include "lvgl.h"

// Splash condivisa — indipendente dal tema.
// Mostra logo + progresso reale dal boot (g_dc_model.boot).
// Chiama ui_dc_home_create() automaticamente al completamento.
lv_obj_t* ui_splash_shared_create(void);
