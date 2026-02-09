#include "RS485_Manager.h"
#include "GestioneMemoria.h"
#include <HTTPClient.h>
#include "Pins.h"
#include "Led.h"
#include <SPIFFS.h>
#include <MD5Builder.h>

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_master.cpp').
// Questo ci permette di accedervi e modificarle da questo file.
extern bool listaPerifericheAttive[101];
extern DatiSlave databaseSlave[101];
extern unsigned long timerScansione;
extern unsigned long timerStampa;
extern bool scansioneInCorso;
extern bool qualchePerifericaTrovata;
extern bool debugViewData;
extern int partialFailCycles;
extern Led greenLed;
extern Impostazioni config; // Mi serve per la API key
extern float currentDeltaP; // Variabile globale definita in main_master.cpp

// Variabili per la modalità Standalone
bool relayBoardDetected[5]; // Indici 1-4 usati

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
const int OTA_TIMEOUT_MS = 2000;   // Timeout attesa risposta
const int OTA_CHUNK_SIZE = 64;     // Dimensione pacchetto dati (64 byte binari -> 128 byte hex)

// Imposta il pin di direzione del transceiver RS485 su LOW per metterlo in ascolto.
void modoRicezione() { Serial1.flush(); digitalWrite(PIN_RS485_DIR, LOW); }
// Imposta il pin di direzione del transceiver RS485 su HIGH per abilitare la trasmissione.
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(50); }

