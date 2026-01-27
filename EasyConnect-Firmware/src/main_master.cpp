#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h> // Per HTTPS
#include <HTTPClient.h>       // Per richieste API
#include <Preferences.h>
#include <ArduinoOTA.h>       // Libreria per aggiornamento OTA
#include "Pins.h"
#include "GestioneMemoria.h"
#include <HTTPUpdate.h>       // Libreria per aggiornamento HTTP/HTTPS
#include "Calibration.h"
#include "Led.h"
#include "Serial_Manager.h"
#include "RS485_Manager.h"
#include "MembraneKeyboard.h"

// --- FUNZIONI DEFINITE IN ALTRI FILE ---
void setupMasterWiFi();
void gestisciWebEWiFi();

// --- VERSIONE FIRMWARE MASTER ---
const char* FW_VERSION = "1.0.1";

// --- OGGETTI GLOBALI ---
Led greenLed(PIN_LED_VERDE);
Led redLed(PIN_LED_ROSSO);

Preferences memoria;
Impostazioni config;

// Oggetto gestione tastiera membrana
MembraneKeyboard membraneKey;

// --- VARIABILI GLOBALI DI STATO ---
bool listaPerifericheAttive[101] = {false}; // Array che tiene traccia degli slave trovati sulla rete.
DatiSlave databaseSlave[101];               // Array di strutture per memorizzare i dati ricevuti da ogni slave.
unsigned long timerScansione = 0;           // Timer per la scansione oraria della rete RS485.
unsigned long timerStampa = 0;              // Timer per il polling (interrogazione) periodico degli slave.
bool qualchePerifericaTrovata = false;      // Flag per sapere se almeno uno slave è stato trovato.
bool debugViewData = false;                 // Flag per abilitare/disabilitare i log di debug RS485.
bool debugViewApi = false;                  // Flag per abilitare/disabilitare i log di debug per l'invio dati al server.
bool scansioneInCorso = false;              // Flag che indica se è in corso una scansione RS485.
int statoInternet = 0;                      // Stato della connessione a Internet (non usato attivamente al momento).
float currentDeltaP = 0.0;                  // Valore corrente del Delta P calcolato.
int partialFailCycles = 0;                  // Contatore per i cicli di polling con fallimenti parziali.
unsigned long timerRemoteSync = 0;          // Timer per l'invio periodico dei dati al server.

// Dichiarazione 'extern' della funzione definita in RS485_Master.cpp.
// Necessaria per poterla chiamare da qui (specificamente, dal setup).
extern void scansionaSlave();
extern void scansionaSlaveStandalone();
extern void RS485_Master_Standalone_Loop();


