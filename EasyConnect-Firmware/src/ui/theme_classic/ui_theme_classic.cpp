#include "ui_theme_interface.h"
#include "ui/theme_classic/ui_tc_home.h"
#include "ui/theme_classic/ui_tc_network.h"

namespace {

const UiTheme k_theme_classic = {
    .id = 0,
    .name = "Classic",
    .create_home = ui_tc_home_create,
    .create_network = ui_tc_network_create,
};

}  // namespace

void ui_theme_classic_register(void) {
    ui_theme_register(&k_theme_classic);
}
