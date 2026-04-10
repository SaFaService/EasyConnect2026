#ifndef _LVGL_PORT_H_
#define _LVGL_PORT_H_

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include <lvgl.h>
#include "display_lock.h"

#include "rgb_lcd_port.h"    // Header for Waveshare RGB LCD driver
#include "gt911.h"           // Header for touch screen operations (GT911)

/*
 * Porta di integrazione fra LVGL, driver RGB e driver touch.
 *
 * Qui si definisce la strategia complessiva di rendering:
 * - tick LVGL;
 * - task dedicato;
 * - allocazione buffer;
 * - modalita' anti-tearing.
 *
 * Il comportamento effettivo dipende dai macro qui sotto, ma anche dal
 * display lock che impedisce combinazioni note come instabili.
 */

#define LVGL_PORT_TICK_PERIOD_MS    (2)


/**
 * LVGL timer handle task related parameters, can be adjusted by users
 *
 */
#define LVGL_PORT_TASK_MAX_DELAY_MS (20)     // Keep touch/keyboard feedback responsive.
#define LVGL_PORT_TASK_MIN_DELAY_MS (5)      // The minimum delay of the LVGL timer task, in milliseconds
#define LVGL_PORT_TASK_STACK_SIZE   (8 * 1024) // Match vendor UI demo stack sizing under WiFi/LVGL concurrency
#define LVGL_PORT_TASK_PRIORITY     (2)        // The priority of the LVGL timer task
#define LVGL_PORT_TASK_CORE         (1)            // The core of the LVGL timer task,
// `-1` means the don't specify the core
/**
 *
 * Parametri dei buffer LVGL.
 *
 * Nota: con l'avoid tearing attivo, il rendering tende a lavorare direttamente
 * sui framebuffer del driver RGB, quindi questi macro diventano meno centrali.
 *
 *  - Memory type for buffer allocation:
 *      - MALLOC_CAP_SPIRAM: Allocate LVGL buffer in PSRAM
 *      - MALLOC_CAP_INTERNAL: Allocate LVGL buffer in SRAM
 *      (The SRAM is faster than PSRAM, but the PSRAM has a larger capacity)
 *
 */

#define CONFIG_EXAMPLE_LVGL_PORT_BUF_PSRAM 1
#define CONFIG_EXAMPLE_LVGL_PORT_BUF_INTERNAL 0

#if CONFIG_EXAMPLE_LVGL_PORT_BUF_PSRAM
#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_SPIRAM)
#elif CONFIG_EXAMPLE_LVGL_PORT_BUF_INTERNAL
#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
#define LVGL_PORT_BUFFER_HEIGHT         (100)

/**
 * Configurazioni anti-tearing.
 *
 * Nel setup attuale la modalita' desiderata e':
 * - avoid tearing attivo;
 * - mode 3 = double buffer RGB + direct mode LVGL.
 *
 */
#define LVGL_PORT_AVOID_TEAR_ENABLE     (DISPLAY_LOCK_AVOID_TEAR_ENABLE)
#if LVGL_PORT_AVOID_TEAR_ENABLE
/**
 * Set the avoid tearing mode:
 *      - 0: Disable avoid tearing function
 *      - 1: LCD double-buffer & LVGL full-refresh
 *      - 2: LCD triple-buffer & LVGL full-refresh
 *      - 3: LCD double-buffer & LVGL direct-mode (recommended)
 *
 */
#define LVGL_PORT_AVOID_TEAR_MODE       (DISPLAY_LOCK_AVOID_TEAR_MODE)

/**
 * Set the rotation degree of the LCD panel when the avoid tearing function is enabled:
 *      - 0: 0 degree
 *      - 90: 90 degree
 *      - 180: 180 degree
 *      - 270: 270 degree
 *
 */
#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE  (0)

/**
 * I macro sottostanti derivano automaticamente dalla modalita' scelta sopra.
 *
 */
#if LVGL_PORT_AVOID_TEAR_MODE == 0
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (1)
#define LVGL_PORT_FULL_REFRESH          (0)
#define LVGL_PORT_DIRECT_MODE           (0)
#elif LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#endif /* LVGL_PORT_AVOID_TEAR_MODE */

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0
#define EXAMPLE_LVGL_PORT_ROTATION_0    (1)
#else
#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 90
#define EXAMPLE_LVGL_PORT_ROTATION_90   (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 180
#define EXAMPLE_LVGL_PORT_ROTATION_180  (1)
#elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 270
#define EXAMPLE_LVGL_PORT_ROTATION_270  (1)
#endif
#ifdef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#undef LVGL_PORT_LCD_RGB_BUFFER_NUMS
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#endif
#endif /* EXAMPLE_LVGL_PORT_ROTATION_DEGREE */
#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (1)
#define LVGL_PORT_FULL_REFRESH          (0)
#define LVGL_PORT_DIRECT_MODE           (0)
#endif /* LVGL_PORT_AVOID_TEAR_ENABLE */

#if DISPLAY_DRIVER_LOCK_ENABLED
// Con display lock attivo, una modifica a questi valori fallisce in build
// invece di produrre regressioni difficili da capire solo a runtime.
#if LVGL_PORT_AVOID_TEAR_ENABLE != 1
#error "Display lock active: LVGL_PORT_AVOID_TEAR_ENABLE must stay 1"
#endif
#if LVGL_PORT_AVOID_TEAR_MODE != 3
#error "Display lock active: LVGL_PORT_AVOID_TEAR_MODE must stay 3"
#endif
#endif

/**
 * @brief Initialize LVGL port
 *
 * @param[in] lcd_handle: LCD panel handle
 * @param[in] tp_handle: Touch panel handle
 *
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid argument
 *      - Others: Fail
 */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);

/**
 * @brief Take LVGL mutex
 *
 * @param[in] timeout_ms: Timeout in [ms]. 0 will block indefinitely.
 *
 * @return
 *      - true:  Mutex was taken
 *      - false: Mutex was NOT taken
 */
bool lvgl_port_lock(int timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void lvgl_port_unlock(void);

/**
 * @brief Notifies the LVGL task when the transmission of the RGB frame buffer is completed.
 *
 * @return
 *      - true:  The tasks need to be re-scheduled
 *      - false: The tasks don't need to be re-scheduled
 */
bool lvgl_port_notify_rgb_vsync(void);

#endif //_LVGL_PORT_H_
