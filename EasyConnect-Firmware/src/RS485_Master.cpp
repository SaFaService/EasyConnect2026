#include "RS485_Manager.h"
#include "GestioneMemoria.h"
#include <HTTPClient.h>
#include "Pins.h"
#include "Led.h"
#include <SPIFFS.h>
#include <MD5Builder.h>
#include <string.h>

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_standalone_rewamping_controller.cpp').
// Questo ci permette di accedervi e modificarle da questo file.
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern unsigned long timerScansione;
extern unsigned long timerStampa;
extern bool scansioneInCorso;
extern bool qualchePerifericaTrovata;
extern bool debugViewData;
extern int partialFailCycles;
extern bool slaveExcludedByPortal[101];
extern Led greenLed;
extern Impostazioni config; // Mi serve per la API key
extern float currentDeltaP; // Variabile globale definita in main_standalone_rewamping_controller.cpp
extern bool currentDeltaPValid; // True quando il delta P e' calcolato da due sensori validi
extern void updateDeltaPMonitoring(float rawDeltaP, bool isValidSample);
extern void requestStandaloneWifiPortalStart();

// Variabili per la modalità Standalone
bool relayBoardDetected[5]; // Indici 1-4 usati
static bool relayOnCommandIssued[5]; // Evita ON ridondanti che resettano il timer feedback.

struct StandaloneRelayStatus {
    bool detectedAtBoot;
    bool online;
    bool relayOn;
    bool safetyClosed;
    bool feedbackMatched;
    bool safetyAlarm;
    bool lifetimeAlarm;
    bool lampFault;
    bool modeMismatch;
    bool hasLifeFields;
    bool hasFeedbackFaultField;
    bool feedbackFaultLatched;
    uint32_t lifeLimitHours;
    int mode;
    uint32_t starts;
    float hours;
    unsigned long lastResponseMs;
    unsigned long lastPollMs;
    unsigned long nextOnRetryMs;
    char stateText[16];
};

static StandaloneRelayStatus standaloneRelay[5];
static bool standaloneKeyboardInitialized = false;
static bool standaloneResetModeActive = false;
static int standaloneResetSelection = -1; // -1 nessuna, 0..3 BAL1..BAL4, 4 TUTTI
static bool standaloneButtonPrevPressed = false;
static bool standaloneButtonLongActionDone = false;
static unsigned long standaloneButtonPressStartMs = 0;
static unsigned long standaloneLastPollMs = 0;
static unsigned long standaloneLastFrameTxMs = 0;
static bool standaloneSafetyInterlockWasActive = false;
enum StandaloneHoldPreviewMode {
    STANDALONE_HOLD_PREVIEW_NONE = 0,
    STANDALONE_HOLD_PREVIEW_RESET,
    STANDALONE_HOLD_PREVIEW_WIFI
};
static StandaloneHoldPreviewMode standaloneHoldPreviewMode = STANDALONE_HOLD_PREVIEW_NONE;
static unsigned long standaloneHoldPreviewStartMs = 0;

static const unsigned long STANDALONE_POLL_INTERVAL_MS = 2000UL;
static const unsigned long STANDALONE_OFFLINE_TIMEOUT_MS = 10000UL;
static const unsigned long STANDALONE_INTERFRAME_GAP_MS = 8UL;
static const uint8_t STANDALONE_BOOT_SWEEP_CYCLES = 4;
static const unsigned long STANDALONE_BOOT_SWEEP_STEP_MS = 140UL;
static const unsigned long STANDALONE_BOOT_DETECTED_PREPAUSE_MS = 180UL;
static const unsigned long STANDALONE_BOOT_DETECTED_STEP_MS = 240UL;
static const unsigned long STANDALONE_BOOT_DETECTED_POSTPAUSE_MS = 220UL;
static const unsigned long STANDALONE_WIFI_START_ANIMATION_MS = 5000UL;
static const unsigned long STANDALONE_HOLD_WIFI_SWEEP_STEP_MS = 180UL;
static const unsigned long STANDALONE_RESET_PREVIEW_BLINK_STEP_MS = 500UL;
static const uint8_t STANDALONE_RESET_PREVIEW_BLINK_COUNT = 5;
static const unsigned long STANDALONE_RESET_PREVIEW_CYCLE_PAUSE_MS = 500UL;
static const unsigned long STANDALONE_ENTER_RESET_HOLD_MS = 5000UL;
static const unsigned long STANDALONE_WIFI_PORTAL_HOLD_MS = 10000UL;
static const unsigned long STANDALONE_CONFIRM_RESET_HOLD_MS = 3000UL;
static const uint8_t STANDALONE_RESET_CMD_RETRIES = 3;
static const float STANDALONE_MIN_VALID_HOURS = 0.0f;

// Mappatura richiesta utente/Manuale membrana:
// Pin4(BAL1)->IP1, Pin3(BAL2)->IP2, Pin2(BAL3)->IP3, Pin1(BAL4)->IP4.
static Led standaloneLedBal1(MK_PIN_WIFI);   // Pin 4 membrana
static Led standaloneLedBal2(MK_PIN_SENS1);  // Pin 3 membrana
static Led standaloneLedBal3(MK_PIN_SENS2);  // Pin 2 membrana
static Led standaloneLedBal4(MK_PIN_AUX1);   // Pin 1 membrana
static Led standaloneLedExpLife(MK_PIN_AUX2); // Pin 5 membrana
static Led standaloneLedSafety(MK_PIN_SAFETY); // Pin 7 membrana

// --- VARIABILI GESTIONE OTA SLAVE ---
bool otaSlaveActive = false;       // Se true, il master è occupato ad aggiornare uno slave
String otaSlaveTargetSn = "";      // Seriale dello slave da aggiornare
int otaSlaveTargetId = -1;         // Indirizzo RS485 dello slave target
File otaSlaveFile;                 // Handle del file firmware
int otaSlaveState = 0;             // Macchina a stati: 0=Idle, 1=Handshake, 2=Sending, 3=Verify
String otaSlaveMD5 = "";           // MD5 del file firmware

// Variabili interne per la macchina a stati OTA
unsigned long otaSlaveLastActionTime = 0;
int otaSlaveRetryCount = 0;
unsigned long otaSlaveLastProgressReport = 0; // Timer per limitare i report HTTP
size_t otaSlaveCurrentOffset = 0;
const int OTA_MAX_RETRIES = 5;
int otaSlaveBaudAttempt = 0; // 0 = 115200, 1 = 9600
const int OTA_TIMEOUT_MS = 2500;   // Timeout standard per i pacchetti dati
const int OTA_HANDSHAKE_TIMEOUT_MS = 30000; // Aumentato a 30s. La cancellazione della flash può essere lenta.
const int OTA_CHUNK_SIZE = 128;    // Aumentato a 128 byte binari (256 hex) per velocità

// Imposta il pin di direzione del transceiver RS485 su LOW per metterlo in ascolto.
void modoRicezione() { 
    Serial1.flush();                 // Attende fine TX UART
    digitalWrite(PIN_RS485_DIR, LOW); // Rilascia subito il bus per evitare collisioni di turn-around
    delayMicroseconds(80);           // Piccolo tempo di assestamento del transceiver
}
// Imposta il pin di direzione del transceiver RS485 su HIGH per abilitare la trasmissione.
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(80); }

// Frame tipico Relay: OK,RELAY,MODE,...
// In modalita' Rewamping le schede Relay devono essere ignorate.
static bool isRelayResponse(const String& payload) {
    return payload.startsWith("OK,RELAY,");
}

// Mantiene una cache minima (SN/FW/ID) delle Relay per consentire OTA remoto via SN.
static void cacheRelayMetadata(String payload, int address) {
    String fields[16];
    int count = 0;
    int start = 0;
    for (int i = 0; i <= payload.length(); i++) {
        const bool isSep = (i == payload.length()) || (payload.charAt(i) == ',');
        if (!isSep) continue;
        if (count < 16) {
            fields[count] = payload.substring(start, i);
            fields[count].trim();
        }
        count++;
        start = i + 1;
    }

    // Formato atteso minimo:
    // OK,RELAY,mode,on,safety,feedback,starts,hours,group,serial,state,fw
    if (count < 12) return;
    if (fields[0] != "OK" || fields[1] != "RELAY") return;

    String groupToken = fields[8];
    String serialToken = fields[9];
    String fwToken = fields[11];

    if (serialToken.length() == 0) return;

    databaseSlave[address].t = 0.0f;
    databaseSlave[address].h = 0.0f;
    databaseSlave[address].p = 0.0f;
    databaseSlave[address].sic = fields[4].toInt();
    databaseSlave[address].grp = groupToken.toInt();
    serialToken.toCharArray(databaseSlave[address].sn, 32);
    fwToken.toCharArray(databaseSlave[address].version, 16);
    databaseSlave[address].lastResponseTime = millis();
}

