/**
 * include/hal/mipi_dsi_types.h — empty stub for ESP-IDF 5.x compatibility
 *
 * The flat SDK (framework-arduinoespressif32-libs) ships an esp_lcd_types.h
 * that includes hal/mipi_dsi_types.h, but the header is not present in the
 * package. ESP32-S3 uses RGB parallel LCD, not MIPI DSI, so no actual
 * definitions are needed — this stub satisfies the #include directive.
 */

#pragma once
