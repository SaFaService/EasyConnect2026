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
// Splash condivisa con progresso boot reale.
#include "ui/shared/ui_splash_shared.h"
// ITA: Home UI del display controller.
// ENG: Display controller home UI.
#include "ui/ui_dc_home.h"
#include "ui_theme_interface.h"
// ITA: Modulo orologio (RTC/NTP/fallback).
// ENG: Clock module (RTC/NTP/fallback).
#include "ui/ui_dc_clock.h"
// ITA: Invio telemetria display verso API cloud.
// ENG: Display telemetry dispatch to cloud APIs.
#include "DisplayApi_Manager.h"
#include "dc_admin_cli.h"
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
    dc_boot_set_step(0, "Avvio");

    // WiFi driver init deve avvenire PRIMA di lvgl_port_init per riservare
    // memoria DMA contigua (~80KB). Dopo LVGL la heap DMA è troppo frammentata.
    WiFi.useStaticBuffers(true);
    WiFi.onEvent(display_wifi_event_logger);
    display_wifi_preinit_driver();

    esp_lcd_touch_handle_t tp_handle = touch_gt911_init();
    esp_lcd_panel_handle_t panel_handle = waveshare_esp32_s3_rgb_lcd_init();

    if (!panel_handle) {
        Serial.println("[ERRORE] Display non inizializzato - halt");
        halt_forever();
    }
    dc_boot_set_step(1, "Display");

    wavesahre_rgb_lcd_bl_on();
    ui_brightness_init();
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));
    Serial.println("[OK] Display + LVGL pronti");
    display_wifi_log_heap("after_lvgl");
    dc_boot_set_step(2, "LVGL pronto");

    // Splash creata subito dopo LVGL — mostra progresso boot in tempo reale.
    if (lvgl_port_lock(-1)) {
        ui_splash_shared_create();
        lvgl_port_unlock();
    }
    display_wifi_log_heap("after_splash");

    // RS485 init DOPO display (conflitti pin su S3).
    rs485_network_init();
    rs485_network_boot_probe_start();
    Serial.println("[OK] RS485 pronto (Serial1)");
    dc_boot_set_step(3, "RS485 pronto");

    // Step 4: sensore SHTC3
    g_shtc3_ok = shtc3_init_sensor();
    if (g_shtc3_ok) {
        Serial.println("[OK] SHTC3 pronto (I2C)");
    } else {
        Serial.println("[WARN] SHTC3 non rilevato o non raggiungibile");
    }
    dc_boot_set_step(4, "Sensore SHTC3");

    // Step 5: clock RTC/NTP
    ui_dc_clock_init();
    if (ui_dc_clock_has_rtc()) {
        Serial.println("[OK] RTC rilevato su I2C");
    } else {
        Serial.println("[WARN] RTC non rilevato: clock software attivo, sync NTP se WiFi disponibile");
    }
    dc_boot_set_step(5, "Orologio");

    // Step 6: impostazioni NVS
    dc_settings_load();
    dc_boot_set_step(6, "Impostazioni NVS");

    // Step 7: controller (RS485 snapshot, safeguard, WiFi state machine)
    dc_controller_init();
    Serial.println("[OK] Controller inizializzato");
    dc_boot_set_step(7, "Controller pronto");

    // Prima lettura SHTC3 — popola g_dc_model.environment prima del caricamento Home.
    // Senza questo, la Home si apre con temp/hum a zero finché il loop non effettua
    // il primo polling (ritardo fino a 2 secondi).
    {
        float t = 0.0f, h = 0.0f;
        const bool valid = g_shtc3_ok && shtc3_read(t, h);
        dc_controller_service(t, h, valid);
        g_shtc3_poll_ms = millis();
        if (valid) {
            Serial.printf("[OK] SHTC3 prima lettura: T=%.1fC RH=%.1f%%\n", t, h);
        } else {
            Serial.println("[WARN] SHTC3 prima lettura non disponibile: placeholder attivo");
        }
    }

    // Step 8: selezione tema UI (il tema è già caricato in g_dc_model.settings.ui_theme_id
    //         da dc_settings_load; qui la splash registra il passo per coerenza con §10.2)
    ui_theme_activate(g_dc_model.settings.ui_theme_id);
    dc_boot_set_step(8, "Tema UI");

    // Step 9: tentativo connessione WiFi (asincrono — loop() gestisce il risultato)
    dc_boot_set_step(9, "Connessione WiFi...");
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

    // Step 10: boot completato — la splash carica Home al prossimo tick del timer
    dc_boot_set_step(10, "Pronto");
    dc_boot_complete();

    dc_admin_cli_init();
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
    dc_admin_cli_service();
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
