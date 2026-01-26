#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> // Per HTTPS
#include <HTTPClient.h>       // Per richieste API
#include <Preferences.h>
#include "Pins.h"
#include "GestioneMemoria.h"
#include "Calibration.h"

// Funzioni esterne
void setupMasterWiFi();
void gestisciWebEWiFi();

// --- VERSIONE FIRMWARE MASTER ---
const char* FW_VERSION = "1.0.1";

Preferences memoria;
Impostazioni config;
bool listaPerifericheAttive[101] = {false};
DatiSlave databaseSlave[101];
unsigned long timerScansione = 0;
unsigned long timerStampa = 0;
bool qualchePerifericaTrovata = false;
bool debugViewData = false; // Visualizzazione dati grezzi
bool debugViewApi = false;  // NUOVO: Visualizzazione log invio API
bool scansioneInCorso = false;
int statoInternet = 0;
float currentDeltaP = 0.0; // Variabile globale per DeltaP
unsigned long timerRemoteSync = 0; // Timer per invio dati remoto

void modoRicezione() { Serial1.flush(); digitalWrite(PIN_RS485_DIR, LOW); }
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(50); }

void gestisciMenuSeriale() {
    static String inputBuffer = "";

    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '\n') {
            String cmd = inputBuffer;
            inputBuffer = "";
            cmd.trim();
            if (cmd.length() == 0) return;

            Serial.print("> Ricevuto: "); Serial.println(cmd);
            String cmdUpper = cmd;
            cmdUpper.toUpperCase();

        if (cmdUpper == "HELP" || cmdUpper == "?") {
            Serial.println("\n=== ELENCO COMANDI MASTER ===");
            Serial.println("INFO             : Visualizza configurazione");
            Serial.println("READSERIAL       : Leggi Seriale");
            Serial.println("READMODE         : Leggi Modo Master");
            Serial.println("READSIC          : Leggi stato Sicurezza");
            Serial.println("SETSERIAL x      : Imposta SN (es. SETSERIAL AABB)");
            Serial.println("SETMODE x        : 1:Standalone, 2:Rewamping");
            Serial.println("SETSIC ON/OFF    : Sicurezza locale (IO2)");
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
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("Rete WiFi   : %s\n", WiFi.SSID().c_str());
                Serial.printf("Indirizzo IP: %s\n", WiFi.localIP().toString().c_str());
            } else {
                Serial.println("Rete WiFi   : DISCONNESSO");
            }
            Serial.println("----------------------------\n");
        }
        // Nuovi comandi READ
        else if (cmdUpper == "READSERIAL") {
            Serial.printf("Seriale: %s\n", config.serialeID);
        }
        else if (cmdUpper == "READMODE") {
            Serial.printf("Modo: %s\n", config.modalitaMaster == 2 ? "REWAMPING" : "STANDALONE");
        }
        else if (cmdUpper == "READSIC") {
            Serial.printf("Sicurezza: %s\n", config.usaSicurezzaLocale ? "ATTIVA (IO2)" : "DISABILITATA");
        }
        // Comandi SET
        else if (cmdUpper.startsWith("SETSERIAL ") || cmdUpper.startsWith("SETSERIAL:")) {
            String s = cmd.substring(10); s.trim();
            s.toCharArray(config.serialeID, 32);
            memoria.putString("serialeID", config.serialeID);
            Serial.println("OK: Seriale Salvato");
            
            // Verifica configurazione e sblocco automatico
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
        // Comando per cambiare gruppo a uno slave remoto
        else if (cmdUpper.startsWith("SETSLAVEGRP ")) {
            // Parsing: SETSLAVEGRP 5 2
            int primoSpazio = cmdUpper.indexOf(' ', 12);
            if (primoSpazio > 0) {
                String idStr = cmdUpper.substring(12, primoSpazio);
                String grpStr = cmdUpper.substring(primoSpazio + 1);
                int id = idStr.toInt();
                int grp = grpStr.toInt();
                
                if (id > 0 && grp > 0) {
                    modoTrasmissione();
                    Serial1.printf("GRP%d:%d!", id, grp);
                    modoRicezione();
                    Serial.printf("Inviato comando cambio gruppo a Slave %d -> Gruppo %d\n", id, grp);
                } else {
                    Serial.println("Errore parametri. Uso: SETSLAVEGRP <ID> <GRP>");
                }
            }
        }
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
        else if (cmdUpper == "CLEARMEM") {
            memoria.begin("easy", false); // Assicura apertura namespace
            memoria.clear();              // Cancella preferenze utente
            memoria.end();
            WiFi.disconnect(true, true);  // Cancella credenziali WiFi SDK
            Serial.println("MEMORIA RESETTATA (FACTORY RESET). Riavvio...");
            delay(1000); ESP.restart();
        }
        } else {
            if (c != '\r') inputBuffer += c;
        }
    }
}