// Funzione per analizzare la stringa di risposta ricevuta da uno slave.
// Estrae i dati (temperatura, pressione, etc.) e li salva nel database.
void parseDatiSlave(String payload, int address) {
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
        modoTrasmissione();
        Serial1.printf("?%d!", i); // Invia una richiesta all'indirizzo 'i'.
        modoRicezione();
        
        unsigned long startWait = millis();
        while (millis() - startWait < 50) { // Aspetta una risposta per 50ms.
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (resp.startsWith("OK")) {
                    listaPerifericheAttive[i] = true;
                    qualchePerifericaTrovata = true;
                    databaseSlave[i].lastResponseTime = millis(); // Inizializza il timestamp
                    Serial.printf("[SCAN] Trovato Slave ID: %d\n", i);
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
        return;
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
        otaSlaveFile.close();
        otaSlaveActive = false;
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
    otaSlaveActive = true;
    otaSlaveState = 1; // Imposta stato iniziale: Handshake
    otaSlaveCurrentOffset = 0;
    otaSlaveRetryCount = 0;
    otaSlaveLastActionTime = 0; // Forza azione immediata
    otaSlaveLastProgressReport = 0;
    Serial.printf("[RS485-OTA] Avvio procedura per Slave ID %d (SN: %s). File size: %d bytes.\n", otaSlaveTargetId, slaveSn.c_str(), otaSlaveFile.size());
    
    // REPORT IMMEDIATO: Sblocca il popup web dallo stato "Pending"
    reportSlaveProgress("Handshake", "Master pronto. Contatto lo slave...");
}

void gestisciAggiornamentoSlave() {
    if (!otaSlaveActive) return;

    unsigned long now = millis();

    switch (otaSlaveState) {
        case 1: // SEND HANDSHAKE
            if (now - otaSlaveLastActionTime > 1000) {
                Serial.printf("[RS485-OTA] >> Invio START (Size: %d) a ID %d\n", otaSlaveFile.size(), otaSlaveTargetId);
                modoTrasmissione();
                // Invia Dimensione e MD5
                Serial1.printf("OTA,START,%d,%s!", otaSlaveFile.size(), otaSlaveMD5.c_str());
                modoRicezione();
                otaSlaveLastActionTime = now;
                otaSlaveState = 2; // WAIT HANDSHAKE
            }
            break;

        case 2: // WAIT HANDSHAKE RESPONSE
            if (now - otaSlaveLastActionTime > OTA_TIMEOUT_MS) {
                otaSlaveRetryCount++;
                Serial.printf("[RS485-OTA] Timeout attesa READY (%d/%d)\n", otaSlaveRetryCount, OTA_MAX_RETRIES);
                if (otaSlaveRetryCount >= OTA_MAX_RETRIES) {
                    Serial.println("[RS485-OTA] ERRORE: Nessuna risposta allo START. Abort.");
                    otaSlaveActive = false;
                    reportSlaveProgress("Failed", "Lo slave non risponde al comando di avvio.");
                    otaSlaveFile.close();
                } else {
                    otaSlaveState = 1; // Riprova Handshake
                    otaSlaveLastActionTime = now;
                }
            }
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[RS485-OTA] << RX: " + resp);
                if (resp.startsWith("OK,OTA,READY")) {
                    Serial.println("[RS485-OTA] Slave PRONTO. Inizio invio dati...");
                    reportSlaveProgress("Sending data", "Trasferimento in corso...");
                    otaSlaveState = 3; // SEND CHUNK
                    otaSlaveCurrentOffset = 0;
                    otaSlaveRetryCount = 0;
                    otaSlaveLastActionTime = 0;
                }
            }
            break;

        case 3: // SEND CHUNK
             if (now - otaSlaveLastActionTime > 50) { // Piccolo delay per stabilità
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
                        otaSlaveActive = false;
                        reportSlaveProgress("Failed", "Errore lettura file locale.");
                        otaSlaveFile.close();
                        return;
                    }
                }
                // ------------------------------------

                String hexData = bufferToHex(buff, bytesRead);
                
                Serial.printf("[RS485-OTA] >> Invio CHUNK Offset %d (Len %d)\n", otaSlaveCurrentOffset, bytesRead);
                modoTrasmissione();
                Serial1.printf("OTA,DATA,%d,%s!", otaSlaveCurrentOffset, hexData.c_str());
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
                    otaSlaveActive = false;
                    reportSlaveProgress("Failed", "Timeout durante il trasferimento dati.");
                    otaSlaveFile.close();
                } else {
                    otaSlaveState = 3; // Riprova invio stesso chunk
                    otaSlaveLastActionTime = now; 
                }
            }
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                if (resp.startsWith("OK,OTA,ACK")) {
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
            Serial.println("[RS485-OTA] >> Invio END");
            modoTrasmissione();
            Serial1.print("OTA,END!");
            modoRicezione();
            reportSlaveProgress("Finalizing", "Verifica integrità e scrittura...");
            otaSlaveLastActionTime = now;
            otaSlaveState = 6; // WAIT SUCCESS
            otaSlaveRetryCount = 0;
            break;

        case 6: // WAIT SUCCESS
             if (now - otaSlaveLastActionTime > 10000) { // Timeout lungo per scrittura flash slave
                otaSlaveRetryCount++;
                if (otaSlaveRetryCount >= 2) {
                     Serial.println("[RS485-OTA] ERRORE: Nessuna conferma finale.");
                     otaSlaveActive = false;
                     reportSlaveProgress("Failed", "Lo slave non ha dato conferma finale.");
                     otaSlaveFile.close();
                } else {
                    otaSlaveState = 5; // Riprova invio END
                    otaSlaveLastActionTime = now;
                }
             }
             if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                Serial.println("[RS485-OTA] << RX: " + resp);
                if (resp.startsWith("OK,OTA,SUCCESS")) {
                    Serial.println("[RS485-OTA] AGGIORNAMENTO SLAVE COMPLETATO CON SUCCESSO!");
                    otaSlaveActive = false;
                    reportSlaveProgress("Success", "Aggiornamento completato.");
                    otaSlaveFile.close();
                } else if (resp.startsWith("OK,OTA,FAIL")) {
                    Serial.println("[RS485-OTA] ERRORE: Lo slave ha riportato fallimento.");
                    otaSlaveActive = false;
                    reportSlaveProgress("Failed", "Lo slave ha riportato un fallimento (MD5 errato o errore scrittura).");
                    otaSlaveFile.close();
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

        // Conta quanti slave dovrebbero rispondere in base all'ultima scansione.
        for (int i = 1; i <= 100; i++) if (listaPerifericheAttive[i]) activeSlavesCount++;

        // Se c'è almeno uno slave attivo...
        if (activeSlavesCount > 0) {
            // ...interroga ogni slave che è risultato attivo durante la scansione.
            for (int i = 1; i <= 100; i++) {
                if (listaPerifericheAttive[i]) {
                    modoTrasmissione(); Serial1.printf("?%d!", i); modoRicezione();
                    unsigned long st = millis();
                    // Aspetta una risposta per 100ms.
                    while (millis() - st < 100) {
                        if (Serial1.available()) {
                            String d = Serial1.readStringUntil('!');
                            // Se la risposta è valida ("OK..."), incrementa il contatore
                            // e analizza i dati.
                            if (d.startsWith("OK")) {
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
        // Se abbiamo sia Slave 1 che Slave 2, calcoliamo la differenza
        if (listaPerifericheAttive[1] && listaPerifericheAttive[2]) {
            currentDeltaP = databaseSlave[1].p - databaseSlave[2].p;
        } else {
            currentDeltaP = 0.0;
        }
    }
}

// --- FUNZIONI PER MODALITA' STANDALONE (MODE 1) ---

void scansionaSlaveStandalone() {
    Serial.println("[SCAN] Avvio scansione RELAY (Standalone)...");
    
    // Reset array rilevamento
    for(int i=0; i<5; i++) relayBoardDetected[i] = false;
    
    int foundCount = 0;
    int unsupportedCount = 0;

    // Scansiona fino a 30 per rilevare eventuali schede in eccesso
    for (int i = 1; i <= 30; i++) {
        modoTrasmissione();
        Serial1.printf("?%d!", i);
        modoRicezione();
        
        unsigned long startWait = millis();
        while (millis() - startWait < 50) {
            if (Serial1.available()) {
                String resp = Serial1.readStringUntil('!');
                // Protocollo atteso Relay: OK,RELAY,MODE,STATE,... (es. OK,RELAY,2,0!)
                
                if (resp.startsWith("OK")) {
                    Serial.printf("[SCAN] Risposta da ID %d: %s\n", i, resp.c_str());
                    
                    // Verifica se è una scheda Relay
                    if (resp.indexOf("RELAY") != -1) {
                        // Estrae il Modo (terzo campo, dopo OK e RELAY)
                        int firstComma = resp.indexOf(',');
                        int secondComma = resp.indexOf(',', firstComma+1);
                        int thirdComma = resp.indexOf(',', secondComma+1);
                        
                        int mode = -1;
                        if (secondComma != -1 && thirdComma != -1) {
                            mode = resp.substring(secondComma+1, thirdComma).toInt();
                        }

                        if (mode == 2) { // Deve essere Mode 2 (UVC)
                            if (i <= 4) {
                                relayBoardDetected[i] = true;
                                foundCount++;
                                Serial.printf("[SCAN] ID %d: Relay Mode 2 -> OK\n", i);
                            } else {
                                Serial.printf("[ALARM] ID %d: Relay rilevato ma ID > 4 (Non gestito)!\n", i);
                                unsupportedCount++;
                            }
                        } else {
                            Serial.printf("[ALARM] ID %d: Relay rilevato ma Mode %d errato (Richiesto 2)!\n", i, mode);
                            unsupportedCount++;
                        }
                    } else {
                        // Risposta ricevuta ma non contiene la firma "RELAY"
                        Serial.printf("[ALARM] ID %d: Scheda sconosciuta o non Relay!\n", i);
                        unsupportedCount++;
                    }
                }
                break; 
            }
        }
    }
    
    if (unsupportedCount > 0) {
        Serial.printf("!!! ATTENZIONE: Trovate %d schede non conformi o fuori range !!!\n", unsupportedCount);
    }
    Serial.printf("[SCAN] Scansione terminata. Relay validi trovati: %d\n", foundCount);
}

void RS485_Master_Standalone_Loop() {
    static unsigned long lastPoll = 0;
    // Polling ogni 1 secondo
    if (millis() - lastPoll < 1000) return; 
    lastPoll = millis();

    for (int i = 1; i <= 4; i++) {
        // Mappatura LED (da Manuale e richiesta):
        // ID 1 -> BAL1 (Pin 4) -> PIN_LED_EXT_4
        // ID 2 -> BAL2 (Pin 3) -> PIN_LED_EXT_3
        // ID 3 -> BAL3 (Pin 2) -> PIN_LED_EXT_2
        // ID 4 -> BAL4 (Pin 1) -> PIN_LED_EXT_1
        int pinLed = -1;
        if (i==1) pinLed = PIN_LED_EXT_4;
        else if (i==2) pinLed = PIN_LED_EXT_3;
        else if (i==3) pinLed = PIN_LED_EXT_2;
        else if (i==4) pinLed = PIN_LED_EXT_1;

        if (relayBoardDetected[i]) {
            // Invia comando di accensione
            modoTrasmissione();
            Serial1.printf("CMD,%d,ON!", i); 
            modoRicezione();
            
            // Per ora accendiamo il LED se la scheda è stata rilevata e (presumibilmente) attivata.
            // In futuro potremmo leggere la risposta per confermare l'accensione effettiva.
            if (pinLed != -1) digitalWrite(pinLed, HIGH);
            
        } else {
            // Se la scheda non è presente, LED spento
            if (pinLed != -1) digitalWrite(pinLed, LOW);
        }
    }
}