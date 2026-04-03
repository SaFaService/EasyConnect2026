/**
 * include/hal/i2c_types.h - compatibility shim for old ESP-IDF I2C API
 */

#pragma once

#include "esp_idf_version.h"

/* Include the original SDK header first. */
#include_next "hal/i2c_types.h"

/*
 * Add missing aliases only on Arduino core 2.x / IDF 4.x.
 * On IDF 5.x these symbols already exist and redefining them breaks build.
 */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
typedef enum {
    I2C_ADDR_BIT_LEN_7  = 0,
    I2C_ADDR_BIT_LEN_10 = 1,
} i2c_addr_bit_len_t;

typedef enum {
    I2C_CLK_SRC_DEFAULT = 11,
    I2C_CLK_SRC_XTAL    = 11,
    I2C_CLK_SRC_RC_FAST = 9,
} i2c_clock_source_t;
#endif
