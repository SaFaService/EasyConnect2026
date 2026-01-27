#include "RS485_Manager.h"
#include "GestioneMemoria.h"
#include "Pins.h"
#include "Led.h"

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

// Variabili per la modalità Standalone
bool relayBoardDetected[5]; // Indici 1-4 usati

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

// Funzione principale del gestore RS485 per il Master, da chiamare nel loop().
void RS485_Master_Loop() {
    unsigned long ora = millis();

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