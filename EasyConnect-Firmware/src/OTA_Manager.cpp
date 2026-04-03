#include "OTA_Manager.h"
#include <ArduinoOTA.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "certificates.h"     // Certificati SSL per HTTPS
#include "Pins.h"             // Definizione dei Pin (per i LED)
#include "Led.h"              // Classe gestione LED
#include "GestioneMemoria.h"  // Per accedere alla configurazione (API Key, URL)
#include <SPIFFS.h>           // Per salvare il firmware dello slave
#include <esp_task_wdt.h>     // Gestione Watchdog
#include "RS485_Manager.h"
#include "Serial_Manager.h"

// --- VARIABILI ESTERNE ---
// Queste variabili sono definite nel main_standalone_rewamping_controller.cpp, ma ci servono qui.
// Usiamo 'extern' per dire al compilatore: "Fidati, esistono da un'altra parte".
extern const char* FW_VERSION;
extern Impostazioni config;
extern Led greenLed;
extern Led redLed;
extern bool debugViewApi;

// Funzione definita in RS485_Master.cpp per mettere il transceiver in ascolto
extern void modoRicezione();

// Funzione definita in RS485_Master.cpp per avviare la procedura di aggiornamento slave
extern void avviaAggiornamentoSlave(String slaveSn, String filePath);
extern void reportSlaveProgressFor(String slaveSn, String status, String message);
extern bool executePressureConfigCommand(const String& slaveSn, int newMode, int newGroup, int newIp, String& outMessage);

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

// Funzione per convertire un link di Google Drive in un link per il download diretto.
// Gestisce formati come "/d/FILE_ID/" e "?id=FILE_ID".
String convertGDriveUrl(String url) {
    int id_start = url.indexOf("id=");
    if (id_start != -1) {
        id_start += 3; // Salta "id="
        int id_end = url.indexOf('&', id_start);
        String file_id = (id_end != -1) ? url.substring(id_start, id_end) : url.substring(id_start);
        return "https://drive.google.com/uc?export=download&id=" + file_id;
    }
    
    id_start = url.indexOf("/d/");
    if (id_start != -1) {
        id_start += 3; // Salta "/d/"
        int id_end = url.indexOf('/', id_start);
        if (id_end != -1) {
            String file_id = url.substring(id_start, id_end);
            return "https://drive.google.com/uc?export=download&id=" + file_id;
        }
    }

    return url; // Ritorna l'URL originale se non trova un formato noto
}

// Costruisce l'URL api_ota_report.php a partire da config.apiUrl.
static String buildOtaReportUrl() {
    String reportUrl = String(config.apiUrl);
    int lastSlash = reportUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        reportUrl = reportUrl.substring(0, lastSlash + 1) + "api_ota_report.php";
    } else {
        reportUrl += "/api_ota_report.php";
    }
    return reportUrl;
}

static String buildCommandReportUrl() {
    String reportUrl = String(config.apiUrl);
    int lastSlash = reportUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        reportUrl = reportUrl.substring(0, lastSlash + 1) + "api_command_report.php";
    } else {
        reportUrl += "/api_command_report.php";
    }
    return reportUrl;
}

static String jsonGetString(const String& src, const String& key) {
    String tag = "\"" + key + "\":\"";
    int p = src.indexOf(tag);
    if (p < 0) return "";
    p += tag.length();
    int e = src.indexOf("\"", p);
    if (e < 0) return "";
    String v = src.substring(p, e);
    v.replace("\\/", "/");
    return v;
}

static int jsonGetInt(const String& src, const String& key, int defaultValue = -1) {
    String tag = "\"" + key + "\":";
    int p = src.indexOf(tag);
    if (p < 0) return defaultValue;
    p += tag.length();
    while (p < src.length() && (src[p] == ' ' || src[p] == '\t')) p++;
    int e = p;
    while (e < src.length() && (isDigit(src[e]) || src[e] == '-')) e++;
    if (e <= p) return defaultValue;
    return src.substring(p, e).toInt();
}

static bool isControllerFirmwareType(const String& deviceType) {
    return deviceType == "master" ||
           deviceType == "controller" ||
           deviceType == "controller_standalone_rewamping" ||
           deviceType == "controller_display";
}