// Funzione per analizzare la stringa di risposta ricevuta da uno slave.
// Estrae i dati (temperatura, pressione, etc.) e li salva nel database.
void parseDatiSlave(String payload, int address) {
    if (isRelayResponse(payload)) return;

    // Formato atteso: OK,T,H,P,S,G,SN,VER!
    // Conta quante virgole ci sono per capire se il formato è nuovo (con versione FW) o vecchio.
    int commaCount = 0;
    for (int k = 0; k < payload.length(); k++) {
        if (payload.charAt(k) == ',') commaCount++;
    }

    // Trova la posizione di ogni virgola.
    int v[7]; 
    int pos = 0;
    for (int j = 0; j < commaCount; j++) { 
        pos = payload.indexOf(',', pos + 1); 
        v[j] = pos; 
    }

    // Se abbiamo abbastanza dati, estraili usando la posizione delle virgole.
    if (commaCount >= 6) {
        databaseSlave[address].t = payload.substring(v[0] + 1, v[1]).toFloat();
        databaseSlave[address].h = payload.substring(v[1] + 1, v[2]).toFloat();
        databaseSlave[address].p = payload.substring(v[2] + 1, v[3]).toFloat();
        databaseSlave[address].sic = payload.substring(v[3] + 1, v[4]).toInt();
        databaseSlave[address].grp = payload.substring(v[4] + 1, v[5]).toInt();
        
        // Gestisce sia il formato con versione FW che quello senza.
        if (commaCount == 7) {
            String sn = payload.substring(v[5] + 1, v[6]);
            sn.toCharArray(databaseSlave[address].sn, 32);
            String ver = payload.substring(v[6] + 1);
            ver.toCharArray(databaseSlave[address].version, 16);
        } else {
            String sn = payload.substring(v[5] + 1);
            sn.toCharArray(databaseSlave[address].sn, 32);
            strcpy(databaseSlave[address].version, "N/A");
        }
    }
}

// Funzione per cercare gli slave presenti sulla rete RS485.
// Viene eseguita all'avvio e poi ogni ora.
void scansionaSlave() {
    Serial.println("[SCAN] Avvio scansione slave RS485...");
    scansioneInCorso = true;
    qualchePerifericaTrovata = false;
    
    // Azzera la lista degli slave attivi prima di iniziare.
    for (int i = 0; i < 101; i++) listaPerifericheAttive[i] = false;

    // Prova a interrogare gli indirizzi da 1 a 30.
    for (int i = 1; i <= 30; i++) {
        while (Serial1.available()) Serial1.read(); // Pulisce eventuali byte residui/sporchi
        if (debugViewData) {
            Serial.printf("[SCAN-TX] -> ?%d!\n", i);
        }
        modoTrasmissione();
        Serial1.printf("?%d!", i); // Invia una richiesta all'indirizzo 'i'.
        modoRicezione();
        
        unsigned long startWait = millis();
        while (millis() - startWait < 50) { // Aspetta una risposta per 50ms.
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (debugViewData) {
                    Serial.printf("[SCAN-RX] <- %s!\n", resp.c_str());
                }
                if (resp.startsWith("OK")) {
                    if (isRelayResponse(resp)) {
                        cacheRelayMetadata(resp, i);
                        if (debugViewData) {
                            Serial.printf("[SCAN] ID %d relay rilevata (cache OTA SN/FW aggiornata)\n", i);
                        }
                    } else {
                        listaPerifericheAttive[i] = true;
                        qualchePerifericaTrovata = true;
                        databaseSlave[i].lastResponseTime = millis(); // Inizializza il timestamp
                        Serial.printf("[SCAN] Trovato Slave ID: %d\n", i);
                    }
                }
                break; // Trovata una risposta (o un disturbo), passa all'indirizzo successivo.
            }
        }
    }
    scansioneInCorso = false;
    Serial.println("[SCAN] Scansione terminata.");
    timerScansione = millis();
}

// Helper per convertire buffer binario in stringa Hex
String bufferToHex(uint8_t* buff, size_t len) {
    String output = "";
    for (size_t i = 0; i < len; i++) {
        if (buff[i] < 16) output += "0";
        output += String(buff[i], HEX);
    }
    output.toUpperCase();
    return output;
}

// Calcola Checksum XOR semplice per stringa Hex
uint8_t calculateChecksum(String &data) {
    uint8_t crc = 0;
    for (int i = 0; i < data.length(); i++) {
        crc ^= data.charAt(i);
    }
    return crc;
}

static bool waitRs485Frame(String& out, unsigned long timeoutMs) {
    unsigned long t0 = millis();
    while (millis() - t0 < timeoutMs) {
        if (Serial1.available()) {
            out = Serial1.readStringUntil('!');
            return true;
        }
        delay(1);
    }
    out = "";
    return false;
}

static bool sendCfgCommandExpect(const String& tx, const String& expectedPrefix, String& rxOut, unsigned long timeoutMs = 900) {
    while (Serial1.available()) Serial1.read();
    modoTrasmissione();
    Serial1.print(tx);
    modoRicezione();

    if (!waitRs485Frame(rxOut, timeoutMs)) {
        return false;
    }
    return rxOut.startsWith(expectedPrefix);
}

bool executePressureConfigCommand(const String& slaveSn, int newMode, int newGroup, int newIp, String& outMessage) {
    outMessage = "";
    if (otaSlaveActive) {
        outMessage = "OTA slave attiva: comando configurazione rimandato.";
        return false;
    }
    if (newMode < 0 && newGroup < 0 && newIp < 0) {
        outMessage = "Nessun parametro da aggiornare.";
        return false;
    }
    if (newIp > 30) {
        outMessage = "IP RS485 non valido (range 1..30).";
        return false;
    }

    int targetId = -1;
    for (int i = 1; i <= 30; i++) {
        if (String(databaseSlave[i].sn) == slaveSn) {
            targetId = i;
            break;
        }
    }
    if (targetId < 1) {
        outMessage = "Slave non trovato in rete RS485.";
        return false;
    }

    String rx;
    int workingId = targetId;

    if (newMode >= 1) {
        String tx = "MOD" + String(workingId) + ":" + String(newMode) + "!";
        String expected = "OK,CFG,MODE," + String(workingId) + "," + String(newMode);
        if (!sendCfgCommandExpect(tx, expected, rx)) {
            outMessage = "Cambio modalita fallito. RX: " + rx;
            return false;
        }
    }

    if (newGroup >= 1) {
        String tx = "GRP" + String(workingId) + ":" + String(newGroup) + "!";
        String expected = "OK,CFG,GRP," + String(workingId) + "," + String(newGroup);
        if (!sendCfgCommandExpect(tx, expected, rx)) {
            outMessage = "Cambio gruppo fallito. RX: " + rx;
            return false;
        }
        databaseSlave[workingId].grp = newGroup;
    }

    if (newIp >= 1 && newIp != workingId) {
        String tx = "IP" + String(workingId) + ":" + String(newIp) + "!";
        String expected = "OK,CFG,IP," + String(workingId) + "," + String(newIp);
        if (!sendCfgCommandExpect(tx, expected, rx)) {
            outMessage = "Cambio IP fallito. RX: " + rx;
            return false;
        }

        // Verifica veloce che lo slave risponda sul nuovo indirizzo.
        while (Serial1.available()) Serial1.read();
        modoTrasmissione();
        Serial1.printf("?%d!", newIp);
        modoRicezione();
        String pingResp;
        if (!waitRs485Frame(pingResp, 500) || !pingResp.startsWith("OK")) {
            outMessage = "IP aggiornato ma slave non risponde subito sul nuovo indirizzo.";
            return false;
        }

        databaseSlave[newIp] = databaseSlave[workingId];
        databaseSlave[newIp].lastResponseTime = millis();
        listaPerifericheAttive[newIp] = true;
        databaseSlave[workingId] = {};
        listaPerifericheAttive[workingId] = false;
        workingId = newIp;
    }

    outMessage = "Configurazione applicata con successo.";
    return true;
}

void reportSlaveProgress(String status, String message) {
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String reportUrl = String(config.apiUrl);
    int lastSlash = reportUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        reportUrl = reportUrl.substring(0, lastSlash + 1) + "api_slave_ota_report.php";
    }

    http.begin(client, reportUrl);
    http.addHeader("Content-Type", "application/json");

    message.replace("\"", "'");

    String jsonPayload = "{";
    jsonPayload += "\"api_key\":\"" + String(config.apiKey) + "\",";
    jsonPayload += "\"slave_sn\":\"" + otaSlaveTargetSn + "\",";
    jsonPayload += "\"status\":\"" + status + "\",";
    jsonPayload += "\"message\":\"" + message + "\"";
    jsonPayload += "}";

    Serial.println("[RS485-OTA] >> Invio report progresso: " + status);

    int httpResponseCode = http.POST(jsonPayload);
    if (debugViewData) {
        Serial.printf("[RS485-OTA] Risposta server report: %d\n", httpResponseCode);
    }
    http.end();
}

