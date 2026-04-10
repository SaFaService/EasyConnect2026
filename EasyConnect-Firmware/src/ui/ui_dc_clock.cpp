#include "ui_dc_clock.h"

#include <Arduino.h>
#include <Preferences.h>

#include "display_port/i2c.h"

namespace {

struct TimeZoneOption {
    const char* name;
    const char* tz;
};

static constexpr TimeZoneOption k_timezones[] = {
    {"Europe/Rome",      "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"UTC",              "UTC0"},
    {"Europe/London",    "GMT0BST,M3.5.0/1,M10.5.0/2"},
    {"America/New_York", "EST5EDT,M3.2.0/2,M11.1.0/2"},
};

static constexpr int k_timezone_count = (int)(sizeof(k_timezones) / sizeof(k_timezones[0]));
static constexpr uint8_t k_rtc_addr = 0x68;  // DS3231/DS1307 compatible

static const char* k_tz_dropdown_opts =
    "Europe/Rome\n"
    "UTC\n"
    "Europe/London\n"
    "America/New_York";

static Preferences g_clock_pref;
static bool g_pref_ready = false;

static bool g_initialized = false;
static bool g_has_rtc = false;
static bool g_auto_enabled = true;
static int  g_tz_index = 0;

static i2c_master_dev_handle_t g_rtc_dev = NULL;

static time_t g_base_epoch_utc = 0;
static uint32_t g_base_ms = 0;

static uint8_t _to_bcd(uint8_t value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static uint8_t _from_bcd(uint8_t value) {
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

static bool _is_leap_year(int year) {
    if ((year % 4) != 0) return false;
    if ((year % 100) != 0) return true;
    return (year % 400) == 0;
}

static int _days_in_month(int year, int month) {
    static const int k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 31;
    if (month == 2 && _is_leap_year(year)) return 29;
    return k_days[month - 1];
}

static void _apply_timezone() {
    if (g_tz_index < 0) g_tz_index = 0;
    if (g_tz_index >= k_timezone_count) g_tz_index = k_timezone_count - 1;
    setenv("TZ", k_timezones[g_tz_index].tz, 1);
    tzset();
}

static void _load_prefs() {
    if (!g_pref_ready) {
        g_pref_ready = g_clock_pref.begin("easy_clock", false);
    }
    if (!g_pref_ready) {
        g_tz_index = 0;
        g_auto_enabled = true;
        return;
    }

    g_tz_index = (int)g_clock_pref.getUChar("tz_idx", 0);
    if (g_tz_index < 0 || g_tz_index >= k_timezone_count) g_tz_index = 0;
    g_auto_enabled = g_clock_pref.getBool("auto_en", true);
}

static void _save_timezone_pref() {
    if (!g_pref_ready) return;
    g_clock_pref.putUChar("tz_idx", (uint8_t)g_tz_index);
}

static void _save_auto_pref() {
    if (!g_pref_ready) return;
    g_clock_pref.putBool("auto_en", g_auto_enabled);
}

static time_t _current_epoch_utc() {
    const uint32_t elapsed_s = (uint32_t)((millis() - g_base_ms) / 1000U);
    return (time_t)(g_base_epoch_utc + (time_t)elapsed_s);
}

static void _set_base_epoch_utc(time_t epoch_utc) {
    g_base_epoch_utc = epoch_utc;
    g_base_ms = millis();
}

static bool _set_from_local_tm(struct tm* local_tm) {
    if (!local_tm) return false;
    local_tm->tm_isdst = -1;
    const time_t epoch = mktime(local_tm);
    if (epoch < 0) return false;
    _set_base_epoch_utc(epoch);
    return true;
}

static bool _rtc_read_regs(uint8_t reg, uint8_t* out, size_t len) {
    if (!g_rtc_dev || !out || len == 0) return false;
    const esp_err_t err = i2c_master_transmit_receive(g_rtc_dev, &reg, 1, out, len, 120);
    return err == ESP_OK;
}

static bool _rtc_write_regs(uint8_t reg, const uint8_t* data, size_t len) {
    if (!g_rtc_dev || !data || len == 0) return false;
    uint8_t payload[1 + 7] = {0};
    if (len > 7) return false;
    payload[0] = reg;
    for (size_t i = 0; i < len; i++) payload[1 + i] = data[i];
    const esp_err_t err = i2c_master_transmit(g_rtc_dev, payload, len + 1, 120);
    return err == ESP_OK;
}

static bool _rtc_attach_device() {
    if (g_rtc_dev) return true;
    if (!DEV_I2C_Get_Bus()) return false;
    DEV_I2C_Set_Slave_Addr(&g_rtc_dev, k_rtc_addr);
    return g_rtc_dev != NULL;
}

static bool _rtc_probe() {
    if (!_rtc_attach_device()) return false;
    uint8_t sec = 0;
    if (!_rtc_read_regs(0x00, &sec, 1)) return false;
    const uint8_t sec_bcd = (uint8_t)(sec & 0x7F);
    return _from_bcd(sec_bcd) <= 59;
}

static bool _rtc_read_local_tm(struct tm* out_tm) {
    if (!out_tm) return false;
    uint8_t raw[7] = {0};
    if (!_rtc_read_regs(0x00, raw, sizeof(raw))) return false;

    int second = (int)_from_bcd((uint8_t)(raw[0] & 0x7F));
    int minute = (int)_from_bcd((uint8_t)(raw[1] & 0x7F));
    int hour = 0;
    if ((raw[2] & 0x40) != 0) {
        int h12 = (int)_from_bcd((uint8_t)(raw[2] & 0x1F));
        if (h12 == 12) h12 = 0;
        hour = ((raw[2] & 0x20) != 0) ? (h12 + 12) : h12;
    } else {
        hour = (int)_from_bcd((uint8_t)(raw[2] & 0x3F));
    }
    const int day = (int)_from_bcd((uint8_t)(raw[4] & 0x3F));
    const int month = (int)_from_bcd((uint8_t)(raw[5] & 0x1F));
    const int year = 2000 + (int)_from_bcd(raw[6]);

    if (year < 2020 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > _days_in_month(year, month)) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;

    struct tm tm_local = {};
    tm_local.tm_year = year - 1900;
    tm_local.tm_mon = month - 1;
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min = minute;
    tm_local.tm_sec = second;
    tm_local.tm_isdst = -1;

    *out_tm = tm_local;
    return true;
}

static bool _rtc_write_local_tm(const struct tm& tm_local) {
    if (!g_has_rtc) return false;
    if (!_rtc_attach_device()) return false;

    struct tm tmp = tm_local;
    tmp.tm_isdst = -1;
    const time_t epoch = mktime(&tmp);
    if (epoch < 0) return false;
    localtime_r(&epoch, &tmp);

    int dow = tmp.tm_wday;
    if (dow <= 0) dow = 7;

    uint8_t raw[7] = {0};
    raw[0] = _to_bcd((uint8_t)tmp.tm_sec);
    raw[1] = _to_bcd((uint8_t)tmp.tm_min);
    raw[2] = _to_bcd((uint8_t)tmp.tm_hour);   // 24h mode
    raw[3] = _to_bcd((uint8_t)dow);
    raw[4] = _to_bcd((uint8_t)tmp.tm_mday);
    raw[5] = _to_bcd((uint8_t)(tmp.tm_mon + 1));
    raw[6] = _to_bcd((uint8_t)((tmp.tm_year + 1900) % 100));
    return _rtc_write_regs(0x00, raw, sizeof(raw));
}

static bool _sync_from_ntp_once() {
    configTzTime(k_timezones[g_tz_index].tz,
                 "pool.ntp.org",
                 "time.google.com",
                 "time.cloudflare.com");

    struct tm tm_local = {};
    for (int i = 0; i < 10; i++) {
        if (getLocalTime(&tm_local, 500)) {
            const time_t epoch = time(nullptr);
            if (epoch <= 0) return false;
            _set_base_epoch_utc(epoch);
            if (g_has_rtc) _rtc_write_local_tm(tm_local);
            return true;
        }
        delay(150);
    }
    return false;
}

static void _set_default_zero_clock() {
    struct tm tm_local = {};
    tm_local.tm_year = 126;  // 2026
    tm_local.tm_mon = 0;
    tm_local.tm_mday = 1;
    tm_local.tm_hour = 0;
    tm_local.tm_min = 0;
    tm_local.tm_sec = 0;
    _set_from_local_tm(&tm_local);
}

}  // namespace

void ui_dc_clock_init(void) {
    if (g_initialized) return;

    _load_prefs();
    _apply_timezone();

    g_has_rtc = _rtc_probe();

    bool have_time = false;
    struct tm tm_local = {};

    if (g_has_rtc && _rtc_read_local_tm(&tm_local)) {
        have_time = _set_from_local_tm(&tm_local);
    }

    if (!g_has_rtc) {
        // Requirement: automatic date/time unavailable without RTC.
        g_auto_enabled = false;
        _save_auto_pref();
        have_time = _sync_from_ntp_once();
    } else if (g_auto_enabled) {
        // With RTC installed, if automatic mode is enabled, prefer NTP when available.
        (void)_sync_from_ntp_once();
    }

    if (!have_time) {
        _set_default_zero_clock();
    }

    g_initialized = true;
}

bool ui_dc_clock_has_rtc(void) {
    return g_has_rtc;
}

bool ui_dc_clock_is_auto_enabled(void) {
    return g_auto_enabled;
}

void ui_dc_clock_set_auto_enabled(bool enabled) {
    if (!g_has_rtc) {
        g_auto_enabled = false;
        _save_auto_pref();
        return;
    }
    g_auto_enabled = enabled;
    _save_auto_pref();
    if (g_auto_enabled) {
        (void)_sync_from_ntp_once();
    }
}

const char* ui_dc_clock_timezone_options(void) {
    return k_tz_dropdown_opts;
}

int ui_dc_clock_timezone_index_get(void) {
    return g_tz_index;
}

void ui_dc_clock_timezone_index_set(int index) {
    if (index < 0) index = 0;
    if (index >= k_timezone_count) index = k_timezone_count - 1;
    if (index == g_tz_index) return;

    g_tz_index = index;
    _apply_timezone();
    _save_timezone_pref();

    if (g_auto_enabled) {
        (void)_sync_from_ntp_once();
    }
}

bool ui_dc_clock_get_local_tm(struct tm* out_tm) {
    if (!out_tm) return false;
    const time_t now_epoch = _current_epoch_utc();
    return localtime_r(&now_epoch, out_tm) != nullptr;
}

bool ui_dc_clock_set_manual_local(int year, int month, int day,
                                  int hour, int minute, int second) {
    if (year < 2020 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > _days_in_month(year, month)) return false;
    if (hour < 0 || hour > 23) return false;
    if (minute < 0 || minute > 59) return false;
    if (second < 0 || second > 59) return false;

    struct tm tm_local = {};
    tm_local.tm_year = year - 1900;
    tm_local.tm_mon = month - 1;
    tm_local.tm_mday = day;
    tm_local.tm_hour = hour;
    tm_local.tm_min = minute;
    tm_local.tm_sec = second;
    tm_local.tm_isdst = -1;

    if (!_set_from_local_tm(&tm_local)) return false;
    if (g_has_rtc) (void)_rtc_write_local_tm(tm_local);
    return true;
}

void ui_dc_clock_format_time_hms(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%02d:%02d:%02d",
             tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
}

void ui_dc_clock_format_date_numeric(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%02d/%02d/%04d",
             tm_local.tm_mday, tm_local.tm_mon + 1, tm_local.tm_year + 1900);
}

void ui_dc_clock_format_date_home(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    struct tm tm_local = {};
    if (!ui_dc_clock_get_local_tm(&tm_local)) {
        out[0] = '\0';
        return;
    }
    if (strftime(out, out_size, "%d %b %Y", &tm_local) == 0) {
        out[0] = '\0';
    }
}
