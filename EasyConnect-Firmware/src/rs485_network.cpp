#include "rs485_network.h"
#include "Pins.h"
#if defined(BOARD_PROFILE_DISPLAY)
#include "display_port/rgb_lcd_port.h"
#endif
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/semphr.h>
#include <string.h>

#define RS485_NET_DIR  PIN_RS485_DIR
#define RS485_NET_TX   PIN_RS485_TX
#define RS485_NET_RX   PIN_RS485_RX

static constexpr uint32_t k_scan_response_timeout_ms   = 50;
static constexpr uint32_t k_manual_response_timeout_ms = 220;
static constexpr uint8_t  k_manual_retries             = 3;
static constexpr uint32_t k_manual_retry_gap_ms        = 20;
static constexpr uint32_t k_boot_probe_timeout_ms      = 220;
static constexpr uint8_t  k_boot_probe_retries         = 2;
static constexpr uint8_t  k_relay_cmd_retries          = 2;
static constexpr uint32_t k_refresh_cycle_pause_ms     = 2500;
static constexpr uint8_t  k_refresh_offline_threshold  = 2;
static constexpr size_t   k_frame_buffer_size          = 256;
static constexpr uint16_t k_cache_magic                = 0xA485;
static constexpr uint8_t  k_cache_version              = 3;
static constexpr int      k_runtime_max_devices        = RS485_NET_MAX_DEVICES * 2;

static volatile Rs485ScanState s_scan_state = Rs485ScanState::IDLE;
static volatile int s_scan_progress = 0;
static volatile int s_device_count = 0;
static Rs485Device s_devices[k_runtime_max_devices];
static volatile int s_plant_count = 0;
static Rs485Device s_plant_devices[RS485_NET_MAX_DEVICES];
static TaskHandle_t s_scan_task = nullptr;
static TaskHandle_t s_refresh_task = nullptr;
static SemaphoreHandle_t s_bus_mutex = nullptr;
static volatile Rs485BootProbeState s_boot_probe_state = Rs485BootProbeState::IDLE;
static volatile bool s_monitor_enabled = false;

static bool _query_raw_locked(int addr, uint32_t timeout_ms, String& raw_response, bool log_txrx);
static bool _device_identity_matches(const Rs485Device& expected, const Rs485Device& actual);
static void _set_runtime_from_plant(bool online_default);
static void _merge_response_for_address(uint8_t address, const String& resp);
static int _find_runtime_preferred_by_address(uint8_t address);
static void _mark_plant_comm_failed(const Rs485Device& plant_dev);
static void _remove_runtime_index(int idx);

static inline void _display_activity_guard_acquire() {
#if defined(BOARD_PROFILE_DISPLAY)
    waveshare_rgb_lcd_activity_guard_acquire();
#endif
}

static inline void _display_activity_guard_release() {
#if defined(BOARD_PROFILE_DISPLAY)
    waveshare_rgb_lcd_activity_guard_release();
#endif
}

struct Rs485CacheRecord {
    uint8_t address;
    uint8_t type;
    uint8_t sensor_profile;
    uint8_t sensor_mode;
    uint8_t data_valid;
    uint8_t group;
    uint8_t relay_mode;
    uint8_t relay_on;
    uint8_t relay_safety_closed;
    uint8_t relay_feedback_ok;
    uint8_t sensor_active;
    float t;
    float h;
    float p;
    char sn[32];
    char version[16];
    char relay_state[24];
};

static Rs485CacheRecord s_cache_records_io[RS485_NET_MAX_DEVICES];

static inline void _tx_mode() {
    Serial1.flush();
    if (RS485_NET_DIR != PIN_NOT_ASSIGNED) {
        digitalWrite(RS485_NET_DIR, HIGH);
        delayMicroseconds(80);
    }
}

static inline void _rx_mode() {
    Serial1.flush();
    if (RS485_NET_DIR != PIN_NOT_ASSIGNED) {
        digitalWrite(RS485_NET_DIR, LOW);
        delayMicroseconds(80);
    }
}

static bool _field_to_bool(const String& v) {
    return v.toInt() != 0;
}

static bool _field_to_uint8_checked(const String& v, uint8_t& out) {
    if (v.length() == 0) return false;
    for (size_t i = 0; i < v.length(); i++) {
        if (!isDigit((unsigned char)v.charAt((unsigned int)i))) return false;
    }
    const int parsed = v.toInt();
    if (parsed < 0 || parsed > 255) return false;
    out = (uint8_t)parsed;
    return true;
}

static String _serial_type_code(const char* sn) {
    if (!sn) return "";
    const String s(sn);
    if (s.length() < 8) return "";
    return s.substring(6, 8);
}

static bool _serial_is_meaningful(const char* sn) {
    if (!sn || !sn[0]) return false;
    return strcmp(sn, "N/A") != 0 && strcmp(sn, "?") != 0;
}

static Rs485SensorProfile _sensor_profile_from_data(const Rs485Device& dev) {
    const String tt = _serial_type_code(dev.sn);
    if (tt == "04") return Rs485SensorProfile::PRESSURE;
    if (tt == "05" || tt == "06") return Rs485SensorProfile::AIR_010;
    return Rs485SensorProfile::UNKNOWN;
}

