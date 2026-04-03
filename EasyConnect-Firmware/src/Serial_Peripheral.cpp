#include "Serial_Manager.h"
#include "GestioneMemoria.h"
#include <Preferences.h>
#include <Update.h> // Per il test OTA

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_pressure_peripheral.cpp').
// Questo ci permette di accedervi e modificarle da questo file.
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern bool debugViewData;

static void printSettingsMenu() {
    Serial.println("=== SETTINGS MENU ===");
    Serial.println("SetIP x       : Imposta indirizzo RS485");
    Serial.println("SetSerial x   : Imposta SN");
    Serial.println("SetGroup x    : Imposta Gruppo");
    Serial.println("SetMode x     : 1:Temp/Hum, 2:Press, 3:Tutto");
    Serial.println("ReadIP        : Leggi IP");
    Serial.println("ReadSerial    : Leggi Seriale");
    Serial.println("ReadGroup     : Leggi Gruppo");
    Serial.println("ReadMode      : Leggi Modalita");
    Serial.println("=====================");
}

static void printLabMenu() {
    Serial.println("=== LAB MENU ===");
    Serial.println("View485       : Abilita monitor dati RS485");
    Serial.println("Stop485       : Disabilita monitor dati RS485");
    Serial.println("ClearMem      : Reset totale della memoria");
    Serial.println("TestOTA       : Esegue un test minimo di Update.begin()");
    Serial.println("================");
}