// Funzione helper per terminare l'OTA e ripristinare lo stato
void terminaOtaSlave(bool success, String message) {
    otaSlaveActive = false;
    if (otaSlaveFile) otaSlaveFile.close();
    
    if (success) {
        reportSlaveProgress("Success", message);
    } else {
        reportSlaveProgress("Failed", message);
    }

    // Ripristina sempre il baud rate a 115200 per il polling normale
    // Lo facciamo solo se abbiamo effettivamente cambiato baud rate
    if (otaSlaveBaudAttempt != 0) {
        Serial.println("[RS485-OTA] Ripristino baud rate a 115200.");
        Serial1.end();
        Serial1.begin(115200, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
        modoRicezione();
    }
}

// Funzione chiamata da OTA_Manager quando il file è pronto
void avviaAggiornamentoSlave(String slaveSn, String filePath) {
    Serial.println("[RS485-OTA] Richiesta avvio aggiornamento per Slave: " + slaveSn);
    
    if (otaSlaveActive) {
        Serial.println("[RS485-OTA] Errore: Aggiornamento già in corso.");
        return;
    }
    
    if (!SPIFFS.begin(true)) SPIFFS.begin(true);
    
    otaSlaveFile = SPIFFS.open(filePath, "r");
    if (!otaSlaveFile) {
        Serial.println("[RS485-OTA] Errore: Impossibile aprire il file firmware da SPIFFS.");
        reportSlaveProgress("Failed", "Errore interno: impossibile leggere file firmware.");
        return; // Non usiamo terminaOtaSlave perché non è ancora attivo
    }

    // Cerca l'ID dello slave basandosi sul seriale
    otaSlaveTargetId = -1;
    for (int i = 1; i <= 30; i++) {
        if (String(databaseSlave[i].sn) == slaveSn) {
            otaSlaveTargetId = i;
            break;
        }
    }

    if (otaSlaveTargetId == -1) {
        Serial.println("[RS485-OTA] ERRORE: Slave SN " + slaveSn + " non trovato nella lista attivi (ID sconosciuto).");
        reportSlaveProgress("Failed", "Slave non trovato o non attivo sulla rete RS485.");
        otaSlaveFile.close();
        return;
    }

    // Calcolo MD5 del file per verifica integrità
    Serial.println("[RS485-OTA] Calcolo MD5 del firmware...");
    MD5Builder md5;
    md5.begin();
    md5.addStream(otaSlaveFile, otaSlaveFile.size());
    md5.calculate();
    otaSlaveMD5 = md5.toString();
    otaSlaveMD5.toUpperCase(); // Normalizza a maiuscolo
    Serial.println("[RS485-OTA] MD5: " + otaSlaveMD5);
    otaSlaveFile.seek(0); // Torna all'inizio del file

    otaSlaveTargetSn = slaveSn;
    otaSlaveBaudAttempt = 0; // Inizia sempre con la velocità alta (115200)
    otaSlaveActive = true;
    otaSlaveState = 1; // Imposta stato iniziale: Handshake
    otaSlaveCurrentOffset = 0;
    otaSlaveRetryCount = 0;
    otaSlaveLastActionTime = 0; // Forza azione immediata
    otaSlaveLastProgressReport = 0;
    Serial.printf("[RS485-OTA] Avvio procedura per Slave ID %d (SN: %s). File size: %d bytes. MD5: %s\n", otaSlaveTargetId, slaveSn.c_str(), otaSlaveFile.size(), otaSlaveMD5.c_str());
    
    // REPORT IMMEDIATO: Sblocca il popup web dallo stato "Pending"
    reportSlaveProgress("Handshake", "Master pronto. Contatto lo slave...");
}

void gestisciAggiornamentoSlave() {
    if (!otaSlaveActive) return;

    unsigned long now = millis();

    switch (otaSlaveState) {
        case 1: // SEND HANDSHAKE
            if (now - otaSlaveLastActionTime > 1000) {
                Serial.printf("[RS485-OTA] >> Invio START (Size: %d) a ID %d. Attesa cancellazione flash...\n", otaSlaveFile.size(), otaSlaveTargetId);
                modoTrasmissione();
                // Invia ID target, dimensione e MD5
                Serial1.printf("OTA,START,%d,%d,%s!", otaSlaveTargetId, otaSlaveFile.size(), otaSlaveMD5.c_str());
                modoRicezione();
                otaSlaveLastActionTime = now;
                otaSlaveState = 2; // WAIT HANDSHAKE
            }
            break;

        case 2: // WAIT HANDSHAKE RESPONSE
            if (now - otaSlaveLastActionTime > OTA_HANDSHAKE_TIMEOUT_MS) {
                otaSlaveRetryCount++;
                Serial.printf("[RS485-OTA] Timeout attesa READY (%d/%d) a %d baud\n", otaSlaveRetryCount, OTA_MAX_RETRIES, (otaSlaveBaudAttempt == 0 ? 115200 : 9600));
                if (otaSlaveRetryCount >= OTA_MAX_RETRIES) {
                    if (otaSlaveBaudAttempt == 0) {
                        Serial.println("[RS485-OTA] Handshake fallito a 115200 baud. Tento fallback a 9600 baud.");
                        otaSlaveBaudAttempt = 1;
                        otaSlaveRetryCount = 0;
                        otaSlaveState = 1; // Riprova Handshake
                        
                        Serial1.end();
                        Serial1.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
                        modoRicezione();
                        
                        otaSlaveLastActionTime = 0; // Riprova subito
                    } else {
                        Serial.println("[RS485-OTA] ERRORE: Nessuna risposta allo START neanche a 9600 baud. Abort.");
                        terminaOtaSlave(false, "Lo slave non risponde al comando di avvio (provato 115200 e 9600 baud).");
                    }
                } else {
                    otaSlaveState = 1; // Riprova Handshake
                    otaSlaveLastActionTime = 0; // Riprova subito
                }
            }
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[RS485-OTA] << RX: " + resp);
                if (resp.startsWith("OK,OTA,READY," + String(otaSlaveTargetId))) {
                    Serial.println("[RS485-OTA] Slave PRONTO (Flash cancellata). Inizio invio dati...");
                    reportSlaveProgress("Sending data", "Trasferimento in corso...");
                    otaSlaveState = 3; // SEND CHUNK
                    otaSlaveCurrentOffset = 0;
                    otaSlaveRetryCount = 0;
                    otaSlaveLastActionTime = 0;
                }
                // --- GESTIONE FALLIMENTO IMMEDIATO ---
                // Se lo slave risponde FAIL subito (es. non ha spazio), abortiamo.
                else if (resp.startsWith("OK,OTA,FAIL," + String(otaSlaveTargetId))) {
                    Serial.println("[RS485-OTA] ERRORE: Lo slave ha rifiutato lo START (possibile spazio insufficiente o errore flash).");
                    terminaOtaSlave(false, "Lo slave ha rifiutato l'avvio dell'aggiornamento (spazio insufficiente o errore flash).");
                }
            }
            break;

        case 3: // SEND CHUNK
             if (now - otaSlaveLastActionTime > 10) { // Ridotto delay per velocità (era 50)
                if (otaSlaveCurrentOffset >= otaSlaveFile.size()) {
                    otaSlaveState = 5; // SEND END
                    break;
                }

                // Assicuriamoci che il file sia aperto
                if (!otaSlaveFile) {
                     otaSlaveFile = SPIFFS.open("/slave_update.bin", "r");
                }

                otaSlaveFile.seek(otaSlaveCurrentOffset);
                uint8_t buff[OTA_CHUNK_SIZE];
                size_t bytesRead = otaSlaveFile.read(buff, OTA_CHUNK_SIZE);
                
                // --- FIX: GESTIONE ERRORE LETTURA ---
                if (bytesRead == 0) {
                    Serial.printf("[RS485-OTA] Errore lettura offset %d. Riprovo apertura file...\n", otaSlaveCurrentOffset);
                    otaSlaveFile.close();
                    otaSlaveFile = SPIFFS.open("/slave_update.bin", "r");
                    if (otaSlaveFile) {
                        otaSlaveFile.seek(otaSlaveCurrentOffset);
                        bytesRead = otaSlaveFile.read(buff, OTA_CHUNK_SIZE);
                    }
                    
                    if (bytesRead == 0) {
                        Serial.println("[RS485-OTA] Errore critico lettura file. Abort.");
                        terminaOtaSlave(false, "Errore lettura file locale.");
                        return; // Esce dalla funzione
                    }
                }
                // ------------------------------------

                String hexData = bufferToHex(buff, bytesRead);
                uint8_t checksum = calculateChecksum(hexData);
                
                Serial.printf("[RS485-OTA] >> Invio CHUNK Offset %d (Len %d)\n", otaSlaveCurrentOffset, bytesRead);
                modoTrasmissione();
                // Nuovo formato: OTA,DATA,ID,OFFSET,HEX,CHECKSUM!
                Serial1.printf("OTA,DATA,%d,%d,%s,%02X!", otaSlaveTargetId, otaSlaveCurrentOffset, hexData.c_str(), checksum);
                modoRicezione();
                
                otaSlaveLastActionTime = now;
                otaSlaveState = 4; // WAIT CHUNK ACK
             }
             break;

        case 4: // WAIT CHUNK ACK
            if (now - otaSlaveLastActionTime > OTA_TIMEOUT_MS) {
                otaSlaveRetryCount++;
                Serial.printf("[RS485-OTA] Timeout ACK Chunk %d (%d/%d)\n", otaSlaveCurrentOffset, otaSlaveRetryCount, OTA_MAX_RETRIES);
                if (otaSlaveRetryCount >= OTA_MAX_RETRIES) {
                    Serial.println("[RS485-OTA] ERRORE: Troppi timeout su chunk. Abort.");                    
                    terminaOtaSlave(false, "Timeout durante il trasferimento dati.");
                } else {
                    otaSlaveState = 3; // Riprova invio stesso chunk
                    otaSlaveLastActionTime = 0; // Riprova subito
                }
            }
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (resp.startsWith("OK,OTA,ACK," + String(otaSlaveTargetId) + ",")) {
                    // Estrae l'offset confermato per sicurezza
                    int lastComma = resp.lastIndexOf(',');
                    int ackOffset = resp.substring(lastComma+1).toInt();
                    
                    if (ackOffset == otaSlaveCurrentOffset) {
                        Serial.printf("[RS485-OTA] << ACK ricevuto per Offset %d\n", ackOffset);
                        
                        // --- INVIO PROGRESSO AL SERVER (Ogni 2 secondi circa) ---
                        if (millis() - otaSlaveLastProgressReport > 2000) {
                            int percent = (otaSlaveCurrentOffset * 100) / otaSlaveFile.size();
                            // Inviamo lo stato "Uploading" e la percentuale come messaggio
                            reportSlaveProgress("Uploading", String(percent));
                            otaSlaveLastProgressReport = millis();
                        }
                        
                        otaSlaveCurrentOffset += OTA_CHUNK_SIZE;
                        otaSlaveState = 3; // Prossimo Chunk
                        otaSlaveRetryCount = 0;
                        otaSlaveLastActionTime = 0;
                    }
                }
            }
            break;

        case 5: // SEND END
            Serial.println("[RS485-OTA] >> Invio END. Attesa verifica MD5 slave...");
            modoTrasmissione();
            Serial1.printf("OTA,END,%d!", otaSlaveTargetId);
            modoRicezione();
            reportSlaveProgress("Finalizing", "Verifica integrità e scrittura...");
            otaSlaveLastActionTime = now;
            otaSlaveState = 6; // WAIT SUCCESS
            otaSlaveRetryCount = 0;
            break;

        case 6: // WAIT SUCCESS
             if (now - otaSlaveLastActionTime > 15000) { // Timeout lungo per verifica finale e scrittura
                otaSlaveRetryCount++;
                if (otaSlaveRetryCount >= 2) {
                     Serial.println("[RS485-OTA] ERRORE: Nessuna conferma finale.");
                     terminaOtaSlave(false, "Lo slave non ha dato conferma finale.");
                } else {
                    otaSlaveState = 5; // Riprova invio END
                    otaSlaveLastActionTime = now;
                }
             }
             if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[RS485-OTA] << RX: " + resp);
                if (resp.startsWith("OK,OTA,SUCCESS," + String(otaSlaveTargetId))) {
                    Serial.println("[RS485-OTA] AGGIORNAMENTO SLAVE COMPLETATO CON SUCCESSO!");                    
                    terminaOtaSlave(true, "Aggiornamento completato.");
                } else if (resp.startsWith("OK,OTA,FAIL," + String(otaSlaveTargetId))) {
                    // Gestione esplicita del FAIL
                    Serial.println("[RS485-OTA] ERRORE: Lo slave ha riportato fallimento (MD5/Write).");
                    terminaOtaSlave(false, "Lo slave ha riportato un fallimento (MD5 errato o errore scrittura).");
                } else if (resp.indexOf("FAIL") != -1) {
                    // Gestione FAIL anche se la stringa è sporca (es. OK,OTA,FAI;␕FAIL)
                    Serial.println("[RS485-OTA] ERRORE: Rilevato FAIL in risposta corrotta.");
                    terminaOtaSlave(false, "Risposta slave corrotta (FAIL rilevato).");
                }
             }
             break;
    }
}