static void _cache_record_from_device(const Rs485Device& dev, Rs485CacheRecord& rec) {
    memset(&rec, 0, sizeof(rec));
    rec.address = dev.address;
    rec.type = (uint8_t)dev.type;
    rec.sensor_profile = (uint8_t)dev.sensor_profile;
    rec.sensor_mode = dev.sensor_mode;
    rec.data_valid = dev.data_valid ? 1 : 0;
    rec.group = dev.group;
    rec.relay_mode = dev.relay_mode;
    rec.relay_on = dev.relay_on ? 1 : 0;
    rec.relay_safety_closed = dev.relay_safety_closed ? 1 : 0;
    rec.relay_feedback_ok = dev.relay_feedback_ok ? 1 : 0;
    rec.sensor_active = dev.sensor_active ? 1 : 0;
    rec.t = dev.t;
    rec.h = dev.h;
    rec.p = dev.p;
    strncpy(rec.sn, dev.sn, sizeof(rec.sn) - 1);
    strncpy(rec.version, dev.version, sizeof(rec.version) - 1);
    strncpy(rec.relay_state, dev.relay_state, sizeof(rec.relay_state) - 1);
    rec.sn[sizeof(rec.sn) - 1] = '\0';
    rec.version[sizeof(rec.version) - 1] = '\0';
    rec.relay_state[sizeof(rec.relay_state) - 1] = '\0';
}

static void _device_from_cache_record(const Rs485CacheRecord& rec, Rs485Device& dev) {
    memset(&dev, 0, sizeof(dev));
    dev.address = rec.address;
    dev.type = (Rs485DevType)rec.type;
    dev.sensor_profile = (Rs485SensorProfile)rec.sensor_profile;
    dev.sensor_mode = rec.sensor_mode;
    dev.data_valid = rec.data_valid != 0;
    dev.group = rec.group;
    dev.relay_mode = rec.relay_mode;
    dev.relay_on = rec.relay_on != 0;
    dev.relay_safety_closed = rec.relay_safety_closed != 0;
    dev.relay_feedback_ok = rec.relay_feedback_ok != 0;
    dev.sensor_active = rec.sensor_active != 0;
    dev.sensor_feedback_ok = true;
    dev.sensor_feedback_fault_latched = false;
    dev.t = rec.t;
    dev.h = rec.h;
    dev.p = rec.p;
    strncpy(dev.sn, rec.sn, sizeof(dev.sn) - 1);
    strncpy(dev.version, rec.version, sizeof(dev.version) - 1);
    strncpy(dev.relay_state, rec.relay_state, sizeof(dev.relay_state) - 1);
    dev.sn[sizeof(dev.sn) - 1] = '\0';
    dev.version[sizeof(dev.version) - 1] = '\0';
    dev.relay_state[sizeof(dev.relay_state) - 1] = '\0';
    strncpy(dev.sensor_state, "N/A", sizeof(dev.sensor_state) - 1);
    dev.sensor_state[sizeof(dev.sensor_state) - 1] = '\0';
    dev.online = true;
    dev.in_plant = true;
    dev.comm_failures = 0;
}

static void _save_plant_to_nvs() {
    Preferences pref;
    if (!pref.begin("rs485_net", false)) {
        Serial.println("[RS485-NET] WARN: NVS rs485_net non disponibile (save plant).");
        return;
    }

    const int count = (int)s_plant_count;
    for (int i = 0; i < count; i++) {
        _cache_record_from_device(s_plant_devices[i], s_cache_records_io[i]);
    }

    pref.putUShort("magic", k_cache_magic);
    pref.putUChar("ver", k_cache_version);
    pref.putUChar("count", (uint8_t)count);
    if (count > 0) {
        pref.putBytes("devs", s_cache_records_io, (size_t)count * sizeof(Rs485CacheRecord));
    } else {
        pref.remove("devs");
    }
    pref.end();
}

static void _load_plant_from_nvs() {
    s_plant_count = 0;

    Preferences pref;
    if (!pref.begin("rs485_net", true)) return;

    const uint16_t magic = pref.getUShort("magic", 0);
    const uint8_t version = pref.getUChar("ver", 0);
    const int requested_count = (int)pref.getUChar("count", 0);

    if (magic != k_cache_magic || version != k_cache_version || requested_count <= 0) {
        pref.end();
        return;
    }

    int count = requested_count;
    if (count > RS485_NET_MAX_DEVICES) count = RS485_NET_MAX_DEVICES;

    const size_t expected_len = (size_t)count * sizeof(Rs485CacheRecord);
    const size_t stored_len = pref.getBytesLength("devs");
    if (stored_len < expected_len || expected_len == 0) {
        pref.end();
        return;
    }

    const size_t read_len = pref.getBytes("devs", s_cache_records_io, expected_len);
    pref.end();
    if (read_len != expected_len) return;

    for (int i = 0; i < count; i++) {
        _device_from_cache_record(s_cache_records_io[i], s_plant_devices[i]);
    }
    s_plant_count = count;
}

static bool _query_with_retries_locked(uint8_t address, uint32_t timeout_ms,
                                       uint8_t retries, String& raw_response) {
    bool ok = false;
    raw_response = "";
    for (uint8_t attempt = 0; attempt < retries; attempt++) {
        String rx;
        ok = _query_raw_locked((int)address, timeout_ms, rx, false);
        if (ok) {
            raw_response = rx;
            break;
        }
        if (rx.length() > 0) raw_response = rx;
        if (attempt + 1 < retries) {
            vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
        }
    }
    return ok;
}

static void _copy_device(Rs485Device& dst, const Rs485Device& src, bool in_plant, bool online) {
    dst = src;
    dst.in_plant = in_plant;
    dst.online = online;
    dst.comm_failures = online ? 0 : k_refresh_offline_threshold;
}

static int _find_plant_index_by_identity(const Rs485Device& dev) {
    for (int i = 0; i < (int)s_plant_count; i++) {
        if (_device_identity_matches(s_plant_devices[i], dev)) return i;
    }
    return -1;
}

