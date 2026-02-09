#include "RS485_Manager.h"
#include "RS485_Slave.h"
#include "GestioneMemoria.h"
#include "Pins.h"
#include <Preferences.h>
#include <Update.h> // Necessario per OTA

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

// Imposta il pin di direzione del transceiver RS485 su LOW per metterlo in ascolto.
void modoRicezione() { Serial1.flush(); digitalWrite(PIN_RS485_DIR, LOW); }
// Imposta il pin di direzione del transceiver RS485 su HIGH per abilitare la trasmissione.
void modoTrasmissione() { digitalWrite(PIN_RS485_DIR, HIGH); delayMicroseconds(50); }

// Funzione principale del gestore RS485 per lo Slave, da chiamare nel loop().
void RS485_Slave_Loop() {
    // Se ci sono dati in arrivo sulla linea RS485...
    if (Serial1.available()) {
        lastAny485Activity = millis(); // ...aggiorna il timer dell'ultima attività rilevata.
        String richiesta = Serial1.readStringUntil('!'); // ...leggi la stringa fino al carattere '!'.
        
        // Gestione comandi OTA
        if (richiesta.startsWith("OTA,")) {
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
// Helper per convertire stringa Hex in byte array (Ottimizzata per memoria)
void hexToBytes(String hex, uint8_t* bytes, int len) {
    for (int i = 0; i < len; i++) {
        String byteStr = hex.substring(i * 2, i * 2 + 2);
        bytes[i] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
        char c1 = hex.charAt(i * 2);
        char c2 = hex.charAt(i * 2 + 1);
        uint8_t b1 = (c1 >= '0' && c1 <= '9') ? c1 - '0' : (c1 >= 'A' && c1 <= 'F') ? c1 - 'A' + 10 : (c1 >= 'a' && c1 <= 'f') ? c1 - 'a' + 10 : 0;
        uint8_t b2 = (c2 >= '0' && c2 <= '9') ? c2 - '0' : (c2 >= 'A' && c2 <= 'F') ? c2 - 'A' + 10 : (c2 >= 'a' && c2 <= 'f') ? c2 - 'a' + 10 : 0;
        bytes[i] = (b1 << 4) | b2;
    }
}

void processOTACommand(String cmd) {
    // Formato: OTA,CMD,PARAM1,PARAM2...
    
    if (cmd.startsWith("OTA,START,")) {
        // Formato: OTA,START,SIZE,MD5
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        
        String sizeStr = cmd.substring(secondComma + 1, thirdComma);
        otaExpectedMD5 = cmd.substring(thirdComma + 1);
        otaExpectedMD5.trim(); 
        otaExpectedMD5.toUpperCase(); // Normalizziamo a maiuscolo

        otaTotalSize = sizeStr.toInt();
        
        Serial.printf("[OTA] Richiesta Start. Size: %d, MD5: %s\n", otaTotalSize, otaExpectedMD5.c_str());

        if (!Update.begin(otaTotalSize)) {
            Serial.println("[OTA] Errore Update.begin");
            modoTrasmissione();
            Serial1.print("OK,OTA,FAIL!");
            modoRicezione();
            return;
        }

        // Non usiamo setMD5() integrato perché è case-sensitive e "nascosto".
        // Faremo la verifica manuale alla fine per avere più dettagli.
        otaInProgress = true;

        // Rispondi READY
        modoTrasmissione();
        Serial1.print("OK,OTA,READY!");
        modoRicezione();
        
    } else if (cmd.startsWith("OTA,DATA,") && otaInProgress) {
        // Formato: OTA,DATA,OFFSET,HEXDATA
        int firstComma = cmd.indexOf(',');
        int secondComma = cmd.indexOf(',', firstComma + 1);
        int thirdComma = cmd.indexOf(',', secondComma + 1);
        
        String offsetStr = cmd.substring(secondComma + 1, thirdComma);
        String hexData = cmd.substring(thirdComma + 1);
        
        int offset = offsetStr.toInt();
        int len = hexData.length() / 2;
        uint8_t buff[128]; 
        
        hexToBytes(hexData, buff, len);
        
        if (Update.write(buff, len) != len) {
             Serial.println("[OTA] Errore Scrittura");
        } else {
             modoTrasmissione();
             Serial1.printf("OK,OTA,ACK,%d!", offset);
             modoRicezione();
        }

    } else if (cmd.startsWith("OTA,END") && otaInProgress) {
        Serial.println("[OTA] Ricevuto END. Finalizzazione...");
        
        // Termina la scrittura (true = anche se la size non corrisponde perfettamente, ma noi ci aspettiamo che lo sia)
        if (Update.end(true)) {
            // Verifica MD5 Manuale
            String calculatedMD5 = Update.md5String();
            calculatedMD5.toUpperCase();
            
            if (calculatedMD5 == otaExpectedMD5) {
                Serial.println("[OTA] Successo! MD5 Corrisponde.");
                modoTrasmissione();
                Serial1.print("OK,OTA,SUCCESS!");
                modoRicezione();
                delay(1000);
                ESP.restart();
            } else {
                Serial.printf("[OTA] Errore MD5! Atteso: %s, Calcolato: %s\n", otaExpectedMD5.c_str(), calculatedMD5.c_str());
                modoTrasmissione();
                Serial1.print("OK,OTA,FAIL!");
                modoRicezione();
            }
        } else {
            Serial.printf("[OTA] Errore Finale: %s\n", Update.errorString());
            modoTrasmissione();
            Serial1.print("OK,OTA,FAIL!");
            modoRicezione();
            otaInProgress = false;
        }
    }
}