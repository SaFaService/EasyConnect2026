#include "RelayController.h"
#include <string.h>

namespace {
static uint8_t clampAttempts(uint8_t attempts) {
    return (attempts == 0) ? 1 : attempts;
}
}

const char* relayControlStateToString(RelayControlState state) {
    switch (state) {
        case RelayControlState::Off: return "OFF";
        case RelayControlState::WaitingFeedback: return "WAIT_FB";
        case RelayControlState::RetryDelay: return "RETRY";
        case RelayControlState::Running: return "RUNNING";
        case RelayControlState::Fault: return "FAULT";
        default: return "UNKNOWN";
    }
}

RelayController::RelayController()
    : cfg_(nullptr),
      counters_(nullptr),
      state_(RelayControlState::Off),
      commandOn_(false),
      relayOn_(false),
      validatedRun_(false),
      safetyClosed_(true),
      feedbackLevel_(false),
      feedbackMatched_(false),
      feedbackFaultLatched_(false),
      attemptsUsed_(0),
      stateTimestampMs_(0),
      lastUpdateMs_(0),
      pendingCountMs_(0),
      countersDirty_(false),
      lastEvent_("") {
    memset(&pins_, 0, sizeof(pins_));
}

void RelayController::begin(const RelayPins &pins, RelayConfig *cfg, RelayCounters *counters) {
    pins_ = pins;
    cfg_ = cfg;
    counters_ = counters;

    pinMode(pins_.relayOutputPin, OUTPUT);
    pinMode(pins_.feedbackInputPin, INPUT_PULLUP);
    pinMode(pins_.safetyInputPin, INPUT_PULLUP);

    setRelayOutput(false);
    safetyClosed_ = (digitalRead(pins_.safetyInputPin) == (pins_.safetyClosedIsLow ? LOW : HIGH));
    feedbackLevel_ = (digitalRead(pins_.feedbackInputPin) == HIGH);
    feedbackMatched_ = feedbackMatchesConfig();

    if (cfg_ && cfg_->bootRelayOn && !modeFollowsSafety()) {
        String ignored;
        commandRelay(true, "BOOT", ignored);
    }
}

void RelayController::setRelayOutput(bool on) {
    if (on == relayOn_) return;

    relayOn_ = on;
    const uint8_t level = (on == pins_.relayActiveHigh) ? HIGH : LOW;
    digitalWrite(pins_.relayOutputPin, level);

    if (on && counters_) {
        counters_->relayStarts++;
        countersDirty_ = true;
    }
}

bool RelayController::modeRequiresValidation() const {
    if (!cfg_) return false;
    return (cfg_->mode == RelayMode::UVC || cfg_->mode == RelayMode::Elettrostatico);
}

bool RelayController::modeFollowsSafety() const {
    if (!cfg_) return false;
    return (cfg_->mode == RelayMode::Comando);
}

bool RelayController::modeCountsHours() const {
    if (!cfg_) return false;
    return (cfg_->mode != RelayMode::Gas);
}

bool RelayController::feedbackMatchesConfig() const {
    if (!cfg_) return false;
    const bool expectPresence = (cfg_->feedback.logic == 0);
    return expectPresence ? feedbackLevel_ : !feedbackLevel_;
}

bool RelayController::isValidationActive() const {
    return state_ == RelayControlState::WaitingFeedback ||
           state_ == RelayControlState::RetryDelay ||
           state_ == RelayControlState::Running;
}

void RelayController::clearFeedbackFaultLatch() {
    feedbackFaultLatched_ = false;
}

void RelayController::setEvent(const String &eventText) {
    if (eventText.length() == 0) return;
    if (lastEvent_ == eventText) return;
    lastEvent_ = eventText;
}

