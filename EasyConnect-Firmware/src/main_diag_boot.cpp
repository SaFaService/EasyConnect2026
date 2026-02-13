#include <Arduino.h>

// Pin usati nel tuo progetto: ci servono come indicatori fisici
static const int DIAG_PIN_5 = 5;
static const int DIAG_PIN_8 = 8;
static const int DIAG_PIN_9 = 9;

void setup() {
    pinMode(DIAG_PIN_5, OUTPUT);
    pinMode(DIAG_PIN_8, OUTPUT);
    pinMode(DIAG_PIN_9, OUTPUT);

    digitalWrite(DIAG_PIN_5, LOW);
    digitalWrite(DIAG_PIN_8, LOW);
    digitalWrite(DIAG_PIN_9, LOW);

    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[DIAG-HARD] setup() reached");
}

void loop() {
    static bool s = false;
    s = !s;

    digitalWrite(DIAG_PIN_5, s ? HIGH : LOW);
    digitalWrite(DIAG_PIN_8, s ? HIGH : LOW);
    digitalWrite(DIAG_PIN_9, s ? HIGH : LOW);

    Serial.println(s ? "[DIAG-HARD] tick=1" : "[DIAG-HARD] tick=0");
    delay(1000);
}
