/*****************************************************************************
 * | File      	 :   rgb_lcd_port.c
 * | Author      :   Waveshare team
 * | Function    :   Hardware underlying interface
 * | Info        :
 *                   RGB LCD driver code
 *----------------
 * |This version :   V1.0
 * | Date        :   2024-11-19
 * | Info        :   Basic version
 *
 ******************************************************************************/

#include "rgb_lcd_port.h"
#include "lvgl_port.h"

const char *TAG = "example";

/*
 * Livello piu' vicino al pannello LCD:
 * - crea il driver RGB di ESP-IDF;
 * - configura timing e GPIO;
 * - registra i callback di fine frame;
 * - espone utility per backlight, framebuffer e recovery del pannello.
 */

#if defined(LVGL_PORT_LCD_RGB_BUFFER_NUMS)
#define RGB_PANEL_NUM_FBS LVGL_PORT_LCD_RGB_BUFFER_NUMS
#else
#define RGB_PANEL_NUM_FBS EXAMPLE_LCD_RGB_BUFFER_NUMS
#endif

// Handle for the RGB LCD panel
static esp_lcd_panel_handle_t panel_handle = NULL; // Declare a handle for the LCD panel
static portMUX_TYPE s_activity_guard_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_activity_guard_count = 0;
static constexpr uint32_t k_activity_guard_pclk_hz = 12U * 1000U * 1000U;

// Callback richiamata dal driver RGB quando un frame e' stato effettivamente
// trasmesso. Qui non facciamo rendering: notifichiamo solo il layer LVGL.
IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

/**
 * @brief Initialize the RGB LCD panel on the ESP32-S3
 *
 * This function configures and initializes an RGB LCD panel driver
 * using the ESP-IDF RGB LCD driver API. It sets up timing parameters,
 * GPIOs, data width, and framebuffer settings for the LCD panel.
 *
 * @return
 *    - ESP_OK: Initialization successful.
 *    - Other error codes: Initialization failed.
 */
esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_init()
{
    // Log the start of the RGB LCD panel driver installation
    ESP_LOGI(TAG, "Install RGB LCD panel driver");

    // Timing e pinout specifici del pannello 1024x600 della board.
    // Piccole variazioni qui possono produrre flicker, shift o tearing.
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT, // Use the default clock source
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ, // Pixel clock frequency in Hz
            .h_res = EXAMPLE_LCD_H_RES,            // Horizontal resolution (number of pixels per row)
            .v_res = EXAMPLE_LCD_V_RES,            // Vertical resolution (number of rows)
            .hsync_pulse_width = 162,                // Horizontal sync pulse width
            .hsync_back_porch = 152,                 // Horizontal back porch
            .hsync_front_porch = 48,                // Horizontal front porch
            .vsync_pulse_width = 45,                // Vertical sync pulse width
            .vsync_back_porch = 13,                 // Vertical back porch
            .vsync_front_porch = 3,                // Vertical front porch
            .flags = {
                .pclk_active_neg = 1, // Set pixel clock polarity to active low
            },
        },
        .data_width = EXAMPLE_RGB_DATA_WIDTH,                    // Data width for RGB signals
        .bits_per_pixel = EXAMPLE_RGB_BIT_PER_PIXEL,             // Number of bits per pixel (color depth)
        // Il numero frame buffer deve restare coerente con la strategia scelta
        // nel layer LVGL, altrimenti pannello e GUI non ragionano sugli stessi buffer.
        .num_fbs = RGB_PANEL_NUM_FBS,
        .bounce_buffer_size_px = EXAMPLE_RGB_BOUNCE_BUFFER_SIZE, // Bounce buffer size in pixels
        .sram_trans_align = 4,                                   // SRAM transaction alignment in bytes
        .psram_trans_align = 64,                                 // PSRAM transaction alignment in bytes
        .hsync_gpio_num = EXAMPLE_LCD_IO_RGB_HSYNC,              // GPIO for horizontal sync signal
        .vsync_gpio_num = EXAMPLE_LCD_IO_RGB_VSYNC,              // GPIO for vertical sync signal
        .de_gpio_num = EXAMPLE_LCD_IO_RGB_DE,                    // GPIO for data enable signal
        .pclk_gpio_num = EXAMPLE_LCD_IO_RGB_PCLK,                // GPIO for pixel clock signal
        .disp_gpio_num = EXAMPLE_LCD_IO_RGB_DISP,                // GPIO for display enable signal
        .data_gpio_nums = {
            // GPIOs for RGB data signals
            EXAMPLE_LCD_IO_RGB_DATA0,  // Data bit 0
            EXAMPLE_LCD_IO_RGB_DATA1,  // Data bit 1
            EXAMPLE_LCD_IO_RGB_DATA2,  // Data bit 2
            EXAMPLE_LCD_IO_RGB_DATA3,  // Data bit 3
            EXAMPLE_LCD_IO_RGB_DATA4,  // Data bit 4
            EXAMPLE_LCD_IO_RGB_DATA5,  // Data bit 5
            EXAMPLE_LCD_IO_RGB_DATA6,  // Data bit 6
            EXAMPLE_LCD_IO_RGB_DATA7,  // Data bit 7
            EXAMPLE_LCD_IO_RGB_DATA8,  // Data bit 8
            EXAMPLE_LCD_IO_RGB_DATA9,  // Data bit 9
            EXAMPLE_LCD_IO_RGB_DATA10, // Data bit 10
            EXAMPLE_LCD_IO_RGB_DATA11, // Data bit 11
            EXAMPLE_LCD_IO_RGB_DATA12, // Data bit 12
            EXAMPLE_LCD_IO_RGB_DATA13, // Data bit 13
            EXAMPLE_LCD_IO_RGB_DATA14, // Data bit 14
            EXAMPLE_LCD_IO_RGB_DATA15, // Data bit 15
        },
        .flags = {
            .fb_in_psram = 1, // Use PSRAM for framebuffers to save internal SRAM
        },
    };

    // Create and register the RGB LCD panel driver with the configuration above
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    // Log the initialization of the RGB LCD panel
    ESP_LOGI(TAG, "Initialize RGB LCD panel");

    // Initialize the RGB LCD panel
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // LVGL aspetta il vero VSYNC prima di considerare sicuro lo swap dei
    // framebuffer. Il bounce-frame-finish indica solo che il frame e' stato
    // consegnato al DMA, quindi non e' equivalente per l'anti-tearing.
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = rgb_lcd_on_vsync_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL)); // Register event callbacks

    // Return success status
    return panel_handle;
}

/**
 * @brief Display a specific window of an image on the RGB LCD.
 *
 * This function updates a rectangular portion of the RGB LCD screen with the
 * image data provided. The region is defined by the start and end coordinates
 * in both X and Y directions. If the specified coordinates exceed the screen
 * boundaries, they will be clipped accordingly.
 *
 * @param Xstart Starting X coordinate of the display window (inclusive).
 * @param Ystart Starting Y coordinate of the display window (inclusive).
 * @param Xend Ending X coordinate of the display window (exclusive, relative to Xstart).
 * @param Yend Ending Y coordinate of the display window (exclusive, relative to Ystart).
 * @param Image Pointer to the image data buffer, representing the full LCD resolution.
 */
void wavesahre_rgb_lcd_display_window(int16_t Xstart, int16_t Ystart, int16_t Xend, int16_t Yend, uint8_t *Image)
{
    // Ensure Xstart is within valid range, clip Xend to the screen width if necessary
    if (Xstart < 0) Xstart = 0;
    else if (Xend > EXAMPLE_LCD_H_RES) Xend = EXAMPLE_LCD_H_RES;

    // Ensure Ystart is within valid range, clip Yend to the screen height if necessary
    if (Ystart < 0) Ystart = 0;
    else if (Yend > EXAMPLE_LCD_V_RES) Yend = EXAMPLE_LCD_V_RES;

    // Calculate the width and height of the cropped region
    int crop_width = Xend - Xstart;
    int crop_height = Yend - Ystart;

    // Allocate memory for the cropped image data
    uint8_t *dst_data = (uint8_t *)malloc(crop_width * crop_height * 2); // 2 bytes per pixel
    if (!dst_data) {
        printf("Error: Failed to allocate memory for cropped bitmap.\n");
        return;
    }

    // Crop the image data (copy each row of the selected region)
    for (int y = 0; y < crop_height; y++) {
        // Calculate the source row start in the original image buffer
        const uint8_t *src_row = Image + ((Ystart + y) * EXAMPLE_LCD_H_RES + Xstart) * 2;
        // Calculate the destination row start in the cropped buffer
        uint8_t *dst_row = dst_data + y * crop_width * 2;
        // Copy the row data
        memcpy(dst_row, src_row, crop_width * 2);
    }

    // Draw the cropped region onto the LCD at the specified coordinates
    // The esp_lcd_panel_draw_bitmap function uses absolute screen coordinates.
    esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, dst_data);

    // Free the allocated memory for the cropped image buffer
    free(dst_data);
}


