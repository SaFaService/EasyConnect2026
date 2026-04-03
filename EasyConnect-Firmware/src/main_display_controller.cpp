#include <Arduino.h>

const char* FW_VERSION = "0.0.1";

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- EASY CONNECT DISPLAY CONTROLLER (placeholder) ---");
    Serial.printf("FW: %s\n", FW_VERSION);
}

void loop() {
    delay(1000);
}