void scansionaSlave() {
    Serial.println("[SCAN] Avvio scansione slave RS485...");
    scansioneInCorso = true;
    qualchePerifericaTrovata = false;
    
    // Resetta array
    for (int i = 0; i < 101; i++) listaPerifericheAttive[i] = false;

    // Scansiona indirizzi 1-30 (o più se necessario)
    for (int i = 1; i <= 30; i++) {
        modoTrasmissione();
        Serial1.printf("?%d!", i);
        modoRicezione();
        
        unsigned long startWait = millis();
        while (millis() - startWait < 50) { // 50ms timeout per slave
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (resp.startsWith("OK")) {
                    listaPerifericheAttive[i] = true;
                    qualchePerifericaTrovata = true;
                    Serial.printf("[SCAN] Trovato Slave ID: %d\n", i);
                }
                break; 
            }
        }
    }
    scansioneInCorso = false;
    Serial.println("[SCAN] Scansione terminata.");
    timerScansione = millis();
}

void parseDatiSlave(String payload, int address) {
    // Formato atteso: OK,T,H,P,S,G,SN,VER! (7 virgole)
    // O formato vecchio: OK,T,H,P,S,G,SN! (6 virgole)

    // Contiamo le virgole per determinare il formato
    int commaCount = 0;
    for (int k = 0; k < payload.length(); k++) {
        if (payload.charAt(k) == ',') {
            commaCount++;
        }
    }

    int v[7]; 
    int pos = 0;
    // Il primo campo è "OK", quindi la prima virgola è v[0]
    for (int j = 0; j < commaCount; j++) { 
        pos = payload.indexOf(',', pos + 1); 
        v[j] = pos; 
    }

    // Se abbiamo almeno 6 virgole, possiamo parsare i dati base
    if (commaCount >= 6) {
        databaseSlave[address].t = payload.substring(v[0] + 1, v[1]).toFloat();
        databaseSlave[address].h = payload.substring(v[1] + 1, v[2]).toFloat();
        databaseSlave[address].p = payload.substring(v[2] + 1, v[3]).toFloat();
        databaseSlave[address].sic = payload.substring(v[3] + 1, v[4]).toInt();
        databaseSlave[address].grp = payload.substring(v[4] + 1, v[5]).toInt();
        
        if (commaCount == 7) { // Formato nuovo con versione
            String sn = payload.substring(v[5] + 1, v[6]);
            sn.toCharArray(databaseSlave[address].sn, 32);
            String ver = payload.substring(v[6] + 1);
            ver.toCharArray(databaseSlave[address].version, 16);
        } else { // Formato vecchio senza versione
            String sn = payload.substring(v[5] + 1);
            sn.toCharArray(databaseSlave[address].sn, 32);
            strcpy(databaseSlave[address].version, "N/A"); // Mettiamo un placeholder
        }
    }
}

void setup() {
    Serial.begin(115200);
    memoria.begin("easy", false);
    
    config.configurata = memoria.getBool("set", false);
    config.modalitaMaster = memoria.getInt("m_mode", 2);
    config.usaSicurezzaLocale = memoria.getBool("m_sic", false);
    String s = memoria.isKey("serialeID") ? memoria.getString("serialeID") : "NON_SET";
    s.toCharArray(config.serialeID, 32);
    String k = memoria.isKey("apiKey") ? memoria.getString("apiKey") : "";
    k.toCharArray(config.apiKey, 65);

    // Inizializzazione Pin LED Esterni
    pinMode(PIN_LED_EXT_1, OUTPUT); pinMode(PIN_LED_EXT_2, OUTPUT);
    pinMode(PIN_LED_EXT_3, OUTPUT); pinMode(PIN_LED_EXT_4, OUTPUT);
    pinMode(PIN_LED_EXT_5, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT); pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_MASTER_SICUREZZA, INPUT_PULLUP);

    Serial.println("\n--- EASY CONNECT MASTER ---");

    while (!config.configurata) {
        static unsigned long tM = 0;
        if (millis() - tM > 2000) {
            Serial.println("[!] MASTER NON CONFIGURATO. Inserire: SETSERIAL...");
            Serial.println("Digitare 'HELP' per la lista comandi.");
            tM = millis();
        }
        gestisciMenuSeriale();
    }

    Serial1.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    pinMode(PIN_RS485_DIR, OUTPUT);
    modoRicezione();

    // Scansione iniziale all'avvio
    scansionaSlave();

    setupMasterWiFi();
}