// Funzione principale del gestore RS485 per il Master, da chiamare nel loop().
void RS485_Master_Loop() {
    unsigned long ora = millis();

    // Se è in corso un aggiornamento OTA Slave, sospendi il normale polling
    if (otaSlaveActive) {
        gestisciAggiornamentoSlave();
        return;
    }

    // Esegue una nuova scansione della rete ogni ora (3.600.000 ms).
    if (ora - timerScansione >= 3600000) {
        scansionaSlave();
    }

    // Esegue il polling (interrogazione) degli slave ogni 2 secondi.
    if (ora - timerStampa >= 2000) {
        int activeSlavesCount = 0;
        int respondedSlavesCount = 0;

        // Conta quanti slave dovrebbero rispondere (escludendo quelli marcati retired/voided dal portale).
        for (int i = 1; i <= 100; i++) {
            if (listaPerifericheAttive[i] && !slaveExcludedByPortal[i]) {
                activeSlavesCount++;
            }
        }

        // Se c'è almeno uno slave attivo...
        if (activeSlavesCount > 0) {
            // ...interroga ogni slave che è risultato attivo durante la scansione.
            for (int i = 1; i <= 100; i++) {
                if (listaPerifericheAttive[i] && !slaveExcludedByPortal[i]) {
                    while (Serial1.available()) Serial1.read(); // Evita contaminazione tra richieste
                    modoTrasmissione(); Serial1.printf("?%d!", i); modoRicezione();
                    unsigned long st = millis();
                    // Aspetta una risposta per 100ms.
                    while (millis() - st < 100) {
                        if (Serial1.available()) {
                            String d = Serial1.readStringUntil('!');
                            // Se la risposta è valida ("OK..."), incrementa il contatore
                            // e analizza i dati.
                            if (d.startsWith("OK")) {
                                if (isRelayResponse(d)) {
                                    if (debugViewData) {
                                        Serial.printf("[POLL] ID %d ignorato: Relay rilevata in Rewamping\n", i);
                                    }
                                    // Rimuove subito l'ID dalla lista attiva per evitare polling futuri.
                                    listaPerifericheAttive[i] = false;
                                    databaseSlave[i] = {};
                                    break;
                                }
                                respondedSlavesCount++;
                                databaseSlave[i].lastResponseTime = millis(); // Aggiorna timestamp
                                if (debugViewData) Serial.println("RX: " + d);
                                parseDatiSlave(d, i);
                                if (debugViewData) Serial.printf("ID:%d T:%.1f P:%.0f GRP:%d\n", i, databaseSlave[i].t, databaseSlave[i].p, databaseSlave[i].grp);
                            }
                            break;
                        }
                    }
                }
            }
            // --- Logica per il LED Verde in base alle risposte ---
            // Se tutti hanno risposto -> Lampeggio Lento (tutto ok).
            if (respondedSlavesCount == activeSlavesCount) { partialFailCycles = 0; greenLed.setState(LED_BLINK_SLOW); }
            // Se nessuno ha risposto -> Lampeggio Veloce (fallimento totale).
            else if (respondedSlavesCount == 0) { partialFailCycles = 0; greenLed.setState(LED_BLINK_FAST); }
            // Se solo alcuni hanno risposto -> Fallimento Parziale.
            else {
                partialFailCycles++;
                // Se il fallimento parziale si ripete per più di 5 volte, il LED diventa Fisso.
                // Altrimenti, lampeggia veloce per segnalare un problema recente.
                greenLed.setState(partialFailCycles > 5 ? LED_SOLID : LED_BLINK_FAST);
            }
        } else {
            // Se non ci sono slave attivi, il LED lampeggia veloce per segnalare un problema di rete.
            greenLed.setState(LED_BLINK_FAST);
        }
        timerStampa = ora; // Aggiorna il timer del polling.

        // --- CALCOLO DELTA PRESSIONE ---
        // Usa solo slave online in questo intervallo e non esclusi dal portale.
        // Priorita':
        // 1) Coppia gruppo 1 / gruppo 2 (logica impianto standard)
        // 2) Fallback: primi due slave online (ordine ID) per evitare delta sempre 0 in test/lab
        const unsigned long OFFLINE_TIMEOUT_MS = 10000;
        bool grp1Found = false;
        bool grp2Found = false;
        float grp1Pressure = 0.0f;
        float grp2Pressure = 0.0f;
        bool firstOnlineFound = false;
        bool secondOnlineFound = false;
        float firstOnlinePressure = 0.0f;
        float secondOnlinePressure = 0.0f;

        for (int i = 1; i <= 100; i++) {
            if (!listaPerifericheAttive[i] || slaveExcludedByPortal[i]) continue;
            bool online485 = (databaseSlave[i].lastResponseTime > 0) && ((ora - databaseSlave[i].lastResponseTime) <= OFFLINE_TIMEOUT_MS);
            if (!online485) continue;

            if (!firstOnlineFound) {
                firstOnlinePressure = databaseSlave[i].p;
                firstOnlineFound = true;
            } else if (!secondOnlineFound) {
                secondOnlinePressure = databaseSlave[i].p;
                secondOnlineFound = true;
            }

            if (!grp1Found && databaseSlave[i].grp == 1) {
                grp1Pressure = databaseSlave[i].p;
                grp1Found = true;
            } else if (!grp2Found && databaseSlave[i].grp == 2) {
                grp2Pressure = databaseSlave[i].p;
                grp2Found = true;
            }
        }

        if (grp1Found && grp2Found) {
            currentDeltaP = grp1Pressure - grp2Pressure;
            currentDeltaPValid = true;
            updateDeltaPMonitoring(currentDeltaP, true);
        } else if (firstOnlineFound && secondOnlineFound) {
            currentDeltaP = firstOnlinePressure - secondOnlinePressure;
            currentDeltaPValid = true;
            updateDeltaPMonitoring(currentDeltaP, true);
        } else {
            currentDeltaP = 0.0f;
            currentDeltaPValid = false;
            updateDeltaPMonitoring(0.0f, false);
        }
    }
}

