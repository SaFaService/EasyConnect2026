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

// Funzione per gestire il loop di comunicazione RS485 per la scheda Slave.
// Contiene la logica di ascolto e risposta al Master.
void RS485_Slave_Loop();

// Funzione di scansione specifica per la modalità Standalone (Cerca Relay Mode 2).
void scansionaSlaveStandalone();

// Loop principale per la modalità Standalone (Gestione Relay e LED).
void RS485_Master_Standalone_Loop();

#endif