static bool isPeripheralFirmwareType(const String& deviceType) {
    return deviceType == "slave_pressure" ||
           deviceType == "slave_relay" ||
           deviceType == "slave_motor" ||
           deviceType == "peripheral_pressure" ||
           deviceType == "peripheral_relay" ||
           deviceType == "peripheral_motor" ||
           deviceType == "slave";
}

static void reportCommandResult(int commandId, String status, String message, const String& resultJson = "") {
    if (commandId <= 0) return;
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String url = buildCommandReportUrl();
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    status.toLowerCase();
    if (status != "success") status = "failed";
    message.replace("\"", "'");

    String payload = "{";
    payload += "\"api_key\":\"" + String(config.apiKey) + "\",";
    payload += "\"command_id\":" + String(commandId) + ",";
    payload += "\"status\":\"" + status + "\",";
    payload += "\"message\":\"" + message + "\"";
    if (resultJson.length() > 0) {
        payload += ",\"result\":" + resultJson;
    }
    payload += "}";

    int code = http.POST(payload);
    if (debugViewApi) {
        Serial.printf("[API-CMD-REPORT] command_id=%d status=%s HTTP=%d\n", commandId, status.c_str(), code);
    }
    http.end();
}

// Report stato OTA verso portale (InProgress / Success / Failed).
static void reportUpdateStatus(String status, String message) {
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) {
        return; // Non possiamo riportare lo stato se non siamo connessi
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String reportUrl = buildOtaReportUrl();

    http.begin(client, reportUrl);
    http.addHeader("Content-Type", "application/json");

    status.trim();
    if (status.length() == 0) {
        status = "Failed";
    }
    message.replace("\"", "'");

    String jsonPayload = "{\"api_key\":\"" + String(config.apiKey) + "\",\"status\":\"" + status + "\",\"message\":\"" + message + "\"}";

    int httpResponseCode = http.POST(jsonPayload);
    if (debugViewApi) {
        Serial.printf("[API-OTA-REPORT] Stato '%s' inviato. Risposta server: %d\n", status.c_str(), httpResponseCode);
    }
    http.end();
}

// Wrapper compatibile col vecchio flusso.
void reportUpdateFailure(String errorMessage) {
    reportUpdateStatus("Failed", errorMessage);
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
    reportUpdateStatus("InProgress", "Download e installazione firmware master in corso.");
    
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
            reportUpdateStatus("Success", "Firmware aggiornato con successo. Riavvio dispositivo.");
            delay(400);
            Serial.println("[OTA] Riavvio del sistema in corso...");
            delay(1000);
            ESP.restart();
            break;
    }
}

