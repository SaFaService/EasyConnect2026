#include "Serial_Manager.h"
#include "GestioneMemoria.h"
#include <Preferences.h>

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_slave.cpp').
// Questo ci permette di accedervi e modificarle da questo file.
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern bool debugViewData;

// Funzione principale per la gestione del menu seriale dello Slave.
void Serial_Slave_Menu() {
    // 'static' significa che la variabile mantiene il suo valore tra le chiamate alla funzione.
    // Usata per accumulare i caratteri in arrivo.
    static String inputBuffer = ""; 

    // Finché ci sono dati disponibili sulla porta seriale...
    while (Serial.available()) {
        char c = Serial.read(); // ...leggi un carattere alla volta.

        // Se il carattere è un "a capo" ('\n'), significa che il comando è completo.
        if (c == '\n') {
            String cmd = inputBuffer; // Copia il buffer in una nuova stringa.
            inputBuffer = "";         // Svuota il buffer per il prossimo comando.
            cmd.trim();               // Rimuove spazi e caratteri invisibili all'inizio e alla fine.
            
            if (cmd.length() == 0) return; // Se il comando è vuoto, esci.

            Serial.print("> Ricevuto (Received): "); Serial.println(cmd);
            String cmdUpper = cmd; // Crea una copia del comando
            cmdUpper.toUpperCase(); // e la converte in maiuscolo per non dover gestire maiuscole/minuscole.

            if (cmdUpper == "HELP" || cmdUpper == "?") {
                Serial.println("\n=== ELENCO COMANDI SLAVE (COMMAND LIST) ===");
                Serial.println("INFO          : Visualizza i parametri attuali");
                Serial.println("SETIP x       : Imposta indirizzo RS485");
                Serial.println("SETSERIAL x   : Imposta SN");
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
            else if (cmdUpper.startsWith("SETGRP ") || cmdUpper.startsWith("SETGRP:")) {
                String val = cmd.substring(7); val.trim();
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
            else if (cmdUpper == "READGRP") Serial.printf("Gruppo: %d\n", config.gruppo);
            else if (cmdUpper == "READMODE") Serial.printf("Modalita: %d\n", config.modalitaSensore);
            // Comandi per il debug.
            else if (cmdUpper == "VIEWDATA") { debugViewData = true; Serial.println("Visualizzazione Dati: ATTIVA"); }
            else if (cmdUpper == "STOPDATA") { debugViewData = false; Serial.println("Visualizzazione Dati: DISATTIVA"); }
            // Comando per il reset di fabbrica.
            else if (cmdUpper == "CLEARMEM") {
                memoria.clear(); Serial.println("MEMORIA RESETTATA. Riavvio...");
                delay(1000); ESP.restart();
            }
        } else { if (c != '\r') inputBuffer += c; } // Se non è un "a capo", aggiungi il carattere al buffer. Ignora '\r'.
    }
}