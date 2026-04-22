/**
 * @file main_display_controller.cpp
 *
 * ITA: Punto di ingresso del firmware "Display Controller".
 * ENG: Entry point of the "Display Controller" firmware.
 *
 * ITA: Questo file coordina display, touch, LVGL, WiFi e sensori ambiente.
 * ENG: This file coordinates display, touch, LVGL, WiFi, and environment sensors.
 */

// ITA: API Arduino base (setup/loop, Serial, delay, millis, ecc.).
// ENG: Core Arduino APIs (setup/loop, Serial, delay, millis, etc.).
#include <Arduino.h>
// ITA: Layer rete di basso livello per ESP32.
// ENG: Low-level networking layer for ESP32.
#include <Network.h>
// ITA: Accesso NVS (memoria non volatile chiave/valore).
// ENG: NVS access (non-volatile key/value storage).
#include <Preferences.h>
// ITA: Driver WiFi Arduino per ESP32.
// ENG: Arduino WiFi driver for ESP32.
#include <WiFi.h>
#include <esp_heap_caps.h>

// ITA: Porta LVGL (init e lock thread-safe).
// ENG: LVGL port (init and thread-safe lock).
#include "display_port/lvgl_port.h"
// ITA: Modulo scansione rete RS485.
// ENG: RS485 network scan module.
#include "rs485_network.h"
// ITA: Helper I2C del progetto.
// ENG: Project I2C helper.
#include "display_port/i2c.h"
// ITA: Splash screen del display controller.
// ENG: Display controller splash screen.
#include "ui/ui_dc_splash.h"
// ITA: Home UI del display controller.
// ENG: Display controller home UI.
#include "ui/ui_dc_home.h"
// ITA: Modulo orologio (RTC/NTP/fallback).
// ENG: Clock module (RTC/NTP/fallback).
#include "ui/ui_dc_clock.h"
// ITA: Invio telemetria display verso API cloud.
// ENG: Display telemetry dispatch to cloud APIs.
#include "DisplayApi_Manager.h"
#include "dc_settings.h"
#include "dc_controller.h"

const char* FW_VERSION = "1.1.26";

// ITA: Handle I2C del sensore SHTC3 (NULL = non inizializzato).
// ENG: I2C handle for SHTC3 sensor (NULL = not initialized).
static i2c_master_dev_handle_t g_shtc3_dev = NULL;
// ITA: Stato operativo del sensore SHTC3.
// ENG: Operational state of the SHTC3 sensor.
static bool g_shtc3_ok = false;
// ITA: Timestamp ultimo polling sensore in millisecondi.
// ENG: Last sensor polling timestamp in milliseconds.
static unsigned long g_shtc3_poll_ms = 0;
static bool g_wifi_display_guard_active = false;
static unsigned long g_wifi_display_guard_start_ms = 0;
static constexpr unsigned long k_wifi_guard_timeout_ms = 30000UL;
static String g_serial_cli_line;

// ─── Boot WiFi state machine ──────────────────────────────────────────────────
enum class WifiBootState : uint8_t { IDLE, CONNECTING, RETRY_WAIT, DONE, ABORTED };
static WifiBootState g_wifi_boot_state = WifiBootState::IDLE;
static uint8_t g_wifi_boot_attempts = 0;
static constexpr uint8_t k_wifi_boot_max_attempts = 3;
static constexpr unsigned long k_wifi_boot_timeout_ms = 15000UL;
static constexpr unsigned long k_wifi_boot_retry_wait_ms = 1500UL;
static unsigned long g_wifi_boot_attempt_start_ms = 0;
static unsigned long g_wifi_boot_retry_start_ms = 0;
static String g_wifi_boot_ssid;
static String g_wifi_boot_pass;

static void display_wifi_log_heap(const char* tag) {
    Serial.printf("[WIFI-BOOT] %s heap_int=%u heap_dma=%u dma_big=%u psram=%u\n",
                  tag ? tag : "?",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
}

static void display_wifi_event_logger(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            Serial.println("[WIFI-EVENT] STA_START");
            break;
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.printf("[WIFI-EVENT] STA_CONNECTED ssid='%s' channel=%u auth=%u\n",
                          (const char*)info.wifi_sta_connected.ssid,
                          (unsigned)info.wifi_sta_connected.channel,
                          (unsigned)info.wifi_sta_connected.authmode);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WIFI-EVENT] GOT_IP ip=%s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason == 0) reason = WIFI_REASON_UNSPECIFIED;
            Serial.printf("[WIFI-EVENT] STA_DISCONNECTED reason=%u/%s ssid='%s' rssi=%ld heap_int=%u heap_dma=%u dma_big=%u\n",
                          (unsigned)reason,
                          WiFi.disconnectReasonName((wifi_err_reason_t)reason),
                          WiFi.SSID().c_str(),
                          WiFi.RSSI(),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
            break;
        }
        case ARDUINO_EVENT_WIFI_SCAN_DONE:
            Serial.printf("[WIFI-EVENT] SCAN_DONE status=%u number=%u\n",
                          (unsigned)info.wifi_scan_done.status,
                          (unsigned)info.wifi_scan_done.number);
            break;
        default:
            break;
    }
}

