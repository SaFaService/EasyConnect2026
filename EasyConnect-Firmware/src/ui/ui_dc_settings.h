#pragma once

#include "ui/shared/ui_settings_shared.h"

// Compatibilita temporanea con i vecchi include.
static inline lv_obj_t* ui_dc_settings_create(void) {
    return ui_settings_shared_create();
}
