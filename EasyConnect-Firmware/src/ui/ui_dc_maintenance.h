#pragma once

#include "lvgl.h"

typedef void (*UiDcMaintenanceSuccessCb)(void* user_data);

void ui_dc_maintenance_request_pin(const char* action_title,
                                   const char* action_label,
                                   UiDcMaintenanceSuccessCb on_success,
                                   void* user_data);