static void display_wifi_preinit_driver() {
    display_wifi_log_heap("before_wifi_preinit");
    WiFi.useStaticBuffers(true);
    const bool sta_ok = WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    if (sta_ok) {
        WiFi.setSleep(false);
        Serial.printf("[WIFI-BOOT] Driver WiFi STA pronto, connessione automatica disabilitata. mode=%d status=%d\n",
                      (int)WiFi.getMode(),
                      (int)WiFi.status());
    } else {
        Serial.printf("[WIFI-BOOT] ERRORE init driver WiFi STA. mode=%d status=%d\n",
                      (int)WiFi.getMode(),
                      (int)WiFi.status());
    }
    display_wifi_log_heap("after_wifi_preinit");
}

static void wifi_display_guard_set(bool enable) {
    if (g_wifi_display_guard_active == enable) return;
    g_wifi_display_guard_active = enable;
    if (enable) {
        waveshare_rgb_lcd_activity_guard_acquire();
        g_wifi_display_guard_start_ms = millis();
    } else {
        waveshare_rgb_lcd_activity_guard_release();
    }
}

static void wifi_display_guard_service() {
    if (!g_wifi_display_guard_active) return;
    const wl_status_t st = WiFi.status();
    const bool done =
        (st == WL_CONNECTED) ||
        (st == WL_CONNECT_FAILED) ||
        (st == WL_NO_SSID_AVAIL) ||
        (st == WL_NO_SHIELD);
    const bool timed_out = (millis() - g_wifi_display_guard_start_ms) >= k_wifi_guard_timeout_ms;
    if (done || timed_out) {
        wifi_display_guard_set(false);
    }
}

static int rs485_split_csv(const String& src, String* out, int max_items) {
    if (!out || max_items <= 0) return 0;

    int count = 0;
    int start = 0;
    while (count < max_items) {
        const int comma = src.indexOf(',', start);
        if (comma < 0) {
            out[count++] = src.substring(start);
            break;
        }
        out[count++] = src.substring(start, comma);
        start = comma + 1;
    }
    return count;
}

static const char* rs485_type_to_text(Rs485DevType t) {
    switch (t) {
        case Rs485DevType::SENSOR: return "SENSOR";
        case Rs485DevType::RELAY:  return "RELAY";
        default:                   return "UNKNOWN";
    }
}

static bool rs485_parse_id_arg(const String& raw, uint8_t& out_id) {
    String t = raw;
    t.trim();
    if (t.length() == 0) return false;

    for (int i = 0; i < (int)t.length(); i++) {
        const char c = t.charAt(i);
        if (c < '0' || c > '9') return false;
    }

    const int id = t.toInt();
    if (id < 1 || id > 200) return false;
    out_id = (uint8_t)id;
    return true;
}

static void rs485_cli_print_help() {
    Serial.println();
    Serial.println("========== MENU RS485 (Display Controller) ==========");
    Serial.println("HELP | 485help");
    Serial.println("  Mostra questo menu.");
    Serial.println("485ping <id>  (alias: 485pin <id>)");
    Serial.println("  Interroga la periferica con '?<id>!' e verifica risposta.");
    Serial.println("485info <id>");
    Serial.println("  Mostra info complete della scheda (raw + campi decodificati).");
    Serial.println("485scan");
    Serial.println("  Avvia scansione asincrona indirizzi 1..200.");
    Serial.println("485status");
    Serial.println("  Stato scansione corrente.");
    Serial.println("485list");
    Serial.println("  Elenca le periferiche trovate nell'ultima scansione.");
    Serial.println("485view");
    Serial.println("  Abilita monitor TX/RX RS485 (richieste master + risposte slave).");
    Serial.println("485stopview");
    Serial.println("  Disabilita monitor TX/RX RS485.");
    Serial.println("SETSERIAL <seriale>");
    Serial.println("  Imposta seriale centralina display.");
    Serial.println("SETAPIURL <url> / SETAPIKEY <key>");
    Serial.println("  Imposta endpoint Antralux via seriale.");
    Serial.println("SETCUSTURL <url> / SETCUSTKEY <key>");
    Serial.println("  Imposta endpoint utente anche da seriale.");
    Serial.println("INFO");
    Serial.println("  Mostra seriale, API configurate, WiFi e ultimo invio.");
    Serial.println("READSERIAL");
    Serial.println("  Legge il seriale centralina display salvato.");
    Serial.println("APISTATUS");
    Serial.println("  Alias di INFO per la parte API.");
    Serial.println("=====================================================");
    Serial.println();
}

