#pragma once

#include <stdint.h>
#include "lvgl.h"

enum UiNotifSeverity : uint8_t {
    UI_NOTIF_NONE = 0,
    UI_NOTIF_INFO = 1,
    UI_NOTIF_ALERT = 2,
};

void ui_notif_panel_init(lv_obj_t* scr, lv_obj_t* header);
void ui_notif_panel_open(void);
void ui_notif_panel_close(void);
void ui_notif_panel_toggle(void);

void ui_notif_push_or_update(const char* key, UiNotifSeverity severity,
                             const char* title, const char* body);
void ui_notif_clear(const char* key);
void ui_notif_clear_prefix(const char* prefix);
void ui_notif_clear_all(void);

UiNotifSeverity ui_notif_highest_severity(void);
int ui_notif_count(void);
