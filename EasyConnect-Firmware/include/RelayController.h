#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>
#include "RelayTypes.h"

enum class RelayControlState : uint8_t {
    Off = 0,             // Comando relay OFF
    WaitingFeedback = 1, // Relay ON, attesa tempo verifica feedback
    RetryDelay = 2,      // Relay OFF, attesa 2s prima del retry
    Running = 3,         // Relay ON in esercizio valido
    Fault = 4            // Safety aperta o fault feedback definitivo
};

const char* relayControlStateToString(RelayControlState state);

class RelayController {
public:
    RelayController();

    void begin(const RelayPins &pins, RelayConfig *cfg, RelayCounters *counters);
    void update();

    bool commandRelay(bool on, const char *origin, String &outMessage);
    bool commandRelayDirect(bool on, const char *origin, String &outMessage);
    bool commandToggle(const char *origin, String &outMessage);
    void onConfigChanged();

    bool isRelayOn() const;
    bool isSafetyClosed() const;
    bool isFeedbackMatched() const;
    bool isFeedbackSignalHigh() const;
    bool isFeedbackExpectedHigh() const;
    bool isFeedbackFaultLatched() const;
    bool isFault() const;
    bool isCountingOnTime() const;
    uint8_t retriesUsed() const;
    RelayControlState state() const;

    const String& lastEvent() const;
    void clearLastEvent();

    bool hasCountersDirty() const;
    void markCountersSaved();

private:
    RelayPins pins_;
    RelayConfig *cfg_;
    RelayCounters *counters_;

    RelayControlState state_;
    bool commandOn_;
    bool relayOn_;
    bool validatedRun_;
    bool safetyClosed_;
    bool feedbackLevel_;
    bool feedbackMatched_;
    bool feedbackFaultLatched_;
    uint8_t attemptsUsed_;
    unsigned long stateTimestampMs_;
    unsigned long lastUpdateMs_;
    uint64_t pendingCountMs_;
    bool countersDirty_;
    String lastEvent_;

    // Primitive hardware e regole modalita'.
    void setRelayOutput(bool on);
    bool modeFollowsSafety() const;
    bool modeRequiresValidation() const;
    bool modeCountsHours() const;
    bool feedbackMatchesConfig() const;
    bool isValidationActive() const;
    void clearFeedbackFaultLatch();

    // Eventi sintetici per log seriale/diagnostica.
    void setEvent(const String &eventText);

    // Avvio sequenza ON: con o senza verifica feedback in base a mode/config.
    bool startOnSequence(const char *origin, String &outMessage);
};

#endif