// --- FUNZIONI PER MODALITA' STANDALONE (MODE 1) ---

static Led* standaloneBalLedByRelayId(int relayId) {
    if (relayId == 1) return &standaloneLedBal1; // BAL1 -> IP1
    if (relayId == 2) return &standaloneLedBal2; // BAL2 -> IP2
    if (relayId == 3) return &standaloneLedBal3; // BAL3 -> IP3
    if (relayId == 4) return &standaloneLedBal4; // BAL4 -> IP4
    return nullptr;
}

static const char* standaloneSelectionName(int selection) {
    if (selection == 0) return "BAL1 (IP1)";
    if (selection == 1) return "BAL2 (IP2)";
    if (selection == 2) return "BAL3 (IP3)";
    if (selection == 3) return "BAL4 (IP4)";
    if (selection == 4) return "TUTTI I BAL";
    return "NESSUNA";
}

static void standaloneBeginKeyboardIfNeeded() {
    if (standaloneKeyboardInitialized) return;

    standaloneLedBal1.begin();
    standaloneLedBal2.begin();
    standaloneLedBal3.begin();
    standaloneLedBal4.begin();
    standaloneLedExpLife.begin();
    standaloneLedSafety.begin();
    pinMode(MK_PIN_BUTTON, INPUT_PULLUP);

    standaloneKeyboardInitialized = true;
}

static void standaloneUpdateLedObjects() {
    standaloneLedBal1.update();
    standaloneLedBal2.update();
    standaloneLedBal3.update();
    standaloneLedBal4.update();
    standaloneLedExpLife.update();
    standaloneLedSafety.update();
}

static void standaloneSetAllBalLeds(LedState state) {
    standaloneLedBal1.setState(state);
    standaloneLedBal2.setState(state);
    standaloneLedBal3.setState(state);
    standaloneLedBal4.setState(state);
}

static void standaloneApplySweepFrame(int frameIndex) {
    static const int seq[6] = {1, 2, 3, 4, 3, 2};
    const int relayId = seq[frameIndex % 6];

    standaloneSetAllBalLeds(LED_OFF);
    Led* led = standaloneBalLedByRelayId(relayId);
    if (led) led->setState(LED_SOLID);
    standaloneLedExpLife.setState(LED_OFF);
    standaloneLedSafety.setState(LED_OFF);
}

static void standaloneShowScanSweepFrame(int frameIndex) {
    standaloneApplySweepFrame(frameIndex);
    standaloneUpdateLedObjects();
    delay(STANDALONE_BOOT_SWEEP_STEP_MS);
}

static void standaloneShowDetectedRelaySequence() {
    standaloneSetAllBalLeds(LED_OFF);
    standaloneLedExpLife.setState(LED_OFF);
    standaloneLedSafety.setState(LED_OFF);
    standaloneUpdateLedObjects();
    delay(STANDALONE_BOOT_DETECTED_PREPAUSE_MS);

    bool anyDetected = false;
    for (int relayId = 1; relayId <= 4; relayId++) {
        // Sequenza sempre BAL1 -> BAL4.
        Led* led = standaloneBalLedByRelayId(relayId);
        if (relayBoardDetected[relayId]) {
            anyDetected = true;
            if (led) led->setState(LED_SOLID);
        }
        standaloneUpdateLedObjects();
        delay(STANDALONE_BOOT_DETECTED_STEP_MS);
    }

    if (!anyDetected) {
        delay(STANDALONE_BOOT_DETECTED_POSTPAUSE_MS);
    } else {
        delay(STANDALONE_BOOT_DETECTED_POSTPAUSE_MS);
    }
}

void standalonePlayWifiStartAnimation() {
    standaloneBeginKeyboardIfNeeded();
    const unsigned long startMs = millis();
    int frame = 0;
    while (millis() - startMs < STANDALONE_WIFI_START_ANIMATION_MS) {
        standaloneShowScanSweepFrame(frame++);
    }
    standaloneSetAllBalLeds(LED_OFF);
    standaloneLedExpLife.setState(LED_OFF);
    standaloneLedSafety.setState(LED_OFF);
    standaloneUpdateLedObjects();
}

static bool standaloneExchangeFrame(const String& tx, String& rxOut, unsigned long timeoutMs) {
    const unsigned long nowMs = millis();
    if (standaloneLastFrameTxMs != 0) {
        const unsigned long elapsed = nowMs - standaloneLastFrameTxMs;
        if (elapsed < STANDALONE_INTERFRAME_GAP_MS) {
            delay(STANDALONE_INTERFRAME_GAP_MS - elapsed);
        }
    }
    while (Serial1.available()) Serial1.read();
    modoTrasmissione();
    Serial1.print(tx);
    modoRicezione();
    standaloneLastFrameTxMs = millis();
    return waitRs485Frame(rxOut, timeoutMs);
}

static int standaloneSplitCsv(const String& src, String* out, int maxItems) {
    int count = 0;
    int start = 0;
    for (int i = 0; i <= src.length(); i++) {
        const bool isSep = (i == src.length()) || (src.charAt(i) == ',');
        if (!isSep) continue;
        if (count < maxItems) {
            out[count] = src.substring(start, i);
            out[count].trim();
        }
        count++;
        start = i + 1;
    }
    return count;
}

static bool standaloneParseRelayStatus(const String& payload, StandaloneRelayStatus& outStatus) {
    String fields[16];
    const int fieldCount = standaloneSplitCsv(payload, fields, 16);
    if (fieldCount < 11) return false;
    if (fields[0] != "OK" || fields[1] != "RELAY") return false;
    if (fields[2] == "CMD") return false; // e' una risposta comando, non uno status.

    outStatus.mode = fields[2].toInt();
    outStatus.relayOn = (fields[3].toInt() != 0);
    outStatus.safetyClosed = (fields[4].toInt() != 0);
    outStatus.feedbackMatched = (fields[5].toInt() != 0);
    outStatus.starts = static_cast<uint32_t>(fields[6].toInt());
    outStatus.hours = fields[7].toFloat();
    if (outStatus.hours < STANDALONE_MIN_VALID_HOURS) outStatus.hours = 0.0f;

    String state = fields[10];
    state.toUpperCase();
    state.toCharArray(outStatus.stateText, sizeof(outStatus.stateText));

    outStatus.modeMismatch = (outStatus.mode != 2);
    outStatus.hasLifeFields = false;
    outStatus.hasFeedbackFaultField = false;
    outStatus.lifeLimitHours = 0;
    outStatus.lifetimeAlarm = false;
    if (fieldCount >= 14) {
        outStatus.lifeLimitHours = static_cast<uint32_t>(fields[12].toInt());
        outStatus.lifetimeAlarm = (fields[13].toInt() != 0);
        outStatus.hasLifeFields = true;
    }
    if (fieldCount >= 15) {
        outStatus.feedbackFaultLatched = (fields[14].toInt() != 0);
        outStatus.hasFeedbackFaultField = true;
    }
    return true;
}