static int _find_plant_index_by_address_and_serial(uint8_t address, const char* serial_number) {
    for (int i = 0; i < (int)s_plant_count; i++) {
        if (s_plant_devices[i].address != address) continue;
        if (_serial_is_meaningful(serial_number) && _serial_is_meaningful(s_plant_devices[i].sn)) {
            if (strncmp(s_plant_devices[i].sn, serial_number, sizeof(s_plant_devices[i].sn)) == 0) {
                return i;
            }
            continue;
        }
        return i;
    }
    return -1;
}

static int _find_runtime_index_for_plant_identity(const Rs485Device& plant_dev) {
    for (int i = 0; i < (int)s_device_count; i++) {
        if (!s_devices[i].in_plant) continue;
        if (_device_identity_matches(s_devices[i], plant_dev)) return i;
    }
    return -1;
}

static int _find_runtime_extra_index(const Rs485Device& dev) {
    for (int i = 0; i < (int)s_device_count; i++) {
        if (s_devices[i].in_plant) continue;
        if (s_devices[i].address != dev.address) continue;
        if (s_devices[i].type != dev.type) continue;
        if (_serial_is_meaningful(s_devices[i].sn) && _serial_is_meaningful(dev.sn)) {
            if (strncmp(s_devices[i].sn, dev.sn, sizeof(s_devices[i].sn)) == 0) return i;
            continue;
        }
        return i;
    }
    return -1;
}

static int _find_runtime_insert_index(const Rs485Device& dev, bool in_plant) {
    int insert_at = (int)s_device_count;
    for (int i = 0; i < (int)s_device_count; i++) {
        if (dev.address < s_devices[i].address) {
            insert_at = i;
            break;
        }
        if (dev.address == s_devices[i].address && in_plant && !s_devices[i].in_plant) {
            insert_at = i;
            break;
        }
    }
    return insert_at;
}

static void _insert_runtime_device(const Rs485Device& dev, bool in_plant, bool online) {
    if ((int)s_device_count >= k_runtime_max_devices) return;

    const int insert_at = _find_runtime_insert_index(dev, in_plant);
    for (int i = (int)s_device_count; i > insert_at; i--) {
        s_devices[i] = s_devices[i - 1];
    }
    _copy_device(s_devices[insert_at], dev, in_plant, online);
    const int next_count = (int)s_device_count + 1;
    s_device_count = next_count;
}

static void _upsert_runtime_plant_device(const Rs485Device& dev, bool online) {
    for (int i = (int)s_device_count - 1; i >= 0; i--) {
        if (s_devices[i].in_plant) continue;
        if (s_devices[i].address != dev.address) continue;
        _remove_runtime_index(i);
    }
    const int idx = _find_runtime_index_for_plant_identity(dev);
    if (idx >= 0) {
        _copy_device(s_devices[idx], dev, true, online);
        return;
    }
    _insert_runtime_device(dev, true, online);
}

static void _upsert_runtime_extra_device(const Rs485Device& dev) {
    const int idx = _find_runtime_extra_index(dev);
    if (idx >= 0) {
        _copy_device(s_devices[idx], dev, false, true);
        return;
    }
    _insert_runtime_device(dev, false, true);
}

static void _remove_runtime_index(int idx) {
    if (idx < 0 || idx >= (int)s_device_count) return;
    for (int i = idx; i + 1 < (int)s_device_count; i++) {
        s_devices[i] = s_devices[i + 1];
    }
    if (s_device_count > 0) {
        const int next_count = (int)s_device_count - 1;
        s_device_count = next_count;
    }
}

static void _remove_runtime_plant_identity(const Rs485Device& plant_dev) {
    for (int i = (int)s_device_count - 1; i >= 0; i--) {
        if (!s_devices[i].in_plant) continue;
        if (_device_identity_matches(s_devices[i], plant_dev)) {
            _remove_runtime_index(i);
        }
    }
}

static void _set_runtime_from_plant(bool online_default) {
    s_device_count = 0;
    for (int i = 0; i < (int)s_plant_count; i++) {
        _insert_runtime_device(s_plant_devices[i], true, online_default);
    }
}

static bool _device_identity_matches(const Rs485Device& expected, const Rs485Device& actual) {
    if (expected.address != actual.address) return false;
    if (expected.type != actual.type) return false;

    const bool expected_has_sn = _serial_is_meaningful(expected.sn);
    const bool actual_has_sn = _serial_is_meaningful(actual.sn);
    if (expected_has_sn && actual_has_sn) {
        return strncmp(expected.sn, actual.sn, sizeof(expected.sn)) == 0;
    }
    return true;
}

static int _find_runtime_preferred_by_address(uint8_t address) {
    int fallback = -1;
    for (int i = 0; i < (int)s_device_count; i++) {
        if (s_devices[i].address != address) continue;
        if (s_devices[i].in_plant) return i;
        if (fallback < 0) fallback = i;
    }
    return fallback;
}

static int _find_online_runtime_plant_by_address(uint8_t address) {
    for (int i = 0; i < (int)s_device_count; i++) {
        if (!s_devices[i].in_plant) continue;
        if (!s_devices[i].online) continue;
        if (s_devices[i].address == address) return i;
    }
    return -1;
}

static void _mark_plant_comm_failed(const Rs485Device& plant_dev) {
    const int idx = _find_runtime_index_for_plant_identity(plant_dev);
    if (idx < 0) return;

    Rs485Device& dev = s_devices[idx];
    if (dev.comm_failures < 255) dev.comm_failures++;
    if (dev.comm_failures >= k_refresh_offline_threshold) {
        dev.online = false;
    }
}

