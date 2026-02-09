#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_SHTC3.h>
#include <MS5837.h>
#include "Pins.h"
#include "Led.h"
#include "Serial_Manager.h"
#include "RS485_Manager.h"
#include "GestioneMemoria.h"

// --- OGGETTI GLOBALI ---
Adafruit_SHTC3 shtc3 = Adafruit_SHTC3();
MS5837 pressSensor;
Preferences memoria;
Impostazioni config;

// Oggetti per la gestione dei LED
Led greenLed(PIN_LED_VERDE);            // LED Verde, logica normale (HIGH = acceso)
Led redLed(PIN_LED_ROSSO, true);        // LED Rosso, logica inversa (Active Low, LOW = acceso)

// --- VERSIONE FIRMWARE SLAVE ---
const char* FW_VERSION = "1.1.0";

// --- VARIABILI GLOBALI DI STATO ---
float tempSHTC3, humSHTC3, pressioneMS;
bool statoSicurezza = false;            // Stato dell'ingresso di sicurezza
unsigned long timerLetturaSensori = 0;  // Timer per la lettura periodica dei sensori
unsigned long lastAny485Activity = 0;   // Timestamp dell'ultima attività RS485 generica
unsigned long lastMy485Request = 0;   // Timestamp dell'ultima richiesta RS485 per questa scheda
unsigned long i2cErrorStartTime = 0;    // Timestamp di quando è iniziato un errore di lettura I2C
bool i2cSensorOk = false;               // Flag che indica se il sensore di pressione è stato inizializzato
bool shtc3SensorOk = false;             // Flag che indica se il sensore di temp/umidità è stato inizializzato
bool debugViewData = false;             // Flag per abilitare/disabilitare i log di debug


