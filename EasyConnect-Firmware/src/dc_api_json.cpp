#include "dc_api_json.h"

#include "dc_controller.h"
#include "dc_data_model.h"
#include "dc_settings.h"

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

static String _u64_to_string(uint64_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    return String(buf);
}

static String _json_escape(const char* value) {
    const char* src = value ? value : "";
    String out;
    out.reserve(strlen(src) + 8);
    for (size_t i = 0; src[i] != '\0'; i++) {
        const char c = src[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((uint8_t)c < 0x20) out += ' ';
                else out += c;
                break;
        }
    }
    return out;
}

static String _json_escape(const String& value) {
    return _json_escape(value.c_str());
}

static const char* _device_type_text(DcDeviceType type) {
    switch (type) {
        case DC_DEV_SENSOR:    return "SENSOR";
        case DC_DEV_RELAY:     return "RELAY";
        case DC_DEV_MOTOR_010: return "MOTOR_010V";
        default:               return "UNKNOWN";
    }
}

static const char* _severity_text(DcNotifSeverity severity) {
    switch (severity) {
        case DC_NOTIF_WARNING: return "warning";
        case DC_NOTIF_ALARM:   return "alarm";
        case DC_NOTIF_INFO:
        default:               return "info";
    }
}

static String _json_bool(bool value) {
    return value ? "true" : "false";
}

static String _json_float_or_null(float value, bool valid, uint8_t decimals = 1) {
    if (!valid || isnan(value) || isinf(value)) return "null";
    return String(value, (unsigned int)decimals);
}

static bool _find_value_start(const String& src, const char* key, int& pos) {
    const String tag = "\"" + String(key) + "\"";
    const int key_pos = src.indexOf(tag);
    if (key_pos < 0) return false;

    pos = key_pos + tag.length();
    while (pos < src.length() && isspace((unsigned char)src[pos])) pos++;
    if (pos >= src.length() || src[pos] != ':') return false;
    pos++;
    while (pos < src.length() && isspace((unsigned char)src[pos])) pos++;
    return pos < src.length();
}

static bool _json_has_key(const String& src, const char* key) {
    int pos = 0;
    return _find_value_start(src, key, pos);
}

