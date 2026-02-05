#include "API_Manager.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "certificates.h"
#include "GestioneMemoria.h"
#include "RS485_Manager.h"    // Per accedere alla struttura DatiSlave
#include <WiFi.h>

// --- VARIABILI ESTERNE ---
extern const char* FW_VERSION;
extern Impostazioni config;
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern float currentDeltaP;
extern bool debugViewApi;

void sendDataToRemoteServer() {
    // 1. DETERMINA DESTINAZIONE
    // Priorità all'URL del cliente se configurato, altrimenti usa Antralux.
    String targetUrl = String(config.customerApiUrl);
    String targetKey = String(config.customerApiKey);

    if (targetUrl.length() < 5) {
        targetUrl = String(config.apiUrl);
        targetKey = String(config.apiKey);
    }

    // 2. CONTROLLI PRELIMINARI
    if (WiFi.status() != WL_CONNECTED || targetUrl.length() < 5) {
        if (debugViewApi) Serial.println("[API-DATA] Invio saltato: No WiFi o No URL.");
        return;
    }

    if (debugViewApi) Serial.println("[API-DATA] Avvio invio dati a: " + targetUrl);

    // 3. CONNESSIONE SICURA
    WiFiClientSecure client;
    client.setCACert(rootCACertificate); // Usa il certificato per HTTPS sicuro

    HTTPClient http;
    
    if (http.begin(client, targetUrl)) {
        http.addHeader("Content-Type", "application/json");
        
        // Aggiunge API Key se presente
        if (targetKey.length() > 0) {
            http.addHeader("X-API-KEY", targetKey);
        }

        // 4. COSTRUZIONE JSON
        // Creiamo manualmente la stringa JSON per evitare l'overhead di librerie pesanti
        long rssi = WiFi.RSSI();
        String json = "{";
        json += "\"master_sn\":\"" + String(config.serialeID) + "\",";
        json += "\"fw_ver\":\"" + String(FW_VERSION) + "\",";
        json += "\"delta_p\":" + String(currentDeltaP) + ",";
        json += "\"rssi\":" + String(rssi) + ",";
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

        if (debugViewApi) Serial.println("[API-DATA] JSON: " + json);

        // 5. INVIO E GESTIONE RISPOSTA
        int httpResponseCode = http.POST(json);
        
        if (debugViewApi) Serial.printf("[API-DATA] HTTP Code: %d\n", httpResponseCode);

        if (httpResponseCode > 0) {
            String response = http.getString();
            if (debugViewApi) Serial.println("[API-DATA] Risposta: " + response);
        } else {
            Serial.printf("[API-DATA] Errore invio: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
    }
}