// Funzione di setup, eseguita una sola volta all'avvio della scheda.
void setup() {
    Serial.begin(115200);
    // Inizializza la memoria non volatile (Preferences) nel namespace "easy".
    memoria.begin("easy", false);
    
    // Carica la configurazione salvata dalla memoria.
    // Se una chiave non esiste, viene usato un valore di default.
    config.configurata = memoria.getBool("set", false);
    config.modalitaMaster = memoria.getInt("m_mode", 2);
    config.usaSicurezzaLocale = memoria.getBool("m_sic", false);
    String s = memoria.isKey("serialeID") ? memoria.getString("serialeID") : "NON_SET";
    s.toCharArray(config.serialeID, 32);
    String k = memoria.isKey("apiKey") ? memoria.getString("apiKey") : "";
    k.toCharArray(config.apiKey, 65);
    String u = memoria.isKey("api_url") ? memoria.getString("api_url") : "";
    u.toCharArray(config.apiUrl, 250);

    // Inizializzazione dei pin di output e input.
    pinMode(PIN_LED_EXT_1, OUTPUT); pinMode(PIN_LED_EXT_2, OUTPUT);
    pinMode(PIN_LED_EXT_3, OUTPUT); pinMode(PIN_LED_EXT_4, OUTPUT);
    pinMode(PIN_LED_EXT_5, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT); pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_MASTER_SICUREZZA, INPUT_PULLUP);

    greenLed.begin();
    redLed.begin();
    membraneKey.begin(); // Inizializza i pin della tastiera

    Serial.println("\n--- EASY CONNECT MASTER ---");

    // Se la scheda non è configurata, entra in un loop di blocco.
    while (!config.configurata) {
        static unsigned long tM = 0;
        if (millis() - tM > 2000) {
            Serial.println("[!] MASTER NON CONFIGURATO. Inserire: SETSERIAL...");
            Serial.println("Digitare 'HELP' per la lista comandi.");
            tM = millis();
        }
        // Fa lampeggiare il LED rosso per indicare lo stato di attesa configurazione.
        redLed.setState(LED_BLINK_FAST);
        redLed.update();
        Serial_Master_Menu();
        delay(10);
    }

    // Inizializza la comunicazione seriale per la RS485.
    Serial1.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    pinMode(PIN_RS485_DIR, OUTPUT);
    modoRicezione();

    // Gestione avvio in base alla modalità
    if (config.modalitaMaster == 1) {
        Serial.println("--- MODALITA' STANDALONE (MODE 1) ---");
        Serial.println("WiFi e AP Disabilitati.");
        scansionaSlaveStandalone();
    } else {
        // Modalità Rewamping (Default)
        scansionaSlave();
        setupMasterWiFi();

        // --- CONFIGURAZIONE OTA (Over-The-Air Update) ---
        ArduinoOTA.setHostname("EasyConnect-Master");
        // ArduinoOTA.setPassword("admin"); // Opzionale: password per l'aggiornamento

        ArduinoOTA.onStart([]() {
            String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
            Serial.println("Start updating " + type);
        });
        ArduinoOTA.onEnd([]() {
            Serial.println("\nEnd");
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });
        ArduinoOTA.begin();
        Serial.println("OTA Ready");
    }
}

// --- FUNZIONE ESECUZIONE AGGIORNAMENTO HTTP/HTTPS ---
void execHttpUpdate(String url) {
    Serial.println("[OTA] Rilevato comando di aggiornamento remoto.");
    Serial.println("[OTA] URL Firmware: " + url);
    
    // Utilizziamo un client sicuro ma "permissivo" (come per le API)
    WiFiClientSecure client;
    client.setInsecure(); // Accetta certificati self-signed
    
    // Imposta il timeout (opzionale, default è spesso sufficiente)
    httpUpdate.setLedPin(PIN_LED_ROSSO, HIGH); // Accende il LED Rosso durante l'update

    // Avvia l'aggiornamento (Questa funzione è bloccante)
    t_httpUpdate_return ret = httpUpdate.update(client, url);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Aggiornamento FALLITO. Errore (%d): %s\n", 
                          httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] Nessun aggiornamento necessario.");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Aggiornamento completato con successo! Riavvio...");
            break;
    }
}

