#ifndef LED_H
#define LED_H

#include <Arduino.h>

// Enum (elenco di costanti) che definisce i possibili stati di funzionamento di un LED.
enum LedState {
    LED_OFF,          // Stato: LED spento.
    LED_SOLID,        // Stato: LED acceso fisso.
    LED_BLINK_SLOW,   // Stato: Lampeggio lento (200ms ON, 800ms OFF).
    LED_BLINK_FAST    // Stato: Lampeggio veloce (250ms ON, 250ms OFF).
};

class Led {
private:
    uint8_t _pin;           // Il pin fisico a cui è collegato il LED.
    bool _activeLow;        // Flag per gestire i LED con logica inversa (si accendono con LOW).
    LedState _state;        // Lo stato corrente del LED (spento, fisso, lampeggiante...).
    bool _ledIsOn;          // Stato fisico del LED (acceso/spento), usato per i lampeggi.
    unsigned long _lastToggleTime; // Memorizza l'ultimo cambio di stato per calcolare gli intervalli.
    unsigned int _onTime;   // Durata (in ms) dello stato ON durante un lampeggio.
    unsigned int _offTime;  // Durata (in ms) dello stato OFF durante un lampeggio.

public:
    // Costruttore della classe.
    // pin: il numero del pin a cui è collegato il LED.
    // activeLow: impostare a 'true' se il LED si accende quando il pin è a livello BASSO (LOW).
    Led(uint8_t pin, bool activeLow = false);

    // Funzione di inizializzazione, da chiamare nel setup().
    void begin();

    // Imposta un nuovo stato di funzionamento per il LED (es. LED_BLINK_FAST).
    void setState(LedState newState);

    // Restituisce lo stato di funzionamento corrente del LED.
    LedState getState();

    // Funzione di aggiornamento, da chiamare ripetutamente nel loop().
    // Gestisce i cicli di lampeggio.
    void update();
};

#endif // LED_H