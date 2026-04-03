#ifndef RS485_MANAGER_H
#define RS485_MANAGER_H

#include <Arduino.h>

// Imposta il transceiver RS485 in modalità di ricezione.
void modoRicezione();

// Imposta il transceiver RS485 in modalità di trasmissione.
void modoTrasmissione();

// Funzione per gestire il loop di comunicazione RS485 per la scheda Master.
// Contiene la logica di polling e scansione degli slave.
void RS485_Master_Loop();
void RS485_Controller_Loop();

// Funzione per gestire il loop di comunicazione RS485 per la scheda Slave.
// Contiene la logica di ascolto e risposta al Master.
void RS485_Slave_Loop();
void RS485_Peripheral_Loop();

// Funzione di scansione specifica per la modalità Standalone (Cerca Relay Mode 2).
void scansionaSlaveStandalone();

// Loop principale per la modalità Standalone (Gestione Relay e LED).
void RS485_Master_Standalone_Loop();
void standalonePlayWifiStartAnimation();

// Snapshot relay Standalone (ID 1..4) per telemetria cloud.
struct StandaloneRelaySnapshot {
    bool detectedAtBoot;
    bool online;
    bool relayOn;
    bool safetyClosed;
    bool feedbackMatched;
    bool safetyAlarm;
    bool lifetimeAlarm;
    bool lampFault;
    bool modeMismatch;
    bool feedbackFaultLatched;
    uint32_t lifeLimitHours;
    int mode;
    uint32_t starts;
    float hours;
    char stateText[16];
};

// Restituisce true se la relay e' stata rilevata in scansione Standalone.
bool getStandaloneRelaySnapshot(int relayId, StandaloneRelaySnapshot& outSnapshot);

// Esegue una configurazione remota su una scheda pressione (mode/group/ip) tramite RS485.
// newMode/newGroup/newIp: passare -1 per "nessuna modifica".
bool executePressureConfigCommand(const String& slaveSn, int newMode, int newGroup, int newIp, String& outMessage);

#endif
