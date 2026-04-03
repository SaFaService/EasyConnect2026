#include <Arduino.h>
#include "Pins.h"

void setup() {
    pinMode(PIN_DIAG_BOOT_1, OUTPUT);
    pinMode(PIN_DIAG_BOOT_2, OUTPUT);
    pinMode(PIN_DIAG_BOOT_3, OUTPUT);

    digitalWrite(PIN_DIAG_BOOT_1, LOW);
    digitalWrite(PIN_DIAG_BOOT_2, LOW);
    digitalWrite(PIN_DIAG_BOOT_3, LOW);

    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[DIAG-HARD] setup() reached");
}

void loop() {
    static bool s = false;
    s = !s;

    digitalWrite(PIN_DIAG_BOOT_1, s ? HIGH : LOW);
    digitalWrite(PIN_DIAG_BOOT_2, s ? HIGH : LOW);
    digitalWrite(PIN_DIAG_BOOT_3, s ? HIGH : LOW);

    Serial.println(s ? "[DIAG-HARD] tick=1" : "[DIAG-HARD] tick=0");
    delay(1000);
}
