#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

/**
 * @brief Initialize shared date/time state.
 *
 * Startup policy:
 * - probe RTC on I2C
 * - if RTC missing, try NTP once
 * - if still unavailable, start from 00:00:00 and count forward
 */
void ui_dc_clock_init(void);

/** True when an RTC device is detected on I2C bus. */
bool ui_dc_clock_has_rtc(void);

/** "Automatic date/time" runtime state. */
bool ui_dc_clock_is_auto_enabled(void);
void ui_dc_clock_set_auto_enabled(bool enabled);

/** Timezone options for LVGL dropdown. */
const char* ui_dc_clock_timezone_options(void);
int ui_dc_clock_timezone_index_get(void);
void ui_dc_clock_timezone_index_set(int index);

/** Read current local date/time from shared clock state. */
bool ui_dc_clock_get_local_tm(struct tm* out_tm);

/** Set local date/time manually (used by settings popups). */
bool ui_dc_clock_set_manual_local(int year, int month, int day,
                                  int hour, int minute, int second);

/** Format helpers for UI labels. */
void ui_dc_clock_format_time_hms(char* out, size_t out_size);
void ui_dc_clock_format_date_numeric(char* out, size_t out_size);
void ui_dc_clock_format_date_home(char* out, size_t out_size);