static void _parse_sensor(const String& resp, Rs485Device& dev) {
    int comma_cnt = 0;
    for (int k = 0; k < (int)resp.length(); k++) {
        if (resp.charAt(k) == ',') comma_cnt++;
    }

    int v[8] = {};
    int pos = 0;
    const int n = (comma_cnt < 8) ? comma_cnt : 8;
    for (int j = 0; j < n; j++) {
        pos = resp.indexOf(',', pos + 1);
        v[j] = pos;
    }

    if (comma_cnt < 6) return;

    const String t_str = resp.substring(v[0] + 1, v[1]);
    const String h_str = resp.substring(v[1] + 1, v[2]);
    const String p_str = resp.substring(v[2] + 1, v[3]);
    const String sic_str = resp.substring(v[3] + 1, v[4]);
    const String grp_str = resp.substring(v[4] + 1, v[5]);

    dev.t = t_str.toFloat();
    dev.h = h_str.toFloat();
    dev.p = p_str.toFloat();
    dev.sensor_active = _field_to_bool(sic_str);
    uint8_t parsed_group = 0;
    if (_field_to_uint8_checked(grp_str, parsed_group)) dev.group = parsed_group;

    if (comma_cnt >= 8) {
        const String mode_str = resp.substring(v[5] + 1, v[6]);
        dev.sensor_mode = (uint8_t)mode_str.toInt();

        String sn = resp.substring(v[6] + 1, v[7]);
        sn.trim();
        sn.toCharArray(dev.sn, sizeof(dev.sn));
        String ver = resp.substring(v[7] + 1);
        ver.trim();
        ver.toCharArray(dev.version, sizeof(dev.version));
    } else if (comma_cnt >= 7) {
        String sn = resp.substring(v[5] + 1, v[6]);
        sn.trim();
        sn.toCharArray(dev.sn, sizeof(dev.sn));
        String ver = resp.substring(v[6] + 1);
        ver.trim();
        ver.toCharArray(dev.version, sizeof(dev.version));
    } else {
        String sn = resp.substring(v[5] + 1);
        sn.trim();
        sn.toCharArray(dev.sn, sizeof(dev.sn));
    }

    dev.sensor_profile = _sensor_profile_from_data(dev);
    dev.data_valid = (dev.sensor_profile != Rs485SensorProfile::UNKNOWN) && (dev.group > 0);
}

static void _parse_0v10v(const String& resp, Rs485Device& dev) {
    int v[10] = {};
    int found = 0;
    for (int k = 0; k < (int)resp.length() && found < 10; k++) {
        if (resp.charAt(k) == ',') v[found++] = k;
    }
    if (found < 7) {
        dev.sensor_profile = Rs485SensorProfile::AIR_010;
        dev.data_valid = false;
        return;
    }

    const String speed_str = resp.substring(v[1] + 1, v[2]);
    const String enabled_str = resp.substring(v[2] + 1, v[3]);
    const String fb_str = resp.substring(v[3] + 1, v[4]);
    const String grp_str = resp.substring(v[4] + 1, v[5]);
    String sn = resp.substring(v[5] + 1, v[6]);
    String ver;
    String state;
    bool feedback_fault_latched = false;
    if (found >= 9) {
        ver = resp.substring(v[6] + 1, v[7]);
        feedback_fault_latched = _field_to_bool(resp.substring(v[7] + 1, v[8]));
        state = resp.substring(v[8] + 1);
    } else if (found >= 8) {
        ver = resp.substring(v[6] + 1, v[7]);
        feedback_fault_latched = _field_to_bool(resp.substring(v[7] + 1));
    } else {
        ver = resp.substring(v[6] + 1);
    }
    sn.trim();
    ver.trim();
    state.trim();

    dev.h = speed_str.toFloat();
    dev.sensor_active = _field_to_bool(enabled_str);
    dev.sensor_feedback_ok = _field_to_bool(fb_str);
    dev.sensor_feedback_fault_latched = feedback_fault_latched;
    dev.p = dev.sensor_feedback_ok ? 1.0f : 0.0f;

    uint8_t parsed_group = 0;
    if (_field_to_uint8_checked(grp_str, parsed_group)) dev.group = parsed_group;
    sn.toCharArray(dev.sn, sizeof(dev.sn));
    ver.toCharArray(dev.version, sizeof(dev.version));
    if (state.length() == 0) {
        state = dev.sensor_active ? (dev.sensor_feedback_ok ? "RUNNING" : "WAIT_FB") : "OFF";
    }
    state.toCharArray(dev.sensor_state, sizeof(dev.sensor_state));

    dev.sensor_profile = Rs485SensorProfile::AIR_010;
    dev.data_valid = (_serial_type_code(dev.sn) == "05" || _serial_type_code(dev.sn) == "06") &&
                     (dev.group == 1 || dev.group == 2);
}

static void _parse_relay(const String& resp, Rs485Device& dev) {
    int v[16] = {};
    int found = 0;
    for (int k = 0; k < (int)resp.length() && found < 16; k++) {
        if (resp.charAt(k) == ',') v[found++] = k;
    }
    if (found >= 3) {
        const String mode = resp.substring(v[1] + 1, v[2]);
        dev.relay_mode = (uint8_t)mode.toInt();
    }
    if (found >= 4) {
        const String on = resp.substring(v[2] + 1, v[3]);
        dev.relay_on = _field_to_bool(on);
    }
    if (found >= 5) {
        const String safety = resp.substring(v[3] + 1, v[4]);
        dev.relay_safety_closed = _field_to_bool(safety);
    }
    if (found >= 6) {
        const String fb = resp.substring(v[4] + 1, v[5]);
        dev.relay_feedback_ok = _field_to_bool(fb);
    }
    if (found >= 9) {
        const String grp = resp.substring(v[7] + 1, v[8]);
        dev.group = (uint8_t)grp.toInt();
    }
    if (found >= 10) {
        String sn = resp.substring(v[8] + 1, v[9]);
        sn.trim();
        sn.toCharArray(dev.sn, sizeof(dev.sn));
    }
    if (found >= 11) {
        String st;
        if (found >= 12) st = resp.substring(v[9] + 1, v[10]);
        else st = resp.substring(v[9] + 1);
        st.trim();
        st.toCharArray(dev.relay_state, sizeof(dev.relay_state));
    }
    if (found >= 11) {
        String ver;
        if (found >= 12) ver = resp.substring(v[10] + 1, v[11]);
        else ver = resp.substring(v[10] + 1);
        ver.trim();
        ver.toCharArray(dev.version, sizeof(dev.version));
    }
    if (found >= 14) {
        const String life_expired = resp.substring(v[12] + 1, v[13]);
        dev.relay_life_expired = _field_to_bool(life_expired);
        String feedback_fault = resp.substring(v[13] + 1);
        feedback_fault.trim();
        dev.relay_feedback_fault_latched = _field_to_bool(feedback_fault);
    }
}

