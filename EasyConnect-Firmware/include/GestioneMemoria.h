#ifndef GESTIONEMEMORIA_H
#define GESTIONEMEMORIA_H
#include <Arduino.h>

enum TipoScheda { MODALITA_MASTER = 1, MODALITA_SLAVE = 2 };
enum ModoMaster { MASTER_STANDALONE = 1, MASTER_REWAMPING = 2 };

// Struttura Dati Slave (Spostata qui per visibilità globale)
struct DatiSlave { 
    float t, h, p; 
    int sic, grp; 
    char sn[32]; 
    char version[16]; // Nuova: Versione Firmware Slave
};

struct Impostazioni {
    // Generali
    bool configurata;          // true se la prima configurazione è stata fatta
    char serialeID[32];        // AAAAMMXXXX
    int indirizzo485;          // IP
    int gruppo;
    
    // Specifiche Slave
    int modalitaSensore;       // 1:TH, 2:P, 3:ALL
    
    // Specifiche Master
    int modalitaMaster;        // 1:Standalone, 2:Rewamping
    bool usaSicurezzaLocale;   // Abilita/Disabilita monitoraggio IO2
    
    // WiFi (Già presenti nel WebHandler)
    char wifiSSID[32];
    char wifiPASS[64];
    float pressioneCalibrata;
    int sogliaManutenzione;
    
    // Impostazioni Rete Avanzate
    bool ipStatico;
    char ipManuale[16];
    char subnetManuale[16];
    char gatewayManuale[16];
    char apiUrl[128];
    char apiKey[65];           // Nuova: API Key per sicurezza (64 char + null)
    bool apAttivo;

    // Calibrazione Rewamping
    int numVelocitaSistema;      // Numero velocità impostate (es. 2 -> rileva 0, 1, 2)
    float deltaP_Calib[11];      // Array valori DeltaP (Max 10 velocità + 0)
    int perc_Calib[11];          // Array percentuali soglia
};

#endif