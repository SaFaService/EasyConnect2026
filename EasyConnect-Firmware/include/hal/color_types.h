/**
 * include/hal/color_types.h — inline copy of ESP-IDF hal/color_types.h
 *
 * The project's include/hal/ shadows the SDK's hal/ path. Adding hal/include
 * globally to CPPPATH causes conflicts with newer gpio_types.h etc.
 * This file is a verbatim copy of the original.
 *
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COLOR_SPACE_RAW = 1,
    COLOR_SPACE_RGB,
    COLOR_SPACE_YUV,
    COLOR_SPACE_GRAY,
    COLOR_SPACE_ARGB,
    COLOR_SPACE_ALPHA,
    COLOR_SPACE_CLUT,
} color_space_t;

typedef enum {
    COLOR_PIXEL_RAW8,
    COLOR_PIXEL_RAW10,
    COLOR_PIXEL_RAW12,
} color_pixel_raw_format_t;

typedef enum {
    COLOR_PIXEL_RGB888,
    COLOR_PIXEL_RGB666,
    COLOR_PIXEL_RGB565,
} color_pixel_rgb_format_t;

typedef enum {
    COLOR_PIXEL_YUV444,
    COLOR_PIXEL_YUV422,
    COLOR_PIXEL_YUV420,
    COLOR_PIXEL_YUV411,
    COLOR_PIXEL_UYVY422,
    COLOR_PIXEL_VYUY422,
    COLOR_PIXEL_YUYV422,
    COLOR_PIXEL_YVYU422,
} color_pixel_yuv_format_t;

typedef enum {
    COLOR_PIXEL_GRAY4,
    COLOR_PIXEL_GRAY8,
} color_pixel_gray_format_t;

typedef enum {
    COLOR_PIXEL_ARGB8888,
} color_pixel_argb_format_t;

typedef enum {
    COLOR_PIXEL_A4,
    COLOR_PIXEL_A8,
} color_pixel_alpha_format_t;

typedef enum {
    COLOR_PIXEL_L4,
    COLOR_PIXEL_L8,
} color_pixel_clut_format_t;

#define COLOR_SPACE_BITWIDTH                 8
#define COLOR_PIXEL_FORMAT_BITWIDTH          24
#define COLOR_SPACE_TYPE(color_type_id)      (((color_type_id) >> COLOR_PIXEL_FORMAT_BITWIDTH) & ((1 << COLOR_SPACE_BITWIDTH) - 1))
#define COLOR_PIXEL_FORMAT(color_type_id)    ((color_type_id) & ((1 << COLOR_PIXEL_FORMAT_BITWIDTH) - 1))
#define COLOR_TYPE_ID(color_space, pixel_format) (((color_space) << COLOR_PIXEL_FORMAT_BITWIDTH) | (pixel_format))

typedef union {
    struct {
        uint32_t pixel_format: COLOR_PIXEL_FORMAT_BITWIDTH;
        uint32_t color_space: COLOR_SPACE_BITWIDTH;
    };
    uint32_t color_type_id;
} color_space_pixel_format_t;

typedef enum {
    COLOR_RANGE_LIMIT,
    COLOR_RANGE_FULL,
} color_range_t;

typedef enum {
    COLOR_CONV_STD_RGB_YUV_BT601,
    COLOR_CONV_STD_RGB_YUV_BT709,
} color_conv_std_rgb_yuv_t;

typedef enum {
    COLOR_RAW_ELEMENT_ORDER_BGGR,
    COLOR_RAW_ELEMENT_ORDER_GBRG,
    COLOR_RAW_ELEMENT_ORDER_GRBG,
    COLOR_RAW_ELEMENT_ORDER_RGGB,
} color_raw_element_order_t;

typedef enum {
    COLOR_RGB_ELEMENT_ORDER_RGB,
    COLOR_RGB_ELEMENT_ORDER_BGR,
} color_rgb_element_order_t;

typedef union {
    struct {
        uint32_t b: 8;
        uint32_t g: 8;
        uint32_t r: 8;
        uint32_t a: 8;
    };
    uint32_t val;
} color_pixel_argb8888_data_t;

typedef union {
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
    };
    uint32_t val;
} color_pixel_rgb888_data_t;

typedef union {
    struct {
        uint16_t b: 5;
        uint16_t g: 6;
        uint16_t r: 5;
    };
    uint16_t val;
} color_pixel_rgb565_data_t;

typedef union {
    struct {
        uint8_t gray;
    };
    uint8_t val;
} color_pixel_gray8_data_t;

typedef struct {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} color_macroblock_yuv_data_t;

typedef enum {
    COLOR_COMPONENT_R,
    COLOR_COMPONENT_G,
    COLOR_COMPONENT_B,
    COLOR_COMPONENT_INVALID,
} color_component_t;

typedef enum {
    COLOR_YUV422_PACK_ORDER_YUYV,
    COLOR_YUV422_PACK_ORDER_YVYU,
    COLOR_YUV422_PACK_ORDER_UYVY,
    COLOR_YUV422_PACK_ORDER_VYUY,
} color_yuv422_pack_order_t;

#ifdef __cplusplus
}
#endif