static void _fill_device_from_response(int addr, const String& resp, Rs485Device& dev) {
    memset(&dev, 0, sizeof(dev));
    dev.address = (uint8_t)addr;
    strncpy(dev.sn, "N/A", sizeof(dev.sn) - 1);
    strncpy(dev.version, "N/A", sizeof(dev.version) - 1);
    dev.sn[sizeof(dev.sn) - 1] = '\0';
    dev.version[sizeof(dev.version) - 1] = '\0';
    strncpy(dev.relay_state, "N/A", sizeof(dev.relay_state) - 1);
    dev.relay_state[sizeof(dev.relay_state) - 1] = '\0';
    strncpy(dev.sensor_state, "N/A", sizeof(dev.sensor_state) - 1);
    dev.sensor_state[sizeof(dev.sensor_state) - 1] = '\0';
    dev.sensor_profile = Rs485SensorProfile::UNKNOWN;
    dev.sensor_mode = 0;
    dev.data_valid = false;
    dev.online = true;
    dev.in_plant = false;
    dev.comm_failures = 0;

    if (resp.startsWith("OK,RELAY,")) {
        dev.type = Rs485DevType::RELAY;
        _parse_relay(resp, dev);
        dev.data_valid = true;
        if (dev.group == 0) dev.data_valid = false;
        return;
    }
    if (resp.startsWith("OK,MOT5,")) {
        dev.type = Rs485DevType::SENSOR;
        _parse_0v10v(resp, dev);
        return;
    }
    if (resp.startsWith("OK,")) {
        dev.type = Rs485DevType::SENSOR;
        _parse_sensor(resp, dev);
        return;
    }

    dev.type = Rs485DevType::UNKNOWN;
    strncpy(dev.sn, "?", sizeof(dev.sn) - 1);
    strncpy(dev.version, "?", sizeof(dev.version) - 1);
    dev.sn[sizeof(dev.sn) - 1] = '\0';
    dev.version[sizeof(dev.version) - 1] = '\0';
}

static bool _exchange_raw_locked(const String& tx, uint32_t timeout_ms, String& raw_response, bool log_txrx) {
    const bool trace = log_txrx || s_monitor_enabled;
    raw_response = "";
    while (Serial1.available()) Serial1.read();

    _tx_mode();
    Serial1.print(tx);
    _rx_mode();

    if (trace) {
        Serial.printf("[RS485-NET] TX -> %s\n", tx.c_str());
    }

    const unsigned long t0 = millis();
    char rx_buf[k_frame_buffer_size];
    size_t rx_len = 0;
    bool frame_done = false;

    while (millis() - t0 < timeout_ms) {
        while (Serial1.available()) {
            const char ch = (char)Serial1.read();
            if (rx_len + 1 < sizeof(rx_buf)) {
                rx_buf[rx_len++] = ch;
            }
            if (ch == '!') {
                frame_done = true;
                break;
            }
        }

        if (frame_done) {
            break;
        }
        vTaskDelay(1);
    }

    if (rx_len > 0) {
        if (rx_len > 0 && rx_buf[rx_len - 1] == '!') {
            rx_len--;
        }
        rx_buf[rx_len] = '\0';
        raw_response = rx_buf;
        raw_response.trim();

        int body_pos = raw_response.indexOf("OK,");
        if (body_pos < 0) body_pos = raw_response.indexOf("ERR,");
        if (body_pos > 0) {
            raw_response = raw_response.substring(body_pos);
            raw_response.trim();
        }
        if (trace) {
            Serial.printf("[RS485-NET] RX <- %s!\n", raw_response.c_str());
        }
        return raw_response.length() > 0;
    }

    if (trace) {
        Serial.printf("[RS485-NET] RX <- <timeout>  (attesa %lu ms)\n", (unsigned long)timeout_ms);
    }
    return false;
}

static bool _query_raw_locked(int addr, uint32_t timeout_ms, String& raw_response, bool log_txrx) {
    const String tx = "?" + String(addr) + "!";
    if (!_exchange_raw_locked(tx, timeout_ms, raw_response, log_txrx)) return false;
    return raw_response.startsWith("OK");
}

static void _merge_response_for_address(uint8_t address, const String& resp) {
    Rs485Device parsed;
    _fill_device_from_response((int)address, resp, parsed);
    const int plant_idx = _find_plant_index_by_identity(parsed);
    if (plant_idx >= 0) {
        _upsert_runtime_plant_device(parsed, true);
        return;
    }
    _upsert_runtime_extra_device(parsed);
}

