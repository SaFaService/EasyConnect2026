#include "RS485_Manager.h"
#include "RS485_Slave.h"
#include "GestioneMemoria.h"
#include "Pins.h"
#include <Preferences.h>
#include <Update.h> // Necessario per OTA
#include <SPIFFS.h>
#include <MD5Builder.h>
#include <ESP.h>

// La parola chiave 'extern' indica al compilatore che queste variabili sono definite
// in un altro file (in questo caso, in 'main_slave.cpp').
// Questo ci permette di accedervi e leggerle da questo file.
extern Impostazioni config;
extern Preferences memoria;
extern const char* FW_VERSION;
extern float tempSHTC3, humSHTC3, pressioneMS;
extern bool statoSicurezza;
extern bool debugViewData;
extern unsigned long lastAny485Activity;
extern unsigned long lastMy485Request;

// Variabili globali per OTA Slave
bool otaInProgress = false;
size_t otaTotalSize = 0;
String otaExpectedMD5 = "";
size_t otaWrittenSize = 0;
MD5Builder otaRunningMd5;
bool otaRunningMd5Active = false;

// Variabili per TEST MODE
File testFile;

// Imposta il pin di direzione del transceiver RS485 su LOW per metterlo in ascolto.
void modoRicezione() { 
    Serial1.flush();                  // Attende fine TX UART
    digitalWrite(PIN_RS485_DIR, LOW); // Rilascia subito il bus per evitare contese
    delayMicroseconds(80);            // Piccolo tempo di assestamento del transceiver
}
// Imposta il pin di direzione del transceiver RS485 su HIGH per abilitare la trasmissione.
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(80); }

// Funzione principale del gestore RS485 per lo Slave, da chiamare nel loop().
void RS485_Slave_Loop() {
    // Se ci sono dati in arrivo sulla linea RS485...
    if (Serial1.available()) {
        lastAny485Activity = millis(); // ...aggiorna il timer dell'ultima attività rilevata.
        String richiesta = Serial1.readStringUntil('!'); // ...leggi la stringa fino al carattere '!'.
        
        // Gestione comandi OTA
        if (richiesta.startsWith("OTA,") || richiesta.startsWith("TEST,")) {
            if (debugViewData) Serial.println("RX OTA: " + richiesta);
            processOTACommand(richiesta);
            return;
        }

        // Se la richiesta è una domanda (inizia con '?')...
        if (richiesta.startsWith("?")) {
            if (debugViewData) Serial.println("RX 485: " + richiesta);

            // ...estrae l'indirizzo a cui è rivolta la domanda.
            int ipChiesto = richiesta.substring(1).toInt();
            // Se l'indirizzo corrisponde a quello di questa scheda...
            if (ipChiesto == config.indirizzo485) {
                lastMy485Request = millis(); // ...aggiorna il timer dell'ultima richiesta ricevuta.
                char buffer[150];
                // ...prepara la stringa di risposta con tutti i dati richiesti.
                // La pressione viene inviata in Pascal (il valore in mBar viene moltiplicato per 100).
                snprintf(buffer, sizeof(buffer), "OK,%.2f,%.2f,%.2f,%d,%d,%s,%s!", 
                         tempSHTC3, humSHTC3, pressioneMS * 100.0f, statoSicurezza, 
                         config.gruppo, config.serialeID, FW_VERSION);
                
                // ...passa in modalità trasmissione, invia la risposta e torna in modalità ricezione.
                modoTrasmissione();
                Serial1.print(buffer);
                modoRicezione();
                
                if (debugViewData) Serial.printf("TX 485: %s\n", buffer);
            }
        }
        // Se la richiesta è un comando per cambiare gruppo (inizia con 'GRP')...
        else if (richiesta.startsWith("GRP")) {
            int duePunti = richiesta.indexOf(':');
            if (duePunti > 3) {
                // ...estrae l'ID a cui è rivolto il comando.
                int idRicevuto = richiesta.substring(3, duePunti).toInt();
                // Se l'ID corrisponde a quello di questa scheda...
                if (idRicevuto == config.indirizzo485) {
                    // ...estrae il nuovo gruppo, lo aggiorna nella configurazione e lo salva in memoria.
                    int nuovoGruppo = richiesta.substring(duePunti + 1).toInt();
                    config.gruppo = nuovoGruppo;
                    memoria.putInt("grp", config.gruppo);
                    if (debugViewData) Serial.printf("Gruppo aggiornato da Master: %d\n", config.gruppo);
                }
            }
        }
    }
}

