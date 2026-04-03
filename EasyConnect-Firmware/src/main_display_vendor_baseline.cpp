#include <Arduino.h>
#include "display_port/lvgl_port.h"
#include <demos/lv_demos.h>

void setup() {
    static esp_lcd_panel_handle_t panel_handle = NULL;
    static esp_lcd_touch_handle_t tp_handle = NULL;

    Serial.begin(115200);
    delay(200);
    Serial.println("[baseline] init touch");
    tp_handle = touch_gt911_init();

    Serial.println("[baseline] init rgb panel");
    panel_handle = waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();

    Serial.println("[baseline] init lvgl");
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    if (lvgl_port_lock(-1)) {
        lv_demo_widgets();
        lvgl_port_unlock();
    }
    Serial.println("[baseline] demo widgets running");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(2000));
}
