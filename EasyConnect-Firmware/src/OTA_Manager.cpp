#include "OTA_Manager.h"
#include <ArduinoOTA.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "certificates.h"     // Certificati SSL per HTTPS
#include "Pins.h"             // Definizione dei Pin (per i LED)
#include "Led.h"              // Classe gestione LED
#include "GestioneMemoria.h"  // Per accedere alla configurazione (API Key, URL)

// --- VARIABILI ESTERNE ---
// Queste variabili sono definite nel main_master.cpp, ma ci servono qui.
// Usiamo 'extern' per dire al compilatore: "Fidati, esistono da un'altra parte".
extern const char* FW_VERSION;
extern Impostazioni config;
extern Led greenLed;
extern Led redLed;
extern bool debugViewApi;

// Funzione definita in RS485_Master.cpp per mettere il transceiver in ascolto
extern void modoRicezione();

// --- FUNZIONI DI SUPPORTO INTERNE ---

// Callback per l'animazione dei LED sulla tastiera durante l'aggiornamento HTTP.
// Crea un effetto visivo "rotante" sui LED esterni per indicare che l'aggiornamento è in corso.
void updateProgressCallback(int cur, int total) {
    static unsigned long lastStep = 0;
    static int ledIndex = 0;
    
    // Mappatura LED Tastiera per l'animazione (BAL1 -> BAL4)
    const int ledPins[] = { PIN_LED_EXT_4, PIN_LED_EXT_3, PIN_LED_EXT_2, PIN_LED_EXT_1 };
    const int numLeds = 4;

    // Esegue lo switch dei LED ogni 250ms
    if (millis() - lastStep >= 250) { 
        lastStep = millis();
        
        // Spegni tutti i LED dell'animazione
        for (int i = 0; i < numLeds; i++) digitalWrite(ledPins[i], LOW);
        
        // Accendi solo il LED corrente
        digitalWrite(ledPins[ledIndex], HIGH);
        
        // Passa al prossimo LED (loop circolare 0->1->2->3->0...)
        ledIndex = (ledIndex + 1) % numLeds;
    }
}

// Funzione per riportare al server il fallimento di un aggiornamento
void reportUpdateFailure(String errorMessage) {
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) {
        return; // Non possiamo riportare l'errore se non siamo connessi
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    // Costruisce l'URL per api_ota_report.php
    String reportUrl = String(config.apiUrl);
    int lastSlash = reportUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        reportUrl = reportUrl.substring(0, lastSlash + 1) + "api_ota_report.php";
    } else {
        reportUrl += "/api_ota_report.php"; // Fallback se apiUrl non ha slash
    }

    http.begin(client, reportUrl);
    http.addHeader("Content-Type", "application/json");

    // Pulisce il messaggio di errore da eventuali virgolette
    errorMessage.replace("\"", "'");

    // Crea il payload JSON
    String jsonPayload = "{\"api_key\":\"" + String(config.apiKey) + "\",\"error_message\":\"" + errorMessage + "\"}";

    int httpResponseCode = http.POST(jsonPayload);
    if (debugViewApi) {
        Serial.printf("[API-OTA-REPORT] Inviato report di fallimento. Risposta server: %d\n", httpResponseCode);
    }
    http.end();
}

// --- IMPLEMENTAZIONE FUNZIONI PUBBLICHE ---

void setupOTA() {
    // Configura il nome host che apparirà nella porta seriale di Arduino/PlatformIO
    ArduinoOTA.setHostname("EasyConnect-Master");
    // ArduinoOTA.setPassword("admin"); // Opzionale: password per sicurezza

    // Definisce cosa fare quando inizia l'aggiornamento
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
    });
    
    // Definisce cosa fare alla fine
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    
    // Definisce la barra di progresso sulla seriale
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    
    // Gestione errori
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Servizio OTA Locale Avviato.");
}

void handleOTA() {
    ArduinoOTA.handle();
}