static void standaloneRefreshDerivedFlags(int relayId, unsigned long nowMs) {
    StandaloneRelayStatus& st = standaloneRelay[relayId];
    if (!st.detectedAtBoot) return;

    const bool stale = (st.lastResponseMs == 0) || ((nowMs - st.lastResponseMs) > STANDALONE_OFFLINE_TIMEOUT_MS);
    if (stale) {
        st.online = false;
        st.safetyAlarm = false;
        st.lifetimeAlarm = false;
        st.lampFault = false;
        relayOnCommandIssued[relayId] = false; // se torna online, ritenta ON.
        return;
    }

    st.online = true;
    st.safetyAlarm = !st.safetyClosed;
    st.modeMismatch = (st.mode != 2);

    if (!st.hasLifeFields) {
        // Fallback legacy: usa soglia lato controller solo per relay con firmware vecchio.
        const bool lifetimeEnabled = (config.sogliaManutenzione > 0);
        st.lifetimeAlarm = lifetimeEnabled && (st.hours >= static_cast<float>(config.sogliaManutenzione));
    }

    const bool isRunning = (strcmp(st.stateText, "RUNNING") == 0);
    if (isRunning && st.relayOn) {
        st.feedbackFaultLatched = false;
    }

    const bool isFault = (strcmp(st.stateText, "FAULT") == 0);
    const bool legacyFeedbackFault =
        (!st.hasFeedbackFaultField) &&
        relayOnCommandIssued[relayId] &&
        st.safetyClosed &&
        !st.relayOn &&
        !st.feedbackMatched &&
        (strcmp(st.stateText, "OFF") == 0);
    if (legacyFeedbackFault) {
        st.feedbackFaultLatched = true;
    }
    st.lampFault = (isFault && st.safetyClosed) || st.feedbackFaultLatched || legacyFeedbackFault;
}

static bool standalonePollRelayStatus(int relayId, unsigned long nowMs) {
    String resp;
    if (!standaloneExchangeFrame("?" + String(relayId) + "!", resp, 180)) {
        return false;
    }
    if (debugViewData) {
        Serial.printf("[STANDALONE][POLL-RX] ID %d <- %s!\n", relayId, resp.c_str());
    }

    StandaloneRelayStatus parsed = standaloneRelay[relayId];
    if (!standaloneParseRelayStatus(resp, parsed)) {
        return false;
    }

    parsed.lastResponseMs = nowMs;
    parsed.lastPollMs = nowMs;
    parsed.detectedAtBoot = true;
    parsed.online = true;
    standaloneRelay[relayId] = parsed;

    databaseSlave[relayId].lastResponseTime = nowMs;
    databaseSlave[relayId].sic = parsed.safetyClosed ? 0 : 1;
    databaseSlave[relayId].p = parsed.hours;

    return true;
}

static void standaloneIssueOnCommandIfNeeded(int relayId) {
    StandaloneRelayStatus& st = standaloneRelay[relayId];
    if (!st.detectedAtBoot || !st.online || st.modeMismatch) return;
    const unsigned long nowMs = millis();
    if (st.nextOnRetryMs > nowMs) return;

    bool shouldIssue = !relayOnCommandIssued[relayId];
    const bool isOffState = (strcmp(st.stateText, "OFF") == 0);
    if (!shouldIssue && !st.relayOn && st.safetyClosed && isOffState && !st.feedbackFaultLatched) {
        shouldIssue = true;
        relayOnCommandIssued[relayId] = false;
    }
    if (!shouldIssue) return;

    String resp;
    const String tx = "CMD," + String(relayId) + ",ON!";
    if (debugViewData) {
        Serial.printf("[STANDALONE][CMD-TX] -> %s\n", tx.c_str());
    }
    if (!standaloneExchangeFrame(tx, resp, 220)) {
        return;
    }
    if (debugViewData) {
        Serial.printf("[STANDALONE][CMD-RX] ID %d <- %s!\n", relayId, resp.c_str());
    }

    const String okPrefix = "OK,RELAY,CMD," + String(relayId) + ",ON";
    const String errPrefix = "ERR,RELAY,CMD," + String(relayId) + ",ON";
    if (resp.startsWith(okPrefix) || resp.startsWith(errPrefix)) {
        relayOnCommandIssued[relayId] = true;

        if (resp.indexOf("FEEDBACK_FAULT_LATCHED") >= 0) {
            st.feedbackFaultLatched = true;
            st.lampFault = true;
            st.nextOnRetryMs = nowMs + 15000UL; // evita spam ON continuo su fault latched.
        } else {
            st.nextOnRetryMs = 0;
        }
    }

    if (resp.startsWith(okPrefix) && resp.indexOf(",ON,1,") >= 0) {
        st.feedbackFaultLatched = false;
        st.lampFault = false;
        st.nextOnRetryMs = 0;
    }
}

static void standaloneIssueOffCommandIfNeeded(int relayId) {
    StandaloneRelayStatus& st = standaloneRelay[relayId];
    if (!st.detectedAtBoot || !st.online || st.modeMismatch) {
        relayOnCommandIssued[relayId] = false;
        return;
    }

    if (!st.relayOn && !relayOnCommandIssued[relayId]) return;

    String resp;
    const String tx = "CMD," + String(relayId) + ",OFF!";
    if (debugViewData) {
        Serial.printf("[STANDALONE][CMD-TX] -> %s\n", tx.c_str());
    }
    if (!standaloneExchangeFrame(tx, resp, 220)) {
        return;
    }
    if (debugViewData) {
        Serial.printf("[STANDALONE][CMD-RX] ID %d <- %s!\n", relayId, resp.c_str());
    }

    const String okPrefix = "OK,RELAY,CMD," + String(relayId) + ",OFF";
    if (resp.startsWith(okPrefix)) {
        relayOnCommandIssued[relayId] = false;
        st.relayOn = false;
        st.feedbackFaultLatched = false;
        st.lampFault = false;
        st.nextOnRetryMs = 0;
    }
}

static bool standaloneIsSystemSafetyInterlockActive(unsigned long nowMs) {
    if (config.usaSicurezzaLocale && (digitalRead(PIN_MASTER_SICUREZZA) == HIGH)) {
        return true;
    }

    for (int relayId = 1; relayId <= 4; relayId++) {
        if (!relayBoardDetected[relayId]) continue;
        standaloneRefreshDerivedFlags(relayId, nowMs);
        if (standaloneRelay[relayId].safetyAlarm) {
            return true;
        }
    }
    return false;
}

static void standaloneSetSelectionVisual(int selection, bool on) {
    const LedState state = on ? LED_SOLID : LED_OFF;
    standaloneLedBal1.setState(LED_OFF);
    standaloneLedBal2.setState(LED_OFF);
    standaloneLedBal3.setState(LED_OFF);
    standaloneLedBal4.setState(LED_OFF);

    if (!on || selection < 0) return;

    if (selection == 0) standaloneLedBal1.setState(state);
    else if (selection == 1) standaloneLedBal2.setState(state);
    else if (selection == 2) standaloneLedBal3.setState(state);
    else if (selection == 3) standaloneLedBal4.setState(state);
    else if (selection == 4) {
        standaloneLedBal1.setState(state);
        standaloneLedBal2.setState(state);
        standaloneLedBal3.setState(state);
        standaloneLedBal4.setState(state);
    }
}

static bool standaloneSendResetCountersCommand(int relayId) {
    for (uint8_t attempt = 0; attempt < STANDALONE_RESET_CMD_RETRIES; attempt++) {
        String resp;
        const String tx = "CNT" + String(relayId) + ":RESET!";
        if (debugViewData) {
            Serial.printf("[STANDALONE][RESET-TX] -> %s\n", tx.c_str());
        }
        if (!standaloneExchangeFrame(tx, resp, 450)) {
            continue;
        }
        if (debugViewData) {
            Serial.printf("[STANDALONE][RESET-RX] ID %d <- %s!\n", relayId, resp.c_str());
        }
        const String expected = "OK,CFG,CNT," + String(relayId) + ",RESET";
        if (resp.startsWith(expected)) {
            return true;
        }
    }
    return false;
}

static void standaloneAnimateResetAck() {
    for (int i = 0; i < 5; i++) {
        standaloneSetSelectionVisual(standaloneResetSelection, true);
        standaloneLedExpLife.setState(LED_SOLID);
        standaloneLedSafety.setState(LED_OFF);
        standaloneUpdateLedObjects();
        delay(180);

        standaloneSetSelectionVisual(standaloneResetSelection, false);
        standaloneLedExpLife.setState(LED_OFF);
        standaloneLedSafety.setState(LED_OFF);
        standaloneUpdateLedObjects();
        delay(180);
    }
}

