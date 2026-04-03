#include "DisplayBoard.h"

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace DisplayBoard {
namespace {

constexpr char kTag[] = "DisplayBoard";

constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
constexpr gpio_num_t kI2cScl = GPIO_NUM_9;
constexpr uint32_t kI2cFrequencyHz = 400000;

constexpr uint8_t kIoExtAddress = 0x24;
constexpr uint8_t kIoExtRegMode = 0x02;
constexpr uint8_t kIoExtRegOutput = 0x03;
constexpr uint8_t kIoExtRegPwm = 0x05;
constexpr uint8_t kIoExtPinTouchReset = 0x01;
constexpr uint8_t kIoExtPinBacklight = 0x02;

constexpr gpio_num_t kTouchIntPin = GPIO_NUM_4;
constexpr uint8_t kGt911Address = 0x5D;
constexpr uint16_t kGt911RegProductId = 0x8140;
constexpr uint16_t kGt911RegStatus = 0x814E;

constexpr int kRgbDataWidth = 16;
// Vendor demos use 30 MHz with the same timings; this project uses many partial updates.
// 30 MHz test profile aligned to vendor demos.
constexpr uint32_t kPixelClockHz = 30000000;

esp_lcd_panel_io_handle_t s_touchIo = nullptr;
esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_frameBufferA = nullptr;
uint16_t* s_frameBufferB = nullptr;
SemaphoreHandle_t s_lcdFrameDone = nullptr;

bool s_touchPressed = false;
uint16_t s_touchX = 0;
uint16_t s_touchY = 0;
uint8_t s_touchCount = 0;
uint8_t s_touchMissCount = 0;
constexpr uint8_t kTouchReleaseMissThreshold = 3;

bool onRgbFrameDone(esp_lcd_panel_handle_t panel, esp_lcd_rgb_panel_event_data_t* edata, void* user_ctx) {
    (void)panel;
    (void)edata;
    (void)user_ctx;
    if (s_lcdFrameDone == nullptr) {
        return false;
    }

    BaseType_t taskWoken = pdFALSE;
    xSemaphoreGiveFromISR(s_lcdFrameDone, &taskWoken);
    return taskWoken == pdTRUE;
}

void waitFrameSlot() {
    if (s_lcdFrameDone == nullptr) {
        return;
    }
    xSemaphoreTake(s_lcdFrameDone, pdMS_TO_TICKS(20));
}

void ioExtSetMode(uint8_t mask) {
    const uint8_t data[] = {kIoExtRegMode, mask};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, kIoExtAddress, data, sizeof(data), pdMS_TO_TICKS(100)));
}

void ioExtWritePin(uint8_t pin, bool level) {
    static uint8_t outputMask = 0xFF;
    if (level) {
        outputMask |= static_cast<uint8_t>(1U << pin);
    } else {
        outputMask &= static_cast<uint8_t>(~(1U << pin));
    }
    const uint8_t data[] = {kIoExtRegOutput, outputMask};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, kIoExtAddress, data, sizeof(data), pdMS_TO_TICKS(100)));
}

void ioExtSetPwmPercent(uint8_t percent) {
    if (percent > 97) {
        percent = 97;
    }
    const uint8_t value = static_cast<uint8_t>((static_cast<uint16_t>(percent) * 255U) / 100U);
    const uint8_t data[] = {kIoExtRegPwm, value};
    ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_NUM_0, kIoExtAddress, data, sizeof(data), pdMS_TO_TICKS(100)));
}

void initI2cBus() {
    static bool initialized = false;
    if (initialized) {
        return;
    }

    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = kI2cSda,
        .scl_io_num = kI2cScl,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master =
            {
                .clk_speed = kI2cFrequencyHz,
            },
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, config.mode, 0, 0, 0));

    ioExtSetMode(0xFF);
    initialized = true;
}

