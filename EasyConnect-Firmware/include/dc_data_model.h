#pragma once

// Contratto dati tra Controller e UI.
// Scritto SOLO dal Controller (dc_controller_service).
// Letto in sola lettura dall'UI e dai template.

#include <stdint.h>
#include <stdbool.h>

#define DC_MAX_DEVICES 32
#define DC_MAX_NOTIF   32

// ─── Dispositivo RS485 ────────────────────────────────────────────────────────

enum DcDeviceType : uint8_t {
    DC_DEV_UNKNOWN   = 0,
    DC_DEV_SENSOR    = 1,
    DC_DEV_RELAY     = 2,
    DC_DEV_MOTOR_010 = 3,
};

enum DcRelayMode : uint8_t {
    DC_RELAY_MANUAL = 0,
    DC_RELAY_AUTO   = 1,
    DC_RELAY_TIMER  = 2,
};

struct DcDeviceSnapshot {
    bool           valid;
    bool           online;
    uint8_t        address;
    uint8_t        group;
    DcDeviceType   type;

    // Relay
    bool           relay_on;
    DcRelayMode    relay_mode;
    bool           safety_fault;
    bool           feedback_fault;
    uint16_t       relay_starts;
    uint32_t       uptime_hours;

    // Motore 0/10V
    bool           motor_enabled;
    uint8_t        speed_pct;

    // Sensore
    float          pressure_pa;
    float          temp_c;
    float          hum_rh;
    bool           pressure_valid;
    bool           temp_valid;
    bool           hum_valid;

    char           sn[16];
    char           fw[8];
};

// ─── Rete RS485 ───────────────────────────────────────────────────────────────

struct DcNetworkSnapshot {
    int              device_count;
    DcDeviceSnapshot devices[DC_MAX_DEVICES];
    unsigned long    last_update_ms;
    bool             scan_running;
    int              scan_progress;   // 0–200
};

// ─── Ambiente (SHTC3 locale) ──────────────────────────────────────────────────

struct DcEnvironment {
    float         temp_c;
    float         hum_rh;
    bool          valid;
    unsigned long last_read_ms;
};

// ─── WiFi ─────────────────────────────────────────────────────────────────────

enum class DcWifiState : uint8_t {
    DC_WIFI_DISABLED,
    DC_WIFI_CONNECTING,
    DC_WIFI_CONNECTED,
    DC_WIFI_FAILED,
};

struct DcWifi {
    DcWifiState   state;
    int           rssi;
    char          ssid[33];
    char          ip[16];
    bool          internet_reachable;
    unsigned long connected_since_ms;
};

// ─── API server ───────────────────────────────────────────────────────────────

enum class DcApiState : uint8_t {
    IDLE,
    SENDING,
    OK,
    ERROR,
};

struct DcApi {
    DcApiState    state;
    unsigned long last_ok_ms;
    unsigned long last_attempt_ms;
    uint32_t      send_count;
    uint32_t      error_count;
    int           last_http_code;
    char          last_error[64];
};

// ─── Air Safeguard runtime ────────────────────────────────────────────────────

struct DcSafeguardState {
    bool  active;
    float duct_temp_ema;
    float duct_hum_ema;
    int   boost_speed_pct;
    int   base_speed_pct;
    unsigned long active_since_ms;
};

// ─── Impostazioni persistite ──────────────────────────────────────────────────

enum DcTempUnit : uint8_t {
    DC_TEMP_C = 0,
    DC_TEMP_F = 1,
};

struct DcSettings {
    // Display
    int          brightness_pct;
    int          screensaver_min;
    DcTempUnit   temp_unit;
    char         plant_name[48];
    uint8_t      ui_theme_id;

    // Ventilazione
    int          vent_min_pct;
    int          vent_max_pct;
    int          vent_steps;
    bool         intake_bar_enabled;
    int          intake_diff_pct;

    // Air safeguard
    bool         safeguard_enabled;
    int          safeguard_temp_max_c;
    int          safeguard_hum_max_rh;

    // Rete (password mai qui — rimane in NVS)
    bool         wifi_enabled;
    char         wifi_ssid[33];

    // API (chiavi mai qui — rimangono in NVS)
    bool         api_factory_enabled;
    bool         api_customer_enabled;
    char         api_customer_url[128];

    // OTA
    bool         ota_auto_enabled;
    char         ota_channel[16];
};

// ─── Notifiche ────────────────────────────────────────────────────────────────

enum DcNotifSeverity : uint8_t {
    DC_NOTIF_INFO    = 0,
    DC_NOTIF_WARNING = 1,
    DC_NOTIF_ALARM   = 2,
};

struct DcNotification {
    DcNotifSeverity severity;
    uint8_t         device_addr;
    uint8_t         code;
    char            message[48];
    unsigned long   timestamp_ms;
};

struct DcNotifications {
    DcNotification items[DC_MAX_NOTIF];
    int            count;
    int            unread_count;
    int            alarm_count;
    int            warning_count;
};

// ─── Boot progress (per splash condivisa) ─────────────────────────────────────

struct DcBoot {
    int  step;           // 0–10
    char label[48];      // descrizione step corrente
    bool complete;
};

// ─── DataModel principale ─────────────────────────────────────────────────────

struct DcDataModel {
    DcNetworkSnapshot  network;
    DcEnvironment      environment;
    DcWifi             wifi;
    DcApi              api;
    DcSafeguardState   safeguard;
    DcSettings         settings;
    DcNotifications    notifications;
    DcBoot             boot;

    bool  system_safety_trip;
    bool  system_bypass_active;
    char  fw_version[16];
    char  device_serial[24];
};

// Istanza globale — aggiornata SOLO dal Controller, letta dall'UI
extern DcDataModel g_dc_model;
