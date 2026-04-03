#ifndef RELAY_TYPES_H
#define RELAY_TYPES_H

#include <Arduino.h>
#include <stdint.h>

enum class RelayMode : uint8_t {
    Luce = 1,
    UVC = 2,
    Elettrostatico = 3,
    Gas = 4,
    Comando = 5
};

struct RelayFeedbackConfig {
    bool enabled;
    uint8_t logic;         // 0 = presenza feedback (HIGH), 1 = assenza feedback (LOW)
    uint16_t checkDelaySec;
    uint8_t attempts;      // Numero tentativi totali (incluso il primo)
};

struct RelayMessageConfig {
    char feedbackFault[96];
    char safetyFault[96];
};

struct RelayConfig {
    bool configured;
    uint8_t rs485Address;
    uint8_t group;
    char serialId[32];
    RelayMode mode;
    bool bootRelayOn;
    uint32_t lifeLimitHours; // Soglia fine vita lampade (default 10000h, 0=disabilitata)
    RelayFeedbackConfig feedback;
    RelayMessageConfig messages;
};

struct RelayCounters {
    uint32_t relayStarts;  // Numero commutazioni OFF->ON
    uint64_t onSeconds;    // Tempo totale ON (solo per le modalita' che lo prevedono)
};

struct RelayPins {
    uint8_t relayOutputPin;
    uint8_t feedbackInputPin;
    uint8_t safetyInputPin;
    uint8_t rs485DirPin;
    uint8_t rs485TxPin;
    uint8_t rs485RxPin;
    uint8_t ledGreenPin;
    uint8_t ledRedPin;
    bool relayActiveHigh;
    bool safetyClosedIsLow;
};

void relaySetDefaultConfig(RelayConfig &cfg);
void relaySetDefaultCounters(RelayCounters &counters);
bool relayHasMinimumConfig(const RelayConfig &cfg);
bool relayIsValidSerialForRelay(const String &serial);
bool relayIsValidSerialForRelay(const char *serial);

const char* relayModeToString(RelayMode mode);
bool relayParseMode(const String &token, RelayMode &mode);
const char* relayBoolToOnOff(bool value);

#endif