// Funzione di setup, eseguita una sola volta all'avvio della scheda.
void setup() {
    Serial.begin(115200);
    // Inizializza la memoria non volatile (Preferences) nel namespace "easy".
    memoria.begin("easy", false);
    
    // Carica la configurazione salvata dalla memoria.
    // Se una chiave non esiste, viene usato un valore di default.
    config.configurata = memoria.getBool("set", false);
    config.indirizzo485 = memoria.getInt("addr", 0);
    config.gruppo = memoria.getInt("grp", 0);
    config.modalitaSensore = memoria.getInt("mode", 3);
    String s = memoria.getString("serialeID", "NON_SET");
    s.toCharArray(config.serialeID, 32);

    // Inizializza i pin dei LED.
    pinMode(PIN_LED_ROSSO, OUTPUT);
    pinMode(PIN_LED_VERDE, OUTPUT);

    greenLed.begin();
    redLed.begin();

    Serial.println("\n--- EASY CONNECT SLAVE ---");

    // Se la scheda non è configurata, entra in un loop di blocco.
    if (!config.configurata) {
        Serial.println("[!] SCHEDA NON CONFIGURATA. Inserire: SETIP, SETSERIAL...");
        Serial.println("Digitare 'HELP' per la lista. (Type 'HELP' for list.)");
        
        // Fa lampeggiare il LED rosso per indicare lo stato di attesa configurazione.
        redLed.setState(LED_BLINK_FAST);
        while (!config.configurata) { // Rimane qui finché la configurazione non è completata tramite seriale.
            redLed.update();
            Serial_Slave_Menu();
            delay(10); // Piccolo delay per non sovraccaricare la CPU
        }
    } else {
        Serial.println("SCHEDA CONFIGURATA CORRETTAMENTE (BOARD CONFIGURED)");
        Serial.printf("IP: %d\n", config.indirizzo485);
        Serial.printf("Seriale: %s\n", config.serialeID);
        Serial.printf("Gruppo: %d\n", config.gruppo);
        Serial.printf("Modalita: %d\n", config.modalitaSensore);
    }

    // A configurazione completata, il LED rosso viene spento (o messo in stato di default).
    redLed.setState(LED_OFF);

    // Inizializza la comunicazione seriale per la RS485.
    Serial1.begin(9600, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
    pinMode(PIN_RS485_DIR, OUTPUT);
    pinMode(PIN_SICUREZZA, INPUT_PULLUP);
    modoRicezione();

    // Inizializza il bus I2C per i sensori.
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    // Controlla se il sensore SHTC3 risponde
    if (shtc3.begin()) {
        shtc3SensorOk = true;
        Serial.println("[I2C] Sensore SHTC3 trovato.");
    } else {
        Serial.println("[I2C] ERRORE: Sensore SHTC3 non trovato.");
    }

    Serial.println("[I2C] Inizializzazione sensore di pressione...");
    // Tenta di inizializzare il sensore di pressione. Questo tentativo è "non bloccante":
    // se il sensore non risponde subito, il programma va avanti per non bloccare la RS485.
    if (pressSensor.init()) {
        pressSensor.setModel(MS5837::MS5837_02BA);
        i2cSensorOk = true;
        Serial.println("[I2C] Sensore di pressione trovato!");
    } 

    if (!i2cSensorOk) {
        Serial.println("[I2C] ERRORE: Sensore non rilevato. Riproverò in background.");
        Serial.println("Il firmware procedera' inviando dati a 0.");
    }
}

// Funzione per gestire la logica dei LED di stato in base alle condizioni attuali.
void gestisciLedSlave() {
    unsigned long now = millis();

    // --- Logica LED Verde (Stato Comunicazione RS485) ---
    // Se abbiamo ricevuto una richiesta specifica per noi negli ultimi 5 secondi...
    if (now - lastMy485Request < 5000) {
        // ...lampeggio lento (funzionamento nominale).
        greenLed.setState(LED_BLINK_SLOW);
    // Altrimenti, se c'è stata attività generica sulla linea negli ultimi 5 secondi...
    } else if (now - lastAny485Activity < 5000) {
        // ...LED fisso (la rete è attiva, ma nessuno ci parla).
        greenLed.setState(LED_SOLID);
    } else {
        // Altrimenti, se la linea è silente da più di 5 secondi...
        // ...lampeggio veloce (problema di comunicazione).
        greenLed.setState(LED_BLINK_FAST);
    }

    // --- Logica LED Rosso (Stato Sensori e Sicurezza) ---
    
    // Un errore di lettura si verifica se il valore è < 300 mBar, ma solo se il sensore è stato inizializzato correttamente.
    bool i2cReadError = i2cSensorOk && (pressioneMS < 300.0f);

    // Gestione del timer per rendere l'errore "persistente".
    if (i2cReadError) {
        if (i2cErrorStartTime == 0) { i2cErrorStartTime = now; } // Se l'errore è appena iniziato, avvia il timer.
    } else {
        i2cErrorStartTime = 0; // Se l'errore scompare, resetta il timer.
    }
    
    // L'errore è considerato persistente se dura da più di 5 secondi.
    bool isPersistentReadError = (i2cErrorStartTime > 0 && (now - i2cErrorStartTime > 5000));

    // La logica "a cascata" (if/else if/else) gestisce le priorità.
    // Priorità 1 (massima): Errore I2C.
    // Se il sensore non è stato trovato all'avvio O c'è un errore di lettura persistente...
    if (!i2cSensorOk || isPersistentReadError) {
        // ...lampeggio veloce.
        redLed.setState(LED_BLINK_FAST);
    } 
    // Priorità 2: Ingresso di sicurezza.
    // Se l'ingresso di sicurezza è attivo...
    else if (statoSicurezza) {
        // ...lampeggio lento.
        redLed.setState(LED_BLINK_SLOW);
    }
    // Priorità 3 (minima): Funzionamento regolare.
    // Se non ci sono errori I2C e la sicurezza non è attiva...
    else {
        // ...LED rosso fisso.
        redLed.setState(LED_SOLID);
    }
}

void loop() {
    Serial_Slave_Menu();
    greenLed.update();
    redLed.update();

    unsigned long ora = millis();

    // Se il sensore di pressione non è stato trovato all'avvio,
    // questa sezione prova a reinizializzarlo ogni 5 secondi.
    static unsigned long timerRetryI2C = 0;
    if (!i2cSensorOk && (ora - timerRetryI2C >= 5000)) {
        timerRetryI2C = ora;
        // Se il tentativo ha successo, il flag viene aggiornato e il sensore
        // verrà usato normalmente dal ciclo di lettura.
        if (pressSensor.init()) {
            pressSensor.setModel(MS5837::MS5837_02BA);
            i2cSensorOk = true;
            Serial.println("[I2C] Sensore di pressione recuperato!");
        }
    }

    // Esegue la lettura dei sensori ogni 2 secondi.
    if (ora - timerLetturaSensori >= 2000) {
        sensors_event_t humidity, temp;
        bool shtc3_ok = false;
        // Legge il sensore SHTC3 solo se è stato trovato durante l'inizializzazione.
        if (shtc3SensorOk) {
            shtc3_ok = shtc3.getEvent(&humidity, &temp);
        }

        // Legge il sensore di pressione solo se è stato trovato.
        if (i2cSensorOk) {
            pressSensor.read();
            pressioneMS = pressSensor.pressure(); // Valore in mBar per il controllo
            if (isnan(pressioneMS)) { pressioneMS = 0.0f; } // Sicurezza contro valori non validi
        } else {
            pressioneMS = 0.0f; // Se il sensore non c'è, il valore di pressione è 0.
        }

        // Aggiorna le variabili globali di temperatura e umidità solo se la lettura è valida.
        if (shtc3_ok) {
            if (!isnan(temp.temperature)) tempSHTC3 = temp.temperature;
            if (!isnan(humidity.relative_humidity)) humSHTC3 = humidity.relative_humidity;
        }

        // Legge lo stato dell'ingresso di sicurezza.
        statoSicurezza = (digitalRead(PIN_SICUREZZA) == HIGH);
        // Aggiorna il timer dell'ultima lettura.
        timerLetturaSensori = ora;
    }

    RS485_Slave_Loop(); // Chiama il gestore della comunicazione RS485.
    
    // Aggiorna lo stato dei LED in base alle condizioni correnti.
    gestisciLedSlave();
}