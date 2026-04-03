#pragma once

#include <Arduino.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

namespace DisplayBoard {

constexpr int kWidth = 1024;
constexpr int kHeight = 600;

struct TouchPoint {
    bool touched;
    uint16_t x;
    uint16_t y;
    uint8_t touchCount;
};

bool begin();
void getFrameBuffers(uint16_t*& bufferA, uint16_t*& bufferB);
void present(const uint16_t* frameBuffer);
void presentWindow(int x, int y, int width, int height, const uint16_t* pixels);
bool readTouch(TouchPoint& point);
void setBacklightPercent(uint8_t percent);

}  // namespace DisplayBoard
