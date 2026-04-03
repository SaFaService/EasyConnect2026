/**
 * include/driver/parlio_types.h — empty stub
 *
 * esp_lcd_io_parl.h includes this header, but ESP32-S3 does not support
 * the Parallel IO peripheral (SOC_PARLIO_SUPPORTED == 0), so no actual
 * definitions are needed. This stub satisfies the #include.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Placeholder — never instantiated on ESP32-S3 */
typedef int parlio_clock_source_t;

#ifdef __cplusplus
}
#endif
