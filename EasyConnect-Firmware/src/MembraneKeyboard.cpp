#include "MembraneKeyboard.h"
#include "GestioneMemoria.h"
#include <WiFi.h>
#include "Pins.h"

// Riferimenti alle variabili globali del Master
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern Impostazioni config;
extern int statoInternet;

MembraneKeyboard::MembraneKeyboard() :
    _ledWifi(MK_PIN_WIFI),
    _ledSens1(MK_PIN_SENS1),
    _ledSens2(MK_PIN_SENS2),
    _ledAux1(MK_PIN_AUX1),
    _ledSafety(MK_PIN_SAFETY),
    _ledAux2(MK_PIN_AUX2)
{
}

void MembraneKeyboard::begin() {
    _ledWifi.begin();
    _ledSens1.begin();
    _ledSens2.begin();
    _ledAux1.begin();
    _ledSafety.begin();
    _ledAux2.begin();
    
    // Il pin 7 è un pulsante (Input)
    pinMode(MK_PIN_BUTTON, INPUT_PULLUP);
}

void MembraneKeyboard::update() {
    // --- LOGICA PIN 1: WIFI ---
    if (WiFi.status() == WL_CONNECTED) {
        _ledWifi.setState(LED_SOLID); // Wifi Connesso
    } else if ((WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) && WiFi.status() != WL_CONNECTED) {
        _ledWifi.setState(LED_BLINK_FAST); // Wifi Disconnesso ma AP Attivo
    } else {
        _ledWifi.setState(LED_OFF); // AP Disattivato e WIFI Disconnesso
    }

    // --- LOGICA SENSORI (PIN 2 e 3) ---
    bool g1_found = false;
    bool g1_alarm = false;
    bool g1_zero = false;
    bool g1_noresponse = false;

    bool g2_found = false;
    bool g2_alarm = false;
    bool g2_zero = false;
    bool g2_noresponse = false;

    bool anySlaveAlarm = false;
    bool allSlavesAlarm = true;
    int activeSlavesCount = 0;

    unsigned long now = millis();
    const unsigned long TIMEOUT_NO_RESPONSE = 10000; // 10 secondi di timeout

    for (int i = 1; i <= 100; i++) {
        if (listaPerifericheAttive[i]) {
            activeSlavesCount++;
            bool isAlarm = (databaseSlave[i].sic == 1);
            bool isNotResponding = (now - databaseSlave[i].lastResponseTime > TIMEOUT_NO_RESPONSE);

            if (isAlarm) anySlaveAlarm = true;
            else allSlavesAlarm = false;

            if (databaseSlave[i].grp == 1) {
                g1_found = true;
                if (isAlarm) g1_alarm = true;
                if (databaseSlave[i].p < 1.0f) g1_zero = true; // Tolleranza per valori float
                if (isNotResponding) g1_noresponse = true;
            } else if (databaseSlave[i].grp == 2) {
                g2_found = true;
                if (isAlarm) g2_alarm = true;
                if (databaseSlave[i].p < 1.0f) g2_zero = true; // Tolleranza per valori float
                if (isNotResponding) g2_noresponse = true;
            }
        }
    }
    if (activeSlavesCount == 0) allSlavesAlarm = false;

    // Pin 3 (BAL2): Scheda Sensore 1
    if (g1_found) {
        if (g1_alarm) {
            _ledSens1.setState(LED_OFF); // Priorità 1: Allarme Sicurezza
        } else if (g1_noresponse) {
            _ledSens1.setState(LED_BLINK_FAST); // Priorità 2: Non risponde
        } else if (g1_zero) {
            _ledSens1.setState(LED_BLINK_SLOW); // Priorità 3: Pressione a zero
        } else {
            _ledSens1.setState(LED_SOLID); // Funzionamento OK
        }
    } else {
        _ledSens1.setState(LED_OFF); // Non trovata
    }

    // Pin 2 (BAL3): Scheda Sensore 2
    if (g2_found) {
        if (g2_alarm) {
            _ledSens2.setState(LED_OFF);
        } else if (g2_noresponse) {
            _ledSens2.setState(LED_BLINK_FAST);
        } else if (g2_zero) {
            _ledSens2.setState(LED_BLINK_SLOW);
        } else {
            _ledSens2.setState(LED_SOLID);
        }
    } else {
        _ledSens2.setState(LED_OFF);
    }

    // --- LOGICA PIN 7 (SaftyAlarm): SICUREZZA ---
    bool masterSafetyAlarm = (digitalRead(PIN_MASTER_SICUREZZA) == HIGH);
    bool totalSystemAlarm = masterSafetyAlarm && allSlavesAlarm && (activeSlavesCount > 0);

    if (totalSystemAlarm) {
        // Priorità 1: Allarme Totale. Tutte le sicurezze (Master + Slave) attive.
        _ledSafety.setState(LED_SOLID);
    } else if (masterSafetyAlarm) {
        // Priorità 2: Allarme Master. L'ingresso di sicurezza locale è attivo.
        _ledSafety.setState(LED_BLINK_FAST);
    } else if (anySlaveAlarm) {
        // Priorità 3: Allarme Parziale. Almeno una slave in allarme (ma non il master).
        _ledSafety.setState(LED_BLINK_SLOW);
    } else {
        // Nessun allarme
        _ledSafety.setState(LED_OFF);
    }

    // Pin 1 (BAL4), Pin 5 (ExpLifeTime): Futura Utilizzazione (Spenti)
    _ledAux1.setState(LED_OFF);
    _ledAux2.setState(LED_OFF);

    // Aggiornamento ciclico dei LED (gestione lampeggi)
    _ledWifi.update();
    _ledSens1.update();
    _ledSens2.update();
    _ledAux1.update();
    _ledSafety.update();
    _ledAux2.update();
}