// --- FUNZIONE INVIO DATI REMOTO (HTTPS) ---
void sendDataToRemoteServer() {
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) {
        if (debugViewApi) Serial.println("[API] Invio saltato: WiFi non connesso o URL API non impostato.");
        return;
    }

    if (debugViewApi) Serial.println("[API] Avvio invio dati al server...");

    WiFiClientSecure client;
    client.setInsecure(); // Accetta certificati self-signed o senza root CA (comune in IoT embedded)
    
    HTTPClient http;
    
    // Inizio connessione
    if (http.begin(client, config.apiUrl)) {
        if (debugViewApi) Serial.printf("[API] Connessione a: %s\n", config.apiUrl);
        http.addHeader("Content-Type", "application/json");
        // Autenticazione tramite API Key nell'header
        if (String(config.apiKey).length() > 0) {
            http.addHeader("X-API-KEY", config.apiKey);
            if (debugViewApi) Serial.println("[API] Header X-API-KEY aggiunto.");
        }

        // Costruzione JSON
        String json = "{";
        json += "\"master_sn\":\"" + String(config.serialeID) + "\",";
        json += "\"fw_ver\":\"" + String(FW_VERSION) + "\",";
        json += "\"delta_p\":" + String(currentDeltaP) + ",";
        json += "\"slaves\":[";
        
        bool first = true;
        for (int i = 1; i <= 100; i++) {
            if (listaPerifericheAttive[i]) {
                if (!first) json += ",";
                json += "{";
                json += "\"id\":" + String(i) + ",";
                json += "\"sn\":\"" + String(databaseSlave[i].sn) + "\",";
                json += "\"ver\":\"" + String(databaseSlave[i].version) + "\",";
                json += "\"grp\":" + String(databaseSlave[i].grp) + ",";
                json += "\"p\":" + String(databaseSlave[i].p) + ",";
                json += "\"t\":" + String(databaseSlave[i].t) + ",";
                json += "\"sic\":" + String(databaseSlave[i].sic);
                json += "}";
                first = false;
            }
        }
        json += "]}";

        if (debugViewApi) Serial.println("[API] JSON da inviare: " + json);

        int httpResponseCode = http.POST(json);
        
        if (debugViewApi) Serial.printf("[API] Codice risposta HTTP: %d\n", httpResponseCode);

        if (httpResponseCode > 0) {
            String response = http.getString();
            if (debugViewApi) Serial.println("[API] Risposta Server: " + response);
            
            // Qui potremmo parsare 'response' per ricevere comandi dal server
            // Es. se response contiene {"cmd":"reset"}, eseguiamo reset.
        } else {
            Serial.printf("[API] Errore invio: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        
        http.end();
    } else {
        if (debugViewApi) Serial.println("[API] Errore: Impossibile connettersi al server.");
    }
}

void loop() {
    gestisciMenuSeriale();
    gestisciWebEWiFi();
    calibrationLoop(); // Gestione campionamento calibrazione
    unsigned long ora = millis();

    // Scansione oraria (3600000 ms)
    if (ora - timerScansione >= 3600000) {
        scansionaSlave();
    }

    // Polling Slave Attivi (ogni 2 secondi)
    if (ora - timerStampa >= 2000) {
        for (int i = 1; i <= 100; i++) {
            if (listaPerifericheAttive[i]) {
                modoTrasmissione();
                Serial1.printf("?%d!", i);
                modoRicezione();
                unsigned long st = millis();
                while (millis() - st < 100) {
                    if (Serial1.available()) {
                        String d = Serial1.readStringUntil('!');
                        if (d.startsWith("OK")) {
                            if (debugViewData) Serial.println("RX: " + d);
                            parseDatiSlave(d, i);
                            
                            if (debugViewData) {
                                Serial.printf("ID:%d T:%.1f P:%.0f GRP:%d\n", i, databaseSlave[i].t, databaseSlave[i].p, databaseSlave[i].grp);
                            }
                        }
                        break;
                    }
                }
            }
        }
        timerStampa = ora;
    }

    // Sincronizzazione Remota (ogni 60 secondi)
    if (ora - timerRemoteSync >= 60000) {
        sendDataToRemoteServer();
        timerRemoteSync = ora;
    }
}