void initTouch() {
    if (s_touchIo != nullptr) {
        return;
    }

    pinMode(static_cast<uint8_t>(kTouchIntPin), OUTPUT);
    ioExtWritePin(kIoExtPinTouchReset, false);
    delay(100);
    digitalWrite(static_cast<uint8_t>(kTouchIntPin), LOW);
    delay(100);
    ioExtWritePin(kIoExtPinTouchReset, true);
    delay(200);

    const esp_lcd_panel_io_i2c_config_t touchConfig = {
        .dev_addr = kGt911Address,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 8,
        .flags = {
            .dc_low_on_data = 0,
            .disable_control_phase = 1,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(reinterpret_cast<esp_lcd_i2c_bus_handle_t>(I2C_NUM_0), &touchConfig, &s_touchIo));

    uint8_t productId[3] = {0};
    if (esp_lcd_panel_io_rx_param(s_touchIo, kGt911RegProductId, productId, sizeof(productId)) == ESP_OK) {
        ESP_LOGI(kTag, "GT911 product id: %02X %02X %02X", productId[0], productId[1], productId[2]);
    }
}

void initPanel() {
    if (s_panel != nullptr) {
        return;
    }

    const esp_lcd_rgb_panel_config_t panelConfig = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings =
            {
                .pclk_hz = kPixelClockHz,
                .h_res = kWidth,
                .v_res = kHeight,
                .hsync_pulse_width = 162,
                .hsync_back_porch = 152,
                .hsync_front_porch = 48,
                .vsync_pulse_width = 45,
                .vsync_back_porch = 13,
                .vsync_front_porch = 3,
                .flags =
                    {
                        .pclk_active_neg = 1,
                    },
            },
        .data_width = kRgbDataWidth,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = GPIO_NUM_46,
        .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5,
        .pclk_gpio_num = GPIO_NUM_7,
        .data_gpio_nums =
            {
                GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17,
                GPIO_NUM_10, GPIO_NUM_39, GPIO_NUM_0,  GPIO_NUM_45,
                GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_1,
                GPIO_NUM_2,  GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40,
            },
        .disp_gpio_num = -1,
        .on_frame_trans_done = onRgbFrameDone,
        .user_ctx = nullptr,
        .flags =
            {
                .fb_in_psram = 1,
            },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panelConfig, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    if (s_lcdFrameDone == nullptr) {
        s_lcdFrameDone = xSemaphoreCreateBinary();
        if (s_lcdFrameDone != nullptr) {
            xSemaphoreGive(s_lcdFrameDone);
        }
    }
}

bool readTouchPointInternal(uint16_t& x, uint16_t& y, uint8_t& touchCount) {
    touchCount = 0;
    if (s_touchIo == nullptr) {
        return false;
    }

    uint8_t status = 0;
    if (esp_lcd_panel_io_rx_param(s_touchIo, kGt911RegStatus, &status, 1) != ESP_OK) {
        return false;
    }

    const uint8_t detectedTouches = status & 0x0F;
    if ((status & 0x80U) == 0 || detectedTouches == 0 || detectedTouches > 5) {
        if (status != 0) {
            const uint8_t clear = 0;
            esp_lcd_panel_io_tx_param(s_touchIo, kGt911RegStatus, &clear, 1);
        }
        return false;
    }

    uint8_t pointData[8] = {0};
    if (esp_lcd_panel_io_rx_param(s_touchIo, static_cast<int>(kGt911RegStatus + 1), pointData, sizeof(pointData)) != ESP_OK) {
        const uint8_t clear = 0;
        esp_lcd_panel_io_tx_param(s_touchIo, kGt911RegStatus, &clear, 1);
        return false;
    }

    const uint8_t clear = 0;
    esp_lcd_panel_io_tx_param(s_touchIo, kGt911RegStatus, &clear, 1);

    touchCount = detectedTouches;
    x = static_cast<uint16_t>(pointData[1] | (pointData[2] << 8));
    y = static_cast<uint16_t>(pointData[3] | (pointData[4] << 8));
    if (x >= kWidth) {
        x = kWidth - 1;
    }
    if (y >= kHeight) {
        y = kHeight - 1;
    }
    return true;
}

}  // namespace

bool begin() {
    if (s_panel != nullptr && s_touchIo != nullptr) {
        return true;
    }

    initI2cBus();
    initTouch();
    initPanel();
    if (s_frameBufferA == nullptr || s_frameBufferB == nullptr) {
        const size_t bytes = static_cast<size_t>(kWidth) * kHeight * sizeof(uint16_t);
        s_frameBufferA = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        s_frameBufferB = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    setBacklightPercent(100);
    return s_panel != nullptr && s_frameBufferA != nullptr && s_frameBufferB != nullptr;
}

void getFrameBuffers(uint16_t*& bufferA, uint16_t*& bufferB) {
    bufferA = s_frameBufferA;
    bufferB = s_frameBufferB;
}

void present(const uint16_t* frameBuffer) {
    if (s_panel == nullptr || frameBuffer == nullptr) {
        return;
    }
    waitFrameSlot();
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_panel,
        0,
        0,
        kWidth,
        kHeight,
        const_cast<uint16_t*>(frameBuffer)));
}

void presentWindow(int x, int y, int width, int height, const uint16_t* pixels) {
    if (s_panel == nullptr || pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    waitFrameSlot();

    const int x0 = max(0, x);
    const int y0 = max(0, y);
    const int x1 = min(kWidth, x + width);
    const int y1 = min(kHeight, y + height);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        s_panel,
        x0,
        y0,
        x1,
        y1,
        const_cast<uint16_t*>(pixels)));
}

bool readTouch(TouchPoint& point) {
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t touchCount = 0;
    const bool touchedNow = readTouchPointInternal(x, y, touchCount);

    if (touchedNow) {
        s_touchPressed = true;
        s_touchX = x;
        s_touchY = y;
        s_touchCount = touchCount;
        s_touchMissCount = 0;
    } else {
        if (s_touchPressed && s_touchMissCount < kTouchReleaseMissThreshold) {
            ++s_touchMissCount;
        } else {
            s_touchPressed = false;
            s_touchCount = 0;
            s_touchMissCount = 0;
        }
    }

    point.touched = s_touchPressed;
    point.x = s_touchX;
    point.y = s_touchY;
    point.touchCount = s_touchCount;
    return point.touched;
}

void setBacklightPercent(uint8_t percent) {
    if (percent == 0) {
        ioExtSetPwmPercent(0);
        ioExtWritePin(kIoExtPinBacklight, false);
        return;
    }

    ioExtWritePin(kIoExtPinBacklight, true);
    const uint8_t duty = static_cast<uint8_t>(100 - min<uint8_t>(percent, 100));
    ioExtSetPwmPercent(duty);
}

}  // namespace DisplayBoard