static void rs485_cli_print_scan_status() {
    const Rs485ScanState st = rs485_network_scan_state();
    if (st == Rs485ScanState::RUNNING) {
        Serial.printf("[485-CLI] Scansione in corso: %d/200\n", rs485_network_scan_progress());
        return;
    }
    if (st == Rs485ScanState::DONE) {
        Serial.printf("[485-CLI] Scansione completata. Trovati: %d\n", rs485_network_device_count());
        return;
    }
    Serial.println("[485-CLI] Nessuna scansione attiva.");
}

static void rs485_cli_print_sensor_details(const String* tok, int n, const String& raw) {
    if (n < 7) {
        Serial.println("[485-CLI] Risposta SENSOR inattesa.");
        Serial.println("[485-CLI] RAW: " + raw + "!");
        return;
    }

    const float p_pa = tok[3].toFloat();
    const float p_mbar = p_pa / 100.0f;
    Serial.printf("  Temperatura: %s C\n", tok[1].c_str());
    Serial.printf("  Umidita': %s %%\n", tok[2].c_str());
    Serial.printf("  Pressione: %.2f Pa (%.2f mbar)\n", p_pa, p_mbar);
    Serial.printf("  Sicurezza: %s\n", tok[4].c_str());
    Serial.printf("  Gruppo: %s\n", tok[5].c_str());
    Serial.printf("  Seriale: %s\n", tok[6].c_str());
    if (n > 7) Serial.printf("  FW: %s\n", tok[7].c_str());
}

static void rs485_cli_print_relay_details(const String* tok, int n, const String& raw) {
    if (n < 12) {
        Serial.println("[485-CLI] Risposta RELAY inattesa.");
        Serial.println("[485-CLI] RAW: " + raw + "!");
        return;
    }

    Serial.printf("  Modalita': %s\n", tok[2].c_str());
    Serial.printf("  Relay ON: %s\n", tok[3].c_str());
    Serial.printf("  Safety closed: %s\n", tok[4].c_str());
    Serial.printf("  Feedback ok: %s\n", tok[5].c_str());
    Serial.printf("  Avvii relay: %s\n", tok[6].c_str());
    Serial.printf("  Ore ON: %s\n", tok[7].c_str());
    Serial.printf("  Gruppo: %s\n", tok[8].c_str());
    Serial.printf("  Seriale: %s\n", tok[9].c_str());
    Serial.printf("  Stato: %s\n", tok[10].c_str());
    Serial.printf("  FW: %s\n", tok[11].c_str());
    if (n > 12) Serial.printf("  Limite vita (h): %s\n", tok[12].c_str());
    if (n > 13) Serial.printf("  Vita scaduta: %s\n", tok[13].c_str());
    if (n > 14) Serial.printf("  Fault feedback latched: %s\n", tok[14].c_str());
}

static void rs485_cli_handle_ping(const String& arg) {
    uint8_t id = 0;
    if (!rs485_parse_id_arg(arg, id)) {
        Serial.println("[485-CLI] Uso: 485ping <id>  (id 1..200)");
        return;
    }

    String raw;
    const bool ok = rs485_network_ping(id, raw);
    if (!ok) {
        if (raw == "BUSY_SCAN") {
            Serial.println("[485-CLI] Bus occupato da scansione in corso. Riprova a scansione finita.");
        } else if (raw == "BUSY") {
            Serial.println("[485-CLI] Bus RS485 occupato. Riprova.");
        } else if (raw == "ERR,INIT") {
            Serial.println("[485-CLI] RS485 non inizializzato.");
        } else if (raw == "ERR,ADDR") {
            Serial.println("[485-CLI] ID non valido. Range: 1..200.");
        } else {
            Serial.printf("[485-CLI] PING %u -> OFFLINE/NO_RESPONSE\n", id);
            if (raw.length() > 0) {
                Serial.println("[485-CLI] RAW RX: " + raw + "!");
            }
        }
        return;
    }

    Serial.printf("[485-CLI] PING %u -> ONLINE\n", id);
    Serial.println("[485-CLI] RAW: " + raw + "!");
}

