#include "Led.h"

// Costruttore della classe Led.
// Inizializza le variabili private dell'oggetto.
Led::Led(uint8_t pin, bool activeLow) {
    _pin = pin;             // Salva il pin del LED.
    _activeLow = activeLow; // Salva la polarità del LED.
    _state = LED_OFF;       // Lo stato iniziale è sempre spento.
    _ledIsOn = false;       // Lo stato fisico iniziale è spento.
    _lastToggleTime = 0;    // Inizializza il timer per i lampeggi.
}

// Funzione di inizializzazione.
// Imposta il pin come OUTPUT e lo mette nello stato iniziale di "spento".
void Led::begin() {
    pinMode(_pin, OUTPUT);
    // Se activeLow è true, per spegnere il LED bisogna dare un segnale HIGH.
    // Altrimenti, per spegnere si dà un segnale LOW.
    digitalWrite(_pin, _activeLow ? HIGH : LOW); 
}

// Funzione per cambiare lo stato del LED (es. da fisso a lampeggiante).
void Led::setState(LedState newState) {
    if (_state == newState) {
        return; // Nessun cambio di stato, nessuna azione necessaria
    }

    _state = newState;
    _lastToggleTime = millis(); // Resetta il timer ad ogni cambio di stato per sincronia
    
    // A seconda del nuovo stato, imposta i parametri corretti.
    switch (_state) {
        case LED_OFF:
            _ledIsOn = false;
            digitalWrite(_pin, _activeLow ? HIGH : LOW); // Spegne il LED
            break;
        case LED_SOLID:
            _ledIsOn = true;
            digitalWrite(_pin, _activeLow ? LOW : HIGH); // Accende il LED
            break;
        case LED_BLINK_SLOW:
            _onTime = 200;  // 200ms acceso
            _offTime = 800; // 800ms spento
            _ledIsOn = true; // Il ciclo di lampeggio inizia con il LED acceso
            digitalWrite(_pin, _activeLow ? LOW : HIGH); // Accende subito il LED
            break;
        case LED_BLINK_FAST:
            _onTime = 250;  // 250ms acceso
            _offTime = 250; // 250ms spento
            _ledIsOn = true; // Il ciclo di lampeggio inizia con il LED acceso
            digitalWrite(_pin, _activeLow ? LOW : HIGH); // Accende subito il LED
            break;
    }
}

// Restituisce lo stato di funzionamento corrente (es. LED_SOLID).
LedState Led::getState() {
    return _state;
}

// Funzione di aggiornamento, da chiamare nel loop per far funzionare i lampeggi.
void Led::update() {
    // L'aggiornamento è necessario solo per gli stati lampeggianti
    if (_state != LED_BLINK_SLOW && _state != LED_BLINK_FAST) {
        return;
    }

    // Ottiene il tempo corrente per fare i calcoli.
    unsigned long currentTime = millis();
    // Determina l'intervallo da aspettare in base allo stato fisico attuale del LED (acceso o spento).
    unsigned long interval = _ledIsOn ? _onTime : _offTime;

    // Se è passato abbastanza tempo dall'ultimo cambio...
    if (currentTime - _lastToggleTime >= interval) {
        _lastToggleTime = currentTime; // ...aggiorna il tempo dell'ultimo cambio.
        _ledIsOn = !_ledIsOn;          // ...inverti lo stato fisico (da acceso a spento o viceversa).
        // ...e applica il nuovo stato fisico al pin, tenendo conto della polarità (activeLow).
        digitalWrite(_pin, _ledIsOn ? (_activeLow ? LOW : HIGH) : (_activeLow ? HIGH : LOW)); 
    }
}