static void _scan_task(void* /*arg*/) {
    s_scan_progress = 0;
    _set_runtime_from_plant(false);
    _display_activity_guard_acquire();

    Serial.println("[RS485-NET] ---- Avvio scansione 1-200 ----");
    Serial.printf("[RS485-NET] DIR=%d  TX=%d  RX=%d\n", RS485_NET_DIR, RS485_NET_TX, RS485_NET_RX);

    for (int addr = 1; addr <= 200; addr++) {
        String resp;
        bool got = false;

        const bool can_use_bus =
            (s_bus_mutex == nullptr) ||
            (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE);

        if (can_use_bus) {
            got = _query_raw_locked(addr, k_scan_response_timeout_ms, resp, false);
            if (s_bus_mutex != nullptr) xSemaphoreGive(s_bus_mutex);
        } else if (addr % 10 == 0) {
            Serial.printf("[RS485-NET] IP %d-%d: bus occupato\n", addr - 9, addr);
        }

        if (got) {
            _merge_response_for_address((uint8_t)addr, resp);
        } else if (addr % 10 == 0) {
            Serial.printf("[RS485-NET] IP %d-%d: nessuna risposta\n", addr - 9, addr);
        }

        s_scan_progress = addr;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    Serial.printf("[RS485-NET] ---- Scansione terminata. Runtime=%d  Impianto=%d ----\n",
                  (int)s_device_count, (int)s_plant_count);

    s_scan_state = Rs485ScanState::DONE;
    s_scan_task = nullptr;
    _display_activity_guard_release();
    vTaskDelete(nullptr);
}

static void _run_boot_probe_once() {
    int plant_count = (int)s_plant_count;
    if (plant_count > RS485_NET_MAX_DEVICES) plant_count = RS485_NET_MAX_DEVICES;

    _set_runtime_from_plant(false);

    if (plant_count <= 0) {
        s_boot_probe_state = Rs485BootProbeState::DONE;
        return;
    }

    Serial.printf("[RS485-NET] Boot probe impianto: %d periferiche\n", plant_count);

    for (int i = 0; i < plant_count; i++) {
        if (s_scan_state == Rs485ScanState::RUNNING) break;
        const Rs485Device plant_dev = s_plant_devices[i];
        const int addr = (int)plant_dev.address;
        if (addr < 1 || addr > 200) continue;

        String resp;
        bool got = false;
        const bool can_use_bus =
            (s_bus_mutex == nullptr) ||
            (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE);
        if (can_use_bus) {
            for (uint8_t attempt = 0; attempt < k_boot_probe_retries; attempt++) {
                got = _query_raw_locked(addr, k_boot_probe_timeout_ms, resp, false);
                if (got) break;
                if (attempt + 1 < k_boot_probe_retries) {
                    vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
                }
            }
            if (s_bus_mutex != nullptr) xSemaphoreGive(s_bus_mutex);
        }

        if (got) {
            Rs485Device parsed;
            _fill_device_from_response(addr, resp, parsed);
            if (_device_identity_matches(plant_dev, parsed)) {
                _upsert_runtime_plant_device(parsed, true);
            } else {
                _upsert_runtime_extra_device(parsed);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(4));
    }

    int online_count = 0;
    for (int i = 0; i < (int)s_device_count; i++) {
        if (s_devices[i].in_plant && s_devices[i].online) online_count++;
    }
    Serial.printf("[RS485-NET] Boot probe done: online=%d/%d\n", online_count, plant_count);
    s_boot_probe_state = Rs485BootProbeState::DONE;
}

static void _refresh_task(void* /*arg*/) {
    while (true) {
        if (s_scan_state == Rs485ScanState::RUNNING) {
            vTaskDelay(pdMS_TO_TICKS(400));
            continue;
        }

        if (s_boot_probe_state == Rs485BootProbeState::RUNNING) {
            _run_boot_probe_once();
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int plant_count = (int)s_plant_count;
        if (plant_count <= 0) {
            vTaskDelay(pdMS_TO_TICKS(k_refresh_cycle_pause_ms));
            continue;
        }
        if (plant_count > RS485_NET_MAX_DEVICES) plant_count = RS485_NET_MAX_DEVICES;

        for (int i = 0; i < plant_count; i++) {
            if (s_scan_state == Rs485ScanState::RUNNING ||
                s_boot_probe_state == Rs485BootProbeState::RUNNING) {
                break;
            }

            const Rs485Device plant_dev = s_plant_devices[i];
            const uint8_t address = plant_dev.address;
            if (address < 1 || address > 200) continue;

            if (s_bus_mutex == nullptr) break;
            if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(300)) != pdTRUE) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }

            String resp;
            const bool ok = _query_with_retries_locked(
                address, k_manual_response_timeout_ms, k_manual_retries, resp);
            xSemaphoreGive(s_bus_mutex);

            if (ok) {
                Rs485Device parsed;
                _fill_device_from_response((int)address, resp, parsed);
                if (_device_identity_matches(plant_dev, parsed)) {
                    _upsert_runtime_plant_device(parsed, true);
                } else {
                    _mark_plant_comm_failed(plant_dev);
                    _upsert_runtime_extra_device(parsed);
                }
            } else {
                _mark_plant_comm_failed(plant_dev);
            }
            vTaskDelay(pdMS_TO_TICKS(12));
        }

        vTaskDelay(pdMS_TO_TICKS(k_refresh_cycle_pause_ms));
    }
}

void rs485_network_init() {
    if (RS485_NET_DIR != PIN_NOT_ASSIGNED) {
        pinMode(RS485_NET_DIR, OUTPUT);
        digitalWrite(RS485_NET_DIR, LOW);
    }

    Serial1.setRxBufferSize(512);
    Serial1.begin(115200, SERIAL_8N1, RS485_NET_RX, RS485_NET_TX);
    Serial1.setTimeout(30);

    if (s_bus_mutex == nullptr) {
        s_bus_mutex = xSemaphoreCreateMutex();
    }

    _load_plant_from_nvs();
    _set_runtime_from_plant(true);

    if (s_refresh_task == nullptr) {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            _refresh_task, "rs485_refresh", 4096, nullptr, 1, &s_refresh_task, 0);
        if (rc != pdPASS) {
            Serial.println("[RS485-NET] WARN: impossibile avviare task refresh periodico.");
            s_refresh_task = nullptr;
        }
    }
}