static void rs485_cli_handle_info(const String& arg) {
    uint8_t id = 0;
    if (!rs485_parse_id_arg(arg, id)) {
        Serial.println("[485-CLI] Uso: 485info <id>  (id 1..200)");
        return;
    }

    String raw;
    Rs485Device dev;
    const bool ok = rs485_network_query_device(id, dev, raw);
    if (!ok) {
        if (raw == "BUSY_SCAN") {
            Serial.println("[485-CLI] Bus occupato da scansione in corso. Riprova a scansione finita.");
        } else if (raw == "BUSY") {
            Serial.println("[485-CLI] Bus RS485 occupato. Riprova.");
        } else if (raw == "ERR,INIT") {
            Serial.println("[485-CLI] RS485 non inizializzato.");
        } else if (raw == "ERR,ADDR") {
            Serial.println("[485-CLI] ID non valido. Range: 1..200.");
        } else {
            Serial.printf("[485-CLI] INFO %u -> OFFLINE/NO_RESPONSE\n", id);
            if (raw.length() > 0) {
                Serial.println("[485-CLI] RAW RX: " + raw + "!");
            }
        }
        return;
    }

    Serial.printf("[485-CLI] INFO periferica ID %u\n", id);
    Serial.println("[485-CLI] RAW: " + raw + "!");
    Serial.printf("  Tipo: %s\n", rs485_type_to_text(dev.type));
    Serial.printf("  Seriale: %s\n", dev.sn);
    Serial.printf("  FW: %s\n", dev.version);

    String tok[20];
    const int n = rs485_split_csv(raw, tok, 20);
    if (raw.startsWith("OK,RELAY,")) {
        rs485_cli_print_relay_details(tok, n, raw);
    } else if (raw.startsWith("OK,")) {
        rs485_cli_print_sensor_details(tok, n, raw);
    }
}

static void rs485_cli_handle_list() {
    const int count = rs485_network_device_count();
    if (count <= 0) {
        Serial.println("[485-CLI] Nessun device in cache. Eseguire prima: 485scan");
        return;
    }

    Serial.printf("[485-CLI] Dispositivi in cache: %d\n", count);
    for (int i = 0; i < count; i++) {
        Rs485Device dev;
        if (!rs485_network_get_device(i, dev)) continue;
        Serial.printf("  ID=%u  TYPE=%s  SN=%s  FW=%s\n",
                      dev.address, rs485_type_to_text(dev.type), dev.sn, dev.version);
    }
}

static void rs485_cli_handle_view(bool enable) {
    rs485_network_set_monitor_enabled(enable);
    Serial.printf("[485-CLI] Monitor RS485: %s\n", enable ? "ATTIVO" : "DISATTIVO");
}

static bool cli_get_value_after_command(const String& line, const String& upper,
                                        const char* command, String& value) {
    const String cmd = String(command);
    if (!upper.startsWith(cmd)) return false;
    if (line.length() <= cmd.length()) return false;

    const char sep = line.charAt(cmd.length());
    if (sep != ' ' && sep != ':' && sep != '=') return false;

    value = line.substring(cmd.length() + 1);
    value.trim();
    return true;
}

static void display_cli_print_serial() {
    const DisplayApiConfig cfg = displayApiLoadConfig();
    Serial.printf("[DISPLAY] Seriale centralina: %s\n", cfg.serialNumber.c_str());
}

