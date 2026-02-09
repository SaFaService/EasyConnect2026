#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "Pins.h"
#include "GestioneMemoria.h"
#include "Calibration.h"
#include "Led.h"
#include "Serial_Manager.h"
#include "RS485_Manager.h"
#include "MembraneKeyboard.h"
#include <esp_task_wdt.h>     // Gestione Watchdog

// --- NUOVI MODULI INCLUSI ---
#include "OTA_Manager.h"      // Gestione Aggiornamenti
#include "API_Manager.h"      // Gestione Invio Dati

// --- FUNZIONI DEFINITE IN ALTRI FILE ---
void setupMasterWiFi();
void gestisciWebEWiFi();

// --- VERSIONE FIRMWARE MASTER ---
const char* FW_VERSION = "1.1.0"; 

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
bool debugViewApi = true;                  // Flag per abilitare/disabilitare i log di debug per l'invio dati al server.
bool scansioneInCorso = false;              // Flag che indica se è in corso una scansione RS485.
int statoInternet = 0;                      // Stato della connessione a Internet (non usato attivamente al momento).
float currentDeltaP = 0.0;                  // Valore corrente del Delta P calcolato.
int partialFailCycles = 0;                  // Contatore per i cicli di polling con fallimenti parziali.
unsigned long timerRemoteSync = 0;          // Timer per l'invio periodico dei dati al server.
unsigned long timerUpdateCheck = 0;         // Timer per il controllo periodico degli aggiornamenti.

// Dichiarazione 'extern' della funzione definita in RS485_Master.cpp.
// Necessaria per poterla chiamare da qui (specificamente, dal setup).
extern void scansionaSlave();
extern void scansionaSlaveStandalone();
extern void RS485_Master_Standalone_Loop();
extern bool otaSlaveActive; // Definito in RS485_Master.cpp

// Funzione di setup, eseguita una sola volta all'avvio della scheda.
void setup() {
    // Inizializzazione Seriale anticipata con delay per USB CDC (ESP32-C3)
    // Questo è fondamentale per vedere i log all'avvio su USB nativa
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Evita blocchi se il terminale non è aperto
    delay(1500); // Attesa fisiologica per l'enumerazione USB
    Serial.println("\n\n!!! CPU BOOT SUCCESSFUL !!!"); // Feedback immediato avvio

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
    String cu = memoria.isKey("custApiUrl") ? memoria.getString("custApiUrl") : "";
    cu.toCharArray(config.customerApiUrl, 128);
    String ck = memoria.isKey("custApiKey") ? memoria.getString("custApiKey") : "";
    ck.toCharArray(config.customerApiKey, 65);

    // Inizializzazione dei pin di output e input.
    pinMode(PIN_LED_EXT_1, OUTPUT); pinMode(PIN_LED_EXT_2, OUTPUT);
    pinMode(PIN_LED_EXT_3, OUTPUT); pinMode(PIN_LED_EXT_4, OUTPUT);
    pinMode(PIN_LED_EXT_5, OUTPUT);
    pinMode(PIN_LED_ROSSO, OUTPUT); pinMode(PIN_LED_VERDE, OUTPUT);
    pinMode(PIN_MASTER_SICUREZZA, INPUT_PULLUP);

    greenLed.begin();
    redLed.begin();
    membraneKey.begin(); // Inizializza i pin della tastiera

    // --- CONFIGURAZIONE WATCHDOG ---
    // Imposta il timeout del Task Watchdog a 45 secondi per evitare reset durante operazioni lunghe (come OTA o SSL handshake)
    esp_task_wdt_init(45, true);
    esp_task_wdt_add(NULL); // Aggiunge il task corrente (loop) al watchdog

    Serial.println("\n--- EASY CONNECT MASTER (Boot Complete) ---");

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
        membraneKey.update(); // FIX: Aggiorna i LED della tastiera per feedback visivo
        Serial_Master_Menu();
        esp_task_wdt_reset(); // Importante: resetta il watchdog anche qui
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
        // Tutta la logica OTA è stata spostata in OTA_Manager.cpp
        setupOTA();
    }
}

// --- GESTIONE LED DI BORDO (PCB) ---
// NOTA IMPORTANTE: Questa funzione gestisce SOLO il LED Rosso saldato sulla scheda elettronica (PCB).
// NON gestisce i LED della tastiera a membrana esterna (quelli sono gestiti da MembraneKeyboard.cpp).
//
// Logica LED Rosso PCB:
// - Lampeggio Veloce: Allarme Sicurezza attivo OPPURE WiFi connesso ma API non configurate.
// - Lampeggio Lento: Modalità Access Point (AP) attiva per la configurazione.
// - Fisso: Tutto OK (WiFi connesso e API configurate).
// - Spento: Non connesso.
void gestisciLedMaster() {
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
    // Reset del Watchdog Timer ad ogni ciclo per confermare che il sistema non è bloccato
    esp_task_wdt_reset();
    Serial_Master_Menu();
    
    if (config.modalitaMaster == 1) {
        // --- LOOP MODALITA' STANDALONE ---
        RS485_Master_Standalone_Loop();
        // In questa modalità i LED della tastiera sono gestiti direttamente dal loop RS485
    } else {
        // --- LOOP MODALITA' REWAMPING ---
        handleOTA(); // Gestione richieste OTA locali (WiFi)
        gestisciWebEWiFi();
        greenLed.update();
        redLed.update();
        gestisciLedMaster();
        membraneKey.update(); // Aggiorna logica LED tastiera

        calibrationLoop(); // Gestione campionamento calibrazione
        
        RS485_Master_Loop();

        unsigned long ora = millis();

        // Sospendi le operazioni di rete se è in corso un aggiornamento slave
        if (!otaSlaveActive) {
            // Invio dati al server (ogni 60 secondi)
            if (ora - timerRemoteSync >= 60000) {
                sendDataToRemoteServer();
                timerRemoteSync = ora;
            }
            // Controllo aggiornamenti periodico (ogni 2 minuti per reattività al pulsante web)
            // Modifica: Esegue il controllo subito se è il primo avvio (timer=0) E c'è connessione.
            if ((ora - timerUpdateCheck >= 120000) || (timerUpdateCheck == 0 && WiFi.status() == WL_CONNECTED)) { 
                checkForFirmwareUpdates();
                timerUpdateCheck = ora;
            }
        }
    }
}