#pragma once

#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

struct UiTheme {
    uint8_t id;
    const char* name;
    lv_obj_t* (*create_home)(void);
    lv_obj_t* (*create_network)(void);
};

void ui_theme_registry_init(void);
bool ui_theme_register(const UiTheme* theme);
bool ui_theme_activate(uint8_t theme_id);
const UiTheme* ui_theme_get_active(void);
const UiTheme* ui_theme_find(uint8_t theme_id);
lv_obj_t* ui_theme_create_home(void);
lv_obj_t* ui_theme_create_network(void);