// Funzione per scaricare il firmware dello Slave e salvarlo in SPIFFS
bool downloadSlaveFirmware(String url, String outputFilename) {
    if (!SPIFFS.begin(true)) {
        Serial.println("[OTA-SLAVE] Errore critico: Impossibile montare SPIFFS.");
        return false;
    }

    // Converte l'URL di Google Drive in un link diretto, se necessario
    String directUrl = convertGDriveUrl(url);

    Serial.println("[OTA-SLAVE] Avvio download firmware da: " + directUrl);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    // Imposta un timeout per la connessione e la risposta per evitare blocchi indefiniti
    http.setTimeout(20000); // 20 secondi

    esp_task_wdt_reset(); // Resetta il watchdog prima di iniziare la connessione
    if (http.begin(client, directUrl)) {
        esp_task_wdt_reset(); // Resetta dopo il begin, prima del GET che può essere bloccante
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            File f = SPIFFS.open(outputFilename, "w");
            if (!f) {
                Serial.println("[OTA-SLAVE] Errore apertura file " + outputFilename + " per scrittura.");
                http.end();
                return false;
            }

            int len = http.getSize();
            // Stream del download per gestire file grandi
            WiFiClient * stream = http.getStreamPtr();
            uint8_t buff[128] = { 0 };

            while (http.connected() && (len > 0 || len == -1)) {
                size_t size = stream->available();
                if (size) {
                    int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                    f.write(buff, c);
                    if (len > 0) { len -= c; }
                    esp_task_wdt_reset(); // "Accarezza" il watchdog durante il download
                }
                delay(1);
            }
            f.close();
            http.end();
            Serial.println("[OTA-SLAVE] Download completato. File salvato in " + outputFilename);
            return true;
        } else {
            Serial.printf("[OTA-SLAVE] Errore HTTP Download: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("[OTA-SLAVE] Impossibile connettersi al server.");
    }
    return false;
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

        // Estrae il tipo di dispositivo target per validazione
        String deviceType = "";
        int typeStart = response.indexOf("\"target_device_type\":\"");
        if (typeStart != -1) {
            typeStart += 22;
            int typeEnd = response.indexOf("\"", typeStart);
            deviceType = response.substring(typeStart, typeEnd);
        }

        // Controllo se è un aggiornamento per il MASTER (la condizione ora è specifica e non causa più false-positive)
        if (response.indexOf("\"status\":\"update_ready\"") != -1) {
            if (isControllerFirmwareType(deviceType)) {
                int urlStart = response.indexOf("\"url\":\"");
                if (urlStart != -1) {
                    urlStart += 7; // Lunghezza di "url":"
                    int urlEnd = response.indexOf("\"", urlStart);
                    String fwUrl = response.substring(urlStart, urlEnd);
                    fwUrl.replace("\\/", "/");
                    execHttpUpdate(fwUrl, ""); 
                }
            } else {
                Serial.println("[OTA] ERRORE CRITICO: Ricevuto comando di aggiornamento MASTER con firmware per '" + deviceType + "'. Aggiornamento annullato.");
                reportUpdateFailure("Tipo firmware errato ricevuto: " + deviceType);
            }
        } 
        // Controllo se è un aggiornamento per uno SLAVE (uso else if per mutua esclusione)
        else if (response.indexOf("\"status\":\"slave_update_ready\"") != -1) {
            if (isPeripheralFirmwareType(deviceType)) {
                Serial.println("[API-OTA] Rilevato aggiornamento periferica RS485. Tipo: " + deviceType);
                
                // Estrazione URL
                int urlStart = response.indexOf("\"url\":\"") + 7;
                int urlEnd = response.indexOf("\"", urlStart);
                String fwUrl = response.substring(urlStart, urlEnd);
                fwUrl.replace("\\/", "/");

                // Estrazione Seriale Slave Target
                int snStart = response.indexOf("\"target_slave_sn\":\"") + 19;
                int snEnd = response.indexOf("\"", snStart);
                String targetSn = response.substring(snStart, snEnd);

                reportSlaveProgressFor(targetSn, "Pending", "Richiesta OTA ricevuta dalla piattaforma.");
                reportSlaveProgressFor(targetSn, "Downloading", "Download firmware in corso sul controller...");
                if (downloadSlaveFirmware(fwUrl)) {
                    reportSlaveProgressFor(targetSn, "Downloaded", "Firmware scaricato. Avvio trasferimento RS485...");
                    avviaAggiornamentoSlave(targetSn, "/slave_update.bin");
                } else {
                    reportSlaveProgressFor(targetSn, "Failed", "Download firmware fallito sul controller.");
                }
            } else {
                Serial.println("[OTA] ERRORE CRITICO: Ricevuto comando di aggiornamento SLAVE con firmware per '" + deviceType + "'. Aggiornamento annullato.");
                // Qui dovremmo riportare l'errore dello slave
            }
        }
        else if (response.indexOf("\"status\":\"command_ready\"") != -1) {
            int commandId = jsonGetInt(response, "command_id", -1);
            String commandType = jsonGetString(response, "command_type");
            String targetSn = jsonGetString(response, "target_slave_sn");
            int newMode = jsonGetInt(response, "new_mode", -1);
            int newGroup = jsonGetInt(response, "new_group", -1);
            int newIp = jsonGetInt(response, "new_ip", -1);
            int totalSpeeds = jsonGetInt(response, "total_speeds", -1);
            int dirtLevel = jsonGetInt(response, "dirt_level", -1);
            int speedIndex = jsonGetInt(response, "speed_index", -1);
            String stopAction = jsonGetString(response, "stop_action");
            int saveIfPossibleRaw = jsonGetInt(response, "save_if_possible", -1);

            // Fallback: payload annidato (parsing semplificato su intera risposta).
            if (newMode < 0) newMode = jsonGetInt(response, "new_mode", -1);
            if (newGroup < 0) newGroup = jsonGetInt(response, "new_group", -1);
            if (newIp < 0) newIp = jsonGetInt(response, "new_ip", -1);
            if (totalSpeeds < 0) totalSpeeds = jsonGetInt(response, "total_speeds", -1);
            if (dirtLevel < 0) dirtLevel = jsonGetInt(response, "dirt_level", -1);
            if (speedIndex < 0) speedIndex = jsonGetInt(response, "speed_index", -1);
            if (stopAction.length() == 0) stopAction = jsonGetString(response, "action");

            const bool isMasterDeltaPCmd =
                (commandType == "deltap_testwiz_start" || commandType == "deltap_testwiz_stop");

            if (commandId <= 0 || commandType.length() == 0 || (!isMasterDeltaPCmd && targetSn.length() == 0)) {
                Serial.println("[API-CMD] Payload comando non valido.");
                reportCommandResult(commandId, "failed", "Payload comando non valido");
            } else if (commandType == "pressure_config") {
                Serial.printf("[API-CMD] Esecuzione pressure_config su SN %s (mode=%d grp=%d ip=%d)\n",
                              targetSn.c_str(), newMode, newGroup, newIp);
                String msg;
                bool ok = executePressureConfigCommand(targetSn, newMode, newGroup, newIp, msg);
                if (!ok && msg.length() == 0) msg = "Errore configurazione remota.";
                if (ok && msg.length() == 0) msg = "Configurazione applicata.";
                String resultJson = "{\"slave_sn\":\"" + targetSn + "\",\"new_mode\":" + String(newMode) +
                                    ",\"new_group\":" + String(newGroup) + ",\"new_ip\":" + String(newIp) + "}";
                reportCommandResult(commandId, ok ? "success" : "failed", msg, resultJson);
            } else if (commandType == "deltap_testwiz_start") {
                Serial.printf("[API-CMD] Esecuzione deltap_testwiz_start (speeds=%d dirt=%d speed=%d)\n",
                              totalSpeeds, dirtLevel, speedIndex);
                String msg;
                bool ok = webStartDeltaPTestWizard(totalSpeeds, dirtLevel, speedIndex, msg);
                if (!ok && msg.length() == 0) msg = "Errore avvio testwiz remoto.";
                if (ok && msg.length() == 0) msg = "Testwiz avviato.";
                String resultJson = "{\"total_speeds\":" + String(totalSpeeds) +
                                    ",\"dirt_level\":" + String(dirtLevel) +
                                    ",\"speed_index\":" + String(speedIndex) + "}";
                reportCommandResult(commandId, ok ? "success" : "failed", msg, resultJson);
            } else if (commandType == "deltap_testwiz_stop") {
                bool saveIfPossible = true;
                if (saveIfPossibleRaw == 0) saveIfPossible = false;
                if (stopAction == "abort") saveIfPossible = false;

                Serial.printf("[API-CMD] Esecuzione deltap_testwiz_stop (save=%s)\n",
                              saveIfPossible ? "true" : "false");
                String msg;
                bool ok = webStopDeltaPTestWizard(saveIfPossible, msg);
                if (!ok && msg.length() == 0) msg = "Errore stop testwiz remoto.";
                if (ok && msg.length() == 0) msg = "Testwiz fermato.";
                String resultJson = String("{\"save_if_possible\":") +
                                    (saveIfPossible ? "true" : "false") + "}";
                reportCommandResult(commandId, ok ? "success" : "failed", msg, resultJson);
            } else {
                Serial.println("[API-CMD] Tipo comando non supportato: " + commandType);
                reportCommandResult(commandId, "failed", "Tipo comando non supportato: " + commandType);
            }
        }
    } else {
        if (debugViewApi) Serial.printf("[API-OTA] Errore HTTP: %d\n", httpResponseCode);
    }
    
    http.end();
}