static bool standaloneExecuteResetSelection() {
    int targets[4] = {0, 0, 0, 0};
    int targetCount = 0;

    if (standaloneResetSelection >= 0 && standaloneResetSelection <= 3) {
        const int relayId = standaloneResetSelection + 1;
        if (!relayBoardDetected[relayId]) {
            Serial.printf("[STANDALONE][RESET] %s non disponibile (IP non rilevato).\n",
                          standaloneSelectionName(standaloneResetSelection));
            return false;
        }
        targets[targetCount++] = relayId;
    } else if (standaloneResetSelection == 4) {
        for (int relayId = 1; relayId <= 4; relayId++) {
            if (relayBoardDetected[relayId]) {
                targets[targetCount++] = relayId;
            }
        }
        if (targetCount == 0) {
            Serial.println("[STANDALONE][RESET] Nessuna scheda relay disponibile per reset contatori.");
            return false;
        }
    } else {
        Serial.println("[STANDALONE][RESET] Selezione non valida.");
        return false;
    }

    standaloneAnimateResetAck();

    bool allOk = true;
    for (int i = 0; i < targetCount; i++) {
        const int relayId = targets[i];
        const bool ok = standaloneSendResetCountersCommand(relayId);
        if (!ok) {
            allOk = false;
            Serial.printf("[STANDALONE][RESET] Fallito reset contatori su ID %d.\n", relayId);
        } else {
            relayOnCommandIssued[relayId] = false;
            standaloneRelay[relayId].lastResponseMs = 0;
            standaloneRelay[relayId].online = false;
            standaloneRelay[relayId].hours = 0.0f;
            standaloneRelay[relayId].lifetimeAlarm = false;
            Serial.printf("[STANDALONE][RESET] OK contatori azzerati su ID %d.\n", relayId);
        }
    }

    if (allOk) {
        Serial.println("[STANDALONE][RESET] Reset contatori confermato, riavvio controller...");
        delay(250);
        ESP.restart();
    }
    return allOk;
}

static void standaloneSetHoldPreviewMode(StandaloneHoldPreviewMode mode, unsigned long nowMs) {
    if (standaloneHoldPreviewMode == mode) return;
    standaloneHoldPreviewMode = mode;
    standaloneHoldPreviewStartMs = nowMs;

    if (mode == STANDALONE_HOLD_PREVIEW_RESET) {
        Serial.println("[STANDALONE][RESET] Hold 5s: rilascia per entrare in reset ore (continua fino a 10s per WiFi).");
    } else if (mode == STANDALONE_HOLD_PREVIEW_WIFI) {
        Serial.println("[STANDALONE][WIFI] Hold 10s: rilascia per avviare il WiFi locale.");
    }
}

static void standaloneHandleResetButton(unsigned long nowMs) {
    const bool pressed = (digitalRead(MK_PIN_BUTTON) == LOW);

    if (pressed && !standaloneButtonPrevPressed) {
        standaloneButtonPressStartMs = nowMs;
        standaloneButtonLongActionDone = false;
        standaloneSetHoldPreviewMode(STANDALONE_HOLD_PREVIEW_NONE, nowMs);
    }

    if (pressed && !standaloneResetModeActive) {
        const unsigned long heldMs = nowMs - standaloneButtonPressStartMs;
        StandaloneHoldPreviewMode targetPreview = STANDALONE_HOLD_PREVIEW_NONE;
        if (heldMs >= STANDALONE_WIFI_PORTAL_HOLD_MS) {
            targetPreview = STANDALONE_HOLD_PREVIEW_WIFI;
        } else if (heldMs >= STANDALONE_ENTER_RESET_HOLD_MS) {
            targetPreview = STANDALONE_HOLD_PREVIEW_RESET;
        }
        standaloneSetHoldPreviewMode(targetPreview, nowMs);
    } else if (pressed) {
        standaloneSetHoldPreviewMode(STANDALONE_HOLD_PREVIEW_NONE, nowMs);
    }

    if (pressed && standaloneResetModeActive && !standaloneButtonLongActionDone && standaloneResetSelection >= 0) {
        const unsigned long heldMs = nowMs - standaloneButtonPressStartMs;
        if (heldMs >= STANDALONE_CONFIRM_RESET_HOLD_MS) {
            standaloneButtonLongActionDone = true;
            const bool ok = standaloneExecuteResetSelection();
            if (!ok) {
                Serial.println("[STANDALONE][RESET] Operazione non completata. Rimango in modalita' reset.");
            }
        }
    }

    if (!pressed && standaloneButtonPrevPressed) {
        const unsigned long heldMs = nowMs - standaloneButtonPressStartMs;
        standaloneSetHoldPreviewMode(STANDALONE_HOLD_PREVIEW_NONE, nowMs);

        if (!standaloneResetModeActive) {
            if (heldMs >= STANDALONE_WIFI_PORTAL_HOLD_MS) {
                standaloneButtonLongActionDone = true;
                requestStandaloneWifiPortalStart();
                Serial.println("[STANDALONE][WIFI] Richiesta avvio WiFi locale (hold 10s).");
            } else if (heldMs >= STANDALONE_ENTER_RESET_HOLD_MS) {
                standaloneResetModeActive = true;
                standaloneResetSelection = -1;
                standaloneButtonLongActionDone = true;
                Serial.println("[STANDALONE][RESET] Modalita' reset ore attiva. Premi il tasto per selezionare BAL.");
            }
        } else {
            if (standaloneResetSelection < 0) {
                if (heldMs >= STANDALONE_ENTER_RESET_HOLD_MS) {
                    standaloneResetModeActive = false;
                    standaloneButtonLongActionDone = true;
                    Serial.println("[STANDALONE][RESET] Uscita dalla modalita' reset ore.");
                } else if (!standaloneButtonLongActionDone) {
                    standaloneResetSelection = 0;
                    Serial.printf("[STANDALONE][RESET] Selezione: %s\n", standaloneSelectionName(standaloneResetSelection));
                }
            } else if (!standaloneButtonLongActionDone && heldMs < STANDALONE_CONFIRM_RESET_HOLD_MS) {
                standaloneResetSelection++;
                if (standaloneResetSelection > 4) standaloneResetSelection = 0;
                Serial.printf("[STANDALONE][RESET] Selezione: %s\n", standaloneSelectionName(standaloneResetSelection));
            }
        }
    }

    standaloneButtonPrevPressed = pressed;
}

static bool standaloneApplyHoldPreviewLeds(unsigned long nowMs) {
    if (standaloneHoldPreviewMode == STANDALONE_HOLD_PREVIEW_NONE) {
        return false;
    }

    standaloneLedExpLife.setState(LED_OFF);
    standaloneLedSafety.setState(LED_OFF);

    if (standaloneHoldPreviewMode == STANDALONE_HOLD_PREVIEW_WIFI) {
        const unsigned long elapsed = nowMs - standaloneHoldPreviewStartMs;
        const int frame = static_cast<int>((elapsed / STANDALONE_HOLD_WIFI_SWEEP_STEP_MS) % 6UL);
        standaloneApplySweepFrame(frame);
        return true;
    }

    const unsigned long activePhaseMs = static_cast<unsigned long>(STANDALONE_RESET_PREVIEW_BLINK_COUNT) * 2UL * STANDALONE_RESET_PREVIEW_BLINK_STEP_MS;
    const unsigned long cycleMs = activePhaseMs + STANDALONE_RESET_PREVIEW_CYCLE_PAUSE_MS;
    const unsigned long elapsedCycle = (nowMs - standaloneHoldPreviewStartMs) % cycleMs;
    bool on = false;
    if (elapsedCycle < activePhaseMs) {
        const unsigned long phase = elapsedCycle / STANDALONE_RESET_PREVIEW_BLINK_STEP_MS;
        on = (phase % 2UL) == 0UL;
    }
    standaloneSetAllBalLeds(on ? LED_SOLID : LED_OFF);
    return true;
}

static void standaloneApplyResetModeLeds() {
    standaloneSetSelectionVisual(standaloneResetSelection, true);
    standaloneLedExpLife.setState(LED_OFF);
    standaloneLedSafety.setState(LED_OFF);
}

static void standaloneApplyNormalLeds(unsigned long nowMs) {
    int detectedCount = 0;
    int relaySafetyOpenCount = 0;
    int lifetimeAlarmCount = 0;
    const bool masterSafetyConsidered = config.usaSicurezzaLocale;
    const bool masterSafetyOpen = masterSafetyConsidered && (digitalRead(PIN_MASTER_SICUREZZA) == HIGH);

    for (int relayId = 1; relayId <= 4; relayId++) {
        Led* balLed = standaloneBalLedByRelayId(relayId);
        if (!balLed) continue;

        if (!relayBoardDetected[relayId]) {
            balLed->setState(LED_OFF);
            continue;
        }

        detectedCount++;
        standaloneRefreshDerivedFlags(relayId, nowMs);

        const StandaloneRelayStatus& st = standaloneRelay[relayId];
        if (st.safetyAlarm) relaySafetyOpenCount++;
        if (st.lifetimeAlarm) lifetimeAlarmCount++;

        const bool fastProblem = (!st.online) || st.modeMismatch || st.safetyAlarm || st.lifetimeAlarm;
        LedState balState = LED_SOLID;
        if (!st.online) balState = LED_BLINK_FAST;
        else if (fastProblem) balState = LED_BLINK_FAST;
        else if (st.lampFault) balState = LED_BLINK_SLOW;
        else balState = LED_SOLID;

        balLed->setState(balState);
    }

    const int totalSafeties = detectedCount + (masterSafetyConsidered ? 1 : 0);
    const int openSafeties = relaySafetyOpenCount + (masterSafetyOpen ? 1 : 0);
    if (openSafeties == 0) {
        standaloneLedSafety.setState(LED_OFF);
    } else if (totalSafeties > 0 && openSafeties >= totalSafeties) {
        standaloneLedSafety.setState(LED_SOLID);
    } else {
        standaloneLedSafety.setState(LED_BLINK_SLOW);
    }

    if (lifetimeAlarmCount == 0) {
        standaloneLedExpLife.setState(LED_OFF);
    } else if (detectedCount > 0 && lifetimeAlarmCount == detectedCount) {
        standaloneLedExpLife.setState(LED_SOLID);
    } else {
        standaloneLedExpLife.setState(LED_BLINK_SLOW);
    }
}