static void rs485_cli_handle_command(String line) {
    line.trim();
    if (line.length() == 0) return;

    String upper = line;
    upper.toUpperCase();

    if (upper == "HELP" || upper == "?" || upper == "485" ||
        upper == "485HELP" || upper == "485 HELP" || upper == "485 - HELP") {
        rs485_cli_print_help();
        return;
    }

    if (upper == "INFO" || upper == "APISTATUS") {
        displayApiPrintStatus();
        return;
    }

    if (upper == "READSERIAL") {
        display_cli_print_serial();
        return;
    }

    String value;
    if (cli_get_value_after_command(line, upper, "SETSERIAL", value)) {
        displayApiSetSerialNumber(value);
        Serial.println("[DISPLAY-API] Seriale centralina salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETAPIURL", value)) {
        displayApiSetFactoryUrl(value);
        Serial.println("[DISPLAY-API] URL API Antralux salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETAPIKEY", value)) {
        displayApiSetFactoryKey(value);
        Serial.printf("[DISPLAY-API] API Key Antralux salvata (%u caratteri).\n",
                      (unsigned)value.length());
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETCUSTURL", value)) {
        displayApiSetCustomerUrl(value);
        Serial.println("[DISPLAY-API] URL API utente salvato.");
        return;
    }

    if (cli_get_value_after_command(line, upper, "SETCUSTKEY", value)) {
        displayApiSetCustomerKey(value);
        Serial.printf("[DISPLAY-API] API Key utente salvata (%u caratteri).\n",
                      (unsigned)value.length());
        return;
    }

    if (!upper.startsWith("485")) {
        Serial.println("[485-CLI] Comando non riconosciuto. Usa HELP.");
        return;
    }

    String tail = line.substring(3);
    tail.trim();
    if (tail.startsWith("-")) {
        tail = tail.substring(1);
        tail.trim();
    }
    if (tail.length() == 0) {
        rs485_cli_print_help();
        return;
    }

    String cmd;
    String arg;
    const int space_idx = tail.indexOf(' ');
    if (space_idx >= 0) {
        cmd = tail.substring(0, space_idx);
        arg = tail.substring(space_idx + 1);
        arg.trim();
    } else {
        cmd = tail;
    }
    cmd.toUpperCase();

    if (cmd == "PING" || cmd == "PIN") {
        rs485_cli_handle_ping(arg);
        return;
    }
    if (cmd == "INFO") {
        rs485_cli_handle_info(arg);
        return;
    }
    if (cmd == "SCAN") {
        if (rs485_network_scan_state() == Rs485ScanState::RUNNING) {
            rs485_cli_print_scan_status();
        } else {
            rs485_network_scan_start();
            Serial.println("[485-CLI] Scansione RS485 avviata (1..200).");
        }
        return;
    }
    if (cmd == "STATUS") {
        rs485_cli_print_scan_status();
        return;
    }
    if (cmd == "LIST") {
        rs485_cli_handle_list();
        return;
    }
    if (cmd == "VIEW") {
        rs485_cli_handle_view(true);
        return;
    }
    if (cmd == "STOPVIEW") {
        rs485_cli_handle_view(false);
        return;
    }

    Serial.println("[485-CLI] Comando sconosciuto. Usa: 485help");
}

static void rs485_cli_poll_serial() {
    while (Serial.available()) {
        const char c = (char)Serial.read();

        if (c == '\r' || c == '\n') {
            if (g_serial_cli_line.length() > 0) {
                Serial.println();
                const String line = g_serial_cli_line;
                g_serial_cli_line = "";
                rs485_cli_handle_command(line);
            }
            continue;
        }

        if (c == '\b' || c == 127) {
            if (g_serial_cli_line.length() > 0) {
                g_serial_cli_line.remove(g_serial_cli_line.length() - 1);
                Serial.print("\b \b");
            }
            continue;
        }

        if (c >= 32 && c <= 126) {
            if (g_serial_cli_line.length() < 160) {
                g_serial_cli_line += c;
                Serial.write(c);
            }
        }
    }
}

/**
 * ITA: Legge credenziali da NVS e avvia connessione WiFi se abilitata.
 * ENG: Reads credentials from NVS and starts WiFi connection if enabled.
 */
static void setup_display_wifi_from_saved_credentials() {
    // ITA: Oggetto helper per namespace NVS.
    // ENG: Helper object for NVS namespace access.
    Preferences pref;

    // ITA: Apre namespace "easy" in sola lettura.
    // ENG: Opens "easy" namespace in read-only mode.
    if (!pref.begin("easy", true)) {
        // ITA: Se NVS non e disponibile, spegne il WiFi.
        // ENG: If NVS is unavailable, WiFi is turned off.
        Serial.println("[WIFI] NVS non disponibile: WiFi inattivo, driver mantenuto pronto per scansione.");
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        return;
    }

    // ITA: Flag utente: WiFi display abilitato/disabilitato.
    // ENG: User flag: display WiFi enabled/disabled.
    const bool wifi_enabled = pref.getBool("dc_wifi_enabled", false);
    // ITA: SSID salvato.
    // ENG: Saved SSID.
    const String ssid = pref.getString("ssid", "");
    // ITA: Password salvata.
    // ENG: Saved password.
    const String pass = pref.getString("pass", "");

    // ITA: Chiude la sessione NVS.
    // ENG: Closes NVS session.
    pref.end();

    // ITA: Se manca configurazione valida, mantiene WiFi OFF.
    // ENG: If valid configuration is missing, keeps WiFi OFF.
    if (!wifi_enabled || ssid.length() == 0) {
        WiFi.setAutoReconnect(false);
        WiFi.disconnect(false, false);
        Serial.println("[WIFI] WiFi display disabilitato o credenziali assenti: STA pronta per scansione manuale.");
        return;
    }

    // ITA: Modalita station, riconnessione automatica, start connessione.
    // ENG: Station mode, auto-reconnect, start connection.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    wifi_display_guard_set(true);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // ITA: Log diagnostico SSID target.
    // ENG: Diagnostic log with target SSID.
    Serial.printf("[WIFI] Tentativo connessione automatica a: %s\n", ssid.c_str());
}