// --- FUNZIONE INVIO DATI REMOTO (HTTPS) ---
void sendDataToRemoteServer() {
    // Controlla le pre-condizioni: WiFi connesso e URL API configurato.
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) {
        if (debugViewApi) Serial.println("[API] Invio saltato: WiFi non connesso o URL API non impostato.");
        return;
    }

    if (debugViewApi) Serial.println("[API] Avvio invio dati al server...");

    WiFiClientSecure client;
    // IMPORTANTE: Questa riga permette la connessione a server con certificati
    // non validati da una CA ufficiale (come i certificati self-signed).
    // Rende la connessione vulnerabile ad attacchi "man-in-the-middle".
    // Da usare con cautela in produzione.
    client.setInsecure(); // Accetta certificati self-signed o senza root CA (comune in IoT embedded)
    
    HTTPClient http;
    
    // Tenta di iniziare la connessione all'URL specificato.
    if (http.begin(client, config.apiUrl)) {
        if (debugViewApi) Serial.printf("[API] Connessione a: %s\n", config.apiUrl);
        http.addHeader("Content-Type", "application/json");
        // Se presente, aggiunge la chiave API nell'header per l'autenticazione.
        if (String(config.apiKey).length() > 0) {
            http.addHeader("X-API-KEY", config.apiKey);
            if (debugViewApi) Serial.println("[API] Header X-API-KEY aggiunto.");
        }

        // Costruzione dinamica della stringa JSON da inviare.
        String json = "{";
        json += "\"master_sn\":\"" + String(config.serialeID) + "\",";
        json += "\"fw_ver\":\"" + String(FW_VERSION) + "\",";
        json += "\"delta_p\":" + String(currentDeltaP) + ",";
        json += "\"slaves\":[";
        
        bool first = true;
        // Aggiunge un oggetto JSON per ogni slave attivo trovato.
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

        // Esegue la richiesta POST inviando il JSON.
        int httpResponseCode = http.POST(json);
        
        if (debugViewApi) Serial.printf("[API] Codice risposta HTTP: %d\n", httpResponseCode);

        // Se la richiesta ha avuto successo (codice > 0)...
        if (httpResponseCode > 0) {
            String response = http.getString();
            if (debugViewApi) Serial.println("[API] Risposta Server: " + response);

            // --- CONTROLLO AGGIORNAMENTO REMOTO ---
            // Cerca se la risposta contiene il campo "ota_url"
            // Esempio JSON atteso dal server: { "status": "ok", "ota_url": "https://..." }
            String searchKey = "\"ota_url\":\"";
            int startIdx = response.indexOf(searchKey);
            if (startIdx != -1) {
                startIdx += searchKey.length();
                int endIdx = response.indexOf("\"", startIdx);
                if (endIdx != -1) {
                    String fwUrl = response.substring(startIdx, endIdx);
                    // Se l'URL è valido, avvia l'aggiornamento
                    if (fwUrl.startsWith("http")) execHttpUpdate(fwUrl);
                }
            }
        } else {
            Serial.printf("[API] Errore invio: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        
        http.end();
    } else {
        if (debugViewApi) Serial.println("[API] Errore: Impossibile connettersi al server.");
    }
}

void gestisciLedMaster() {
    // --- Logica LED Rosso (Stato Scheda Master & Connettività) ---
    
    // Priorità 1: Allarme di sicurezza locale o configurazione API mancante
    bool securityTriggered = (digitalRead(PIN_MASTER_SICUREZZA) == HIGH);
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool apiConfigured = (String(config.apiUrl).length() > 5);

    if (config.usaSicurezzaLocale && securityTriggered) {
        redLed.setState(LED_BLINK_FAST);
        return; // L'allarme di sicurezza ha la precedenza su tutto
    }
    if (wifiConnected && !apiConfigured) {
        redLed.setState(LED_BLINK_FAST);
        return;
    }

    // Priorità 2: Modalità Access Point attiva
    if (WiFi.getMode() == WIFI_AP) {
        redLed.setState(LED_BLINK_SLOW);
        return;
    }

    // Priorità 3: Funzionamento regolare (connesso e configurato)
    if (wifiConnected && apiConfigured) {
        redLed.setState(LED_SOLID);
        return;
    }

    // Stato di default: Non connesso
    redLed.setState(LED_OFF);
}

void loop() {
    Serial_Master_Menu();
    
    if (config.modalitaMaster == 1) {
        // --- LOOP MODALITA' STANDALONE ---
        RS485_Master_Standalone_Loop();
        // In questa modalità i LED della tastiera sono gestiti direttamente dal loop RS485
    } else {
        // --- LOOP MODALITA' REWAMPING ---
        ArduinoOTA.handle(); // Gestione richieste OTA
        gestisciWebEWiFi();
        greenLed.update();
        redLed.update();
        gestisciLedMaster();
        membraneKey.update(); // Aggiorna logica LED tastiera

        calibrationLoop(); // Gestione campionamento calibrazione
        
        RS485_Master_Loop();

        // Sincronizzazione Remota (ogni 60 secondi)
        unsigned long ora = millis();
        if (ora - timerRemoteSync >= 60000) {
            sendDataToRemoteServer();
            timerRemoteSync = ora;
        }
    }
}