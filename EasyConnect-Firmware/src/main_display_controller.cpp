/**
 * @file main_display_controller.cpp
 * @brief EasyConnect Display Controller — entry point
 *
 * Sequenza di boot:
 *   1. touch_gt911_init()               → GT911 I2C
 *   2. waveshare_esp32_s3_rgb_lcd_init() → panel RGB 1024×600 (PSRAM)
 *   3. wavesahre_rgb_lcd_bl_on()         → backlight ON via IO expander
 *   4. lvgl_port_init()                  → LVGL su Core 1 (priorità 2)
 *   5. ui_dc_splash_create()             → splash ~5.5 s → Home
 */

#include <Arduino.h>
#include "display_port/lvgl_port.h"
#include "ui/ui_dc_splash.h"

static void halt_forever() {
    while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== EasyConnect Display Controller ===");

    esp_lcd_touch_handle_t  tp_handle    = touch_gt911_init();
    esp_lcd_panel_handle_t  panel_handle = waveshare_esp32_s3_rgb_lcd_init();

    if (!panel_handle) {
        Serial.println("[ERRORE] Display non inizializzato — halt");
        halt_forever();
    }

    wavesahre_rgb_lcd_bl_on();
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));
    Serial.println("[OK] Display + LVGL pronti");

    if (lvgl_port_lock(-1)) {
        ui_dc_splash_create();
        lvgl_port_unlock();
    }
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(2000));
}