/**
 * ITA: Calcola CRC-8 secondo protocollo SHTC3 (poly 0x31, init 0xFF).
 * ENG: Computes CRC-8 according to SHTC3 protocol (poly 0x31, init 0xFF).
 */
static uint8_t shtc3_crc8(const uint8_t* data, size_t len) {
    // ITA: Seed iniziale richiesto da Sensirion.
    // ENG: Initial seed required by Sensirion.
    uint8_t crc = 0xFF;

    // ITA: Processa tutti i byte del payload.
    // ENG: Processes all payload bytes.
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];

        // ITA: Aggiornamento bit-a-bit del registro CRC.
        // ENG: Bit-by-bit CRC register update.
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }

    return crc;
}

/**
 * ITA: Invia comando a 2 byte al sensore SHTC3.
 * ENG: Sends a 2-byte command to the SHTC3 sensor.
 */
static bool shtc3_send_cmd(uint8_t msb, uint8_t lsb, int timeout_ms = 100) {
    if (!g_shtc3_dev) return false;

    const uint8_t cmd[2] = {msb, lsb};
    return i2c_master_transmit(g_shtc3_dev, cmd, sizeof(cmd), timeout_ms) == ESP_OK;
}

/**
 * ITA: Inizializza sensore SHTC3 e lo porta in stato awake.
 * ENG: Initializes SHTC3 sensor and brings it to awake state.
 */
static bool shtc3_init_sensor() {
    if (g_shtc3_dev) return true;

    // ITA: Address I2C default SHTC3 = 0x70.
    // ENG: Default SHTC3 I2C address = 0x70.
    DEV_I2C_Set_Slave_Addr(&g_shtc3_dev, 0x70);
    if (!g_shtc3_dev) return false;

    // ITA: Wake-up command dal datasheet.
    // ENG: Wake-up command from datasheet.
    if (!shtc3_send_cmd(0x35, 0x17)) return false;
    delay(2);

    return true;
}

/**
 * ITA: Legge temperatura e umidita dal SHTC3 e valida CRC.
 * ENG: Reads temperature and humidity from SHTC3 and validates CRC.
 */
static bool shtc3_read(float& temp_c, float& hum_rh) {
    if (!g_shtc3_dev) return false;

    // ITA: Wake-up prima della misura.
    // ENG: Wake-up before measurement.
    if (!shtc3_send_cmd(0x35, 0x17)) return false;
    delay(2);

    // ITA: Start misura (Temperature first, normal power mode).
    // ENG: Start measurement (Temperature first, normal power mode).
    if (!shtc3_send_cmd(0x7C, 0xA2)) return false;
    delay(14);

    // ITA: Payload atteso: T_MSB,T_LSB,CRC, RH_MSB,RH_LSB,CRC.
    // ENG: Expected payload: T_MSB,T_LSB,CRC, RH_MSB,RH_LSB,CRC.
    uint8_t raw[6] = {0};
    if (i2c_master_receive(g_shtc3_dev, raw, sizeof(raw), 100) != ESP_OK) {
        return false;
    }

    // ITA: Verifica integrita dati con CRC per T e RH.
    // ENG: Data integrity check using CRC for T and RH.
    const bool temp_crc_ok = (shtc3_crc8(raw, 2) == raw[2]);
    const bool hum_crc_ok = (shtc3_crc8(raw + 3, 2) == raw[5]);
    if (!temp_crc_ok || !hum_crc_ok) return false;

    // ITA: Ricostruzione raw a 16 bit.
    // ENG: Rebuild 16-bit raw values.
    const uint16_t raw_t = (uint16_t)((raw[0] << 8) | raw[1]);
    const uint16_t raw_h = (uint16_t)((raw[3] << 8) | raw[4]);

    // ITA: Conversioni ufficiali datasheet.
    // ENG: Official datasheet conversions.
    temp_c = -45.0f + 175.0f * ((float)raw_t / 65536.0f);
    hum_rh = 100.0f * ((float)raw_h / 65536.0f);

    // ITA: Clamp umidita al range fisico 0..100.
    // ENG: Clamp humidity to physical range 0..100.
    if (hum_rh < 0.0f) hum_rh = 0.0f;
    if (hum_rh > 100.0f) hum_rh = 100.0f;

    return true;
}