void execHttpUpdate(String url, String md5) {
    Serial.println("[OTA] Rilevato comando di aggiornamento remoto.");
    Serial.println("[OTA] URL Firmware: " + url);
    
    // 1. PREPARAZIONE SISTEMA
    // Mette la RS485 in ascolto per evitare conflitti o trasmissioni parziali durante il flash
    modoRicezione();
    
    // Spegne i LED di bordo e resetta quelli della tastiera per prepararsi all'animazione
    greenLed.setState(LED_OFF); greenLed.update();
    redLed.setState(LED_OFF); redLed.update();
    digitalWrite(PIN_LED_EXT_1, LOW); digitalWrite(PIN_LED_EXT_2, LOW);
    digitalWrite(PIN_LED_EXT_3, LOW); digitalWrite(PIN_LED_EXT_4, LOW);
    digitalWrite(PIN_LED_EXT_5, LOW);

    // 2. CONFIGURAZIONE CLIENT SICURO
    WiFiClientSecure client;
    client.setInsecure(); // Usa insecure per garantire compatibilità con redirect (es. Google Drive)
    
    // Configura la callback per l'animazione dei LED definita sopra
    httpUpdate.onProgress(updateProgressCallback);
    httpUpdate.setLedPin( -1 ); // Disabilita il controllo LED automatico della libreria

    // Abilita il following dei redirect (fondamentale per Error 301/302 e Google Drive)
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    // Disabilita il riavvio automatico per poter stampare il messaggio di successo anche se appare l'errore SSL
    httpUpdate.rebootOnUpdate(false);

    // 3. ESECUZIONE AGGIORNAMENTO
    t_httpUpdate_return ret = httpUpdate.update(client, url, FW_VERSION);

    // 4. GESTIONE RISULTATO
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            { // Blocco di codice per creare uno scope per le variabili
                String errorString = httpUpdate.getLastErrorString();
                Serial.printf("[OTA] Aggiornamento FALLITO. Errore (%d): %s\n", 
                              httpUpdate.getLastError(), errorString.c_str());
                
                // Riporta il fallimento al server
                reportUpdateFailure(errorString);
            }
            digitalWrite(PIN_LED_EXT_1, LOW); digitalWrite(PIN_LED_EXT_2, LOW);
            digitalWrite(PIN_LED_EXT_3, LOW); digitalWrite(PIN_LED_EXT_4, LOW);
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] Nessun aggiornamento necessario (Versione già aggiornata).");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Aggiornamento completato con successo!");
            Serial.println("[OTA] Riavvio del sistema in corso...");
            delay(1000);
            ESP.restart();
            break;
    }
}

void checkForFirmwareUpdates() {
    // Controlla le pre-condizioni: WiFi connesso e URL API Antralux configurato.
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) {
        if (debugViewApi) Serial.println("[API-OTA] Controllo saltato: WiFi non connesso o URL mancante.");
        return;
    }

    if (debugViewApi) Serial.println("[API-OTA] Controllo aggiornamenti su server Antralux...");

    WiFiClientSecure client;
    client.setInsecure(); // Per semplicità usiamo insecure per la chiamata API (non per il download firmware)
    HTTPClient http;

    // Costruisce l'URL per api_update.php basandosi sull'URL API configurato
    // Es. se config.apiUrl è "http://sito.com/api/data.php", diventa "http://sito.com/api_update.php"
    String updateUrl = String(config.apiUrl);
    int lastSlash = updateUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        updateUrl = updateUrl.substring(0, lastSlash + 1) + "api_update.php";
    } else {
        updateUrl += "/api_update.php";
    }

    http.begin(client, updateUrl);
    http.addHeader("Content-Type", "application/json");

    // Crea il JSON manualmente
    String jsonPayload = "{";
    jsonPayload += "\"api_key\":\"" + String(config.apiKey) + "\",";
    jsonPayload += "\"version\":\"" + String(FW_VERSION) + "\"";
    jsonPayload += "}";

    int httpResponseCode = http.POST(jsonPayload);

    if (httpResponseCode == 200) {
        String response = http.getString();
        if (debugViewApi) Serial.println("[API-OTA] Risposta: " + response);

        // Parsing manuale semplice per trovare "update_ready" e l'URL
        if (response.indexOf("update_ready") != -1) {
            int urlStart = response.indexOf("\"url\":\"");
            if (urlStart != -1) {
                urlStart += 7; // Lunghezza di "url":"
                int urlEnd = response.indexOf("\"", urlStart);
                String fwUrl = response.substring(urlStart, urlEnd);
                // Sicurezza: rimuove eventuali backslash di escape (\/) che JSON può inserire
                fwUrl.replace("\\/", "/");
                
                // Esegui l'aggiornamento
                // Nota: I link di Google Drive vengono gestiti, ma assicurati che siano link diretti o convertiti dal PHP
                // Il PHP firmware.php li converte già in link di export diretti.
                execHttpUpdate(fwUrl, ""); 
            }
        }
    } else {
        if (debugViewApi) Serial.printf("[API-OTA] Errore HTTP: %d\n", httpResponseCode);
    }
    
    http.end();
}