bool RelayController::startOnSequence(const char *origin, String &outMessage) {
    if (!cfg_) {
        outMessage = "ERR:CFG_NULL";
        return false;
    }
    if (modeFollowsSafety()) {
        outMessage = String("ERR:MODE_COMANDO_AUTO (") + origin + ")";
        return false;
    }
    if (!safetyClosed_) {
        state_ = RelayControlState::Fault;
        commandOn_ = false;
        validatedRun_ = false;
        outMessage = String("ERR:SAFETY_OPEN (") + origin + ")";
        setEvent(strlen(cfg_->messages.safetyFault) ? cfg_->messages.safetyFault : "SAFETY_OPEN");
        return false;
    }

    if (feedbackFaultLatched_) {
        outMessage = String("ERR:FEEDBACK_FAULT_LATCHED (") + origin + ")";
        return false;
    }

    commandOn_ = true;
    if (modeRequiresValidation() && cfg_->feedback.enabled) {
        attemptsUsed_ = 1;
        validatedRun_ = false;
        setRelayOutput(true);
        state_ = RelayControlState::WaitingFeedback;
        stateTimestampMs_ = millis();
        outMessage = String("OK:ON_WAIT_FB (") + origin + ")";
    } else {
        validatedRun_ = true;
        setRelayOutput(true);
        state_ = RelayControlState::Running;
        stateTimestampMs_ = millis();
        outMessage = String("OK:ON (") + origin + ")";
    }
    return true;
}