/**
 * ITA: Blocco infinito usato in caso di errore fatale all'avvio display.
 * ENG: Infinite halt used on fatal display startup errors.
 */
static void halt_forever() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * ITA: Setup Arduino, eseguito una sola volta dopo il boot.
 * ENG: Arduino setup, executed once after boot.
 */
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("=== EasyConnect Display Controller ===");

    // WiFi driver init deve avvenire PRIMA di lvgl_port_init per riservare
    // memoria DMA contigua (~80KB). Dopo LVGL la heap DMA è troppo frammentata.
    WiFi.useStaticBuffers(true);
    WiFi.onEvent(display_wifi_event_logger);
    display_wifi_preinit_driver();

    // ITA: Init touch e pannello RGB.
    // ENG: Initialize touch and RGB panel.
    esp_lcd_touch_handle_t tp_handle = touch_gt911_init();
    esp_lcd_panel_handle_t panel_handle = waveshare_esp32_s3_rgb_lcd_init();

    if (!panel_handle) {
        Serial.println("[ERRORE] Display non inizializzato - halt");
        halt_forever();
    }

    // ITA: Backlight ON + init gestione luminosita + init LVGL.
    // ENG: Backlight ON + brightness management init + LVGL init.
    wavesahre_rgb_lcd_bl_on();
    ui_brightness_init();

    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));
    Serial.println("[OK] Display + LVGL pronti");
    display_wifi_log_heap("after_lvgl");

    // ITA: Inizializza interfaccia RS485 DOPO il display per evitare conflitti
    //      di pin: GPIO7=PCLK e GPIO21=G7 sull'S3 non sono usabili per RS485.
    //      I pin del display controller sono separati (vedere Pins.h).
    // ENG: Initialize RS485 AFTER display to avoid pin conflicts.
    rs485_network_init();
    Serial.println("[OK] RS485 pronto (Serial1)");
    Serial.println("Digita 'HELP' o '485 - HELP' per il menu comandi RS485.");

    dc_settings_load();
    dc_controller_init();
    Serial.println("[OK] Controller inizializzato");

    // ITA: Crea splash in sezione critica LVGL.
    // ENG: Creates splash inside LVGL critical section.
    if (lvgl_port_lock(-1)) {
        ui_dc_splash_create();
        lvgl_port_unlock();
    }
    display_wifi_log_heap("after_splash");

    // Tenta connessione automatica alla rete salvata (max k_wifi_boot_max_attempts tentativi).
    // Se dopo tutti i tentativi non si connette, la connessione è possibile solo dalla pagina
    // impostazioni. La pagina impostazioni può annullare il tentativo in corso via wifi_boot_abort().
    {
        Preferences pref;
        if (pref.begin("easy", true)) {
            const bool enabled = pref.getBool("dc_wifi_enabled", false);
            g_wifi_boot_ssid = pref.getString("ssid", "");
            g_wifi_boot_pass = pref.getString("pass", "");
            pref.end();

            if (enabled && g_wifi_boot_ssid.length() > 0) {
                WiFi.setAutoReconnect(false);
                WiFi.begin(g_wifi_boot_ssid.c_str(), g_wifi_boot_pass.c_str());
                g_wifi_boot_state = WifiBootState::CONNECTING;
                g_wifi_boot_attempts = 1;
                g_wifi_boot_attempt_start_ms = millis();
                wifi_display_guard_set(true);
                Serial.printf("[WIFI-BOOT] Tentativo 1/%d a: %s\n",
                              (int)k_wifi_boot_max_attempts, g_wifi_boot_ssid.c_str());
            } else {
                Serial.println("[WIFI-BOOT] WiFi disabilitato o credenziali assenti: in attesa dalla pagina impostazioni.");
            }
        } else {
            Serial.println("[WIFI-BOOT] NVS non disponibile: in attesa dalla pagina impostazioni.");
        }
    }

    // ITA: Inizializza sensore SHTC3.
    // ENG: Initialize SHTC3 sensor.
    g_shtc3_ok = shtc3_init_sensor();
    if (g_shtc3_ok) {
        Serial.println("[OK] SHTC3 pronto (I2C)");
    } else {
        Serial.println("[WARN] SHTC3 non rilevato o non raggiungibile");
    }

    // ITA: Inizializza clock UI (RTC se presente, altrimenti fallback).
    // ENG: Initializes UI clock (RTC if present, fallback otherwise).
    ui_dc_clock_init();
    if (ui_dc_clock_has_rtc()) {
        Serial.println("[OK] RTC rilevato su I2C");
    } else {
        Serial.println("[WARN] RTC non rilevato: fallback NTP/contatore attivo");
    }

    rs485_cli_print_help();
}

