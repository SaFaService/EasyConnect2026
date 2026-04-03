#ifndef MEMBRANE_KEYBOARD_H
#define MEMBRANE_KEYBOARD_H

#include <Arduino.h>
#include "Pins.h"
#include "Led.h"

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