void scansionaSlaveStandalone() {
    Serial.println("[SCAN] Avvio scansione RELAY (Standalone)...");

    standaloneBeginKeyboardIfNeeded();
    standaloneLastFrameTxMs = 0;
    standaloneSafetyInterlockWasActive = false;

    for (int i = 0; i < 5; i++) {
        relayBoardDetected[i] = false;
        relayOnCommandIssued[i] = false;
        standaloneRelay[i] = {};
        standaloneRelay[i].stateText[0] = '\0';
    }
    for (int i = 1; i <= 30; i++) {
        databaseSlave[i] = {};
        listaPerifericheAttive[i] = false;
    }

    int foundCount = 0;
    int unsupportedCount = 0;
    const unsigned long nowMs = millis();

    // Scansione estesa: rileva eventuali schede non conformi ma abilita solo Relay UVC su ID 1..4.
    for (int i = 1; i <= 30; i++) {
        if (i <= static_cast<int>(STANDALONE_BOOT_SWEEP_CYCLES) * 6) {
            standaloneShowScanSweepFrame((i - 1) % 6); // Cicli sweep configurabili durante ricerca.
        }
        while (Serial1.available()) Serial1.read();
        if (debugViewData) {
            Serial.printf("[SCAN-TX] -> ?%d!\n", i);
        }
        modoTrasmissione();
        Serial1.printf("?%d!", i);
        modoRicezione();

        unsigned long startWait = millis();
        while (millis() - startWait < 90) {
            if (!Serial1.available()) continue;
            String resp = Serial1.readStringUntil('!');
            if (debugViewData) {
                Serial.printf("[SCAN-RX] <- %s!\n", resp.c_str());
            }
            if (!resp.startsWith("OK")) break;

            if (!resp.startsWith("OK,RELAY,")) {
                unsupportedCount++;
                Serial.printf("[SCAN] ID %d ignorato: non e' una relay.\n", i);
                break;
            }

            cacheRelayMetadata(resp, i);

            StandaloneRelayStatus parsed = {};
            parsed.stateText[0] = '\0';
            if (!standaloneParseRelayStatus(resp, parsed)) {
                unsupportedCount++;
                Serial.printf("[SCAN] ID %d relay con formato status non valido.\n", i);
                break;
            }

            if (parsed.mode != 2) {
                unsupportedCount++;
                Serial.printf("[SCAN] ID %d relay ignorata: mode=%d (richiesto UVC=2).\n", i, parsed.mode);
                break;
            }

            if (i > 4) {
                unsupportedCount++;
                Serial.printf("[SCAN] ID %d relay UVC ignorata: fuori range Standalone (1..4).\n", i);
                break;
            }

            relayBoardDetected[i] = true;
            listaPerifericheAttive[i] = true;
            parsed.detectedAtBoot = true;
            parsed.online = true;
            parsed.lastResponseMs = nowMs;
            parsed.lastPollMs = nowMs;
            standaloneRelay[i] = parsed;
            foundCount++;
            Serial.printf("[SCAN] ID %d: Relay UVC valida.\n", i);
            break;
        }
    }

    if (unsupportedCount > 0) {
        Serial.printf("[SCAN] Trovate %d schede non conformi/non gestite (ignorate).\n", unsupportedCount);
    }
    Serial.printf("[SCAN] Scansione terminata. Relay UVC valide: %d\n", foundCount);
    standaloneShowDetectedRelaySequence();
}

void RS485_Master_Standalone_Loop() {
    standaloneBeginKeyboardIfNeeded();
    const unsigned long nowMs = millis();

    standaloneHandleResetButton(nowMs);

    if (nowMs - standaloneLastPollMs >= STANDALONE_POLL_INTERVAL_MS) {
        standaloneLastPollMs = nowMs;

        for (int relayId = 1; relayId <= 4; relayId++) {
            if (!relayBoardDetected[relayId]) continue;

            standaloneRelay[relayId].lastPollMs = nowMs;
            standalonePollRelayStatus(relayId, nowMs);
            standaloneRefreshDerivedFlags(relayId, nowMs);
        }

        const bool safetyInterlockActive = standaloneIsSystemSafetyInterlockActive(nowMs);
        if (safetyInterlockActive) {
            if (!standaloneSafetyInterlockWasActive) {
                Serial.println("[STANDALONE][SAFETY] Interlock attivo: OFF su tutte le relay.");
                standaloneSafetyInterlockWasActive = true;
            }
            for (int relayId = 1; relayId <= 4; relayId++) {
                if (!relayBoardDetected[relayId]) continue;
                standaloneIssueOffCommandIfNeeded(relayId);
            }
        } else {
            if (standaloneSafetyInterlockWasActive) {
                Serial.println("[STANDALONE][SAFETY] Interlock rientrato: riprendo gestione ON.");
                standaloneSafetyInterlockWasActive = false;
            }
            for (int relayId = 1; relayId <= 4; relayId++) {
                if (!relayBoardDetected[relayId]) continue;
                standaloneIssueOnCommandIfNeeded(relayId);
            }
        }

        for (int relayId = 1; relayId <= 4; relayId++) {
            if (!relayBoardDetected[relayId]) continue;
            standaloneRefreshDerivedFlags(relayId, millis());
        }
    }

    if (standaloneApplyHoldPreviewLeds(nowMs)) {
        // Preview "rilascia per entrare" durante pressione lunga.
    } else if (standaloneResetModeActive) {
        standaloneApplyResetModeLeds();
    } else {
        standaloneApplyNormalLeds(nowMs);
    }

    standaloneUpdateLedObjects();
}

bool getStandaloneRelaySnapshot(int relayId, StandaloneRelaySnapshot& outSnapshot) {
    if (relayId < 1 || relayId > 4) {
        return false;
    }

    const StandaloneRelayStatus& st = standaloneRelay[relayId];
    outSnapshot.detectedAtBoot = st.detectedAtBoot;
    outSnapshot.online = st.online;
    outSnapshot.relayOn = st.relayOn;
    outSnapshot.safetyClosed = st.safetyClosed;
    outSnapshot.feedbackMatched = st.feedbackMatched;
    outSnapshot.safetyAlarm = st.safetyAlarm;
    outSnapshot.lifetimeAlarm = st.lifetimeAlarm;
    outSnapshot.lampFault = st.lampFault;
    outSnapshot.modeMismatch = st.modeMismatch;
    outSnapshot.feedbackFaultLatched = st.feedbackFaultLatched;
    outSnapshot.lifeLimitHours = st.lifeLimitHours;
    outSnapshot.mode = st.mode;
    outSnapshot.starts = st.starts;
    outSnapshot.hours = st.hours;
    strncpy(outSnapshot.stateText, st.stateText, sizeof(outSnapshot.stateText));
    outSnapshot.stateText[sizeof(outSnapshot.stateText) - 1] = '\0';

    return st.detectedAtBoot;
}

// Report progresso OTA con seriale slave esplicito (utile prima di avviare la state machine OTA)
void reportSlaveProgressFor(String slaveSn, String status, String message) {
    if (WiFi.status() != WL_CONNECTED || String(config.apiUrl).length() < 5) return;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String reportUrl = String(config.apiUrl);
    int lastSlash = reportUrl.lastIndexOf('/');
    if (lastSlash != -1) {
        reportUrl = reportUrl.substring(0, lastSlash + 1) + "api_slave_ota_report.php";
    }

    http.begin(client, reportUrl);
    http.addHeader("Content-Type", "application/json");

    message.replace("\"", "'");

    String jsonPayload = "{";
    jsonPayload += "\"api_key\":\"" + String(config.apiKey) + "\",";
    jsonPayload += "\"slave_sn\":\"" + slaveSn + "\",";
    jsonPayload += "\"status\":\"" + status + "\",";
    jsonPayload += "\"message\":\"" + message + "\"";
    jsonPayload += "}";

    if (debugViewData) {
        Serial.println("[RS485-OTA] >> Report(" + slaveSn + "): " + status + " - " + message);
    }
    http.POST(jsonPayload);
    http.end();
}

// Alias neutro mantenuto in parallelo alla nomenclatura RS485 legacy.
void RS485_Controller_Loop() {
    RS485_Master_Loop();
}
