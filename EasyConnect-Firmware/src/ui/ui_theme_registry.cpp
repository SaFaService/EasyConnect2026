#include "ui_theme_interface.h"

void ui_theme_classic_register(void);

namespace {

constexpr size_t k_max_themes = 4;

const UiTheme* s_themes[k_max_themes] = {};
size_t s_theme_count = 0;
const UiTheme* s_active_theme = nullptr;
bool s_registry_ready = false;

}  // namespace

void ui_theme_registry_init(void) {
    if (s_registry_ready) return;
    s_registry_ready = true;
    ui_theme_classic_register();
}

bool ui_theme_register(const UiTheme* theme) {
    if (!theme || !theme->create_home || !theme->create_network) return false;

    for (size_t i = 0; i < s_theme_count; ++i) {
        if (s_themes[i] && s_themes[i]->id == theme->id) {
            s_themes[i] = theme;
            if (s_active_theme && s_active_theme->id == theme->id) {
                s_active_theme = theme;
            }
            return true;
        }
    }

    if (s_theme_count >= k_max_themes) return false;
    s_themes[s_theme_count++] = theme;
    return true;
}

const UiTheme* ui_theme_find(uint8_t theme_id) {
    ui_theme_registry_init();

    for (size_t i = 0; i < s_theme_count; ++i) {
        if (s_themes[i] && s_themes[i]->id == theme_id) {
            return s_themes[i];
        }
    }

    return nullptr;
}

bool ui_theme_activate(uint8_t theme_id) {
    const UiTheme* theme = ui_theme_find(theme_id);
    if (!theme) return false;
    s_active_theme = theme;
    return true;
}

const UiTheme* ui_theme_get_active(void) {
    ui_theme_registry_init();
    return s_active_theme;
}

lv_obj_t* ui_theme_create_home(void) {
    const UiTheme* theme = ui_theme_get_active();
    return (theme && theme->create_home) ? theme->create_home() : nullptr;
}

lv_obj_t* ui_theme_create_network(void) {
    const UiTheme* theme = ui_theme_get_active();
    return (theme && theme->create_network) ? theme->create_network() : nullptr;
}
