#include "dc_controller.h"
#include "dc_data_model.h"
#include "dc_settings.h"
#include "rs485_network.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include <math.h>

DcDataModel g_dc_model = {};

// ─── Internal helpers ─────────────────────────────────────────────────────────

static DcDeviceType _map_dev_type(const Rs485Device& d) {
    if (d.type == Rs485DevType::RELAY)  return DC_DEV_RELAY;
    if (d.type == Rs485DevType::SENSOR) {
        if (d.sensor_profile == Rs485SensorProfile::AIR_010) return DC_DEV_MOTOR_010;
        return DC_DEV_SENSOR;
    }
    return DC_DEV_UNKNOWN;
}

static void _snapshot_device(DcDeviceSnapshot& dst, const Rs485Device& src) {
    dst.valid          = true;
    dst.online         = src.online;
    dst.address        = src.address;
    dst.group          = src.group;
    dst.type           = _map_dev_type(src);

    dst.relay_on       = src.relay_on;
    dst.relay_mode     = (DcRelayMode)constrain((int)src.relay_mode, 0, 5);
    dst.safety_fault   = !src.relay_safety_closed;
    dst.feedback_fault = src.relay_feedback_fault_latched ||
                         (src.type == Rs485DevType::SENSOR &&
                          src.sensor_profile == Rs485SensorProfile::AIR_010 &&
                          src.sensor_feedback_fault_latched);
    dst.relay_starts   = 0;
    dst.uptime_hours   = 0;

    const bool is_air_010 = (src.type == Rs485DevType::SENSOR &&
                              src.sensor_profile == Rs485SensorProfile::AIR_010);
    dst.motor_enabled  = src.sensor_active;
    // Per AIR_010 il parser RS485 codifica la velocità in dev.h (0–100 %)
    dst.speed_pct      = is_air_010
                         ? (uint8_t)constrain((int)(src.h + 0.5f), 0, 100)
                         : 0;

    dst.pressure_pa    = src.p;
    dst.temp_c         = src.t;
    // dev.h su AIR_010 = velocità, non umidità: non esporre come campo T/H
    dst.hum_rh         = is_air_010 ? 0.0f : src.h;
    dst.pressure_valid = (src.type == Rs485DevType::SENSOR &&
                          src.sensor_profile == Rs485SensorProfile::PRESSURE &&
                          src.data_valid);
    dst.temp_valid     = (!is_air_010 && src.type == Rs485DevType::SENSOR && src.data_valid);
    dst.hum_valid      = dst.temp_valid;

    strncpy(dst.sn, src.sn, sizeof(dst.sn) - 1);
    dst.sn[sizeof(dst.sn) - 1] = '\0';
    strncpy(dst.fw, src.version, sizeof(dst.fw) - 1);
    dst.fw[sizeof(dst.fw) - 1] = '\0';
}

static void _update_network(void) {
    const int count = rs485_network_device_count();
    g_dc_model.network.device_count  = (count < DC_MAX_DEVICES) ? count : DC_MAX_DEVICES;
    g_dc_model.network.scan_running  = (rs485_network_scan_state() == Rs485ScanState::RUNNING);
    g_dc_model.network.scan_progress = rs485_network_scan_progress();
    g_dc_model.network.last_update_ms = millis();

    for (int i = 0; i < g_dc_model.network.device_count; i++) {
        Rs485Device dev;
        if (rs485_network_get_device(i, dev)) {
            _snapshot_device(g_dc_model.network.devices[i], dev);
        }
    }
}

