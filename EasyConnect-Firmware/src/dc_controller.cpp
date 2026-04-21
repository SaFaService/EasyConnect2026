#include "dc_controller.h"
#include "dc_data_model.h"
#include "dc_settings.h"
#include "rs485_network.h"
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

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
    dst.relay_mode     = DC_RELAY_MANUAL;
    dst.safety_fault   = src.relay_safety_closed;
    dst.feedback_fault = src.relay_feedback_fault_latched;
    dst.relay_starts   = 0;
    dst.uptime_hours   = 0;

    dst.motor_enabled  = src.sensor_active;
    dst.speed_pct      = 0;

    dst.pressure_pa    = src.p;
    dst.temp_c         = src.t;
    dst.hum_rh         = src.h;
    dst.pressure_valid = (src.type == Rs485DevType::SENSOR &&
                          src.sensor_profile == Rs485SensorProfile::PRESSURE &&
                          src.data_valid);
    dst.temp_valid     = (src.type == Rs485DevType::SENSOR && src.data_valid);
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
    switch (WiFi.status()) {
        case WL_CONNECTED:
            g_dc_model.wifi.state = DcWifiState::DC_WIFI_CONNECTED;
            g_dc_model.wifi.rssi  = WiFi.RSSI();
            strncpy(g_dc_model.wifi.ssid, WiFi.SSID().c_str(), sizeof(g_dc_model.wifi.ssid) - 1);
            g_dc_model.wifi.ssid[sizeof(g_dc_model.wifi.ssid) - 1] = '\0';
            strncpy(g_dc_model.wifi.ip, WiFi.localIP().toString().c_str(), sizeof(g_dc_model.wifi.ip) - 1);
            g_dc_model.wifi.ip[sizeof(g_dc_model.wifi.ip) - 1] = '\0';
            break;
        case WL_IDLE_STATUS:
        case WL_SCAN_COMPLETED:
            g_dc_model.wifi.state = DcWifiState::DC_WIFI_CONNECTING;
            break;
        case WL_NO_SSID_AVAIL:
        case WL_CONNECT_FAILED:
        case WL_CONNECTION_LOST:
        case WL_DISCONNECTED:
            g_dc_model.wifi.state = DcWifiState::DC_WIFI_FAILED;
            g_dc_model.wifi.rssi  = 0;
            break;
        default:
            g_dc_model.wifi.state = DcWifiState::DC_WIFI_DISABLED;
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

bool dc_wifi_boot_is_active(void) {
    // Task 1.5: collegato alla state machine WiFi del main
    return false;
}

void dc_ota_check(void) {
    // Task 3.3
}

void dc_ota_start(void) {
    // Task 3.3
}

// ─── Air safeguard ───────────────────────────────────────────────────────────

void dc_air_safeguard_service(void) {
    // Task 1.4: logica migrata da ui_dc_home.cpp
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
