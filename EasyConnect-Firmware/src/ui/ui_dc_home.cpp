#include "ui_dc_home.h"
#include "dc_data_model.h"
#include "ui_theme_interface.h"

namespace {

void _sync_active_theme() {
    ui_theme_registry_init();

    const uint8_t selected_theme = g_dc_model.settings.ui_theme_id;
    const UiTheme* active_theme = ui_theme_get_active();
    if (active_theme && active_theme->id == selected_theme) return;

    if (!ui_theme_activate(selected_theme)) {
        ui_theme_activate(0);
    }
}

}  // namespace

lv_obj_t* ui_dc_home_create(void) {
    _sync_active_theme();
    return ui_theme_create_home();
}