// Funzione principale per la gestione del menu seriale della Periferica.
void Serial_Peripheral_Menu() {
    // 'static' significa che la variabile mantiene il suo valore tra le chiamate alla funzione.
    // Usata per accumulare i caratteri in arrivo.
    static String inputBuffer = ""; 

    // Finché ci sono dati disponibili sulla porta seriale...
    while (Serial.available()) {
        char c = Serial.read(); // ...leggi un carattere alla volta.

        // Se il carattere è un "a capo" ('\n'), significa che il comando è completo.
        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() == 0) {
                continue;
            }
            Serial.println();
            String cmd = inputBuffer; // Copia il buffer in una nuova stringa.
            inputBuffer = "";         // Svuota il buffer per il prossimo comando.
            cmd.trim();               // Rimuove spazi e caratteri invisibili all'inizio e alla fine.
            
            if (cmd.length() == 0) return; // Se il comando è vuoto, esci.

            Serial.print("> Ricevuto (Received): "); Serial.println(cmd);
            String cmdUpper = cmd; // Crea una copia del comando
            cmdUpper.toUpperCase(); // e la converte in maiuscolo per non dover gestire maiuscole/minuscole.

            if (cmdUpper == "HELP" || cmdUpper == "?") {
                Serial.println("\n=== MENU COMANDI PERIFERICA ===");
                Serial.println("INFO          : Stato attuale della scheda");
                Serial.println("SETTINGSMENU  : Comandi di configurazione");
                Serial.println("LABMENU       : Comandi laboratorio/test");
                Serial.println("==========================");
                printSettingsMenu();
                printLabMenu();
                Serial.println();
            } 
            else if (cmdUpper == "SETTINGSMENU") {
                printSettingsMenu();
            }
            else if (cmdUpper == "LABMENU") {
                printLabMenu();
            }
            else if (cmdUpper == "INFO") {
                Serial.println("\n--- STATO ATTUALE PERIFERICA ---");
                Serial.printf("Configurata : %s\n", config.configurata ? "SI" : "NO (BLOCCATA)");
                Serial.printf("IP (485)    : %d\n", config.indirizzo485);
                Serial.printf("Seriale     : %s\n", config.serialeID);
                Serial.printf("Gruppo      : %d\n", config.gruppo);
                Serial.printf("Modalita    : %d\n", config.modalitaSensore);
                Serial.printf("Versione FW : %s\n", FW_VERSION);
                Serial.println("---------------------------\n");
            }
            // Blocco di comandi 'SET' per configurare la scheda.
            else if (cmdUpper.startsWith("SETIP ") || cmdUpper.startsWith("SETIP:")) {
                String val = cmd.substring(6); val.trim();
                config.indirizzo485 = val.toInt();
                memoria.putInt("addr", config.indirizzo485);
                Serial.println("OK: IP Salvato");
                if(config.indirizzo485 > 0 && String(config.serialeID) != "NON_SET") {
                     config.configurata = true; memoria.putBool("set", true);
                     Serial.println("Configurazione Completa!");
                }
            }
            else if (cmdUpper.startsWith("SETSERIAL ") || cmdUpper.startsWith("SETSERIAL:")) {
                String s = cmd.substring(10); s.trim();
                s.toCharArray(config.serialeID, 32);
                memoria.putString("serialeID", config.serialeID);
                Serial.println("OK: Seriale Salvato");
                if(config.indirizzo485 > 0) {
                     config.configurata = true; memoria.putBool("set", true);
                     Serial.println("Configurazione Completa!");
                }
            }
            else if (cmdUpper.startsWith("SETGROUP ") || cmdUpper.startsWith("SETGROUP:")
                  || cmdUpper.startsWith("SETGRP ") || cmdUpper.startsWith("SETGRP:")) {
                int sepIdx = cmd.indexOf(' ');
                if (sepIdx < 0) sepIdx = cmd.indexOf(':');
                String val = (sepIdx >= 0) ? cmd.substring(sepIdx + 1) : "";
                val.trim();
                if (cmdUpper.startsWith("SETGRP")) {
                    Serial.println("NOTE: comando legacy SETGRP accettato. Usa SetGroup.");
                }
                config.gruppo = val.toInt();
                memoria.putInt("grp", config.gruppo);
                Serial.println("OK: Gruppo Salvato");
            }
            else if (cmdUpper.startsWith("SETMODE ") || cmdUpper.startsWith("SETMODE:")) {
                String val = cmd.substring(8); val.trim();
                config.modalitaSensore = val.toInt();
                memoria.putInt("mode", config.modalitaSensore);
                Serial.println("OK: Modalita Salvata");
            }
            // Blocco di comandi 'READ' per leggere la configurazione attuale.
            else if (cmdUpper == "READIP") Serial.printf("IP: %d\n", config.indirizzo485);
            else if (cmdUpper == "READSERIAL") Serial.printf("Seriale: %s\n", config.serialeID);
            else if (cmdUpper == "READGROUP" || cmdUpper == "READGRP") {
                if (cmdUpper == "READGRP") {
                    Serial.println("NOTE: comando legacy READGRP accettato. Usa ReadGroup.");
                }
                Serial.printf("Gruppo: %d\n", config.gruppo);
            }
            else if (cmdUpper == "READMODE") Serial.printf("Modalita: %d\n", config.modalitaSensore);
            // Comandi per il debug.
            else if (cmdUpper == "VIEW485" || cmdUpper == "VIEWDATA") {
                if (cmdUpper == "VIEWDATA") {
                    Serial.println("NOTE: comando legacy VIEWDATA accettato. Usa View485.");
                }
                debugViewData = true;
                Serial.println("Monitor RS485: ATTIVO");
            }
            else if (cmdUpper == "STOP485" || cmdUpper == "STOPDATA") {
                if (cmdUpper == "STOPDATA") {
                    Serial.println("NOTE: comando legacy STOPDATA accettato. Usa Stop485.");
                }
                debugViewData = false;
                Serial.println("Monitor RS485: DISATTIVO");
            }
            // Comando per il reset di fabbrica.
            else if (cmdUpper == "CLEARMEM") {
                memoria.clear(); Serial.println("MEMORIA RESETTATA. Riavvio...");
                delay(1000); ESP.restart();
            }
            // Comando di test per isolare il problema di Update.begin()
            else if (cmdUpper == "TESTOTA") {
                Serial.println("[TEST] Avvio test di Update.begin()...");
                size_t testSize = 450000; // Una dimensione realistica
                Serial.printf("[TEST] Tentativo di avviare l'aggiornamento con una dimensione di %u bytes.\n", testSize);
                if (!Update.begin(testSize)) {
                    Serial.printf("[TEST] RISULTATO: FALLITO. Errore: %s\n", Update.errorString());
                } else {
                    Serial.println("[TEST] RISULTATO: SUCCESSO! Update.begin() ha funzionato.");
                    Update.abort(); // Annulliamo subito l'aggiornamento, era solo un test.
                }
            }
        } else if (c == 8 || c == 127) {
            if (inputBuffer.length() > 0) {
                inputBuffer.remove(inputBuffer.length() - 1);
                Serial.print("\b \b");
            }
        } else {
            if (inputBuffer.length() < 200) {
                inputBuffer += c;
                Serial.write(c);
            }
        }
    }
}

// Alias legacy mantenuto per compatibilita'.
void Serial_Slave_Menu() {
    Serial_Peripheral_Menu();
}