bool rs485_network_ping(uint8_t address, String& raw_response) {
    raw_response = "";

    if (address < 1 || address > 200) {
        raw_response = "ERR,ADDR";
        return false;
    }
    if (s_scan_state == Rs485ScanState::RUNNING) {
        raw_response = "BUSY_SCAN";
        return false;
    }
    if (s_bus_mutex == nullptr) {
        raw_response = "ERR,INIT";
        return false;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        raw_response = "BUSY";
        return false;
    }

    bool ok = false;
    String last_raw = "";
    for (uint8_t attempt = 0; attempt < k_manual_retries; attempt++) {
        String rx;
        ok = _query_raw_locked((int)address, k_manual_response_timeout_ms, rx, false);
        if (ok) {
            raw_response = rx;
            break;
        }
        if (rx.length() > 0) last_raw = rx;
        if (attempt + 1 < k_manual_retries) {
            vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
        }
    }

    if (!ok) raw_response = last_raw;
    xSemaphoreGive(s_bus_mutex);
    return ok;
}

bool rs485_network_query_device(uint8_t address, Rs485Device& out, String& raw_response) {
    memset(&out, 0, sizeof(out));
    out.address = address;

    const bool ok = rs485_network_ping(address, raw_response);
    if (raw_response.startsWith("OK")) {
        _fill_device_from_response((int)address, raw_response, out);
    }
    return ok;
}

bool rs485_network_relay_command(uint8_t address, const char* action, String& raw_response) {
    raw_response = "";
    if (address < 1 || address > 200) {
        raw_response = "ERR,ADDR";
        return false;
    }
    if (!action || !action[0]) {
        raw_response = "ERR,ACTION";
        return false;
    }
    if (s_scan_state == Rs485ScanState::RUNNING) {
        raw_response = "BUSY_SCAN";
        return false;
    }
    if (_find_online_runtime_plant_by_address(address) < 0) {
        raw_response = "ERR,UNMANAGED";
        return false;
    }
    if (s_bus_mutex == nullptr) {
        raw_response = "ERR,INIT";
        return false;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        raw_response = "BUSY";
        return false;
    }

    String act(action);
    act.trim();
    act.toUpperCase();
    if (act != "ON" && act != "OFF" && act != "TOGGLE") {
        xSemaphoreGive(s_bus_mutex);
        raw_response = "ERR,ACTION";
        return false;
    }

    const String tx = "RLY," + String((int)address) + "," + act + "!";
    bool ok = false;
    String last_raw = "";
    for (uint8_t attempt = 0; attempt < k_relay_cmd_retries; attempt++) {
        String rx;
        const bool got = _exchange_raw_locked(tx, k_manual_response_timeout_ms, rx, false);
        if (got) {
            last_raw = rx;
            if (rx.startsWith("OK,RELAY,CMD,")) {
                ok = true;
                break;
            }
        }
        if (attempt + 1 < k_relay_cmd_retries) {
            vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
        }
    }

    raw_response = last_raw;
    xSemaphoreGive(s_bus_mutex);
    return ok;
}

bool rs485_network_motor_speed_command(uint8_t address, uint8_t percent, String& raw_response) {
    raw_response = "";
    if (address < 1 || address > 200) {
        raw_response = "ERR,ADDR";
        return false;
    }
    if (percent > 100) {
        raw_response = "ERR,PCT";
        return false;
    }
    if (s_scan_state == Rs485ScanState::RUNNING) {
        raw_response = "BUSY_SCAN";
        return false;
    }

    const int runtime_idx = _find_online_runtime_plant_by_address(address);
    if (runtime_idx < 0) {
        raw_response = "ERR,UNMANAGED";
        return false;
    }
    if (s_devices[runtime_idx].type != Rs485DevType::SENSOR ||
        s_devices[runtime_idx].sensor_profile != Rs485SensorProfile::AIR_010 ||
        !s_devices[runtime_idx].data_valid) {
        raw_response = "ERR,TYPE";
        return false;
    }
    if (s_bus_mutex == nullptr) {
        raw_response = "ERR,INIT";
        return false;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        raw_response = "BUSY";
        return false;
    }

    const String tx = "SPD" + String((int)address) + ":" + String((int)percent) + "!";
    bool ok = false;
    String last_raw = "";
    for (uint8_t attempt = 0; attempt < k_relay_cmd_retries; attempt++) {
        String rx;
        const bool got = _exchange_raw_locked(tx, k_manual_response_timeout_ms, rx, false);
        if (got) {
            last_raw = rx;
            if (rx.startsWith("OK,SPD")) {
                ok = true;
                break;
            }
        }
        if (attempt + 1 < k_relay_cmd_retries) {
            vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
        }
    }

    raw_response = last_raw;
    if (ok) {
        s_devices[runtime_idx].h = (float)percent;
    }
    xSemaphoreGive(s_bus_mutex);
    return ok;
}

