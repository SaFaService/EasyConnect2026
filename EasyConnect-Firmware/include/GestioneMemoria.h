#ifndef GESTIONEMEMORIA_H
#define GESTIONEMEMORIA_H

#include <Arduino.h>

enum TipoScheda {
    MODALITA_CONTROLLER = 1,
    MODALITA_PERIPHERAL = 2,
    // Alias legacy
    MODALITA_MASTER = MODALITA_CONTROLLER,
    MODALITA_SLAVE = MODALITA_PERIPHERAL
};

enum ModoController {
    CONTROLLER_STANDALONE = 1,
    CONTROLLER_REWAMPING = 2
};

enum ModoMaster {
    MASTER_STANDALONE = CONTROLLER_STANDALONE,
    MASTER_REWAMPING = CONTROLLER_REWAMPING
};

// Struttura dati periferica RS485 (visibilita' globale)
struct DatiPeriferica {
    float t, h, p;
    int sic, grp;
    char sn[32];
    char version[16];
    unsigned long lastResponseTime;
};

// Alias legacy mantenuto per compatibilita'.
using DatiSlave = DatiPeriferica;

struct Impostazioni {
    // Generali
    bool configurata;          // true se la prima configurazione e' stata fatta
    char serialeID[32];        // YYYYMMXXNNNN
    int indirizzo485;          // IP
    int gruppo;

    // Specifiche periferica
    int modalitaSensore;       // 1:TH, 2:P, 3:ALL

    // Specifiche controller (legacy field name kept)
    int modalitaMaster;        // 1:Standalone, 2:Rewamping
    bool usaSicurezzaLocale;   // Abilita/Disabilita monitoraggio IO2

    // WiFi
    char wifiSSID[32];
    char wifiPASS[64];
    float pressioneCalibrata;
    int sogliaManutenzione;

    // Impostazioni rete avanzate
    bool ipStatico;
    char ipManuale[16];
    char subnetManuale[16];
    char gatewayManuale[16];
    char apiUrl[128];          // URL API Antralux (aggiornamenti e clienti SaaS)
    char apiKey[65];           // API key Antralux
    char customerApiUrl[128];  // URL API personalizzato cliente
    char customerApiKey[65];   // API key personalizzata cliente
    bool apAttivo;

    // Calibrazione Rewamping
    int numVelocitaSistema;    // Numero velocita' impostate (es. 2 -> rileva 0,1,2)
    float deltaP_Calib[11];    // Valori DeltaP (max 10 velocita' + 0)
    int perc_Calib[11];        // Percentuali soglia
};

#endif
