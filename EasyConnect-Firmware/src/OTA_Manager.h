#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>

// --- GESTORE AGGIORNAMENTI OTA (Over-The-Air) ---
// Questo modulo si occupa di due tipi di aggiornamento:
// 1. Aggiornamento Locale: Tramite IDE Arduino o PlatformIO sulla rete WiFi locale.
// 2. Aggiornamento Remoto: Scaricando il firmware da un URL (HTTP/HTTPS) fornito dal server.

// Inizializza il sistema OTA locale (ArduinoOTA) per permettere il caricamento via WiFi.
void setupOTA();

// Funzione da chiamare nel loop() per gestire le richieste OTA locali.
void handleOTA();

// Esegue un aggiornamento firmware scaricandolo da un URL specifico.
// Questa funzione è "bloccante": ferma il resto del programma finché non finisce o fallisce.
// Parametri:
// - url: L'indirizzo web del file .bin del firmware.
// - md5: (Opzionale) L'hash MD5 per verificare l'integrità del file.
void execHttpUpdate(String url, String md5 = "");

// Contatta il server Antralux per verificare se ci sono aggiornamenti disponibili.
// Se ne trova uno, avvia automaticamente execHttpUpdate.
void checkForFirmwareUpdates();

#endif