void RelayController::update() {
    if (!cfg_ || !counters_) return;

    // Ciclo sequenziale non bloccante: aggiorna ingressi, stati e contatori.
    const unsigned long now = millis();
    if (lastUpdateMs_ == 0) lastUpdateMs_ = now;
    const unsigned long dt = now - lastUpdateMs_;
    lastUpdateMs_ = now;

    const bool prevSafetyClosed = safetyClosed_;
    const bool prevFeedbackMatched = feedbackMatched_;
    const RelayControlState prevState = state_;

    safetyClosed_ = (digitalRead(pins_.safetyInputPin) == (pins_.safetyClosedIsLow ? LOW : HIGH));
    feedbackLevel_ = (digitalRead(pins_.feedbackInputPin) == HIGH);
    feedbackMatched_ = feedbackMatchesConfig();

    if (modeFollowsSafety()) {
        commandOn_ = safetyClosed_;
        attemptsUsed_ = 0;
        validatedRun_ = safetyClosed_;
        clearFeedbackFaultLatch();

        if (safetyClosed_) {
            if (!relayOn_) setRelayOutput(true);
            state_ = RelayControlState::Running;
            if (!prevSafetyClosed || prevState != RelayControlState::Running) {
                setEvent("COMANDO_ON");
            }
        } else {
            if (relayOn_) setRelayOutput(false);
            state_ = RelayControlState::Off;
            if (prevSafetyClosed || prevState == RelayControlState::Running) {
                setEvent("COMANDO_OFF");
            }
        }
    } else if (!safetyClosed_) {
        // Safety aperta: spegnimento immediato in tutte le modalita'.
        if (relayOn_) setRelayOutput(false);
        commandOn_ = false;
        validatedRun_ = false;
        attemptsUsed_ = 0;
        state_ = RelayControlState::Fault;
        if (prevSafetyClosed || prevState != RelayControlState::Fault) {
            setEvent(strlen(cfg_->messages.safetyFault) ? cfg_->messages.safetyFault : "SAFETY_OPEN");
        }
    } else {
        if (!commandOn_) {
            if (relayOn_) setRelayOutput(false);
            validatedRun_ = false;
            attemptsUsed_ = 0;
            state_ = RelayControlState::Off;
        } else if (!modeRequiresValidation() || !cfg_->feedback.enabled) {
            // Modalita' Luce/Gas o feedback disabilitato: ON diretto.
            if (!relayOn_) setRelayOutput(true);
            validatedRun_ = true;
            state_ = RelayControlState::Running;
            if (cfg_->feedback.enabled && relayOn_ && !feedbackMatched_ && prevFeedbackMatched) {
                setEvent(strlen(cfg_->messages.feedbackFault) ? cfg_->messages.feedbackFault : "FEEDBACK_MISMATCH");
            }
        } else {
            // Modalita' UVC/Elettrostatico: ON -> attesa delay -> verifica -> retry.
            switch (state_) {
                case RelayControlState::Off:
                case RelayControlState::Fault: {
                    String ignored;
                    startOnSequence("AUTO", ignored);
                    break;
                }
                case RelayControlState::WaitingFeedback: {
                    const unsigned long waitMs = static_cast<unsigned long>(cfg_->feedback.checkDelaySec) * 1000UL;
                    if (now - stateTimestampMs_ >= waitMs) {
                        if (feedbackMatched_) {
                            validatedRun_ = true;
                            state_ = RelayControlState::Running;
                            stateTimestampMs_ = now;
                            setEvent("FEEDBACK_OK");
                        } else {
                            setRelayOutput(false);
                            validatedRun_ = false;
                            const uint8_t maxAttempts = clampAttempts(cfg_->feedback.attempts);
                            if (attemptsUsed_ < maxAttempts) {
                                state_ = RelayControlState::RetryDelay;
                                stateTimestampMs_ = now;
                                setEvent("FEEDBACK_RETRY");
                            } else {
                                commandOn_ = false;
                                state_ = RelayControlState::Fault;
                                feedbackFaultLatched_ = true;
                                setEvent(strlen(cfg_->messages.feedbackFault) ? cfg_->messages.feedbackFault : "FEEDBACK_FAULT");
                            }
                        }
                    }
                    break;
                }
                case RelayControlState::RetryDelay: {
                    if (now - stateTimestampMs_ >= 2000UL) {
                        attemptsUsed_++;
                        setRelayOutput(true);
                        state_ = RelayControlState::WaitingFeedback;
                        stateTimestampMs_ = now;
                    }
                    break;
                }
                case RelayControlState::Running: {
                    const unsigned long waitMs = static_cast<unsigned long>(cfg_->feedback.checkDelaySec) * 1000UL;
                    if (feedbackMatched_) {
                        // Feedback stabile: aggiorna il riferimento temporale per controllo continuo.
                        stateTimestampMs_ = now;
                        break;
                    }
                    if (now - stateTimestampMs_ < waitMs) {
                        // Mismatch breve/transitorio: aspetta il tempo configurato prima di dichiarare perdita feedback.
                        break;
                    }

                    // Perdita feedback in esercizio: rilancia la stessa sequenza tentativi prevista all'avvio.
                    setRelayOutput(false);
                    validatedRun_ = false;
                    attemptsUsed_ = 1;
                    const uint8_t maxAttempts = clampAttempts(cfg_->feedback.attempts);
                    if (attemptsUsed_ < maxAttempts) {
                        state_ = RelayControlState::RetryDelay;
                        stateTimestampMs_ = now;
                        setEvent("FEEDBACK_RETRY");
                    } else {
                        commandOn_ = false;
                        state_ = RelayControlState::Fault;
                        feedbackFaultLatched_ = true;
                        setEvent(strlen(cfg_->messages.feedbackFault) ? cfg_->messages.feedbackFault : "FEEDBACK_FAULT");
                    }
                    break;
                }
            }
        }
    }

    // Conteggio ore: solo con relay in ON valido e per modalita' che lo richiedono.
    if (isCountingOnTime()) {
        pendingCountMs_ += dt;
        while (pendingCountMs_ >= 1000ULL) {
            counters_->onSeconds++;
            pendingCountMs_ -= 1000ULL;
            countersDirty_ = true;
        }
    } else {
        pendingCountMs_ = 0;
    }
}

