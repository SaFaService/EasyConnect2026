#include <Arduino.h>
#include "Pins.h"
#include "Led.h"
#include "RelayTypes.h"
#include "RelayStorage.h"
#include "RelayController.h"
#include "RelaySerial.h"
#include "RelayRS485.h"

const char* RELAY_FW_VERSION = "0.1.6";

static const RelayPins RELAY_PINS = {
    PIN_RELAY_OUTPUT,
    PIN_RELAY_FEEDBACK,
    PIN_RELAY_SAFETY,
    PIN_RS485_DIR,
    PIN_RS485_TX,
    PIN_RS485_RX,
    PIN_RELAY_LED_GREEN,
    PIN_RELAY_LED_RED,
    true,
    true
};

RelayStorage g_storage;
RelayController g_controller;
RelaySerialInterface g_serialUi;
RelayRs485Interface g_rs485;

RelayConfig g_cfg;
RelayCounters g_counters;

bool g_debug485 = false;

Led g_ledGreen(PIN_RELAY_LED_GREEN);
Led g_ledRed(PIN_RELAY_LED_RED, true);

unsigned long g_lastCounterSaveMs = 0;
static const unsigned long UVC_RS485_FAILSAFE_TIMEOUT_MS = 60000UL;

static void enforceUvcRs485Failsafe() {
    if (g_cfg.mode != RelayMode::UVC) {
        g_rs485.clearUvcRemoteActivation();
        return;
    }

    if (!g_rs485.hasUvcRemoteActivation()) return;

    if (!g_controller.isRelayOn()) {
        g_rs485.clearUvcRemoteActivation();
        return;
    }

    const unsigned long lastDirected = g_rs485.lastDirectedActivityMs();
    const unsigned long now = millis();
    if (lastDirected > 0 && (now - lastDirected) < UVC_RS485_FAILSAFE_TIMEOUT_MS) return;

    String result;
    g_controller.commandRelay(false, "RS485_TIMEOUT", result);
    g_rs485.clearUvcRemoteActivation();
    Serial.printf("[SAFE] RS485 timeout UVC: relay OFF (%s)\n", result.c_str());
}

static void updateStatusLeds() {
    // LED VERDE: stessa logica della scheda pressione (stato comunicazione RS485).
    const unsigned long now = millis();
    const unsigned long lastDirected = g_rs485.lastDirectedActivityMs();
    const unsigned long lastAny = g_rs485.lastAnyActivityMs();
    if (lastDirected > 0 && (now - lastDirected) < 5000UL) {
        g_ledGreen.setState(LED_BLINK_SLOW);
    } else if (lastAny > 0 && (now - lastAny) < 5000UL) {
        g_ledGreen.setState(LED_SOLID);
    } else {
        g_ledGreen.setState(LED_BLINK_FAST);
    }

    // LED ROSSO: stessa logica pressione per safety + fault critici relay.
    if (!g_cfg.configured) {
        g_ledRed.setState(LED_BLINK_FAST);
    } else if (!g_controller.isSafetyClosed()) {
        g_ledRed.setState(LED_BLINK_SLOW);
    } else if (g_controller.isFault()) {
        g_ledRed.setState(LED_BLINK_FAST);
    } else {
        g_ledRed.setState(LED_SOLID);
    }

    g_ledGreen.update();
    g_ledRed.update();
}

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("\n--- EASY CONNECT RELAY ---");
    Serial.printf("FW: %s\n", RELAY_FW_VERSION);
    Serial.printf("PIN_OUT=%d | PIN_FB=%d | PIN_SAFE=%d | LED_R=%d | LED_G=%d\n",
                  PIN_RELAY_OUTPUT, PIN_RELAY_FEEDBACK, PIN_RELAY_SAFETY, PIN_RELAY_LED_RED, PIN_RELAY_LED_GREEN);

    relaySetDefaultConfig(g_cfg);
    relaySetDefaultCounters(g_counters);

    const bool storageReady = g_storage.begin();
    if (!storageReady) {
        Serial.println("[ERR] Preferences non disponibili. Avvio con default RAM.");
    } else {
        g_storage.load(g_cfg, g_counters);
    }

    pinMode(PIN_RELAY_LED_GREEN, OUTPUT);
    pinMode(PIN_RELAY_LED_RED, OUTPUT);
    g_ledGreen.begin();
    g_ledRed.begin();

    g_controller.begin(RELAY_PINS, &g_cfg, &g_counters);
    g_serialUi.begin(RELAY_FW_VERSION, &g_cfg, &g_counters, &g_controller, &g_storage, &g_debug485);
    g_rs485.begin(RELAY_FW_VERSION, RELAY_PINS, &g_cfg, &g_counters, &g_controller, &g_storage, &g_debug485);

    if (!g_cfg.configured) {
        Serial.println("[INFO] Relay non ancora configurata (SETIP + SETSERIAL).");
    } else {
        Serial.printf("[INFO] Config caricata: IP=%u MODE=%s SERIAL=%s\n",
                      g_cfg.rs485Address, relayModeToString(g_cfg.mode), g_cfg.serialId);
    }

    Serial.println("Digitare HELP su seriale USB per menu comandi.");
}

void loop() {
    g_serialUi.update();
    g_rs485.update();
    g_controller.update();
    enforceUvcRs485Failsafe();

    const String &event = g_controller.lastEvent();
    if (event.length() > 0) {
        Serial.printf("[EVENT] %s\n", event.c_str());
        g_controller.clearLastEvent();
    }

    const unsigned long now = millis();
    const bool shouldPeriodicSave = (now - g_lastCounterSaveMs >= 15000UL);
    const bool shouldStateSave = g_controller.hasCountersDirty() && !g_controller.isRelayOn();
    if (g_controller.hasCountersDirty() && (shouldPeriodicSave || shouldStateSave)) {
        g_storage.saveCounters(g_counters);
        g_controller.markCountersSaved();
        g_lastCounterSaveMs = now;
    }

    updateStatusLeds();
    delay(2);
}