// Helper per convertire stringa Hex in byte array
void hexToBytes(String hex, uint8_t* bytes, int len) {
    for (int i = 0; i < len; i++) {
        char c1 = hex.charAt(i * 2);
        char c2 = hex.charAt(i * 2 + 1);
        
        uint8_t b1 = 0;
        if (c1 >= '0' && c1 <= '9') b1 = c1 - '0';
        else if (c1 >= 'A' && c1 <= 'F') b1 = c1 - 'A' + 10;
        else if (c1 >= 'a' && c1 <= 'f') b1 = c1 - 'a' + 10;

        uint8_t b2 = 0;
        if (c2 >= '0' && c2 <= '9') b2 = c2 - '0';
        else if (c2 >= 'A' && c2 <= 'F') b2 = c2 - 'A' + 10;
        else if (c2 >= 'a' && c2 <= 'f') b2 = c2 - 'a' + 10;
        
        bytes[i] = (b1 << 4) | b2;
    }
}

// Calcola Checksum XOR (deve corrispondere a quello del Master)
uint8_t calculateChecksum(String &data) {
    uint8_t crc = 0;
    for (int i = 0; i < data.length(); i++) {
        crc ^= data.charAt(i);
    }
    return crc;
}

void processOTACommand(String cmd) {
    // Formato: OTA,CMD,PARAM1,PARAM2...
    const int myId = config.indirizzo485;
    
    // --- GESTIONE COMANDI TEST (Scrittura su File SPIFFS) ---
    if (cmd.startsWith("TEST,SPACE?")) {
        if(!SPIFFS.begin(true)) SPIFFS.begin(true);
        uint32_t total = SPIFFS.totalBytes();
        uint32_t used = SPIFFS.usedBytes();
        
        modoTrasmissione();
        Serial1.printf("OK,TEST,SPACE,%d,%u,%u!", myId, total, used);
        modoRicezione();
        return;
    }

    if (cmd.startsWith("TEST,SPACE,")) {
        // Formato: TEST,SPACE,ID,SIZE
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;

        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != myId) return;

        size_t requestedSize = (size_t)cmd.substring(c3 + 1).toInt();
        uint32_t maxSpace = (uint32_t)ESP.getFreeSketchSpace();
        bool enough = (requestedSize <= maxSpace);
        bool ok = false;
        String err = "";
        if (enough) {
            ok = Update.begin(requestedSize);
            err = ok ? "" : String(Update.errorString());
            if (ok) Update.abort();
        }

        modoTrasmissione();
        if (ok) {
            Serial1.printf("OK,TEST,SPACE,%d,OK,%u,%u!", myId, (uint32_t)requestedSize, maxSpace);
        } else if (!enough) {
            Serial1.printf("OK,TEST,SPACE,%d,NO,%u,%u!", myId, (uint32_t)requestedSize, maxSpace);
        } else {
            err.replace(",", ";");
            Serial1.printf("OK,TEST,SPACE,%d,FAIL,%s!", myId, err.c_str());
        }
        modoRicezione();
        return;
    }

    if (cmd.startsWith("TEST,ERASE,")) {
        // Formato: TEST,ERASE,ID,SIZE
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;

        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != myId) return;

        size_t requestedSize = (size_t)cmd.substring(c3 + 1).toInt();
        uint32_t maxSpace = (uint32_t)ESP.getFreeSketchSpace();
        bool enough = (requestedSize <= maxSpace);
        bool ok = false;
        String err = "";
        if (enough) {
            ok = Update.begin(requestedSize); // Esegue erase dell'area OTA
            err = ok ? "" : String(Update.errorString());
            if (ok) Update.abort(); // Non iniziamo l'OTA, solo erase/pre-check
        }

        modoTrasmissione();
        if (ok) {
            Serial1.printf("OK,TEST,ERASE,%d,OK,%u!", myId, maxSpace);
        } else if (!enough) {
            Serial1.printf("OK,TEST,ERASE,%d,NO,%u!", myId, maxSpace);
        } else {
            err.replace(",", ";");
            Serial1.printf("OK,TEST,ERASE,%d,FAIL,%s!", myId, err.c_str());
        }
        modoRicezione();
        return;
    }

    if (cmd.startsWith("TEST,START,")) {
        // Formato nuovo: TEST,START,ID,SIZE,MD5
        // Legacy: TEST,START,SIZE,MD5
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        int c4 = cmd.indexOf(',', c3 + 1);
        if (c3 == -1) return;

        if (c4 != -1) {
            int targetId = cmd.substring(c2 + 1, c3).toInt();
            if (targetId != myId) return;
        }

        if(!SPIFFS.begin(true)) SPIFFS.begin(true);
        
        // Rimuovi file precedente se esiste
        if(SPIFFS.exists("/test_recv.bin")) SPIFFS.remove("/test_recv.bin");
        
        testFile = SPIFFS.open("/test_recv.bin", "w");
        if(testFile) {
            Serial.println("[TEST] File aperto per scrittura.");
            modoTrasmissione();
            Serial1.printf("OK,TEST,READY,%d!", myId);
            modoRicezione();
        } else {
            Serial.println("[TEST] Errore apertura file.");
        }
        return;
    }

    if (cmd.startsWith("TEST,DATA,")) {
        if(!testFile) return;

        // Formato nuovo: TEST,DATA,ID,OFFSET,HEX,CHECKSUM
        // Legacy: TEST,DATA,OFFSET,HEX,CHECKSUM
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        int fifthComma = cmd.indexOf(',', fourthComma + 1);
        if (thirdComma == -1 || fourthComma == -1) return;

        String offsetStr;
        String hexData;
        String checksumStr;
        if (fifthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != myId) return;
            offsetStr = cmd.substring(thirdComma + 1, fourthComma);
            hexData = cmd.substring(fourthComma + 1, fifthComma);
            checksumStr = cmd.substring(fifthComma + 1);
        } else {
            offsetStr = cmd.substring(secondComma + 1, thirdComma);
            hexData = cmd.substring(thirdComma + 1, fourthComma);
            checksumStr = cmd.substring(fourthComma + 1);
        }
        
        uint8_t receivedCrc = (uint8_t) strtol(checksumStr.c_str(), NULL, 16);
        uint8_t calcCrc = calculateChecksum(hexData);
        
        if (receivedCrc == calcCrc) {
            int len = hexData.length() / 2;
            uint8_t buff[128];
            hexToBytes(hexData, buff, len);
            testFile.write(buff, len);
            
            modoTrasmissione();
            Serial1.printf("OK,TEST,ACK,%d,%s!", myId, offsetStr.c_str());
            modoRicezione();
        }
        return;
    }

    if (cmd.startsWith("TEST,END")) {
        // Formato nuovo: TEST,END,ID
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        if (c2 != -1) {
            int targetId = cmd.substring(c2 + 1).toInt();
            if (targetId != myId) return;
        }

        if(testFile) {
            testFile.close();
            Serial.println("[TEST] File chiuso.");
        }
        modoTrasmissione();
        Serial1.printf("OK,TEST,END,%d!", myId);
        modoRicezione();
        return;
    }

    if (cmd.startsWith("TEST,VERIFY,")) {
        // Formato nuovo: TEST,VERIFY,ID,MD5
        // Legacy: TEST,VERIFY,MD5
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c2 == -1) return;

        String expectedMD5;
        if (c3 != -1) {
            int targetId = cmd.substring(c2 + 1, c3).toInt();
            if (targetId != myId) return;
            expectedMD5 = cmd.substring(c3 + 1);
        } else {
            expectedMD5 = cmd.substring(c2 + 1);
        }

        expectedMD5.trim();
        expectedMD5.toUpperCase();
        
        File f = SPIFFS.open("/test_recv.bin", "r");
        if (!f) return;
        MD5Builder md5;
        md5.begin();
        md5.addStream(f, f.size());
        md5.calculate();
        String calcMD5 = md5.toString();
        calcMD5.toUpperCase();
        f.close();
        
        modoTrasmissione();
        if(calcMD5 == expectedMD5) Serial1.printf("OK,TEST,PASS,%d!", myId);
        else Serial1.printf("OK,TEST,FAIL,%d,%s!", myId, calcMD5.c_str());
        modoRicezione();
        return;
    }

    if (cmd.startsWith("TEST,DELETE,")) {
        // Formato: TEST,DELETE,ID
        if(!SPIFFS.begin(true)) SPIFFS.begin(true);
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        if (c2 == -1) return;
        int targetId = cmd.substring(c2 + 1).toInt();
        if (targetId != myId) return;

        bool removed = false;
        bool hadFile = false;
        if (SPIFFS.exists("/test_recv.bin")) {
            hadFile = true;
            removed = SPIFFS.remove("/test_recv.bin");
        }

        modoTrasmissione();
        if (removed) {
            Serial1.printf("OK,TEST,DELETE,%d,OK!", myId);
        } else if (!hadFile) {
            Serial1.printf("OK,TEST,DELETE,%d,NOFILE!", myId);
        } else {
            Serial1.printf("OK,TEST,DELETE,%d,FAIL!", myId);
        }
        modoRicezione();
        return;
    }

    // --- GESTIONE COMANDI OTA REALI (Flash) ---

    if (cmd.startsWith("OTA,START,")) {
        // Formato nuovo: OTA,START,ID,SIZE,MD5
        // Legacy: OTA,START,SIZE,MD5
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        if (thirdComma == -1) return;

        String sizeStr;
        if (fourthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != config.indirizzo485) return;
            sizeStr = cmd.substring(thirdComma + 1, fourthComma);
            otaExpectedMD5 = cmd.substring(fourthComma + 1);
        } else {
            sizeStr = cmd.substring(secondComma + 1, thirdComma);
            otaExpectedMD5 = cmd.substring(thirdComma + 1);
        }
        otaExpectedMD5.trim(); 
        otaExpectedMD5.toUpperCase(); // Normalizziamo a maiuscolo

        otaTotalSize = sizeStr.toInt();
        
        Serial.printf("[OTA] Richiesta Start. Size: %d, MD5 Atteso: %s. Cancellazione partizione...\n", otaTotalSize, otaExpectedMD5.c_str());

        if (!Update.begin(otaTotalSize)) {
            // Aggiunto dettaglio errore per diagnosi
            Serial.printf("[OTA] Errore Update.begin: %s\n", Update.errorString());
            delay(50); // Pausa di stabilizzazione anche in caso di fallimento
            modoTrasmissione(); 
            Serial1.printf("OK,OTA,FAIL,%d!", config.indirizzo485);
            modoRicezione();
            return;
        }

        // Non usiamo setMD5() integrato perché è case-sensitive e "nascosto".
        // Faremo la verifica manuale alla fine per avere più dettagli.
        otaInProgress = true;
        otaWrittenSize = 0;
        otaRunningMd5.begin();
        otaRunningMd5Active = true;
        
        // Pausa CRUCIALE: permette alla tensione di alimentazione di stabilizzarsi dopo la cancellazione della flash,
        // prima di attivare il transceiver RS485 che richiede corrente.
        delay(1000); // Aumentato a 1 secondo per garantire stabilità alimentazione post-erase
        
        // FIX: Re-inizializza la seriale per resettare eventuali glitch del clock UART post-erase
        Serial1.end();
        Serial1.begin(115200, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
        
        Serial.println("[OTA] Update.begin() OK. Invio READY.");
        // Rispondi READY
        modoTrasmissione();
        delay(10); // Piccola pausa per assestamento driver
        Serial1.printf("OK,OTA,READY,%d!", config.indirizzo485);
        modoRicezione();
        
        } else if (cmd.startsWith("OTA,DATA,") && otaInProgress) {
        // Formato nuovo: OTA,DATA,ID,OFFSET,HEXDATA,CHECKSUM
        // Legacy: OTA,DATA,OFFSET,HEXDATA,CHECKSUM
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        int fourthComma = cmd.indexOf(',', thirdComma + 1);
        int fifthComma = cmd.indexOf(',', fourthComma + 1);
        if (thirdComma == -1 || fourthComma == -1) return;
        
        String offsetStr;
        String hexData;
        String checksumStr;

        if (fifthComma != -1) {
            int targetId = cmd.substring(secondComma + 1, thirdComma).toInt();
            if (targetId != config.indirizzo485) return;
            offsetStr = cmd.substring(thirdComma + 1, fourthComma);
            hexData = cmd.substring(fourthComma + 1, fifthComma);
            checksumStr = cmd.substring(fifthComma + 1);
        } else {
            offsetStr = cmd.substring(secondComma + 1, thirdComma);
            hexData = cmd.substring(thirdComma + 1, fourthComma);
            checksumStr = cmd.substring(fourthComma + 1);
        }

        // Verifica checksum
        uint8_t receivedCrc = (uint8_t) strtol(checksumStr.c_str(), NULL, 16);
        uint8_t calcCrc = calculateChecksum(hexData);
        if (receivedCrc != calcCrc) {
            Serial.printf("[OTA] Errore Checksum! Calc: %02X, Rx: %02X. Ignoro pacchetto.\n", calcCrc, receivedCrc);
            return;
        }
        
        int offset = offsetStr.toInt();
        int len = hexData.length() / 2;
        uint8_t buff[128]; 
        
        hexToBytes(hexData, buff, len);
        
        if (Update.write(buff, len) != len) {
             Serial.println("[OTA] Errore Scrittura");
        } else {
             otaWrittenSize += len;
             if (otaRunningMd5Active) otaRunningMd5.add(buff, len);
             modoTrasmissione();
             Serial1.printf("OK,OTA,ACK,%d,%d!", config.indirizzo485, offset);
             modoRicezione();
        }

    } else if (cmd.startsWith("OTA,VERIFY,") && otaInProgress) {
        // Formato: OTA,VERIFY,ID,MD5
        int c1 = cmd.indexOf(',');
        int c2 = cmd.indexOf(',', c1 + 1);
        int c3 = cmd.indexOf(',', c2 + 1);
        if (c3 == -1) return;
        int targetId = cmd.substring(c2 + 1, c3).toInt();
        if (targetId != config.indirizzo485) return;

        String expectedMd5 = cmd.substring(c3 + 1);
        expectedMd5.trim();
        expectedMd5.toUpperCase();

        if (otaWrittenSize != otaTotalSize) {
            modoTrasmissione();
            Serial1.printf("OK,OTA,VERIFY,%d,FAIL,SIZE,%u,%u!", config.indirizzo485, (uint32_t)otaWrittenSize, (uint32_t)otaTotalSize);
            modoRicezione();
            return;
        }

        if (otaRunningMd5Active) {
            otaRunningMd5.calculate();
            otaRunningMd5Active = false;
        }
        String calcMd5 = otaRunningMd5.toString();
        calcMd5.toUpperCase();

        modoTrasmissione();
        if (calcMd5 == expectedMd5) {
            Serial1.printf("OK,OTA,VERIFY,%d,PASS,%s!", config.indirizzo485, calcMd5.c_str());
        } else {
            Serial1.printf("OK,OTA,VERIFY,%d,FAIL,%s!", config.indirizzo485, calcMd5.c_str());
        }
        modoRicezione();

    } else if (cmd.startsWith("OTA,END") && otaInProgress) {
        // Formato nuovo: OTA,END,ID
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        if (secondComma != -1) {
            int targetId = cmd.substring(secondComma + 1).toInt();
            if (targetId != config.indirizzo485) return;
        }

        Serial.println("[OTA] Ricevuto END. Verifica MD5 e Finalizzazione...");
        
        // Termina la scrittura (true = anche se la size non corrisponde perfettamente, ma noi ci aspettiamo che lo sia)
        if (Update.end(true)) {
            // Verifica MD5 Manuale
            String calculatedMD5 = Update.md5String();
            calculatedMD5.toUpperCase();
            
            if (calculatedMD5 == otaExpectedMD5) {
                Serial.println("[OTA] Successo! MD5 Corrisponde. Riavvio...");
                modoTrasmissione();
                Serial1.printf("OK,OTA,SUCCESS,%d!", config.indirizzo485);
                modoRicezione();
                otaInProgress = false;
                otaRunningMd5Active = false;
                delay(1000);
                ESP.restart();
            } else {
                Serial.printf("[OTA] Errore MD5! Atteso: %s, Calcolato: %s\n", otaExpectedMD5.c_str(), calculatedMD5.c_str());
                modoTrasmissione();
                Serial1.printf("OK,OTA,FAIL,%d!", config.indirizzo485);
                modoRicezione();
                otaInProgress = false;
                otaRunningMd5Active = false;
            }
        } else {
            Serial.printf("[OTA] Errore Finale: %s\n", Update.errorString());
            modoTrasmissione();
            Serial1.printf("OK,OTA,FAIL,%d!", config.indirizzo485);
            modoRicezione();
            otaInProgress = false;
            otaRunningMd5Active = false;
        }
    }
}