bool RelayController::commandRelay(bool on, const char *origin, String &outMessage) {
    if (!cfg_) {
        outMessage = "ERR:CFG_NULL";
        return false;
    }
    if (modeFollowsSafety()) {
        outMessage = String("ERR:MODE_COMANDO_AUTO (") + origin + ")";
        return false;
    }

    if (!on) {
        commandOn_ = false;
        validatedRun_ = false;
        attemptsUsed_ = 0;
        clearFeedbackFaultLatch();
        if (relayOn_) setRelayOutput(false);
        state_ = RelayControlState::Off;
        outMessage = String("OK:OFF (") + origin + ")";
        return true;
    }

    if (feedbackFaultLatched_) {
        outMessage = String("ERR:FEEDBACK_FAULT_LATCHED (") + origin + ")";
        return false;
    }

    if (commandOn_ && isValidationActive()) {
        outMessage = String("OK:ALREADY_ON (") + origin + ")";
        return true;
    }

    return startOnSequence(origin, outMessage);
}

bool RelayController::commandRelayDirect(bool on, const char *origin, String &outMessage) {
    if (!cfg_) {
        outMessage = "ERR:CFG_NULL";
        return false;
    }
    if (modeFollowsSafety()) {
        outMessage = String("ERR:MODE_COMANDO_AUTO (") + origin + ")";
        return false;
    }

    if (!on) {
        return commandRelay(false, origin, outMessage);
    }

    // Comando diretto: safety sempre attiva, bypass della validazione feedback.
    if (!safetyClosed_) {
        state_ = RelayControlState::Fault;
        commandOn_ = false;
        validatedRun_ = false;
        outMessage = String("ERR:SAFETY_OPEN (") + origin + ")";
        setEvent(strlen(cfg_->messages.safetyFault) ? cfg_->messages.safetyFault : "SAFETY_OPEN");
        return false;
    }

    commandOn_ = true;
    validatedRun_ = true;
    attemptsUsed_ = 0;
    clearFeedbackFaultLatch();
    setRelayOutput(true);
    state_ = RelayControlState::Running;
    stateTimestampMs_ = millis();
    outMessage = String("OK:ON_DIRECT (") + origin + ")";
    return true;
}

bool RelayController::commandToggle(const char *origin, String &outMessage) {
    return commandRelay(!relayOn_, origin, outMessage);
}

void RelayController::onConfigChanged() {
    if (!cfg_) return;

    if (cfg_->feedback.attempts == 0) cfg_->feedback.attempts = 1;
    if (cfg_->feedback.checkDelaySec == 0) cfg_->feedback.checkDelaySec = 1;
    clearFeedbackFaultLatch();

    if (modeFollowsSafety()) {
        commandOn_ = false;
        validatedRun_ = false;
        attemptsUsed_ = 0;
        return;
    }

    if (commandOn_) {
        String ignored;
        commandRelay(true, "CFG", ignored);
    }
}

bool RelayController::isRelayOn() const { return relayOn_; }
bool RelayController::isSafetyClosed() const { return safetyClosed_; }
bool RelayController::isFeedbackMatched() const { return feedbackMatched_; }
bool RelayController::isFeedbackSignalHigh() const { return feedbackLevel_; }
bool RelayController::isFeedbackExpectedHigh() const { return cfg_ ? (cfg_->feedback.logic == 0) : false; }
bool RelayController::isFeedbackFaultLatched() const { return feedbackFaultLatched_; }
bool RelayController::isFault() const { return state_ == RelayControlState::Fault; }
uint8_t RelayController::retriesUsed() const { return attemptsUsed_; }
RelayControlState RelayController::state() const { return state_; }
const String& RelayController::lastEvent() const { return lastEvent_; }
void RelayController::clearLastEvent() { lastEvent_ = ""; }

bool RelayController::isCountingOnTime() const {
    if (!relayOn_) return false;
    if (!modeCountsHours()) return false;
    if (modeRequiresValidation() && cfg_->feedback.enabled) return validatedRun_;
    return true;
}

bool RelayController::hasCountersDirty() const { return countersDirty_; }
void RelayController::markCountersSaved() { countersDirty_ = false; }
