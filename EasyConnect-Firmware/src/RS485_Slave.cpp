#include "RS485_Manager.h"
#include "GestioneMemoria.h"
#include "Pins.h"
#include <Preferences.h>

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