bool rs485_network_motor_enable_command(uint8_t address, bool enable, String& raw_response) {
    raw_response = "";
    if (address < 1 || address > 200) {
        raw_response = "ERR,ADDR";
        return false;
    }
    if (s_scan_state == Rs485ScanState::RUNNING) {
        raw_response = "BUSY_SCAN";
        return false;
    }

    const int runtime_idx = _find_online_runtime_plant_by_address(address);
    if (runtime_idx < 0) {
        raw_response = "ERR,UNMANAGED";
        return false;
    }
    if (s_devices[runtime_idx].type != Rs485DevType::SENSOR ||
        s_devices[runtime_idx].sensor_profile != Rs485SensorProfile::AIR_010 ||
        !s_devices[runtime_idx].data_valid) {
        raw_response = "ERR,TYPE";
        return false;
    }
    if (s_bus_mutex == nullptr) {
        raw_response = "ERR,INIT";
        return false;
    }
    if (xSemaphoreTake(s_bus_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        raw_response = "BUSY";
        return false;
    }

    const String tx = "ENA" + String((int)address) + ":" + String(enable ? 1 : 0) + "!";
    bool ok = false;
    String last_raw = "";
    for (uint8_t attempt = 0; attempt < k_relay_cmd_retries; attempt++) {
        String rx;
        const bool got = _exchange_raw_locked(tx, k_manual_response_timeout_ms, rx, false);
        if (got) {
            last_raw = rx;
            if (rx.startsWith("OK,ENA")) {
                ok = true;
                break;
            }
        }
        if (attempt + 1 < k_relay_cmd_retries) {
            vTaskDelay(pdMS_TO_TICKS(k_manual_retry_gap_ms));
        }
    }

    raw_response = last_raw;
    if (ok) {
        s_devices[runtime_idx].sensor_active = enable;
        if (enable) {
            s_devices[runtime_idx].sensor_feedback_fault_latched = false;
            strncpy(s_devices[runtime_idx].sensor_state, "WAIT_FB", sizeof(s_devices[runtime_idx].sensor_state) - 1);
        } else {
            s_devices[runtime_idx].sensor_feedback_ok = false;
            s_devices[runtime_idx].sensor_feedback_fault_latched = false;
            strncpy(s_devices[runtime_idx].sensor_state, "OFF", sizeof(s_devices[runtime_idx].sensor_state) - 1);
        }
        s_devices[runtime_idx].sensor_state[sizeof(s_devices[runtime_idx].sensor_state) - 1] = '\0';
    }
    xSemaphoreGive(s_bus_mutex);
    return ok;
}

void rs485_network_scan_start() {
    if (s_scan_state == Rs485ScanState::RUNNING) return;
    s_scan_state = Rs485ScanState::RUNNING;
    const BaseType_t rc = xTaskCreatePinnedToCore(_scan_task, "rs485_scan", 4096, nullptr, 1, &s_scan_task, 0);
    if (rc != pdPASS) {
        Serial.println("[RS485-NET] WARN: impossibile avviare task scansione.");
        s_scan_task = nullptr;
        s_scan_state = Rs485ScanState::IDLE;
    }
}

Rs485ScanState rs485_network_scan_state() { return s_scan_state; }
int rs485_network_scan_progress() { return s_scan_progress; }
int rs485_network_device_count() { return (int)s_device_count; }

bool rs485_network_get_device(int idx, Rs485Device& out) {
    if (idx < 0 || idx >= (int)s_device_count) return false;
    out = s_devices[idx];
    return true;
}

bool rs485_network_get_device_by_address(uint8_t address, Rs485Device& out) {
    if (address < 1 || address > 200) return false;
    const int idx = _find_runtime_preferred_by_address(address);
    if (idx < 0) return false;
    out = s_devices[idx];
    return true;
}

int rs485_network_cached_device_count() {
    return (int)s_plant_count;
}

int rs485_network_plant_device_count() {
    return (int)s_plant_count;
}

bool rs485_network_has_saved_plant() {
    return s_plant_count > 0;
}

bool rs485_network_save_current_as_plant() {
    if (s_scan_state == Rs485ScanState::RUNNING ||
        s_boot_probe_state == Rs485BootProbeState::RUNNING) {
        return false;
    }

    int new_count = 0;
    for (int i = 0; i < (int)s_device_count; i++) {
        if (!s_devices[i].online) continue;
        if (new_count >= RS485_NET_MAX_DEVICES) break;
        s_plant_devices[new_count] = s_devices[i];
        s_plant_devices[new_count].in_plant = true;
        s_plant_devices[new_count].comm_failures = 0;
        new_count++;
    }

    s_plant_count = new_count;
    _save_plant_to_nvs();
    _set_runtime_from_plant(true);
    return true;
}

bool rs485_network_remove_device_from_plant(uint8_t address, const char* serial_number) {
    if (s_scan_state == Rs485ScanState::RUNNING ||
        s_boot_probe_state == Rs485BootProbeState::RUNNING) {
        return false;
    }

    const int plant_idx = _find_plant_index_by_address_and_serial(address, serial_number);
    if (plant_idx < 0) return false;

    const Rs485Device removed = s_plant_devices[plant_idx];
    for (int i = plant_idx; i + 1 < (int)s_plant_count; i++) {
        s_plant_devices[i] = s_plant_devices[i + 1];
    }
    if (s_plant_count > 0) {
        const int next_count = (int)s_plant_count - 1;
        s_plant_count = next_count;
    }

    _remove_runtime_plant_identity(removed);
    _save_plant_to_nvs();
    return true;
}

void rs485_network_boot_probe_start() {
    if (s_boot_probe_state == Rs485BootProbeState::RUNNING) return;
    s_boot_probe_state = Rs485BootProbeState::RUNNING;
}

Rs485BootProbeState rs485_network_boot_probe_state() {
    return s_boot_probe_state;
}

void rs485_network_set_monitor_enabled(bool enabled) {
    s_monitor_enabled = enabled;
}

bool rs485_network_is_monitor_enabled() {
    return s_monitor_enabled;
}