static void wifi_boot_service() {
    if (g_wifi_boot_state == WifiBootState::RETRY_WAIT) {
        if (millis() - g_wifi_boot_retry_start_ms >= k_wifi_boot_retry_wait_ms) {
            WiFi.begin(g_wifi_boot_ssid.c_str(), g_wifi_boot_pass.c_str());
            g_wifi_boot_attempt_start_ms = millis();
            g_wifi_boot_state = WifiBootState::CONNECTING;
            Serial.printf("[WIFI-BOOT] Tentativo %d/%d a: %s\n",
                          (int)g_wifi_boot_attempts, (int)k_wifi_boot_max_attempts,
                          g_wifi_boot_ssid.c_str());
        }
        return;
    }
    if (g_wifi_boot_state != WifiBootState::CONNECTING) return;

    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        g_wifi_boot_state = WifiBootState::DONE;
        wifi_display_guard_set(false);
        Serial.printf("[WIFI-BOOT] Connesso a %s, IP=%s\n",
                      g_wifi_boot_ssid.c_str(), WiFi.localIP().toString().c_str());
        return;
    }

    const bool failed = (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL);
    const bool timed_out = (millis() - g_wifi_boot_attempt_start_ms) >= k_wifi_boot_timeout_ms;
    if (!failed && !timed_out) return;

    Serial.printf("[WIFI-BOOT] Tentativo %d/%d fallito (status=%d)\n",
                  (int)g_wifi_boot_attempts, (int)k_wifi_boot_max_attempts, (int)st);

    if (g_wifi_boot_attempts >= k_wifi_boot_max_attempts) {
        g_wifi_boot_state = WifiBootState::DONE;
        wifi_display_guard_set(false);
        WiFi.disconnect(false, false);
        Serial.println("[WIFI-BOOT] Tutti i tentativi esauriti. Connessione disponibile dalla pagina impostazioni.");
        return;
    }

    g_wifi_boot_attempts++;
    WiFi.disconnect(false, false);
    g_wifi_boot_retry_start_ms = millis();
    g_wifi_boot_state = WifiBootState::RETRY_WAIT;
}

void wifi_boot_abort() {
    if (g_wifi_boot_state != WifiBootState::CONNECTING &&
        g_wifi_boot_state != WifiBootState::RETRY_WAIT) return;
    g_wifi_boot_state = WifiBootState::ABORTED;
    wifi_display_guard_set(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    Serial.println("[WIFI-BOOT] Tentativo boot annullato dall'utente (scansione avviata).");
}

bool wifi_boot_is_active() {
    return g_wifi_boot_state == WifiBootState::CONNECTING ||
           g_wifi_boot_state == WifiBootState::RETRY_WAIT;
}

/**
 * ITA: Loop principale con polling periodico sensore ambiente.
 * ENG: Main loop with periodic environment sensor polling.
 */
void loop() {
    const unsigned long now = millis();

    wifi_display_guard_service();
    wifi_boot_service();
    rs485_cli_poll_serial();
    displayApiService();

    // ITA: Polling ogni 2 secondi.
    // ENG: Poll every 2 seconds.
    if (now - g_shtc3_poll_ms >= 2000UL) {
        g_shtc3_poll_ms = now;

        // ITA: Retry init se il sensore era offline.
        // ENG: Retry initialization if sensor was offline.
        if (!g_shtc3_ok) {
            g_shtc3_ok = shtc3_init_sensor();
        }

        float t = 0.0f;
        float h = 0.0f;
        const bool valid = g_shtc3_ok && shtc3_read(t, h);

        dc_controller_service(t, h, valid);
    }

    // ITA: Piccolo sleep per evitare busy-loop.
    // ENG: Small sleep to avoid busy-looping.
    vTaskDelay(pdMS_TO_TICKS(100));
}
