#include "Serial_Manager.h"
#include "GestioneMemoria.h"
#include "Pins.h"
#include <WiFi.h>
#include <Preferences.h>

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_master.cpp').
// Questo ci permette di accedervi e modificarle da questo file.
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern bool debugViewData;
extern bool debugViewApi;
// Anche le funzioni definite altrove possono essere dichiarate 'extern'.
extern void modoTrasmissione();
extern void modoRicezione();

// Funzione principale per la gestione del menu seriale del Master.
void Serial_Master_Menu() {
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

            Serial.print("> Ricevuto: "); Serial.println(cmd);
            String cmdUpper = cmd; // Crea una copia del comando
            cmdUpper.toUpperCase(); // e la converte in maiuscolo per non dover gestire maiuscole/minuscole.

            if (cmdUpper == "HELP" || cmdUpper == "?") {
                Serial.println("\n=== ELENCO COMANDI MASTER ===");
                Serial.println("INFO             : Visualizza configurazione");
                Serial.println("READSERIAL       : Leggi Seriale");
                Serial.println("READMODE         : Leggi Modo Master");
                Serial.println("READSIC          : Leggi stato Sicurezza");
                Serial.println("SETSERIAL x      : Imposta SN (es. SETSERIAL AABB)");
                Serial.println("SETMODE x        : 1:Standalone, 2:Rewamping");
                Serial.println("SETSIC ON/OFF    : Sicurezza locale (IO2)");
                Serial.println("SETAPIURL url    : Imposta URL API Antralux");
                Serial.println("SETAPIKEY key    : Imposta API Key Antralux");
                Serial.println("SETCUSTURL url   : Imposta URL API Cliente");
                Serial.println("SETCUSTKEY key   : Imposta API Key Cliente");
                Serial.println("SETSLAVEGRP id g : Cambia gruppo a uno slave (es. SETSLAVEGRP 5 2)");
                Serial.println("VIEWDATA         : Abilita visualizzazione dati RS485");
                Serial.println("STOPDATA         : Disabilita visualizzazione dati RS485");
                Serial.println("VIEWAPI          : Abilita log invio dati al server");
                Serial.println("STOPAPI          : Disabilita log invio dati al server");
                Serial.println("CLEARMEM         : Reset Fabbrica");
                Serial.println("=============================\n");
            }
            else if (cmdUpper == "INFO") {
                Serial.println("\n--- STATO ATTUALE MASTER ---");
                Serial.printf("Configurato : %s\n", config.configurata ? "SI" : "NO");
                Serial.printf("Seriale     : %s\n", config.serialeID);
                Serial.printf("Modo        : %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
                Serial.printf("Sicurezza   : %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
                Serial.printf("Versione FW : %s\n", FW_VERSION);
                Serial.printf("URL API Antralux : %s\n", config.apiUrl);
                Serial.printf("URL API Cliente  : %s\n", config.customerApiUrl);
                Serial.printf("API Key Antralux : %s\n", String(config.apiKey).length() > 0 ? "Impostata" : "NON IMPOSTATA");
                Serial.printf("API Key Cliente  : %s\n", String(config.customerApiKey).length() > 0 ? "Impostata" : "NON IMPOSTATA");
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("Rete WiFi   : %s\n", WiFi.SSID().c_str());
                    Serial.printf("Indirizzo IP: %s\n", WiFi.localIP().toString().c_str());
                } else {
                    Serial.println("Rete WiFi   : DISCONNESSO");
                }
                Serial.println("----------------------------\n");
            }
            // Blocco di comandi 'READ' per leggere la configurazione attuale.
            else if (cmdUpper == "READSERIAL") {
                Serial.printf("Seriale: %s\n", config.serialeID);
            }
            else if (cmdUpper == "READMODE") {
                Serial.printf("Modo: %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
            }
            else if (cmdUpper == "READSIC") {
                Serial.printf("Sicurezza: %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
            }
            // Blocco di comandi 'SET' per configurare la scheda.
            else if (cmdUpper.startsWith("SETSERIAL ") || cmdUpper.startsWith("SETSERIAL:")) {
                String s = cmd.substring(10); s.trim();
                s.toCharArray(config.serialeID, 32);
                memoria.putString("serialeID", config.serialeID);
                Serial.println("OK: Seriale Salvato");
                
                if (String(config.serialeID) != "NON_SET") {
                     config.configurata = true;
                     memoria.putBool("set", true);
                     Serial.println("Configurazione Completa! (Configuration Complete!)");
                }
            }
            else if (cmdUpper.startsWith("SETMODE ") || cmdUpper.startsWith("SETMODE:")) {
                String val = cmd.substring(8); val.trim();
                config.modalitaMaster = val.toInt();
                memoria.putInt("m_mode", config.modalitaMaster);
                Serial.println("OK: Modo Salvato");
            }
            else if (cmdUpper.startsWith("SETSIC ") || cmdUpper.startsWith("SETSIC:")) {
                String val = cmdUpper.substring(7); val.trim();
                config.usaSicurezzaLocale = (val == "ON");
                memoria.putBool("m_sic", config.usaSicurezzaLocale);
                Serial.println("OK: Sicurezza Salvata");
            }
            else if (cmdUpper.startsWith("SETAPIURL ")) {
                String val = cmd.substring(10); val.trim();
                val.toCharArray(config.apiUrl, 128);
                memoria.putString("api_url", val);
                Serial.println("OK: URL API Antralux salvato.");
            }
            else if (cmdUpper.startsWith("SETAPIKEY ")) {
                String val = cmd.substring(10); val.trim();
                val.toCharArray(config.apiKey, 65);
                memoria.putString("apiKey", val);
                Serial.println("OK: API Key Antralux salvata.");
            }
            else if (cmdUpper.startsWith("SETCUSTURL ")) {
                String val = cmd.substring(11); val.trim();
                val.toCharArray(config.customerApiUrl, 128);
                memoria.putString("custApiUrl", val);
                Serial.println("OK: URL API Cliente salvato.");
            }
            else if (cmdUpper.startsWith("SETCUSTKEY ")) {
                String val = cmd.substring(11); val.trim();
                val.toCharArray(config.customerApiKey, 65);
                memoria.putString("custApiKey", val);
                Serial.println("OK: API Key Cliente salvata.");
            }
            // Comando speciale per configurare uno slave da remoto.
            else if (cmdUpper.startsWith("SETSLAVEGRP ")) {
                int primoSpazio = cmdUpper.indexOf(' ', 12);
                if (primoSpazio > 0) {
                    String idStr = cmdUpper.substring(12, primoSpazio);
                    String grpStr = cmdUpper.substring(primoSpazio + 1);
                    int id = idStr.toInt();
                    int grp = grpStr.toInt();
                    
                    if (id > 0 && grp > 0) {
                        modoTrasmissione(); // Attiva la modalità di trasmissione RS485.
                        Serial1.printf("GRP%d:%d!", id, grp);
                        modoRicezione();
                        Serial.printf("Inviato comando cambio gruppo a Slave %d -> Gruppo %d\n", id, grp);
                    } else {
                        Serial.println("Errore parametri. Uso: SETSLAVEGRP <ID> <GRP>");
                    }
                }
            }
            // Comandi per il debug.
            else if (cmdUpper == "VIEWDATA") {
                debugViewData = true;
                Serial.println("Visualizzazione Dati RS485: ATTIVA");
            }
            else if (cmdUpper == "STOPDATA") {
                debugViewData = false;
                Serial.println("Visualizzazione Dati RS485: DISATTIVA");
            }
            else if (cmdUpper == "VIEWAPI") {
                debugViewApi = true;
                Serial.println("Visualizzazione Log API: ATTIVA");
            }
            else if (cmdUpper == "STOPAPI") {
                debugViewApi = false;
                Serial.println("Visualizzazione Log API: DISATTIVA");
            }
            // Comando per il reset di fabbrica.
            else if (cmdUpper == "CLEARMEM") {
                memoria.begin("easy", false); memoria.clear(); memoria.end();
                WiFi.disconnect(true, true);
                Serial.println("MEMORIA RESETTATA (FACTORY RESET). Riavvio...");
                delay(1000); ESP.restart();
            }
        } else { if (c != '\r') inputBuffer += c; } // Se non è un "a capo", aggiungi il carattere al buffer. Ignora '\r'.
    }
}