/**
 * @brief Display a full-screen image on the RGB LCD.
 *
 * This function replaces the entire LCD screen content with the image data
 * provided. It assumes the display resolution is 800x480.
 *
 * @param Image Pointer to the image data buffer.
 */
void wavesahre_rgb_lcd_display(uint8_t *Image)
{
    // Draw the entire image on the screen
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, Image);
}

void waveshare_get_frame_buffer(void **buf1, void **buf2)
{
    // Helper usato dal layer LVGL per lavorare direttamente sui framebuffer RGB.
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, buf1, buf2));
}

void waveshare_rgb_lcd_set_pclk(uint32_t freq_hz)
{
    if (!panel_handle || freq_hz == 0) {
        return;
    }

    // Ridurre il pixel clock puo' essere utile quando WiFi/PSRAM contendono banda.
    esp_err_t err = esp_lcd_rgb_panel_set_pclk(panel_handle, freq_hz);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_rgb_panel_set_pclk(%lu) failed: 0x%x",
                 (unsigned long)freq_hz, (unsigned int)err);
    }
}

void waveshare_rgb_lcd_request_restart()
{
    if (!panel_handle) {
        return;
    }

    // Tenta un riallineamento del motore RGB quando il pannello si "sfasa".
    esp_err_t err = esp_lcd_rgb_panel_restart(panel_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_rgb_panel_restart failed: 0x%x", (unsigned int)err);
    }
}

void waveshare_rgb_lcd_activity_guard_acquire()
{
    bool apply_low_clock = false;

    portENTER_CRITICAL(&s_activity_guard_lock);
    if (s_activity_guard_count == 0) {
        apply_low_clock = true;
    }
    s_activity_guard_count++;
    portEXIT_CRITICAL(&s_activity_guard_lock);

    if (apply_low_clock) {
        waveshare_rgb_lcd_set_pclk(k_activity_guard_pclk_hz);
    }
}

void waveshare_rgb_lcd_activity_guard_release()
{
    bool restore_normal_clock = false;

    portENTER_CRITICAL(&s_activity_guard_lock);
    if (s_activity_guard_count > 0) {
        s_activity_guard_count--;
        if (s_activity_guard_count == 0) {
            restore_normal_clock = true;
        }
    }
    portEXIT_CRITICAL(&s_activity_guard_lock);

    if (restore_normal_clock) {
        waveshare_rgb_lcd_set_pclk(EXAMPLE_LCD_PIXEL_CLOCK_HZ);
        waveshare_rgb_lcd_request_restart();
    }
}
/**
 * @brief Turn on the RGB LCD screen backlight.
 *
 * This function enables the backlight of the screen by configuring the IO EXTENSION
 * I/O expander to output mode and setting the backlight pin to high. The IO EXTENSION
 * is controlled via I2C.
 *
 * @return
 *    - ESP_OK: Operation successful.
 */
void wavesahre_rgb_lcd_bl_on()
{
    // Il backlight non e' pilotato da un GPIO diretto dell'S3 ma dall'IO expander.
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);  // Backlight ON configuration
}

/**
 * @brief Turn off the RGB LCD screen backlight.
 *
 * This function disables the backlight of the screen by configuring the IO EXTENSION
 * I/O expander to output mode and setting the backlight pin to low. The IO EXTENSION
 * is controlled via I2C.
 *
 * @return
 *    - ESP_OK: Operation successful.
 */
void wavesahre_rgb_lcd_bl_off()
{
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);  // Backlight OFF configuration
}
