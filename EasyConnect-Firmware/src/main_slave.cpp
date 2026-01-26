#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_SHTC3.h>
#include <MS5837.h>
#include "Pins.h"
#include "GestioneMemoria.h"

Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();
MS5837 pressSensor;
Preferences memoria;
Impostazioni config;

// --- VERSIONE FIRMWARE SLAVE ---
const char* FW_VERSION = "1.0.0";

float tempSHTC3, humSHTC3, pressioneMS;
bool statoSicurezza = false;
unsigned long timerLetturaSensori = 0;
bool debugViewData = false; // Abilita visualizzazione dati debug (Enable debug data view)

void modoRicezione() { Serial1.flush(); digitalWrite(PIN_RS485_DIR, LOW); }
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(50); }

void gestisciMenuSeriale() {
    static String inputBuffer = ""; // Buffer per accumulare i caratteri

    while (Serial.available()) {
        char c = Serial.read();

        // Se riceviamo Invio (\n), elaboriamo il comando
        if (c == '\n') {
            String cmd = inputBuffer;
            inputBuffer = ""; // Reset del buffer
            cmd.trim();       // Rimuove spazi e \r
            
            if (cmd.length() == 0) return;

        // Visualizza il comando ricevuto (Show received command)
        Serial.print("> Ricevuto (Received): "); Serial.println(cmd);

        // Creiamo una copia maiuscola per i controlli (Case Insensitive)
        String cmdUpper = cmd;
        cmdUpper.toUpperCase();

        if (cmdUpper == "HELP" || cmdUpper == "?") {
            Serial.println("\n=== ELENCO COMANDI SLAVE (COMMAND LIST) ===");
            Serial.println("INFO          : Visualizza i parametri attuali (Show parameters)");
            Serial.println("SETIP x       : Imposta indirizzo RS485 (es. SETIP 1 o SETIP:1)");
            Serial.println("SETSERIAL x   : Imposta SN (es. SETSERIAL AABB)");
            Serial.println("SETGRP x      : Imposta Gruppo");
            Serial.println("SETMODE x     : 1:Temp/Hum, 2:Press, 3:Tutto");
            Serial.println("READIP        : Leggi IP");
            Serial.println("READSERIAL    : Leggi Seriale");
            Serial.println("READGRP       : Leggi Gruppo");
            Serial.println("READMODE      : Leggi Modalita");
            Serial.println("VIEWDATA      : Abilita visualizzazione dati");
            Serial.println("STOPDATA      : Disabilita visualizzazione dati");
            Serial.println("CLEARMEM      : Reset totale della memoria");
            Serial.println("===========================================\n");
        } 
        else if (cmdUpper == "INFO") {
            Serial.println("\n--- STATO ATTUALE SLAVE ---");
            Serial.printf("Configurata : %s\n", config.configurata ? "SI" : "NO (BLOCCATA)");
            Serial.printf("IP (485)    : %d\n", config.indirizzo485);
            Serial.printf("Seriale     : %s\n", config.serialeID);
            Serial.printf("Gruppo      : %d\n", config.gruppo);
            Serial.printf("Modalita    : %d\n", config.modalitaSensore);
            Serial.printf("Versione FW : %s\n", FW_VERSION);
            Serial.println("---------------------------\n");
        }
        // Gestione SETIP (spazio o due punti)
        else if (cmdUpper.startsWith("SETIP ") || cmdUpper.startsWith("SETIP:")) {
            String val = cmd.substring(6); // "SETIP " o "SETIP:" sono 6 char
            val.trim();
            config.indirizzo485 = val.toInt();
            memoria.putInt("addr", config.indirizzo485);
            Serial.println("OK: IP Salvato (IP Saved)");
            // Verifica se possiamo sbloccare la scheda (Check if we can unlock the board)
            if(config.indirizzo485 > 0 && String(config.serialeID) != "NON_SET") {
                 config.configurata = true;
                 memoria.putBool("set", true);
                 Serial.println("Configurazione Completa! (Configuration Complete!)");
            }
        }
        // Gestione SETSERIAL
        else if (cmdUpper.startsWith("SETSERIAL ") || cmdUpper.startsWith("SETSERIAL:")) {
            String s = cmd.substring(10); // "SETSERIAL " o "SETSERIAL:" sono 10 char
            s.trim();
            s.toCharArray(config.serialeID, 32);
            memoria.putString("serialeID", config.serialeID);
            Serial.println("OK: Seriale Salvato (Serial Saved)");
            if(config.indirizzo485 > 0) {
                 config.configurata = true;
                 memoria.putBool("set", true);
                 Serial.println("Configurazione Completa! (Configuration Complete!)");
            }
        }
        // Gestione SETGRP
        else if (cmdUpper.startsWith("SETGRP ") || cmdUpper.startsWith("SETGRP:")) {
            String val = cmd.substring(7);
            val.trim();
            config.gruppo = val.toInt();
            memoria.putInt("grp", config.gruppo);
            Serial.println("OK: Gruppo Salvato (Group Saved)");
        }
        // Gestione SETMODE
        else if (cmdUpper.startsWith("SETMODE ") || cmdUpper.startsWith("SETMODE:")) {
            String val = cmd.substring(8);
            val.trim();
            config.modalitaSensore = val.toInt();
            memoria.putInt("mode", config.modalitaSensore);
            Serial.println("OK: Modalita Salvata (Mode Saved)");
        }
        // Nuovi comandi di lettura (READ)
        else if (cmdUpper == "READIP") {
            Serial.printf("IP: %d\n", config.indirizzo485);
        }
        else if (cmdUpper == "READSERIAL") {
            Serial.printf("Seriale: %s\n", config.serialeID);
        }
        else if (cmdUpper == "READGRP") {
            Serial.printf("Gruppo: %d\n", config.gruppo);
        }
        else if (cmdUpper == "READMODE") {
            Serial.printf("Modalita: %d\n", config.modalitaSensore);
        }
        else if (cmdUpper == "VIEWDATA") {
            debugViewData = true;
            Serial.println("Visualizzazione Dati: ATTIVA (Data View: ON)");
        }
        else if (cmdUpper == "STOPDATA") {
            debugViewData = false;
            Serial.println("Visualizzazione Dati: DISATTIVA (Data View: OFF)");
        }
        // SAVE manuale rimosso come obbligo, ma mantenuto per compatibilità o riavvio
        // Manual SAVE removed as requirement, but kept for compatibility or reboot
        else if (cmdUpper == "SAVE") {
             Serial.println("Salvataggio effettuato. Riavvio... (Saved. Rebooting...)");
             config.configurata = true;
             memoria.putBool("set", true);
             delay(1000); ESP.restart();
        }
        else if (cmdUpper == "CLEARMEM") {
            memoria.clear();
            Serial.println("MEMORIA RESETTATA. Riavvio...");
            delay(1000); ESP.restart();
        }
        } else {
            // Accumula caratteri nel buffer (ignora carriage return \r)
            if (c != '\r') {
                inputBuffer += c;
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    memoria.begin("easy", false);
    
    // Caricamento dati
    config.configurata = memoria.getBool("set", false);
    config.indirizzo485 = memoria.getInt("addr", 0);
    config.gruppo = memoria.getInt("grp", 0);
    config.modalitaSensore = memoria.getInt("mode", 3);
    String s = memoria.getString("serialeID", "NON_SET");
    s.toCharArray(config.serialeID, 32);

    pinMode(PIN_LED_ROSSO, OUTPUT);
    pinMode(PIN_LED_VERDE, OUTPUT);

    Serial.println("\n--- EASY CONNECT SLAVE ---");

    // BLOCCO PRIMA CONFIGURAZIONE
    if (!config.configurata) {
        Serial.println("[!] SCHEDA NON CONFIGURATA. Inserire: SETIP, SETSERIAL...");
        Serial.println("Digitare 'HELP' per la lista. (Type 'HELP' for list.)");
        
        while (!config.configurata) {
            static unsigned long tL = 0;
            if (millis() - tL > 500) {
                digitalWrite(PIN_LED_ROSSO, !digitalRead(PIN_LED_ROSSO));
                tL = millis();
            }
            gestisciMenuSeriale();
        }
    } else {
        Serial.println("SCHEDA CONFIGURATA CORRETTAMENTE (BOARD CONFIGURED)");
        Serial.printf("IP: %d\n", config.indirizzo485);
        Serial.printf("Seriale: %s\n", config.serialeID);
        Serial.printf("Gruppo: %d\n", config.gruppo);
        Serial.printf("Modalita: %d\n", config.modalitaSensore);
    }

    digitalWrite(PIN_LED_ROSSO, LOW);

    Serial1.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    pinMode(PIN_RS485_DIR, OUTPUT);
    pinMode(PIN_SICUREZZA, INPUT_PULLUP);
    modoRicezione();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    shtc3.begin();
    if (pressSensor.init()) {
        pressSensor.setModel(MS5837::MS5837_02BA);
    }
}

void loop() {
    gestisciMenuSeriale();
    unsigned long ora = millis();

    if (ora - timerLetturaSensori >= 2000) {
        sensors_event_t humidity, temp;
        shtc3.getEvent(&humidity, &temp);
        tempSHTC3 = temp.temperature;
        humSHTC3 = humidity.relative_humidity;
        pressSensor.read();
        pressioneMS = pressSensor.pressure() * 100.0f; 
        statoSicurezza = (digitalRead(PIN_SICUREZZA) == LOW);
        timerLetturaSensori = ora;
    }

    if (Serial1.available()) {
        String richiesta = Serial1.readStringUntil('!');
        if (richiesta.startsWith("?")) {
            
            // Debug RX
            if (debugViewData) Serial.println("RX 485: " + richiesta);

            int ipChiesto = richiesta.substring(1).toInt();
            if (ipChiesto == config.indirizzo485) {
                char buffer[150];
                // Aggiunta versione FW alla fine della stringa
                snprintf(buffer, sizeof(buffer), "OK,%.2f,%.2f,%.2f,%d,%d,%s,%s!", tempSHTC3, humSHTC3, pressioneMS, statoSicurezza, config.gruppo, config.serialeID, FW_VERSION);
                
                modoTrasmissione();
                Serial1.print(buffer);
                modoRicezione();
                
                // Visualizza dati solo se abilitato (Show data only if enabled)
                if (debugViewData) Serial.printf("TX 485: %s\n", buffer);
            }
        }
        // Gestione cambio gruppo da remoto (Remote Group Change)
        // Formato atteso: GRP<ID>:<NUOVO_GRUPPO>! es. GRP5:2!
        else if (richiesta.startsWith("GRP")) {
            int duePunti = richiesta.indexOf(':');
            if (duePunti > 3) {
                int idRicevuto = richiesta.substring(3, duePunti).toInt();
                if (idRicevuto == config.indirizzo485) {
                    int nuovoGruppo = richiesta.substring(duePunti + 1).toInt();
                    config.gruppo = nuovoGruppo;
                    memoria.putInt("grp", config.gruppo);
                    if (debugViewData) Serial.printf("Gruppo aggiornato da Master: %d\n", config.gruppo);
                }
            }
        }
    }
}