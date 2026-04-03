#include "RelayTypes.h"
#include <string.h>

void relaySetDefaultConfig(RelayConfig &cfg) {
    cfg.configured = false;
    cfg.rs485Address = 1;
    cfg.group = 1;
    strncpy(cfg.serialId, "NON_SET", sizeof(cfg.serialId));
    cfg.serialId[sizeof(cfg.serialId) - 1] = '\0';
    cfg.mode = RelayMode::Luce;
    cfg.bootRelayOn = false;
    cfg.lifeLimitHours = 10000;

    cfg.feedback.enabled = true;
    cfg.feedback.logic = 0;
    cfg.feedback.checkDelaySec = 3;
    cfg.feedback.attempts = 3;

    strncpy(cfg.messages.feedbackFault, "FEEDBACK_FAULT", sizeof(cfg.messages.feedbackFault));
    cfg.messages.feedbackFault[sizeof(cfg.messages.feedbackFault) - 1] = '\0';

    strncpy(cfg.messages.safetyFault, "SAFETY_OPEN", sizeof(cfg.messages.safetyFault));
    cfg.messages.safetyFault[sizeof(cfg.messages.safetyFault) - 1] = '\0';
}

void relaySetDefaultCounters(RelayCounters &counters) {
    counters.relayStarts = 0;
    counters.onSeconds = 0;
}

bool relayIsValidSerialForRelay(const String &serial) {
    if (serial.length() != 12) return false; // YYYYMMTTXXXX
    for (int i = 0; i < serial.length(); i++) {
        if (!isDigit(serial.charAt(i))) return false;
    }

    const int year = serial.substring(0, 4).toInt();
    const int month = serial.substring(4, 6).toInt();
    const String typeCode = serial.substring(6, 8);
    const int sequence = serial.substring(8, 12).toInt();

    if (year < 2020 || year > 2099) return false;
    if (month < 1 || month > 12) return false;
    if (typeCode != "03") return false;  // 03 = Relay
    if (sequence < 1 || sequence > 9999) return false;

    return true;
}

bool relayIsValidSerialForRelay(const char *serial) {
    if (!serial) return false;
    return relayIsValidSerialForRelay(String(serial));
}

bool relayHasMinimumConfig(const RelayConfig &cfg) {
    if (cfg.rs485Address < 1 || cfg.rs485Address > 30) return false;
    return relayIsValidSerialForRelay(cfg.serialId);
}

const char* relayModeToString(RelayMode mode) {
    switch (mode) {
        case RelayMode::Luce: return "LUCE";
        case RelayMode::UVC: return "UVC";
        case RelayMode::Elettrostatico: return "ELETTROSTATICO";
        case RelayMode::Gas: return "GAS";
        case RelayMode::Comando: return "COMANDO";
        default: return "UNKNOWN";
    }
}

bool relayParseMode(const String &token, RelayMode &mode) {
    String t = token;
    t.trim();
    t.toUpperCase();

    if (t == "1" || t == "LUCE" || t == "LIGHT") {
        mode = RelayMode::Luce;
        return true;
    }
    if (t == "2" || t == "UVC") {
        mode = RelayMode::UVC;
        return true;
    }
    if (t == "3" || t == "ELETTROSTATICO" || t == "ELETTRO" || t == "ELECTROSTATIC") {
        mode = RelayMode::Elettrostatico;
        return true;
    }
    if (t == "4" || t == "GAS") {
        mode = RelayMode::Gas;
        return true;
    }
    if (t == "5" || t == "COMANDO" || t == "COMMAND" || t == "CMD") {
        mode = RelayMode::Comando;
        return true;
    }
    return false;
}

const char* relayBoolToOnOff(bool value) {
    return value ? "ON" : "OFF";
}