static void _update_wifi(void) {
    if (!g_dc_model.settings.wifi_enabled) {
        g_dc_model.wifi.state            = DcWifiState::DC_WIFI_DISABLED;
        g_dc_model.wifi.rssi             = 0;
        g_dc_model.wifi.connected_since_ms = 0;
        return;
    }

    const wl_status_t status = WiFi.status();
    switch (status) {
        case WL_CONNECTED:
            if (g_dc_model.wifi.state != DcWifiState::DC_WIFI_CONNECTED) {
                g_dc_model.wifi.connected_since_ms = millis();
            }
            g_dc_model.wifi.state = DcWifiState::DC_WIFI_CONNECTED;
            g_dc_model.wifi.rssi  = WiFi.RSSI();
            strncpy(g_dc_model.wifi.ssid, WiFi.SSID().c_str(), sizeof(g_dc_model.wifi.ssid) - 1);
            g_dc_model.wifi.ssid[sizeof(g_dc_model.wifi.ssid) - 1] = '\0';
            strncpy(g_dc_model.wifi.ip, WiFi.localIP().toString().c_str(), sizeof(g_dc_model.wifi.ip) - 1);
            g_dc_model.wifi.ip[sizeof(g_dc_model.wifi.ip) - 1] = '\0';
            break;
        case WL_IDLE_STATUS:
        case WL_SCAN_COMPLETED:
            g_dc_model.wifi.state              = DcWifiState::DC_WIFI_CONNECTING;
            g_dc_model.wifi.connected_since_ms = 0;
            break;
        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
        case WL_DISCONNECTED:
            g_dc_model.wifi.state              = DcWifiState::DC_WIFI_FAILED;
            g_dc_model.wifi.rssi               = 0;
            g_dc_model.wifi.connected_since_ms = 0;
            break;
        default:
            g_dc_model.wifi.state              = DcWifiState::DC_WIFI_FAILED;
            g_dc_model.wifi.connected_since_ms = 0;
            break;
    }
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void dc_controller_init(void) {
#ifdef FW_VERSION
    strncpy(g_dc_model.fw_version, FW_VERSION, sizeof(g_dc_model.fw_version) - 1);
#else
    strncpy(g_dc_model.fw_version, "0.0.0", sizeof(g_dc_model.fw_version) - 1);
#endif
    g_dc_model.fw_version[sizeof(g_dc_model.fw_version) - 1] = '\0';

    const uint64_t mac = ESP.getEfuseMac();
    snprintf(g_dc_model.device_serial, sizeof(g_dc_model.device_serial),
             "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
}

void dc_controller_service(float temp_c, float hum_rh, bool env_valid) {
    g_dc_model.environment.temp_c       = temp_c;
    g_dc_model.environment.hum_rh       = hum_rh;
    g_dc_model.environment.valid        = env_valid;
    g_dc_model.environment.last_read_ms = millis();

    _update_network();
    _update_wifi();
    dc_air_safeguard_service();
}

// ─── Comandi dispositivi ─────────────────────────────────────────────────────

bool dc_cmd_relay_set(uint8_t address, bool on) {
    String resp;
    return rs485_network_relay_command(address, on ? "ON" : "OFF", resp);
}

bool dc_cmd_motor_enable(uint8_t address, bool enable) {
    String resp;
    return rs485_network_motor_enable_command(address, enable, resp);
}

bool dc_cmd_motor_speed(uint8_t address, uint8_t speed_pct) {
    String resp;
    return rs485_network_motor_speed_command(address, speed_pct, resp);
}

// ─── Azioni di sistema ───────────────────────────────────────────────────────

void dc_scan_rs485(void) {
    rs485_network_scan_start();
}

void dc_wifi_reconnect(void) {
    g_dc_model.wifi.state = DcWifiState::DC_WIFI_CONNECTING;
    WiFi.reconnect();
}

void dc_wifi_abort(void) {
    WiFi.disconnect(false);
    g_dc_model.wifi.state = DcWifiState::DC_WIFI_FAILED;
}

void dc_ota_check(void) {
    // Task 3.3
}

void dc_ota_start(void) {
    // Task 3.3
}

void dc_factory_reset(void) {
    bool ok = true;
    Preferences pref;

    if (pref.begin("easy", false)) {
        ok = pref.clear() && ok;
        pref.end();
    } else {
        ok = false;
    }

    if (pref.begin("easy_disp", false)) {
        ok = pref.clear() && ok;
        pref.end();
    } else {
        ok = false;
    }

    if (pref.begin("easy_sys", false)) {
        ok = pref.clear() && ok;
        pref.end();
    } else {
        ok = false;
    }

    if (!ok) {
        Serial.println("[DC-CTRL] Factory reset incompleto: errore accesso NVS.");
        return;
    }

    Serial.println("[DC-CTRL] Factory reset completato. Riavvio...");
    delay(250);
    ESP.restart();
}

// ─── Air safeguard (implementation detail) ────────────────────────────────────

struct AirSafeguardDuctSample {
    bool temp_valid;
    bool hum_valid;
    float temp_c;
    float hum_rh;
};

struct AirSafeguardMotorGroup {
    bool valid;
    uint8_t addresses_count;
    uint8_t addresses[RS485_NET_MAX_DEVICES];
    int speed_pct;
    bool enabled;
};

struct AirSafeguardRuntime {
    bool active;
    bool filter_ready;
    bool base_enabled;
    int base_speed_pct;
    int last_target_pct;
    float temp_ema;
    float hum_ema;
    unsigned long last_service_ms;
    unsigned long above_since_ms;
    unsigned long below_since_ms;
    unsigned long invalid_since_ms;
    unsigned long last_command_ms;
};

static AirSafeguardRuntime s_air_safeguard = {};

static bool _elapsed_ms(unsigned long now, unsigned long since, unsigned long duration_ms) {
    return since != 0 && (unsigned long)(now - since) >= duration_ms;
}

static bool _air_safeguard_read_group1_duct(AirSafeguardDuctSample& out) {
    memset(&out, 0, sizeof(out));

    float temp_sum = 0.0f;
    float hum_sum = 0.0f;
    int temp_count = 0;
    int hum_count = 0;

    const int n = rs485_network_device_count();
    for (int i = 0; i < n; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.data_valid || !dev.online) continue;
        if (dev.type != Rs485DevType::SENSOR) continue;
        if (dev.sensor_profile != Rs485SensorProfile::PRESSURE) continue;
        if (dev.group != 1) continue;

        if (!isnan(dev.t) && dev.t > -20.0f && dev.t < 160.0f) {
            temp_sum += dev.t;
            temp_count++;
        }
        if (!isnan(dev.h) && dev.h >= 0.0f && dev.h <= 100.0f) {
            hum_sum += dev.h;
            hum_count++;
        }
    }

    if (temp_count > 0) {
        out.temp_valid = true;
        out.temp_c = temp_sum / (float)temp_count;
    }
    if (hum_count > 0) {
        out.hum_valid = true;
        out.hum_rh = hum_sum / (float)hum_count;
    }
    return out.temp_valid || out.hum_valid;
}

static bool _air_safeguard_read_extraction_motor(AirSafeguardMotorGroup& out) {
    memset(&out, 0, sizeof(out));

    int speed_sum = 0;
    int speed_count = 0;
    const int n = rs485_network_device_count();
    for (int i = 0; i < n; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        if (!dev.in_plant || !dev.data_valid || !dev.online) continue;
        if (dev.type != Rs485DevType::SENSOR) continue;
        if (dev.sensor_profile != Rs485SensorProfile::AIR_010) continue;
        if (dev.group != 1) continue;

        if (out.addresses_count < RS485_NET_MAX_DEVICES) {
            out.addresses[out.addresses_count++] = dev.address;
        }
        speed_sum += constrain((int)(dev.h + 0.5f), 0, 100);
        speed_count++;
        if (dev.sensor_active) out.enabled = true;
    }

    if (speed_count <= 0 || out.addresses_count == 0) return false;
    out.valid = true;
    out.speed_pct = constrain((speed_sum + (speed_count / 2)) / speed_count, 0, 100);
    return true;
}

static bool _air_safeguard_command_group1(const AirSafeguardMotorGroup& motor,
                                          int speed_pct,
                                          bool enable) {
    if (!motor.valid || motor.addresses_count == 0) return false;

    speed_pct = constrain(speed_pct, 0, 100);
    int ok_count = 0;
    for (uint8_t i = 0; i < motor.addresses_count; i++) {
        String raw;
        bool ok = true;
        if (enable) {
            ok = rs485_network_motor_enable_command(motor.addresses[i], true, raw);
        }
        if (ok) {
            ok = rs485_network_motor_speed_command(motor.addresses[i], (uint8_t)speed_pct, raw);
        }
        if (ok && !enable) {
            ok = rs485_network_motor_enable_command(motor.addresses[i], false, raw);
        }
        if (ok) ok_count++;
    }
    return ok_count > 0;
}

static void _air_safeguard_learn_manual_speed(const AirSafeguardMotorGroup& motor) {
    if (!motor.valid || s_air_safeguard.active) return;
    s_air_safeguard.base_speed_pct = motor.speed_pct;
    s_air_safeguard.base_enabled = motor.enabled;
    s_air_safeguard.last_target_pct = motor.speed_pct;
}

static void _air_safeguard_stop_boost(const AirSafeguardMotorGroup& motor,
                                      const char* reason) {
    if (motor.valid) {
        const int restore_pct = constrain(s_air_safeguard.base_speed_pct, 0, 100);
        if (_air_safeguard_command_group1(motor, restore_pct, s_air_safeguard.base_enabled)) {
            Serial.printf("[AIR-SAFE] Ripristino gruppo 1 a %d%% (%s)\n",
                          restore_pct, reason ? reason : "fine");
        }
    }

    const bool filter_ready = s_air_safeguard.filter_ready;
    const float temp_ema = s_air_safeguard.temp_ema;
    const float hum_ema = s_air_safeguard.hum_ema;
    memset(&s_air_safeguard, 0, sizeof(s_air_safeguard));
    s_air_safeguard.filter_ready = filter_ready;
    s_air_safeguard.temp_ema = temp_ema;
    s_air_safeguard.hum_ema = hum_ema;

    memset(&g_dc_model.safeguard, 0, sizeof(g_dc_model.safeguard));
}

static int _air_safeguard_boost_target(float temp_ema, float hum_ema) {
    const int base = constrain(s_air_safeguard.base_speed_pct, 0, 100);
    float severity = 0.0f;
    const int temp_max = g_dc_model.settings.safeguard_temp_max_c;
    const int hum_max  = g_dc_model.settings.safeguard_hum_max_rh;

    if (temp_ema >= (float)temp_max) {
        const float temp_severity = (temp_ema - (float)temp_max) / (float)temp_max;
        if (temp_severity > severity) severity = temp_severity;
    }
    if (hum_ema >= (float)hum_max) {
        const float hum_severity = (hum_ema - (float)hum_max) / (float)hum_max;
        if (hum_severity > severity) severity = hum_severity;
    }

    int target = base + 20 + (int)(severity * 100.0f);
    target = constrain(target, base, 100);
    return target;
}

void dc_air_safeguard_service(void) {
    static constexpr unsigned long k_service_period_ms  = 2000UL;
    static constexpr unsigned long k_activate_hold_ms   = 30000UL;
    static constexpr unsigned long k_clear_hold_ms      = 90000UL;
    static constexpr unsigned long k_invalid_restore_ms = 60000UL;
    static constexpr unsigned long k_command_gap_ms     = 15000UL;
    static constexpr int   k_ramp_step_pct  = 10;
    static constexpr float k_filter_alpha   = 0.18f;

    const unsigned long now = millis();
    if (s_air_safeguard.last_service_ms != 0 &&
        (unsigned long)(now - s_air_safeguard.last_service_ms) < k_service_period_ms) {
        return;
    }
    s_air_safeguard.last_service_ms = now;

    AirSafeguardMotorGroup motor;
    const bool motor_valid = _air_safeguard_read_extraction_motor(motor);

    const bool sg_enabled = g_dc_model.settings.safeguard_enabled;

    if (!sg_enabled || !motor_valid) {
        if (s_air_safeguard.active && motor_valid) {
            _air_safeguard_stop_boost(motor, sg_enabled ? "motore non disponibile" : "disabilitata");
        } else if (motor_valid) {
            _air_safeguard_learn_manual_speed(motor);
        }
        if (!sg_enabled) {
            s_air_safeguard.above_since_ms = 0;
            s_air_safeguard.below_since_ms = 0;
        }
        return;
    }

    AirSafeguardDuctSample duct;
    const bool duct_valid = _air_safeguard_read_group1_duct(duct);
    if (!duct_valid) {
        if (s_air_safeguard.invalid_since_ms == 0) s_air_safeguard.invalid_since_ms = now;
        if (s_air_safeguard.active &&
            _elapsed_ms(now, s_air_safeguard.invalid_since_ms, k_invalid_restore_ms)) {
            _air_safeguard_stop_boost(motor, "sensore gruppo 1 non valido");
        }
        return;
    }
    s_air_safeguard.invalid_since_ms = 0;

    if (!s_air_safeguard.filter_ready) {
        s_air_safeguard.temp_ema = duct.temp_valid ? duct.temp_c : 0.0f;
        s_air_safeguard.hum_ema = duct.hum_valid ? duct.hum_rh : 0.0f;
        s_air_safeguard.filter_ready = true;
    } else {
        if (duct.temp_valid) {
            s_air_safeguard.temp_ema += k_filter_alpha * (duct.temp_c - s_air_safeguard.temp_ema);
        }
        if (duct.hum_valid) {
            s_air_safeguard.hum_ema += k_filter_alpha * (duct.hum_rh - s_air_safeguard.hum_ema);
        }
    }

    if (!s_air_safeguard.active) {
        _air_safeguard_learn_manual_speed(motor);
    }

    const int temp_max = g_dc_model.settings.safeguard_temp_max_c;
    const int hum_max  = g_dc_model.settings.safeguard_hum_max_rh;

    const bool temp_exceeded =
        duct.temp_valid && s_air_safeguard.temp_ema >= (float)temp_max;
    const bool hum_exceeded =
        duct.hum_valid && s_air_safeguard.hum_ema >= (float)hum_max;
    const bool exceeded = temp_exceeded || hum_exceeded;

    const float temp_clear =
        (float)temp_max - fmaxf(3.0f, (float)temp_max * 0.05f);
    const float hum_clear =
        (float)hum_max - fmaxf(5.0f, (float)hum_max * 0.05f);
    const bool below_clear =
        (!duct.temp_valid || s_air_safeguard.temp_ema <= temp_clear) &&
        (!duct.hum_valid || s_air_safeguard.hum_ema <= hum_clear);

    if (exceeded) {
        if (s_air_safeguard.above_since_ms == 0) s_air_safeguard.above_since_ms = now;
        s_air_safeguard.below_since_ms = 0;
    } else {
        s_air_safeguard.above_since_ms = 0;
    }

    if (!s_air_safeguard.active &&
        _elapsed_ms(now, s_air_safeguard.above_since_ms, k_activate_hold_ms)) {
        s_air_safeguard.active = true;
        s_air_safeguard.base_speed_pct = motor.speed_pct;
        s_air_safeguard.base_enabled = motor.enabled;
        s_air_safeguard.last_target_pct = motor.speed_pct;
        s_air_safeguard.last_command_ms = 0;
        g_dc_model.safeguard.active          = true;
        g_dc_model.safeguard.active_since_ms = now;
        g_dc_model.safeguard.base_speed_pct  = motor.speed_pct;
        g_dc_model.safeguard.duct_temp_ema   = s_air_safeguard.temp_ema;
        g_dc_model.safeguard.duct_hum_ema    = s_air_safeguard.hum_ema;
        g_dc_model.safeguard.boost_speed_pct = motor.speed_pct;
        Serial.printf("[AIR-SAFE] Attiva G1: T=%.1f/%dC RH=%.1f/%d%% base=%d%%\n",
                      s_air_safeguard.temp_ema, temp_max,
                      s_air_safeguard.hum_ema, hum_max,
                      s_air_safeguard.base_speed_pct);
    }

    if (!s_air_safeguard.active) return;

    if (below_clear) {
        if (s_air_safeguard.below_since_ms == 0) s_air_safeguard.below_since_ms = now;
        if (_elapsed_ms(now, s_air_safeguard.below_since_ms, k_clear_hold_ms)) {
            _air_safeguard_stop_boost(motor, "soglie rientrate");
        }
        return;
    }
    s_air_safeguard.below_since_ms = 0;

    int desired = _air_safeguard_boost_target(s_air_safeguard.temp_ema, s_air_safeguard.hum_ema);
    if (desired < motor.speed_pct) desired = motor.speed_pct;

    int target = desired;
    if (s_air_safeguard.last_target_pct > 0 &&
        target > s_air_safeguard.last_target_pct + k_ramp_step_pct) {
        target = s_air_safeguard.last_target_pct + k_ramp_step_pct;
    }
    target = constrain(target, 0, 100);

    const bool speed_changed = abs(target - motor.speed_pct) >= 2;
    const bool enable_changed = !motor.enabled;
    if (!speed_changed && !enable_changed) return;
    if (s_air_safeguard.last_command_ms != 0 &&
        (unsigned long)(now - s_air_safeguard.last_command_ms) < k_command_gap_ms) {
        return;
    }

    if (_air_safeguard_command_group1(motor, target, true)) {
        s_air_safeguard.last_command_ms = now;
        s_air_safeguard.last_target_pct = target;
        g_dc_model.safeguard.boost_speed_pct = target;
        g_dc_model.safeguard.duct_temp_ema   = s_air_safeguard.temp_ema;
        g_dc_model.safeguard.duct_hum_ema    = s_air_safeguard.hum_ema;
        Serial.printf("[AIR-SAFE] Boost G1 a %d%%: T=%.1fC RH=%.1f%%\n",
                      target, s_air_safeguard.temp_ema, s_air_safeguard.hum_ema);
    }
}

// ─── Boot progress ────────────────────────────────────────────────────────────

void dc_boot_set_step(int step, const char* label) {
    g_dc_model.boot.step  = step;
    strncpy(g_dc_model.boot.label, label, sizeof(g_dc_model.boot.label) - 1);
    g_dc_model.boot.label[sizeof(g_dc_model.boot.label) - 1] = '\0';
}

void dc_boot_complete(void) {
    g_dc_model.boot.complete = true;
}