static bool _json_get_string(const String& src, const char* key, String& out) {
    int pos = 0;
    if (!_find_value_start(src, key, pos)) return false;
    if (pos >= src.length() || src[pos] != '"') return false;

    pos++;
    out = "";
    bool escape = false;
    for (int i = pos; i < src.length(); i++) {
        const char c = src[i];
        if (escape) {
            switch (c) {
                case '"':
                case '\\':
                case '/':
                    out += c;
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                default:
                    out += c;
                    break;
            }
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') return true;
        out += c;
    }
    return false;
}

static bool _json_get_int(const String& src, const char* key, int& out) {
    int pos = 0;
    if (!_find_value_start(src, key, pos)) return false;

    int end = pos;
    if (src[end] == '-') end++;
    while (end < src.length() && isdigit((unsigned char)src[end])) end++;
    if (end <= pos || (src[pos] == '-' && end == (pos + 1))) return false;

    out = src.substring(pos, end).toInt();
    return true;
}

static bool _json_get_bool(const String& src, const char* key, bool& out) {
    int pos = 0;
    if (!_find_value_start(src, key, pos)) return false;

    if (src.startsWith("true", pos)) {
        out = true;
        return true;
    }
    if (src.startsWith("false", pos)) {
        out = false;
        return true;
    }
    return false;
}

static bool _json_get_address(const String& src, int& address) {
    if (_json_get_int(src, "device_address", address)) return true;
    return _json_get_int(src, "address", address);
}

static bool _command_allowed(const String& command, DcApiCommandAccess access) {
    if (command == "relay_set" ||
        command == "motor_enable" ||
        command == "motor_speed" ||
        command == "settings_set") {
        return true;
    }

    if (access != DC_API_ACCESS_FACTORY) return false;

    return command == "rs485_scan" ||
           command == "ota_check" ||
           command == "ota_start" ||
           command == "factory_reset" ||
           command == "calibration_set" ||
           command == "device_group_set";
}

static bool _parse_temp_unit(const String& src, DcTempUnit& out) {
    int raw = 0;
    if (_json_get_int(src, "temp_unit", raw)) {
        out = (raw == (int)DC_TEMP_F) ? DC_TEMP_F : DC_TEMP_C;
        return true;
    }

    String text;
    if (_json_get_string(src, "temp_unit", text)) {
        text.trim();
        text.toUpperCase();
        out = (text == "F" || text == "FAHRENHEIT") ? DC_TEMP_F : DC_TEMP_C;
        return true;
    }
    return false;
}

static bool _handle_settings_set(const String& src) {
    bool handled = false;

    String plant_name;
    if (_json_get_string(src, "plant_name", plant_name)) {
        dc_settings_plant_name_set(plant_name.c_str());
        handled = true;
    }

    int int_value = 0;
    if (_json_get_int(src, "brightness_pct", int_value)) {
        dc_settings_brightness_set(int_value);
        dc_settings_brightness_apply_hw();
        handled = true;
    }
    if (_json_get_int(src, "screensaver_min", int_value)) {
        dc_settings_screensaver_set(int_value);
        handled = true;
    }

    DcTempUnit unit = DC_TEMP_C;
    if (_parse_temp_unit(src, unit)) {
        dc_settings_temp_unit_set(unit);
        handled = true;
    }

    if (_json_get_int(src, "ui_theme_id", int_value)) {
        dc_settings_theme_set((uint8_t)constrain(int_value, 0, 255));
        handled = true;
    }
    if (_json_get_int(src, "vent_min_pct", int_value)) {
        dc_settings_vent_min_set(int_value);
        handled = true;
    }
    if (_json_get_int(src, "vent_max_pct", int_value)) {
        dc_settings_vent_max_set(int_value);
        handled = true;
    }
    if (_json_get_int(src, "vent_steps", int_value)) {
        dc_settings_vent_steps_set(int_value);
        handled = true;
    }

    bool bool_value = false;
    if (_json_get_bool(src, "intake_bar_enabled", bool_value)) {
        dc_settings_intake_bar_set(bool_value);
        handled = true;
    }
    if (_json_get_int(src, "intake_diff_pct", int_value)) {
        dc_settings_intake_diff_set(int_value);
        handled = true;
    }
    if (_json_get_bool(src, "safeguard_enabled", bool_value)) {
        dc_settings_safeguard_enabled_set(bool_value);
        handled = true;
    }
    if (_json_get_int(src, "safeguard_temp_max_c", int_value)) {
        dc_settings_safeguard_temp_max_set(int_value);
        handled = true;
    }
    if (_json_get_int(src, "safeguard_hum_max_rh", int_value)) {
        dc_settings_safeguard_hum_max_set(int_value);
        handled = true;
    }

    const bool has_wifi_enabled = _json_has_key(src, "wifi_enabled");
    const bool has_wifi_ssid = _json_has_key(src, "wifi_ssid");
    if (has_wifi_enabled || has_wifi_ssid) {
        bool wifi_enabled = g_dc_model.settings.wifi_enabled;
        String wifi_ssid = g_dc_model.settings.wifi_ssid;
        if (has_wifi_enabled) (void)_json_get_bool(src, "wifi_enabled", wifi_enabled);
        if (has_wifi_ssid) (void)_json_get_string(src, "wifi_ssid", wifi_ssid);
        dc_settings_wifi_set(wifi_enabled, wifi_ssid.c_str(), nullptr);
        handled = true;
    }

    const bool has_api_customer_enabled = _json_has_key(src, "api_customer_enabled");
    const bool has_api_customer_url = _json_has_key(src, "api_customer_url");
    if (has_api_customer_enabled || has_api_customer_url) {
        bool api_enabled = g_dc_model.settings.api_customer_enabled;
        String api_url = g_dc_model.settings.api_customer_url;
        if (has_api_customer_enabled) (void)_json_get_bool(src, "api_customer_enabled", api_enabled);
        if (has_api_customer_url) (void)_json_get_string(src, "api_customer_url", api_url);
        dc_settings_api_customer_set(api_enabled, api_url.c_str(), nullptr);
        handled = true;
    }

    if (_json_has_key(src, "ota_auto_enabled") || _json_has_key(src, "ota_channel")) {
        Serial.println("[DC-API] settings_set: campi OTA disponibili dal Task 3.3.");
    }

    return handled;
}

bool dc_api_build_payload(char* buf, size_t len) {
    if (!buf || len == 0) return false;
    buf[0] = '\0';

    String json;
    json.reserve(1536 + (g_dc_model.network.device_count * 224) +
                 (g_dc_model.notifications.count * 112));

    json += "{";
    json += "\"api_version\":\"1.0\",";
    json += "\"device\":{";
    json += "\"serial\":\"" + _json_escape(g_dc_model.device_serial) + "\",";
    json += "\"fw_version\":\"" + _json_escape(g_dc_model.fw_version) + "\",";
    json += "\"plant_name\":\"" + _json_escape(g_dc_model.settings.plant_name) + "\",";
    json += "\"uptime_s\":" + String(millis() / 1000UL);
    json += "},";
    json += "\"timestamp_ms\":" + _u64_to_string((uint64_t)millis()) + ",";
    json += "\"environment\":{";
    json += "\"temp_c\":" + _json_float_or_null(g_dc_model.environment.temp_c,
                                                  g_dc_model.environment.valid, 1) + ",";
    json += "\"hum_rh\":" + _json_float_or_null(g_dc_model.environment.hum_rh,
                                                 g_dc_model.environment.valid, 1) + ",";
    json += "\"valid\":" + _json_bool(g_dc_model.environment.valid);
    json += "},";
    json += "\"wifi\":{";
    json += "\"connected\":" + _json_bool(g_dc_model.wifi.state == DcWifiState::DC_WIFI_CONNECTED) + ",";
    json += "\"rssi\":" + String(g_dc_model.wifi.rssi) + ",";
    json += "\"ssid\":\"" + _json_escape(g_dc_model.wifi.ssid) + "\"";
    json += "},";
    json += "\"devices\":[";

    bool first = true;
    for (int i = 0; i < g_dc_model.network.device_count && i < DC_MAX_DEVICES; i++) {
        const DcDeviceSnapshot& dev = g_dc_model.network.devices[i];
        if (!dev.valid) continue;

        if (!first) json += ",";
        first = false;

        json += "{";
        json += "\"address\":" + String(dev.address) + ",";
        json += "\"group\":" + String(dev.group) + ",";
        json += "\"type\":\"" + String(_device_type_text(dev.type)) + "\",";
        json += "\"online\":" + _json_bool(dev.online);

        switch (dev.type) {
            case DC_DEV_MOTOR_010:
                json += ",\"motor_enabled\":" + _json_bool(dev.motor_enabled);
                json += ",\"speed_pct\":" + String(dev.speed_pct);
                json += ",\"temp_c\":null";
                json += ",\"hum_rh\":null";
                json += ",\"temp_valid\":false";
                json += ",\"hum_valid\":false";
                break;

            case DC_DEV_RELAY:
                json += ",\"relay_on\":" + _json_bool(dev.relay_on);
                json += ",\"relay_mode\":" + String((int)dev.relay_mode);
                json += ",\"relay_starts\":" + String(dev.relay_starts);
                json += ",\"uptime_hours\":" + String(dev.uptime_hours);
                json += ",\"safety_fault\":" + _json_bool(dev.safety_fault);
                json += ",\"feedback_fault\":" + _json_bool(dev.feedback_fault);
                break;

            case DC_DEV_SENSOR:
                json += ",\"pressure_pa\":" + _json_float_or_null(dev.pressure_pa, dev.pressure_valid, 1);
                json += ",\"temp_c\":" + _json_float_or_null(dev.temp_c, dev.temp_valid, 1);
                json += ",\"hum_rh\":" + _json_float_or_null(dev.hum_rh, dev.hum_valid, 1);
                json += ",\"pressure_valid\":" + _json_bool(dev.pressure_valid);
                json += ",\"temp_valid\":" + _json_bool(dev.temp_valid);
                json += ",\"hum_valid\":" + _json_bool(dev.hum_valid);
                break;

            case DC_DEV_UNKNOWN:
            default:
                break;
        }

        json += ",\"sn\":\"" + _json_escape(dev.sn) + "\"";
        json += ",\"fw\":\"" + _json_escape(dev.fw) + "\"";
        json += "}";
    }

    json += "],";
    json += "\"alerts\":[";
    first = true;
    for (int i = 0; i < g_dc_model.notifications.count && i < DC_MAX_NOTIF; i++) {
        const DcNotification& notif = g_dc_model.notifications.items[i];
        if (!first) json += ",";
        first = false;

        json += "{";
        json += "\"severity\":\"" + String(_severity_text(notif.severity)) + "\",";
        json += "\"device_address\":" + String(notif.device_addr) + ",";
        json += "\"code\":" + String(notif.code) + ",";
        json += "\"message\":\"" + _json_escape(notif.message) + "\",";
        json += "\"timestamp_ms\":" + _u64_to_string((uint64_t)notif.timestamp_ms);
        json += "}";
    }
    json += "],";
    json += "\"safeguard\":{";
    json += "\"active\":" + _json_bool(g_dc_model.safeguard.active) + ",";
    // Bug 9: quando inattivo i valori EMA sono 0.0 — emettere null per evitare ambiguità con il client
    if (g_dc_model.safeguard.active) {
        json += "\"duct_temp_c\":" + String(g_dc_model.safeguard.duct_temp_ema, 1) + ",";
        json += "\"duct_hum_rh\":" + String(g_dc_model.safeguard.duct_hum_ema, 1) + ",";
    } else {
        json += "\"duct_temp_c\":null,";
        json += "\"duct_hum_rh\":null,";
    }
    json += "\"boost_speed_pct\":" + String(g_dc_model.safeguard.boost_speed_pct);
    json += "}";
    json += "}";

    if ((json.length() + 1U) > len) return false;
    json.toCharArray(buf, len);
    return true;
}

bool dc_api_parse_command_with_access(const char* json, DcApiCommandAccess access) {
    if (!json || json[0] == '\0') return false;

    const String src(json);
    String api_version;
    if (_json_get_string(src, "api_version", api_version) && api_version != "1.0") {
        Serial.printf("[DC-API] api_version non supportata: %s\n", api_version.c_str());
        return false;
    }

    String command;
    if (!_json_get_string(src, "command", command) || command.length() == 0) {
        Serial.println("[DC-API] Comando JSON mancante.");
        return false;
    }

    if (!_command_allowed(command, access)) {
        Serial.printf("[DC-API] Comando non autorizzato per endpoint %s: %s\n",
                      access == DC_API_ACCESS_FACTORY ? "factory" : "customer",
                      command.c_str());
        return false;
    }

    if (command == "relay_set") {
        int address = -1;
        bool on = false;
        if (!_json_get_address(src, address) || !_json_get_bool(src, "on", on) || address <= 0) {
            Serial.println("[DC-API] relay_set non valido.");
            return false;
        }
        return dc_cmd_relay_set((uint8_t)address, on);
    }

    if (command == "motor_enable") {
        int address = -1;
        bool enable = false;
        if (!_json_get_address(src, address) || !_json_get_bool(src, "enable", enable) || address <= 0) {
            Serial.println("[DC-API] motor_enable non valido.");
            return false;
        }
        return dc_cmd_motor_enable((uint8_t)address, enable);
    }

    if (command == "motor_speed") {
        int address = -1;
        int speed_pct = -1;
        if (!_json_get_address(src, address) || !_json_get_int(src, "speed_pct", speed_pct) || address <= 0) {
            Serial.println("[DC-API] motor_speed non valido.");
            return false;
        }
        speed_pct = constrain(speed_pct, 0, 100);
        return dc_cmd_motor_speed((uint8_t)address, (uint8_t)speed_pct);
    }

    if (command == "settings_set") {
        const bool ok = _handle_settings_set(src);
        if (!ok) {
            Serial.println("[DC-API] settings_set senza campi supportati.");
        }
        return ok;
    }

    if (command == "rs485_scan") {
        dc_scan_rs485();
        return true;
    }

    if (command == "factory_reset") {
        String confirm_token;
        if (!_json_get_string(src, "confirm_token", confirm_token) || confirm_token != "CONFIRM") {
            Serial.println("[DC-API] factory_reset rifiutato: confirm_token mancante o invalido.");
            return false;
        }
        dc_factory_reset();
        return true;
    }

    if (command == "ota_check" || command == "ota_start") {
        Serial.printf("[DC-API] %s disponibile dal Task 3.3.\n", command.c_str());
        return false;
    }

    if (command == "calibration_set" || command == "device_group_set") {
        Serial.printf("[DC-API] %s non ancora disponibile nel data/config model.\n", command.c_str());
        return false;
    }

    Serial.printf("[DC-API] Comando sconosciuto: %s\n", command.c_str());
    return false;
}

bool dc_api_parse_command(const char* json) {
    return dc_api_parse_command_with_access(json, DC_API_ACCESS_FACTORY);
}
