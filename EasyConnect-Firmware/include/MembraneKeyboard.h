#ifndef MEMBRANE_KEYBOARD_H
#define MEMBRANE_KEYBOARD_H

#include <Arduino.h>
#include "Led.h"

// Mappatura Pin Tastiera Membrana (basata su nuova revisione)
// GPIO -> Pin Tastiera
#define MK_PIN_WIFI     10  // IO10 -> Pin 4 Tastiera (BAL1)
#define MK_PIN_SENS1    4   // IO4  -> Pin 3 Tastiera (BAL2)
#define MK_PIN_SENS2    6   // IO6  -> Pin 2 Tastiera (BAL3)
#define MK_PIN_AUX1     5   // IO5  -> Pin 1 Tastiera (BAL4)
#define MK_PIN_SAFETY   3   // IO3  -> Pin 7 Tastiera (SaftyAlarm)
#define MK_PIN_AUX2     1   // IO1  -> Pin 5 Tastiera (ExpLifeTime)
#define MK_PIN_BUTTON   0   // IO0  -> Pin 6 Tastiera (PULS ResetLifeTime)

class MembraneKeyboard {
private:
    Led _ledWifi;
    Led _ledSens1;
    Led _ledSens2;
    Led _ledAux1;
    Led _ledSafety;
    Led _ledAux2;

public:
    MembraneKeyboard();
    void begin();
    void update();
};

#endif