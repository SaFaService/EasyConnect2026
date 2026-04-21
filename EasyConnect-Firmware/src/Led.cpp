/**
 * ITA: Implementazione helper LED con stati statici e lampeggio temporizzato.
 * ENG: LED helper implementation with static and timed blink states.
 */
#include "Led.h"

/**
 * ITA: Costruttore: salva pin/polarita' e inizializza stato interno.
 * ENG: Constructor: stores pin/polarity and initializes internal state.
 */
Led::Led(uint8_t pin, bool activeLow) {
    _pin = pin;
    _activeLow = activeLow;
    _state = LED_OFF;
    _ledIsOn = false;
    _lastToggleTime = 0;
}

/**
 * ITA: Inizializza GPIO e forza LED spento.
 * ENG: Initializes GPIO and forces LED off.
 */
void Led::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, _activeLow ? HIGH : LOW);
}

/**
 * ITA: Cambia modalita' LED (OFF/SOLID/BLINK) e reimposta timer blink.
 * ENG: Changes LED mode (OFF/SOLID/BLINK) and resets blink timer.
 */
void Led::setState(LedState newState) {
    if (_state == newState) {
        return;
    }

    _state = newState;
    _lastToggleTime = millis();

    switch (_state) {
        case LED_OFF:
            _ledIsOn = false;
            digitalWrite(_pin, _activeLow ? HIGH : LOW);
            break;

        case LED_SOLID:
            _ledIsOn = true;
            digitalWrite(_pin, _activeLow ? LOW : HIGH);
            break;

        case LED_BLINK_SLOW:
            _onTime = 200;
            _offTime = 800;
            _ledIsOn = true;
            digitalWrite(_pin, _activeLow ? LOW : HIGH);
            break;

        case LED_BLINK_FAST:
            _onTime = 250;
            _offTime = 250;
            _ledIsOn = true;
            digitalWrite(_pin, _activeLow ? LOW : HIGH);
            break;
    }
}

/**
 * ITA: Restituisce lo stato logico corrente del LED.
 * ENG: Returns current logical LED state.
 */
LedState Led::getState() {
    return _state;
}

/**
 * ITA: Tick periodico da chiamare nel loop per far avanzare il blink.
 * ENG: Periodic tick to be called in loop to advance blinking.
 */
void Led::update() {
    if (_state != LED_BLINK_SLOW && _state != LED_BLINK_FAST) {
        return;
    }

    const unsigned long currentTime = millis();
    const unsigned long interval = _ledIsOn ? _onTime : _offTime;

    if (currentTime - _lastToggleTime >= interval) {
        _lastToggleTime = currentTime;
        _ledIsOn = !_ledIsOn;

        digitalWrite(
            _pin,
            _ledIsOn
                ? (_activeLow ? LOW : HIGH)
                : (_activeLow ? HIGH : LOW));
    }
}
