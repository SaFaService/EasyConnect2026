#ifndef API_MANAGER_H
#define API_MANAGER_H

#include <Arduino.h>

// --- GESTORE API (Invio Dati) ---
// Questo modulo si occupa di raccogliere i dati dai sensori e inviarli al cloud.

// Raccoglie i dati correnti (DeltaP, Slave, ecc.) e li invia al server configurato.
// Gestisce automaticamente la scelta tra server Cliente e server Antralux.
void sendDataToRemoteServer();

#endif