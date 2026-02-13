#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(1500); // tempo per enumerazione USB CDC su ESP32-C3
    Serial.println("\n[DIAG] Boot OK");
}

void loop() {
    Serial.println("Hello World");
    delay(3000);
}
